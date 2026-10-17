// The engine server's protocol: one Client per connected text service, JSON requests in,
// JSON responses and pushes out (the transport — a named pipe on Windows — only moves messages).
//
// Requests carry "op" and "id"; the response echoes "id". Pushes carry "ev" instead:
//   {"ev":"view", "seq":n, "view":{...}}       the LLM changed the composition or its marks
//   {"ev":"fix",  "id":n, "original":"...", "corrected":"..."}   a late English fix (no "corrected": none)
//   {"ev":"mode", "mode":{...}}                the Chinese/English mode or a setting changed (any app)
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine.h"
#include "session.h"

namespace zl {

class Service {
public:
    /// Sends one message to the client; must be thread-safe (responses and pushes interleave).
    using Writer = std::function<void(const std::string&)>;

    struct Client {
        Writer write;
        std::shared_ptr<Session> session;
        std::string program;  // for diagnostics
    };

    explicit Service(Engine& engine);
    ~Service();

    std::shared_ptr<Client> connect(Writer writer);
    void disconnect(const std::shared_ptr<Client>& client);
    /// Handles one request; returns the response message.
    std::string handle(Client& client, const std::string& message);
    size_t clientCount() const;

private:
    void broadcastMode(const Settings& s);

    Engine& engine_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Client>> clients_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace zl
