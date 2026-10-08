// SPDX-License-Identifier: GPL-3.0-or-later
// The RomM backend of the download sources; see romm_source.h.
#include "romm_source.h"

#include "remote/http.h"
#include "remote/romm/romm_client.h"

#include <cstdint>
#include <utility>

namespace Eden::Remote::Romm {
namespace {

// The most a cover may be.
constexpr std::size_t kMostCover = 8u << 20;

// A file's bytes, into the download's receiver.
struct Transfer {
    Receiver* receiver = nullptr;
    int status = 0;
};

int TransferBegin(void* user, int status, std::uint64_t) {
    Transfer& transfer = *static_cast<Transfer*>(user);
    transfer.status = status;
    // 206: from the offset asked for; 200: the whole file. Anything else is an error page.
    if (status != 200 && status != 206) return 0;
    return transfer.receiver->begin(status == 200) ? 1 : 0;
}

int TransferTake(void* user, const void* data, std::size_t size) {
    return static_cast<Transfer*>(user)->receiver->take(data, size) ? 1 : 0;
}

int TransferStop(void* user) { return static_cast<Transfer*>(user)->receiver->stopped() ? 1 : 0; }

class RommSource final : public Source {
  public:
    explicit RommSource(std::unique_ptr<Client> client) : client_(std::move(client)) {}

    std::string address() const override { return client_->url(); }

    bool list(std::vector<SourceGame>* games, std::string* error, const Stopped& stopped) override {
        return client_->Games(games, stopped, error);
    }

    bool cover(const SourceGame& game, std::string* picture, const Stopped& stopped) override {
        if (game.cover.empty()) return false;
        // Its "?ts=..." only tells versions of the server's own picture apart.
        const bool own = !game.cover.starts_with("http://") && !game.cover.starts_with("https://");
        return client_->Fetch(own ? game.cover.substr(0, game.cover.find('?')) : game.cover, kMostCover, stopped,
                              picture);
    }

    bool fetch(const SourceGame&, const SourceFile& file, std::uint64_t offset, Receiver& receiver,
               std::string* error) override {
        char escaped[1024];
        if (remote_http_escape(file.name.c_str(), escaped, sizeof(escaped)) != 0) {
            *error = "The file name is too long";
            return false;
        }
        // The file by its own id, as RomM's feeds give it.
        const std::string url = client_->url() + "/api/roms/" + file.id + "/files/content/" + escaped;
        for (;;) {
            Transfer transfer;
            transfer.receiver = &receiver;
            remote_http_request request{};
            request.url = url.c_str();
            request.authorization = client_->authorization().c_str();
            request.resume_from = offset;
            request.raw = 1;
            request.begin = TransferBegin;
            request.sink = TransferTake;
            request.stop = TransferStop;
            request.user = &transfer;
            remote_http_result result{};
            const int got = remote_http_get(&request, &result);
            // 416: the file on the server is shorter than what was begun: it starts again.
            if (transfer.status == 416 && offset > 0 && !receiver.stopped()) {
                offset = 0;
                continue;
            }
            if (transfer.status != 200 && transfer.status != 206) {
                *error = transfer.status == 0 ? std::string{result.error} : client_->StatusError(transfer.status);
                return false;
            }
            if (got != 0) {
                *error = result.error[0] ? std::string{result.error} : "The download stopped";
                return false;
            }
            return true;
        }
    }

  private:
    const std::unique_ptr<Client> client_;
};

} // namespace

std::unique_ptr<Source> MakeSource(const nlohmann::json& settings, std::string* error) {
    std::unique_ptr<Client> client = Client::Make(settings, "sources.json", error);
    if (!client) return nullptr;
    return std::make_unique<RommSource>(std::move(client));
}

} // namespace Eden::Remote::Romm
