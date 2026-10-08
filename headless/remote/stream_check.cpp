// SPDX-License-Identifier: GPL-3.0-or-later
// A downloaded game file checked while it streams by; see stream_check.h.
#include "stream_check.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Eden::Remote {
namespace {

// The first bytes kept, for headers asked for only once the bytes before them were read.
constexpr std::size_t kPrefix = 1u << 20;
// The most a header may be (a file that says more is not one of these).
constexpr std::uint64_t kMostHeader = 16u << 20;
constexpr std::uint32_t kMostEntries = 100000;
// The most a place or size in a file may be (1 PB): what a header says beyond it is not believed.
constexpr std::uint64_t kMostFile = 1ull << 50;

// The steps of reading a file's layout: an NSP's PFS0, an XCI's header, its root HFS0 and the
// HFS0 of its secure partition (the one Eden checks).
enum Stage : int {
    pfs_head,
    pfs_entries,
    card_header,
    root_head,
    root_entries,
    secure_head,
    secure_entries,
};

std::uint32_t U32(const std::uint8_t* at) {
    return static_cast<std::uint32_t>(at[0]) | static_cast<std::uint32_t>(at[1]) << 8 |
           static_cast<std::uint32_t>(at[2]) << 16 | static_cast<std::uint32_t>(at[3]) << 24;
}

std::uint64_t U64(const std::uint8_t* at) { return U32(at) | static_cast<std::uint64_t>(U32(at + 4)) << 32; }

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The SHA-256 half an NCA is named after ("<32 hex digits>.nca"); false for other names (its
// contents list, ".cnmt.nca", has a longer one).
bool NamedHash(const std::string& name, std::uint8_t* hash) {
    if (name.size() != 36 || Lower(name.substr(32)) != ".nca") return false;
    for (int i = 0; i < 16; ++i) {
        const auto digit = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        };
        const int high = digit(name[static_cast<std::size_t>(2 * i)]);
        const int low = digit(name[static_cast<std::size_t>(2 * i + 1)]);
        if (high < 0 || low < 0) return false;
        hash[i] = static_cast<std::uint8_t>(high << 4 | low);
    }
    return true;
}

} // namespace

struct StreamCheck::Want {
    std::uint64_t offset = 0; // where in the file the bytes are
    std::uint64_t size = 0;
    int stage = 0;
    std::uint64_t base = 0;   // where the partition the header is of begins
    std::vector<std::uint8_t> bytes;
    bool parsed = false;
};

struct StreamCheck::Nca {
    std::uint64_t begin = 0;
    std::uint64_t size = 0;
    std::uint8_t expected[16]{};
    EVP_MD_CTX* context = nullptr;
    std::uint64_t hashed = 0;
    int state = 0; // 0: being hashed, 1: as named, 2: damaged
    ~Nca() { EVP_MD_CTX_free(context); }
};

StreamCheck::StreamCheck(const std::string& name) {
    const std::string lower = Lower(name);
    if (lower.ends_with(".nsp")) kind_ = "nsp";
    else if (lower.ends_with(".xci")) kind_ = "xci";
    Restart();
}

StreamCheck::~StreamCheck() = default;

void StreamCheck::Restart() {
    next_ = 0;
    broken_ = false;
    prefix_.clear();
    wants_.clear();
    ncas_.clear();
    if (kind_ == "nsp") Want_(0, 0x10, pfs_head, 0);
    else if (kind_ == "xci") Want_(0x100, 0x40, card_header, 0);
}

void StreamCheck::Want_(std::uint64_t offset, std::uint64_t size, int stage, std::uint64_t base) {
    if (size == 0 || size > kMostHeader || offset > kMostFile) {
        broken_ = true;
        return;
    }
    auto want = std::make_unique<Want>();
    want->offset = offset;
    want->size = size;
    want->stage = stage;
    want->base = base;
    wants_.push_back(std::move(want));
}

// The bytes of a stretch of the file to every header and NCA that wants them next.
void StreamCheck::Catch(std::uint64_t offset, const std::uint8_t* data, std::size_t size) {
    const std::uint64_t end = offset + size;
    for (const auto& want : wants_) {
        const std::uint64_t at = want->offset + want->bytes.size();
        if (want->bytes.size() >= want->size || at < offset || at >= end) continue;
        const std::uint64_t take = std::min(want->offset + want->size, end) - at;
        want->bytes.insert(want->bytes.end(), data + (at - offset), data + (at - offset + take));
    }
    for (const auto& nca : ncas_) {
        const std::uint64_t at = nca->begin + nca->hashed;
        if (nca->state != 0 || at < offset || at >= end) continue;
        const std::uint64_t take = std::min(nca->begin + nca->size, end) - at;
        EVP_DigestUpdate(nca->context, data + (at - offset), static_cast<std::size_t>(take));
        nca->hashed += take;
        if (nca->hashed == nca->size) Finish(*nca);
    }
}

// An NCA all of whose bytes were hashed: as named, or damaged.
void StreamCheck::Finish(Nca& nca) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    const bool done = EVP_DigestFinal_ex(nca.context, digest, &length) == 1 && length >= 16;
    nca.state = done && std::memcmp(digest, nca.expected, 16) == 0 ? 1 : 2;
}

// A header that came whole: what it says about where the rest is.
void StreamCheck::Parsed(const Want& want) {
    const std::uint8_t* bytes = want.bytes.data();
    const auto head = [&](const char* magic, int entries) {
        if (std::memcmp(bytes, magic, 4) != 0) {
            broken_ = true;
            return;
        }
        const std::uint32_t count = U32(bytes + 4);
        const std::uint32_t names = U32(bytes + 8);
        if (count > kMostEntries) {
            broken_ = true;
            return;
        }
        const std::uint64_t entry_size = entries == pfs_entries ? 0x18 : 0x40;
        Want_(want.offset, 0x10 + count * entry_size + names, entries, want.base);
    };
    // Each entry of a partition's header: its name, where it is in the file and how large.
    const auto entries = [&](std::size_t entry_size, auto&& found) {
        const std::uint32_t count = U32(bytes + 4);
        const std::uint32_t names = U32(bytes + 8);
        const std::uint64_t table = 0x10 + static_cast<std::uint64_t>(count) * entry_size;
        const std::uint64_t content = table + names;
        if (content > want.bytes.size()) {
            broken_ = true;
            return;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint8_t* entry = bytes + 0x10 + static_cast<std::size_t>(i) * entry_size;
            const std::uint64_t offset = U64(entry);
            const std::uint64_t size = U64(entry + 8);
            // Where none of a file can be: it is no such header (and the sums below stay in range).
            if (offset > kMostFile || size > kMostFile || want.base + content + offset > kMostFile) {
                broken_ = true;
                return;
            }
            const std::uint32_t name_at = U32(entry + 16);
            if (name_at >= names) {
                broken_ = true;
                return;
            }
            const char* name = reinterpret_cast<const char*>(bytes + table + name_at);
            found(std::string(name, strnlen(name, names - name_at)), want.base + content + offset, size);
        }
    };
    const auto ncas = [&](const std::string& name, std::uint64_t begin, std::uint64_t size) {
        auto nca = std::make_unique<Nca>();
        if (!NamedHash(name, nca->expected)) return;
        nca->begin = begin;
        nca->size = size;
        nca->context = EVP_MD_CTX_new();
        if (nca->context == nullptr || EVP_DigestInit_ex(nca->context, EVP_sha256(), nullptr) != 1) {
            broken_ = true;
            return;
        }
        if (size == 0) Finish(*nca);
        ncas_.push_back(std::move(nca));
    };
    switch (want.stage) {
    case pfs_head:
        return head("PFS0", pfs_entries);
    case pfs_entries:
        return entries(0x18, ncas);
    case card_header: {
        if (std::memcmp(bytes, "HEAD", 4) != 0) {
            broken_ = true;
            return;
        }
        const std::uint64_t root = U64(bytes + 0x30);
        return Want_(root, 0x10, root_head, root);
    }
    case root_head:
        return head("HFS0", root_entries);
    case root_entries: {
        bool secure = false;
        entries(0x40, [&](const std::string& name, std::uint64_t begin, std::uint64_t) {
            if (name != "secure") return;
            secure = true;
            Want_(begin, 0x10, secure_head, begin);
        });
        if (!secure) broken_ = true;
        return;
    }
    case secure_head:
        return head("HFS0", secure_entries);
    case secure_entries:
        return entries(0x40, ncas);
    default:
        broken_ = true;
    }
}

void StreamCheck::Feed(std::uint64_t offset, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (offset > next_) broken_ = true; // bytes it never got
    if (offset + size <= next_ || offset > next_) {
        next_ = std::max(next_, offset + size);
        return;
    }
    // Only what is new.
    const std::size_t skip = static_cast<std::size_t>(next_ - offset);
    bytes += skip;
    size -= skip;
    offset = next_;
    if (kind_.empty() || broken_) {
        next_ = offset + size;
        return;
    }
    if (offset < kPrefix)
        prefix_.insert(prefix_.end(), bytes, bytes + std::min<std::uint64_t>(size, kPrefix - offset));
    Catch(offset, bytes, size);
    // Headers that came whole: what they say asks for more, which the bytes kept and these may have.
    for (bool more = true; more && !broken_;) {
        more = false;
        for (std::size_t i = 0; i < wants_.size(); ++i) {
            Want& want = *wants_[i];
            if (want.parsed || want.bytes.size() < want.size) continue;
            want.parsed = true;
            Parsed(want);
            more = true;
        }
        if (more) {
            Catch(0, prefix_.data(), prefix_.size());
            Catch(offset, bytes, size);
        }
    }
    next_ = offset + size;
    // What wanted bytes that went by without being taken (they were not kept): not checkable.
    for (const auto& want : wants_)
        if (!want->parsed && want->bytes.size() < want->size && want->offset + want->bytes.size() < next_) broken_ = true;
    for (const auto& nca : ncas_)
        if (nca->state == 0 && nca->begin + nca->hashed < next_) broken_ = true;
}

Verified StreamCheck::Result() const {
    if (std::any_of(ncas_.begin(), ncas_.end(), [](const auto& nca) { return nca->state == 2; })) return Verified::damaged;
    if (kind_.empty() || broken_ || ncas_.empty()) return Verified::unknown;
    if (std::any_of(ncas_.begin(), ncas_.end(), [](const auto& nca) { return nca->state != 1; })) return Verified::unknown;
    if (std::any_of(wants_.begin(), wants_.end(), [](const auto& want) { return !want->parsed; })) return Verified::unknown;
    return Verified::intact;
}

std::size_t StreamCheck::Contents() const { return ncas_.size(); }

} // namespace Eden::Remote
