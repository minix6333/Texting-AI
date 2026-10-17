#include "service.h"

#include <algorithm>

#include "../core/text.h"

namespace zl {

Service::Service(Engine& engine) : engine_(engine) {
    std::weak_ptr<bool> alive = alive_;
    engine_.addSettingsListener([this, alive](const Settings& s) {
        if (alive.lock()) broadcastMode(s);
    });
}

Service::~Service() { *alive_ = false; }

std::shared_ptr<Service::Client> Service::connect(Writer writer) {
    auto client = std::make_shared<Client>();
    client->write = std::move(writer);
    std::weak_ptr<Client> weak = client;
    client->session = std::make_shared<Session>(engine_, [weak](const json::Value& msg) {
        if (auto c = weak.lock()) c->write(msg.dump());
    });
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.push_back(client);
    return client;
}

void Service::disconnect(const std::shared_ptr<Client>& client) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
}

size_t Service::clientCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.size();
}

void Service::broadcastMode(const Settings& s) {
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients = clients_;
    }
    json::Value msg = json::Value::object();
    msg.set("ev", "mode");
    msg.set("mode", Session::modeJson(s, engine_.modelState()));
    const std::string text = msg.dump();
    for (auto& c : clients) c->write(text);
}

std::string Service::handle(Client& client, const std::string& message) {
    json::Value req;
    json::Value res;
    try {
        req = json::Value::parse(message);
        const std::string& op = req["op"].asString();
        Session& s = *client.session;
        if (op == "key") {
            KeyEvent e;
            e.code = req["code"].asInt();
            e.shift = req["shift"].asBool();
            e.ctrl = req["ctrl"].asBool();
            e.alt = req["alt"].asBool();
            e.caps = req["caps"].asBool();
            if (req["ctx"].isString()) e.context = toU32(req["ctx"].asString());
            res = s.key(e);
        } else if (op == "commit") {
            res = s.commit();
        } else if (op == "cancel") {
            res = s.cancel();
        } else if (op == "select") {
            res = s.select(req["index"].asInt(-1));
        } else if (op == "lang") {
            res = req["english"].isBool() ? s.setLanguage(req["english"].asBool()) : s.switchLanguage();
        } else if (op == "width") {
            res = s.toggleWidth();
        } else if (op == "set") {
            engine_.updateSettings([&](Settings& st) {
                st.aiEnabled = req["ai"].asBool(st.aiEnabled);
                st.typoTolerant = req["typo"].asBool(st.typoTolerant);
                st.shiftToggles = req["shift"].asBool(st.shiftToggles);
                st.ctrlSymbols = req["ctrl"].asBool(st.ctrlSymbols);
                st.fullWidth = req["full"].asBool(st.fullWidth);
            });
            res = s.state();
        } else if (op == "state") {
            res = s.state();
        } else if (op == "userword") {
            engine_.addUserWord(toU32(req["word"].asString()));
            res = json::Value::object();
            res.set("ok", true);
        } else if (op == "hello") {
            client.program = req["program"].asString();
            res = s.state();
            res.set("ver", 1);
        } else if (op == "ping") {
            res = json::Value::object();
            res.set("pong", true);
        } else {
            res = json::Value::object();
            res.set("error", "unknown op");
        }
    } catch (const std::exception& e) {
        res = json::Value::object();
        res.set("error", e.what());
    }
    if (req.has("id")) res.set("id", req["id"]);
    return res.dump();
}

}  // namespace zl
