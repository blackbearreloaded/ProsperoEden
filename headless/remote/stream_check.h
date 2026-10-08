// SPDX-License-Identifier: GPL-3.0-or-later
// A downloaded game file checked while it streams by, as Eden's integrity check checks it: a
// Switch game, update or DLC file (NSP, XCI) holds its contents as NCAs, each named after the first
// half of the SHA-256 of all of its bytes. The file's own layout (an NSP's PFS0 header, an XCI's
// header and the HFS0 headers of its root and secure partition) says where each NCA is; it is read
// from the stream as well, so nothing needs the keys, and the bytes are hashed as they come, not
// read again from the drive afterwards. Bytes the stream did not bring (a download that went on
// from where it was) are fed from the drive first (remote.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Eden::Remote {

// What a downloaded file holds about itself says about it.
enum class Verified : std::uint8_t {
    intact,
    damaged, // its bytes are not what they should be: it is downloaded again
    unknown, // it cannot be checked (no NSP or XCI, or its bytes did not all come by): taken as it is
};

class StreamCheck {
  public:
    // name: the file's name, which says its kind (.nsp, .xci; others are not checked).
    explicit StreamCheck(const std::string& name);
    ~StreamCheck();
    StreamCheck(const StreamCheck&) = delete;
    StreamCheck& operator=(const StreamCheck&) = delete;

    // The file's bytes from `offset` on, in order. Bytes it had already are left out; a gap (bytes
    // it did not get) leaves the file unchecked.
    void Feed(std::uint64_t offset, const void* data, std::size_t size);
    // The file comes again from its start (a server that does not go on where it was).
    void Restart();
    // Where the next bytes are expected: what it has of the file so far.
    std::uint64_t Next() const { return next_; }
    // After the file's last byte.
    Verified Result() const;
    // How many NCAs named after their contents it found: for the log and the checks.
    std::size_t Contents() const;

  private:
    struct Want;
    struct Nca;
    void Want_(std::uint64_t offset, std::uint64_t size, int stage, std::uint64_t base);
    void Parsed(const Want& want);
    void Catch(std::uint64_t offset, const std::uint8_t* data, std::size_t size);
    void Finish(Nca& nca);

    std::string kind_; // "nsp", "xci" or empty
    std::uint64_t next_ = 0;
    bool broken_ = false; // its layout could not be read, or bytes were missed
    std::vector<std::uint8_t> prefix_; // its first bytes, for headers asked for after they went by
    std::vector<std::unique_ptr<Want>> wants_;
    std::vector<std::unique_ptr<Nca>> ncas_;
};

} // namespace Eden::Remote
