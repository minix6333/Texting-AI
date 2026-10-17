// Windows: talks to zhuyinllm-server.exe through the named pipe with the text service's own
// PipeClient (starting the server from this folder if it is not running), and checks a few
// round trips: Zhuyin keys, a commit, and the Chinese/English switch pushed to a second client.
//   pipe_test.exe            (zhuyinllm-server.exe and data\ next to it)

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "../core/json.h"
#include "../engine/keys.h"
#include "../tsf/pipe_client.h"

namespace zl::tsf {
HINSTANCE g_module = GetModuleHandleW(nullptr);
std::atomic<long> g_objects{0};
std::wstring moduleDir() {
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(MAX_PATH * 2));
    std::wstring p(path, n);
    return p.substr(0, p.find_last_of(L'\\'));
}
}  // namespace zl::tsf

using namespace zl;
using zl::json::Value;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

struct Client {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::string> pushes;
    tsf::PipeClient pipe{[this](std::string msg) {
        std::lock_guard<std::mutex> lock(m);
        pushes.push_back(std::move(msg));
        cv.notify_all();
    }};
    int id = 0;

    bool waitConnected(int seconds) {
        for (int i = 0; i < seconds * 10 && !pipe.connected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return pipe.connected();
    }
    Value call(Value req) {
        req.set("id", ++id);
        std::string r = pipe.call(req.dump(), id, 3000);
        return r.empty() ? Value() : Value::parse(r);
    }
    Value key(int code, bool shift = false) {
        Value req = Value::object();
        req.set("op", "key");
        req.set("code", code);
        req.set("shift", shift);
        return call(req);
    }
    /// The first push with "ev" == `ev` within `ms`.
    Value waitPush(const std::string& ev, int ms) {
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        std::unique_lock<std::mutex> lock(m);
        while (true) {
            while (!pushes.empty()) {
                Value v = Value::parse(pushes.front());
                pushes.pop_front();
                if (v["ev"].asString() == ev) return v;
            }
            if (cv.wait_until(lock, until) == std::cv_status::timeout && pushes.empty()) return Value();
        }
    }
};
}  // namespace

int main() {
    Client a, b;
    a.pipe.start();
    check(a.waitConnected(30), "connected to the server (started it if needed)");
    b.pipe.start();
    check(b.waitConnected(10), "a second client connected");
    if (!a.pipe.connected() || !b.pipe.connected()) return 1;

    Value hello = a.call(Value(Value::Object{{"op", "hello"}, {"program", "pipe_test.exe"}}));
    check(hello.has("mode"), "hello answers with the mode");
    a.call(Value(Value::Object{{"op", "lang"}, {"english", false}}));

    // ㄋㄧˇ ㄏㄠˇ = s u 3 c l 3
    Value r;
    for (char k : std::string("su3cl3")) r = a.key(k >= 'a' ? k - 'a' + 'A' : k);
    check(r["eat"].asBool(), "Zhuyin keys are eaten");
    check(r["view"]["comp"]["text"].asString() == "你好", "composition: " + r["view"]["comp"]["text"].asString());
    r = a.key(vk::Return);
    check(r["commit"].asString() == "你好", "Enter commits: " + r["commit"].asString());
    check(r["view"]["comp"].isNull(), "and ends the composition");

    // Shift tap in app a: app b is told the new mode
    r = a.call(Value(Value::Object{{"op", "lang"}}));
    check(r["mode"]["english"].asBool(), "switched to English");
    Value push = b.waitPush("mode", 2000);
    check(push["mode"]["english"].asBool(), "the other app got the mode push");
    for (char k : std::string("Teh ")) {
        const bool upper = k >= 'A' && k <= 'Z';
        r = b.key(k == ' ' ? vk::Space : upper ? k : k - 'a' + 'A', upper);
    }
    bool fixed = false;
    for (const auto& act : r["actions"].items()) fixed = fixed || (act["type"].asString() == "fix" && act["corrected"].asString() == "The");
    check(fixed, "English: Teh is fixed to The at the space");
    a.call(Value(Value::Object{{"op", "lang"}, {"english", false}}));

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
    a.pipe.stop();
    b.pipe.stop();
    return failures == 0 ? 0 : 1;
}
