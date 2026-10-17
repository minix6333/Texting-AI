// End-to-end tests of the engine server's key handling, through the same JSON protocol the
// Windows text service speaks. A fake text service keeps a document (text + caret + composition)
// and applies responses and pushes the way tsf/ does.
//   session_test <data dir> [model.gguf]     (without a model: dictionary-only tests)

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/json.h"
#include "../core/text.h"
#include "../core/zhuyin.h"
#include "../engine/engine.h"
#include "../engine/keys.h"
#include "../engine/service.h"

using namespace zl;

namespace {
int failures = 0, checks = 0;

#define CHECK(cond, what)                                                         \
    do {                                                                          \
        ++checks;                                                                 \
        if (!(cond)) {                                                            \
            ++failures;                                                           \
            std::printf("  FAIL %s (%s:%d)\n", std::string(what).c_str(), __FILE__, __LINE__); \
        }                                                                         \
    } while (0)

struct KeySpec {
    int code;
    bool shift;
};

/// The key (and Shift) that types `c` on a US keyboard.
KeySpec keyFor(char c) {
    if (c >= 'a' && c <= 'z') return {c - 'a' + 'A', false};
    if (c >= 'A' && c <= 'Z') return {c, true};
    if (c >= '0' && c <= '9') return {c, false};
    if (c == ' ') return {vk::Space, false};
    const std::string plain = ";=,-./`[\\]'";
    const int codes[] = {vk::Oem1, vk::OemPlus, vk::OemComma, vk::OemMinus, vk::OemPeriod, vk::Oem2,
                         vk::Oem3, vk::Oem4,    vk::Oem5,     vk::Oem6,     vk::Oem7};
    auto p = plain.find(c);
    if (p != std::string::npos) return {codes[p], false};
    for (char u : std::string("1234567890-=[]\\;',./`")) {
        if (vk::shifted(u) == c) {
            KeySpec k = keyFor(u);
            k.shift = true;
            return k;
        }
    }
    return {0, false};
}

/// Dachen keys for Zhuyin readings (space = tone 1).
std::string keysFor(const std::vector<std::u32string>& readings) {
    std::string keys;
    for (const auto& r : readings) {
        Syllable s = Syllable::parse(r);
        for (char32_t sym : {s.initial, s.medial, s.final}) if (sym) keys.push_back(keyOfSymbol(sym));
        keys.push_back(s.tone == 1 ? ' ' : s.tone == 2 ? '6' : s.tone == 3 ? '3' : s.tone == 4 ? '4' : '7');
    }
    return keys;
}

class FakeTextService {
public:
    FakeTextService(Service& service, const char* name) : service_(service), name_(name) {
        auto box = box_;  // pushes may still arrive while this object is being destroyed
        client_ = service_.connect([box](const std::string& msg) {
            std::lock_guard<std::mutex> lock(box->m);
            box->pushes.push_back(msg);
            box->cv.notify_all();
        });
        apply(request("hello"));
    }
    ~FakeTextService() { service_.disconnect(client_); }

    std::u32string doc;
    size_t caret = 0;
    bool composing = false;
    size_t compStart = 0, compLen = 0;
    json::Value lastView, lastResponse, mode;
    std::vector<std::u32string> watched;
    bool idle = true;

    json::Value request(const std::string& op, json::Value extra = json::Value::object()) {
        extra.set("op", op);
        extra.set("id", ++id_);
        return json::Value::parse(service_.handle(*client_, extra.dump()));
    }

    /// A key down, as the text service sends it; the app's own handling when not eaten.
    bool key(int code, bool shift = false, bool ctrl = false) {
        json::Value req = json::Value::object();
        req.set("code", code);
        req.set("shift", shift);
        req.set("ctrl", ctrl);
        if (idle) req.set("ctx", toU8(doc.substr(caret > 400 ? caret - 400 : 0, std::min<size_t>(caret, 400))));
        json::Value res = request("key", req);
        lastResponse = res;
        apply(res);
        const bool eaten = res["eat"].asBool();
        if (!eaten && !ctrl) appKey(code, shift);
        return eaten;
    }

    void type(const std::string& text) {
        for (char c : text) {
            KeySpec k = keyFor(c);
            key(k.code, k.shift);
        }
    }

    /// Applies pushes that arrive within `ms`.
    void pump(int ms) {
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (true) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lock(box_->m);
                if (!box_->cv.wait_until(lock, until, [&] { return !box_->pushes.empty(); })) return;
                msg = box_->pushes.front();
                box_->pushes.pop_front();
            }
            apply(json::Value::parse(msg));
        }
    }

    std::u32string compText() const { return composing ? doc.substr(compStart, compLen) : U""; }

private:
    void appKey(int code, bool shift) {
        if (code == vk::Back) {
            if (caret > 0) {
                doc.erase(caret - 1, 1);
                --caret;
            }
            return;
        }
        if (code == vk::Return) {
            insert(U"\n");
            return;
        }
        const char c = vk::usKey(code);
        if (code == vk::Space) insert(U" ");
        else if (c) insert(std::u32string(1, char32_t(shift ? vk::shifted(c) : c)));
    }

    void insert(const std::u32string& t) {
        doc.insert(caret, t);
        caret += t.size();
    }

    /// The word just before the caret, allowing one character typed after it.
    bool findWord(const std::u32string& w, size_t& at) const {
        for (size_t extra = 0; extra <= 1; ++extra) {
            if (caret >= w.size() + extra && doc.compare(caret - extra - w.size(), w.size(), w) == 0) {
                at = caret - extra - w.size();
                return true;
            }
        }
        return false;
    }

    void replaceWord(const std::u32string& from, const std::u32string& to) {
        size_t at;
        if (!findWord(from, at)) return;
        doc.replace(at, from.size(), to);
        caret = caret + to.size() - from.size();
    }

    void apply(const json::Value& r) {
        if (r.has("mode")) mode = r["mode"];
        if (r.has("idle")) idle = r["idle"].asBool();
        if (r["ev"].asString() == "fix") {
            auto original = toU32(r["original"].asString());
            if (r.has("corrected")) {
                // a watched word: the real text service keeps a range on it; here it is still the last word
                size_t at = doc.rfind(original);
                if (at != std::u32string::npos) {
                    auto to = toU32(r["corrected"].asString());
                    doc.replace(at, original.size(), to);
                    if (caret > at) caret = caret + to.size() - original.size();
                }
            }
            return;
        }
        for (const auto& a : r["actions"].items()) {
            const auto& type = a["type"].asString();
            if (type == "fix") replaceWord(toU32(a["original"].asString()), toU32(a["corrected"].asString()));
            if (type == "revert") {
                replaceWord(toU32(a["corrected"].asString()), toU32(a["original"].asString()));
                request("userword", json::Value(json::Value::Object{{"word", a["original"]}}));
            }
            if (type == "watch") watched.push_back(toU32(a["original"].asString()));
        }
        if (r.has("commit")) {
            auto t = toU32(r["commit"].asString());
            if (composing) {
                doc.replace(compStart, compLen, t);
                caret = compStart + t.size();
                composing = false;
            } else {
                insert(t);
            }
        }
        if (r.has("view") && r["seq"].asInt() > seq_) {
            seq_ = r["seq"].asInt();
            lastView = r["view"];
            const auto& comp = r["view"]["comp"];
            if (comp.isNull()) {
                if (composing) {
                    doc.erase(compStart, compLen);
                    caret = compStart;
                    composing = false;
                }
            } else {
                auto t = toU32(comp["text"].asString());
                if (!composing) {
                    composing = true;
                    compStart = caret;
                    compLen = 0;
                }
                doc.replace(compStart, compLen, t);
                compLen = t.size();
                caret = compStart + size_t(comp["caret"].asInt());  // test text has no surrogate pairs
            }
        }
    }

    Service& service_;
    std::string name_;
    std::shared_ptr<Service::Client> client_;
    struct Mailbox {
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::string> pushes;
    };
    std::shared_ptr<Mailbox> box_ = std::make_shared<Mailbox>();
    int id_ = 0;
    int seq_ = 0;
};

bool hasStyle(const json::Value& view, int style) {
    for (const auto& run : view["comp"]["runs"].items()) if (run[2].asInt() == style || run[2].asInt() == style + 3) return true;
    return false;
}

void setEnglish(FakeTextService& t, bool english) {
    t.request("lang", json::Value(json::Value::Object{{"english", english}}));
    t.mode = t.request("state")["mode"];
}

void setWidth(FakeTextService& t, bool full) {
    t.request("set", json::Value(json::Value::Object{{"full", full}}));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: session_test <data dir> [model.gguf]\n");
        return 2;
    }
    EngineConfig cfg;
    cfg.dataDir = argv[1];
    cfg.modelPath = argc > 2 ? argv[2] : "";
    cfg.log = [](const std::string& line) { std::printf("  [engine] %s\n", line.c_str()); };
    Engine engine(cfg);
    const bool ai = argc > 2;
    if (ai) {
        for (int i = 0; i < 300 && engine.modelState() == Engine::ModelState::Loading; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::printf("model: %s\n", engine.modelState() == Engine::ModelState::Ready ? "ready" : "NOT READY");
    }
    Service service(engine);
    const int settle = ai ? 400 : 20;

    std::printf("Chinese: plain sentence\n");
    {
        FakeTextService t(service, "a");
        setEnglish(t, false);
        std::vector<std::u32string> read = {U"ㄨㄛˇ", U"ㄇㄧㄥˊ", U"ㄊㄧㄢ", U"ㄑㄩˋ", U"ㄍㄨㄥ", U"ㄙ"};
        t.type(keysFor(read));
        t.pump(settle);
        CHECK(t.composing, "composition is shown while typing");
        CHECK(t.compText() == U"我明天去公司", "converted text: " + toU8(t.compText()));
        CHECK(t.key(vk::Return), "Enter is eaten while composing");
        CHECK(!t.composing && t.doc == U"我明天去公司", "Enter commits: " + toU8(t.doc));
        CHECK(!t.key(vk::Return), "Enter passes through when idle");
    }

    std::printf("Chinese: typo corrected and marked, Shift+Backspace restores\n");
    {
        FakeTextService t(service, "a");
        // 我明天早上九點要去公司開會, with ㄉㄧㄢˇ typed as ㄉㄨㄢˇ (adjacent key)
        std::vector<std::u32string> typed = {U"ㄨㄛˇ", U"ㄇㄧㄥˊ", U"ㄊㄧㄢ", U"ㄗㄠˇ", U"ㄕㄤˋ", U"ㄐㄧㄡˇ", U"ㄉㄨㄢˇ",
                                             U"ㄧㄠˋ", U"ㄑㄩˋ", U"ㄍㄨㄥ", U"ㄙ", U"ㄎㄞ", U"ㄏㄨㄟˋ"};
        t.type(keysFor(typed));
        t.pump(settle);
        CHECK(t.compText() == U"我明天早上九點要去公司開會", "typo corrected: " + toU8(t.compText()));
        CHECK(hasStyle(t.lastView, 1), "the corrected character carries the corrected mark");
        // move the cursor onto 點 (reading 6) and undo the correction
        for (int i = 0; i < 6; ++i) t.key(vk::Left);
        CHECK(t.lastView["bubble"]["dot"].asInt() == 1, "the bubble explains the correction at the cursor");
        t.key(vk::Back, true);
        t.pump(settle);
        CHECK(t.compText() != U"我明天早上九點要去公司開會", "Shift+Backspace puts back what was typed: " + toU8(t.compText()));
        t.key(vk::Escape);
        t.key(vk::Escape);
        CHECK(!t.composing && t.doc.empty(), "Esc clears the composition");
    }

    std::printf("Chinese: candidate window (↓ opens, ↓ moves, number picks, Esc restores)\n");
    {
        FakeTextService t(service, "a");
        t.type(keysFor({U"ㄕˋ"}));
        const auto before = t.compText();
        CHECK(t.key(vk::Down), "↓ opens the candidate window");
        CHECK(!t.lastView["cand"].isNull(), "candidate window shown");
        const int n = int(t.lastView["cand"]["items"].size());
        CHECK(n > 9, "many candidates for ㄕˋ");
        t.key(vk::Down);
        CHECK(t.lastView["cand"]["sel"].asInt() == 1, "↓ moves the highlight");
        CHECK(t.compText() != before, "the text previews the highlighted candidate");
        t.key(vk::Escape);
        CHECK(t.lastView["cand"].isNull() && t.compText() == before, "Esc closes and restores");
        t.key(vk::Space);  // Space after a converted syllable also opens it
        CHECK(!t.lastView["cand"].isNull(), "Space opens the window when nothing is being typed");
        t.key(vk::Space);
        CHECK(t.lastView["cand"]["sel"].asInt() == 9, "Space turns the page in the window");
        t.key('3');
        CHECK(t.lastView["cand"].isNull(), "a number picks a candidate");
        CHECK(t.compText().size() == 1, "picked: " + toU8(t.compText()));
        const auto picked = t.compText();
        t.key(vk::Up);
        CHECK(t.compText() != picked, "↑ changes the character in place: " + toU8(t.compText()));
        CHECK(t.lastView["cand"].isNull(), "↑ does not open the window");
        t.key(vk::Return);
        CHECK(t.doc.size() == 1, "committed one character");
    }

    if (ai) {
        std::printf("Chinese: Tab jumps to marks with the candidate window open, Esc keeps the marks\n");
        FakeTextService t(service, "a");
        // a sentence whose syllables are ambiguous enough to leave doubtful characters
        std::vector<std::u32string> typed = {U"ㄊㄚ", U"ㄕㄨㄛ", U"ㄓㄜˋ", U"ㄍㄜ˙", U"ㄕˋ", U"ㄑㄧㄥˊ", U"ㄅㄨˋ",
                                             U"ㄒㄧㄤˋ", U"ㄋㄧˇ", U"ㄒㄧㄤˇ", U"ㄉㄜ˙", U"ㄋㄚˋ", U"ㄇㄜ˙", U"ㄐㄧㄢˇ", U"ㄉㄢ"};
        t.type(keysFor(typed));
        t.pump(settle);
        const bool marked = hasStyle(t.lastView, 1) || hasStyle(t.lastView, 2);
        std::printf("  text: %s, marked: %s\n", toU8(t.compText()).c_str(), marked ? "yes" : "no");
        if (marked) {
            t.key(vk::Tab);
            CHECK(!t.lastView["cand"].isNull(), "Tab opens the candidate window on a mark");
            t.key(vk::Escape);
            CHECK(t.lastView["cand"].isNull(), "Esc closes it");
            CHECK(hasStyle(t.lastView, 1) || hasStyle(t.lastView, 2), "the marks survive Esc");
            t.key(vk::Tab);
            CHECK(!t.lastView["cand"].isNull(), "Tab again opens the window");
            t.key(vk::Tab);
            CHECK(!t.lastView["cand"].isNull(), "Tab inside the window goes to the next mark (window open)");
        }
        t.key(vk::Return);
        t.key(vk::Return);
    }

    std::printf("Chinese mode: symbols and letters (Microsoft Bopomofo keys)\n");
    {
        FakeTextService t(service, "a");
        t.type(keysFor({U"ㄋㄧˇ", U"ㄏㄠˇ"}));
        t.key(vk::OemComma, true);  // Shift+, → ，
        CHECK(t.doc == U"你好，", "Shift+, commits and types ，: " + toU8(t.doc));
        t.key('A', true);
        CHECK(t.doc == U"你好，A", "Shift+letter types the capital: " + toU8(t.doc));
        t.key(vk::OemPeriod, false, true);  // Ctrl+. → 。 (full-width .)
        CHECK(t.doc == U"你好，A．", "Ctrl+. types the full-width symbol: " + toU8(t.doc));
        t.key(vk::Oem3);  // ` then ,
        CHECK(t.composing && t.compText() == U"`", "` waits for a symbol");
        t.key(vk::OemComma);
        CHECK(t.doc == U"你好，A．，", "` + , types ，: " + toU8(t.doc));
        t.type("`u4e00 ");
        CHECK(t.doc == U"你好，A．，一", "` U 4e00 Space types 一: " + toU8(t.doc));
        t.key(vk::Oem4);
        CHECK(t.doc == U"你好，A．，一「", "[ types 「: " + toU8(t.doc));
        setWidth(t, true);
        t.key(vk::Space);
        CHECK(t.doc.back() == 0x3000, "full-width space when nothing is composed");
        t.key(vk::Space, true);  // Shift+Space: back to half width
        CHECK(!t.lastResponse["mode"]["full"].asBool(), "Shift+Space toggles the width");
        setWidth(t, false);
    }

    std::printf("English mode\n");
    {
        FakeTextService t(service, "a");
        setEnglish(t, true);
        CHECK(t.mode["english"].asBool(), "English mode on");
        t.type("teh i dont ");
        t.pump(settle);
        CHECK(t.doc == U"the I don't ", "rules fix teh / i / dont: " + toU8(t.doc));
        if (ai) {
            t.type("I recieved two seperate messags.");
            t.pump(settle);
            CHECK(t.doc == U"the I don't I received two separate messages.", "AI fixes misspellings: " + toU8(t.doc));
        }
        // Backspace right after a fix undoes it and remembers the word
        FakeTextService u(service, "b");
        u.type("Teh ");
        CHECK(u.doc == U"The ", "Teh → The: " + toU8(u.doc));
        CHECK(u.key(vk::Back), "Backspace right after a fix is eaten");
        CHECK(u.doc == U"Teh ", "and undoes the fix: " + toU8(u.doc));
        u.type("teh ");
        CHECK(u.doc == U"Teh teh ", "the undone word is not corrected again: " + toU8(u.doc));
        u.key(vk::Back);
        CHECK(u.doc == U"Teh teh", "a normal Backspace passes through: " + toU8(u.doc));
        setWidth(u, true);
        u.type(" ab");
        CHECK(u.doc == U"Teh teh　ａｂ", "full-width letters in English mode: " + toU8(u.doc));
        setWidth(u, false);
    }

    std::printf("One Chinese/English switch for every app\n");
    {
        FakeTextService a(service, "a"), b(service, "b");
        setEnglish(a, false);
        b.pump(50);
        CHECK(!b.mode["english"].asBool(), "b sees Chinese");
        a.type(keysFor({U"ㄋㄧˇ"}));
        json::Value r = a.request("lang");  // Shift tap in app a
        a.mode = r["mode"];
        CHECK(r["commit"].asString() == "你", "switching commits the composition");
        CHECK(r["flash"]["text"].asString() == "英", "and flashes 英");
        b.pump(50);
        CHECK(b.mode["english"].asBool(), "app b was told: English");
        b.type("hello ");
        CHECK(b.doc == U"hello ", "b types English");
        setEnglish(a, false);
    }

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
