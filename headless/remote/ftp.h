// SPDX-License-Identifier: GPL-3.0-or-later
// ProsperoEden - A download's bytes to the drive through the console's own FTP server.
// Copyright (C) 2026 BlackBearReloaded
//
// The app does not write a download's file itself: written from a title, gigabytes of it filled
// the memory the menu and the GPU share, the drive got slower with each one (65 MB/s at first,
// 2 MB/s after 2 GB) and the menu stopped. An FTP server on the console (ftpsrv, etaHEN's, ...)
// runs as a payload, where the same drive keeps its speed, so the bytes go to it on loopback:
// REST to go on at a byte, STOR to write. Downloads need such a server; without one they fail
// and say so.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Eden::Remote {

struct FtpServer {
    int port = 2121; // ftpsrv's; sources.json's "ftp_port" names another
    std::string user = "anonymous";
    std::string password = "anonymous";
};

// One file written on the server, from a byte on.
class FtpUpload {
  public:
    FtpUpload() = default;
    FtpUpload(const FtpUpload&) = delete;
    FtpUpload& operator=(const FtpUpload&) = delete;
    ~FtpUpload() { Abort(); }

    // Signs in, and has the server write `path` (the console's own path) from byte `offset` on.
    bool Open(const FtpServer& server, const std::string& path, std::uint64_t offset, std::string* error);
    // The next bytes of the file.
    bool Send(const void* data, std::size_t size);
    // The file is whole: the server says it wrote all of it.
    bool Finish(std::string* error);
    // Why the server stopped taking the file (Send failed), as it said it: "No space left on device"
    // for a full drive. Empty when it said nothing.
    std::string Reason();
    // Ends the transfer where it is; what was sent stays in the file, to go on from.
    void Abort();
    bool open() const { return data_ >= 0; }

  private:
    bool Reply(int* code, std::string* text);
    bool Command(const std::string& line, int* code, std::string* text);

    int control_ = -1;
    int data_ = -1;
    std::string pending_; // what came on the control connection after the last reply
};

} // namespace Eden::Remote
