// SPDX-License-Identifier: GPL-3.0-or-later
// ProsperoEden - A download's bytes to the console's FTP server; see ftp.h.
// Copyright (C) 2026 BlackBearReloaded

#include "ftp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace Eden::Remote {
namespace {

// How long the server may take to answer, or to take the next bytes.
constexpr int kWaitSeconds = 30;
// How long a server that stopped taking a file has to say why.
constexpr int kReasonSeconds = 2;

int Connect(int port) {
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) return -1;
    timeval wait{};
    wait.tv_sec = kWaitSeconds;
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    (void)setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
#ifdef SO_NOSIGPIPE
    const int on = 1;
    (void)setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(socket);
        return -1;
    }
    return socket;
}

bool SendAll(int socket, const void* data, std::size_t size) {
    const char* at = static_cast<const char*>(data);
    while (size > 0) {
#ifdef MSG_NOSIGNAL
        const ssize_t sent = send(socket, at, size, MSG_NOSIGNAL);
#else
        const ssize_t sent = send(socket, at, size, 0);
#endif
        if (sent <= 0) return false;
        at += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

// The port of a "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)" answer; its address is the
// console's own, and loopback reaches it.
// What the player is told when the server did not write the file, with what it said ("550 No
// space left on device") without its code.
std::string WriteError(const std::string& reply) {
    std::string error = "The console's FTP server could not write the file";
    if (reply.size() > 4) error += ": " + reply.substr(4);
    return error;
}

int PassivePort(const std::string& text) {
    const std::size_t open = text.find('(');
    if (open == std::string::npos) return -1;
    int numbers[6];
    if (std::sscanf(text.c_str() + open + 1, "%d,%d,%d,%d,%d,%d", &numbers[0], &numbers[1], &numbers[2], &numbers[3],
                    &numbers[4], &numbers[5]) != 6)
        return -1;
    const int port = numbers[4] * 256 + numbers[5];
    return port > 0 && port < 65536 ? port : -1;
}

} // namespace

bool FtpUpload::Reply(int* code, std::string* text) {
    // One line, or several: "123-..." up to "123 ...".
    std::string first;
    for (;;) {
        std::size_t end;
        while ((end = pending_.find('\n')) == std::string::npos) {
            char buffer[512];
            const ssize_t got = recv(control_, buffer, sizeof(buffer), 0);
            if (got <= 0) return false;
            pending_.append(buffer, static_cast<std::size_t>(got));
        }
        std::string line = pending_.substr(0, end);
        pending_.erase(0, end + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first.empty()) {
            if (line.size() < 3) return false;
            first = line;
        }
        if (line.size() >= 4 && line.compare(0, 3, first, 0, 3) == 0 && line[3] == ' ') break;
        if (first.size() < 4 || first[3] != '-') break;
    }
    *code = std::atoi(first.substr(0, 3).c_str());
    *text = first;
    return true;
}

bool FtpUpload::Command(const std::string& line, int* code, std::string* text) {
    const std::string sent = line + "\r\n";
    return SendAll(control_, sent.data(), sent.size()) && Reply(code, text);
}

bool FtpUpload::Open(const FtpServer& server, const std::string& path, std::uint64_t offset, std::string* error) {
    Abort();
    // A command is one line: a line break in a file name (it comes from the source) or a sign-in
    // would send the server commands of its own, such as deleting a file.
    for (const std::string* text : {&path, &server.user, &server.password})
        if (text->find_first_of(std::string_view{"\r\n\0", 3}) != std::string::npos) {
            *error = "A file name or FTP sign-in with a line break cannot be sent to the FTP server";
            return false;
        }
    control_ = Connect(server.port);
    if (control_ < 0) {
        *error = "Downloads need an FTP server on this console; none answers on port " + std::to_string(server.port);
        return false;
    }
    int code = 0;
    std::string text;
    const auto refused = [&](const char* step) {
        *error = std::string{"The console's FTP server refused "} + step + (text.empty() ? "" : ": " + text);
        Abort();
        return false;
    };
    if (!Reply(&code, &text) || code / 100 != 2) return refused("the connection");
    if (!Command("USER " + server.user, &code, &text)) return refused("the sign-in");
    if (code == 331 && !Command("PASS " + server.password, &code, &text)) return refused("the sign-in");
    if (code / 100 != 2) return refused("the sign-in");
    if (!Command("TYPE I", &code, &text) || code / 100 != 2) return refused("binary transfers");
    if (!Command("PASV", &code, &text) || code != 227) return refused("a passive transfer");
    const int port = PassivePort(text);
    if (port < 0) return refused("a passive transfer");
    // REST comes last before STOR, as the protocol wants it.
    if (offset > 0 && (!Command("REST " + std::to_string(offset), &code, &text) || code != 350))
        return refused("to go on at a byte");
    data_ = Connect(port);
    if (data_ < 0) return refused("the transfer's connection");
    if (!Command("STOR " + path, &code, &text) || code / 100 != 1) return refused("to write the file");
    return true;
}

bool FtpUpload::Send(const void* data, std::size_t size) { return data_ >= 0 && SendAll(data_, data, size); }

bool FtpUpload::Finish(std::string* error) {
    if (data_ < 0) return false;
    // The end of the data connection is the end of the file; the server then says it has it all.
    ::close(data_);
    data_ = -1;
    int code = 0;
    std::string text;
    const bool whole = Reply(&code, &text) && code / 100 == 2;
    if (!whole) *error = WriteError(text);
    Abort();
    return whole;
}

std::string FtpUpload::Reason() {
    if (control_ < 0) return {};
    timeval wait{};
    wait.tv_sec = kReasonSeconds;
    (void)setsockopt(control_, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    int code = 0;
    std::string text;
    if (!Reply(&code, &text) || code / 100 < 4) return {};
    return text.size() > 4 ? text.substr(4) : std::string{};
}

void FtpUpload::Abort() {
    if (data_ >= 0) ::close(data_);
    data_ = -1;
    if (control_ >= 0) ::close(control_);
    control_ = -1;
    pending_.clear();
}

} // namespace Eden::Remote
