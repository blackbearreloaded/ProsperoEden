// SPDX-License-Identifier: GPL-3.0-or-later
// A game's save data as one file; see save_archive.h.
#include "save_archive.h"

#include "miniz.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>

namespace Eden::Remote::SaveArchive {
namespace {

// The most an entry of a zip may unpack to, as RomM allows it.
constexpr std::uint64_t kMostEntry = 512ull << 20;

// MD5 fed piece by piece: OpenSSL's, which the app links already (Eden's crypto, HTTPS).
class Md5State {
  public:
    Md5State() : context_(EVP_MD_CTX_new()) {
        if (context_ != nullptr) EVP_DigestInit_ex(context_, EVP_md5(), nullptr);
    }
    ~Md5State() { EVP_MD_CTX_free(context_); }
    Md5State(const Md5State&) = delete;
    Md5State& operator=(const Md5State&) = delete;

    void Add(const void* data, std::size_t size) {
        if (context_ != nullptr) EVP_DigestUpdate(context_, data, size);
    }

    // 32 lower-case hex digits; empty when OpenSSL could not do it.
    std::string Hex() {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        if (context_ == nullptr || EVP_DigestFinal_ex(context_, digest, &length) != 1) return {};
        static const char digits[] = "0123456789abcdef";
        std::string hex;
        for (unsigned int i = 0; i < length; ++i) {
            hex += digits[digest[i] >> 4];
            hex += digits[digest[i] & 15];
        }
        return hex;
    }

  private:
    EVP_MD_CTX* context_;
};

bool FileMd5(const std::string& path, std::string* hash) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    Md5State md5;
    std::vector<char> buffer(64 * 1024);
    for (std::size_t count; (count = std::fread(buffer.data(), 1, buffer.size(), file)) > 0;) md5.Add(buffer.data(), count);
    const bool ok = !std::ferror(file);
    std::fclose(file);
    if (ok) *hash = md5.Hex();
    return ok;
}

// RomM's hash of a zip's contents, from its lines (name, MD5 of its bytes) in the order of names.
std::string ContentHash(std::vector<std::pair<std::string, std::string>> lines) {
    std::sort(lines.begin(), lines.end());
    std::string combined;
    for (const auto& [name, md5] : lines) {
        if (!combined.empty()) combined += '\n';
        combined += name + ":" + md5;
    }
    return Md5(combined.data(), combined.size());
}

bool Walk(const std::string& folder, const std::string& name, Lister lister, Folder* files, std::string* error) {
    std::vector<std::string> entries;
    if (!lister(folder, &entries)) {
        *error = "Cannot read " + folder;
        return false;
    }
    for (const std::string& entry : entries) {
        const std::string path = folder + "/" + entry;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0) {
            *error = "Cannot read " + path;
            return false;
        }
        if (S_ISDIR(info.st_mode)) {
            files->folders.push_back(name + "/" + entry + "/");
            if (!Walk(path, name + "/" + entry, lister, files, error)) return false;
        } else if (S_ISREG(info.st_mode)) {
            files->files.emplace_back(name + "/" + entry, path);
            files->newest = std::max<std::int64_t>(files->newest, info.st_mtime);
            files->bytes += static_cast<std::uint64_t>(info.st_size);
        }
    }
    return true;
}

// An entry's place under the folder it is unpacked into ("" for the folder itself); false when it
// would be outside of it.
bool Place(const std::string& name, std::string* place) {
    place->clear();
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        return false;
    for (std::size_t start = 0; start < name.size();) {
        const std::size_t end = std::min(name.find('/', start), name.size());
        const std::string part = name.substr(start, end - start);
        if (part == "..") return false;
        if (!part.empty() && part != ".") *place += (place->empty() ? "" : "/") + part;
        start = end + 1;
    }
    return true;
}

class Reader {
  public:
    explicit Reader(const std::string& path) { open_ = mz_zip_reader_init_file(&zip_, path.c_str(), 0); }
    ~Reader() {
        if (open_) mz_zip_reader_end(&zip_);
    }
    bool open() const { return open_; }
    mz_zip_archive* zip() { return &zip_; }

  private:
    mz_zip_archive zip_{};
    bool open_ = false;
};

size_t Md5Take(void* user, mz_uint64, const void* data, size_t size) {
    static_cast<Md5State*>(user)->Add(data, size);
    return size;
}

} // namespace

std::string Md5(const void* data, std::size_t size) {
    Md5State md5;
    md5.Add(data, size);
    return md5.Hex();
}

bool List(const std::string& folder, const std::string& root, Lister lister, Folder* files, std::string* error) {
    *files = Folder{};
    struct stat info {};
    if (stat(folder.c_str(), &info) != 0) {
        if (errno == ENOENT) return true;
        *error = "Cannot read " + folder;
        return false;
    }
    if (!S_ISDIR(info.st_mode)) {
        *error = folder + " is no folder";
        return false;
    }
    if (!Walk(folder, root, lister, files, error)) return false;
    std::sort(files->files.begin(), files->files.end());
    std::sort(files->folders.begin(), files->folders.end());
    return true;
}

bool Hash(const Folder& files, std::string* hash, std::string* error) {
    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto& [name, path] : files.files) {
        std::string md5;
        if (!FileMd5(path, &md5)) {
            *error = "Cannot read " + path;
            return false;
        }
        lines.emplace_back(name, md5);
    }
    *hash = ContentHash(std::move(lines));
    return true;
}

bool Pack(const Folder& files, const std::string& path, std::string* error) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) {
        *error = "Cannot write " + path;
        return false;
    }
    bool ok = true;
    for (const std::string& folder : files.folders)
        ok = ok && mz_zip_writer_add_mem(&zip, folder.c_str(), nullptr, 0, MZ_NO_COMPRESSION);
    for (const auto& [name, file] : files.files)
        ok = ok && mz_zip_writer_add_file(&zip, name.c_str(), file.c_str(), nullptr, 0, MZ_DEFAULT_LEVEL);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    ok = mz_zip_writer_end(&zip) && ok;
    if (!ok) {
        *error = "Cannot pack the save data (" + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&zip))) + ")";
        std::remove(path.c_str());
    }
    return ok;
}

bool HashZip(const std::string& path, std::string* hash, std::string* error) {
    Reader reader(path);
    if (!reader.open()) {
        *error = "The save data is no zip";
        return false;
    }
    std::vector<std::pair<std::string, std::string>> lines;
    const mz_uint count = mz_zip_reader_get_num_files(reader.zip());
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(reader.zip(), index, &stat)) {
            *error = "The save data's zip is damaged";
            return false;
        }
        const std::string name = stat.m_filename;
        if (name.ends_with("/")) continue;
        if (stat.m_uncomp_size > kMostEntry) {
            *error = "A file in the save data is too large";
            return false;
        }
        Md5State md5;
        if (!mz_zip_reader_extract_to_callback(reader.zip(), index, Md5Take, &md5, 0)) {
            *error = "The save data's zip is damaged";
            return false;
        }
        lines.emplace_back(name, md5.Hex());
    }
    *hash = ContentHash(std::move(lines));
    return true;
}

bool Unpack(const std::string& path, const std::string& folder, const std::string& root, std::string* error) {
    Reader reader(path);
    if (!reader.open()) {
        *error = "The save data is no zip";
        return false;
    }
    struct Item {
        mz_uint index;
        std::string place;
        bool folder;
    };
    std::vector<Item> items;
    const mz_uint count = mz_zip_reader_get_num_files(reader.zip());
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(reader.zip(), index, &stat)) {
            *error = "The save data's zip is damaged";
            return false;
        }
        const std::string name = stat.m_filename;
        std::string place;
        if (!Place(name, &place)) {
            *error = "The save data's zip has a file outside its folder (" + name + ")";
            return false;
        }
        if (place.empty()) continue; // "./": the folder itself
        // What a Mac's zip program adds of its own: none of the game's.
        const std::string leaf = place.substr(place.rfind('/') + 1);
        if (place == "__MACOSX" || place.starts_with("__MACOSX/") || leaf == ".DS_Store" || leaf.starts_with("._")) continue;
        if (stat.m_uncomp_size > kMostEntry) {
            *error = "A file in the save data is too large";
            return false;
        }
        items.push_back({index, place, name.ends_with("/") || stat.m_is_directory});
    }
    // The title's folder as the only thing at its top: its contents are the save data. A zip
    // whose save data is one folder of the game's own (no title folder) keeps it.
    const auto lower = [](std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::string top;
    bool one_top = !items.empty() && !root.empty();
    for (const Item& item : items) {
        const std::string first = item.place.substr(0, item.place.find('/'));
        const bool inside = item.place.find('/') != std::string::npos || item.folder;
        if (!inside || lower(first) != lower(root)) {
            one_top = false;
            break;
        }
        top = first;
    }
    // A zip without a file is not save data: unpacked, it would put an empty folder in the place
    // of the console's own.
    if (std::none_of(items.begin(), items.end(), [](const Item& item) { return !item.folder; })) {
        *error = "The save data's zip has no files";
        return false;
    }
    std::error_code ignored;
    if (std::filesystem::exists(folder, ignored)) {
        *error = folder + " is there already";
        return false;
    }
    std::filesystem::create_directories(folder, ignored);
    if (ignored) {
        *error = "Cannot make " + folder;
        return false;
    }
    for (const Item& item : items) {
        std::string place = item.place;
        if (one_top) {
            if (place == top) continue;
            place = place.substr(top.size() + 1);
        } else if (place == ".nx_save_meta.bin") {
            continue; // JKSV's own, not the game's
        }
        const std::string target = folder + "/" + place;
        if (item.folder) {
            std::filesystem::create_directories(target, ignored);
            if (ignored) {
                *error = "Cannot make " + target;
                return false;
            }
            continue;
        }
        std::filesystem::create_directories(std::filesystem::path(target).parent_path(), ignored);
        if (ignored || !mz_zip_reader_extract_to_file(reader.zip(), item.index, target.c_str(), 0)) {
            *error = "Cannot unpack " + place;
            return false;
        }
    }
    return true;
}

} // namespace Eden::Remote::SaveArchive
