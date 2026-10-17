// imecli — command-line harness for the C++ engine (same commands and numbers as the Mac imecli,
// so both versions can be checked against each other).
//   imecli eval    <tsi.csv> <model.gguf> <zh_candidates.json> [--k 8] [--clean] [--strict] [--calibrate]
//   imecli keys    <tsi.csv> <model.gguf> "<Dachen keys, space = tone 1>" ["<left context>"]
//   imecli en-eval <tsi.csv> <model.gguf> <en_freq.txt> <en_eval.json> [--verbose] [--no-known] [--no-list]
// Options: --cpu (no GPU offload), --threads N.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../core/composition.h"
#include "../core/english.h"
#include "../core/fusion.h"
#include "../core/json.h"
#include "../core/lattice.h"
#include "../core/lexicon.h"
#include "../core/text.h"
#include "../core/zhuyin.h"
#include "../engine/scorer.h"

using namespace zl;
using Clock = std::chrono::steady_clock;

namespace {
std::vector<std::string> args;

double ms(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

double pct(std::vector<double> xs, double p) {
    if (xs.empty()) return NAN;
    std::sort(xs.begin(), xs.end());
    return xs[std::min(xs.size() - 1, size_t(double(xs.size()) * p))];
}

double opt(const std::string& name, double def) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) return std::atof(args[i + 1].c_str());
    return def;
}

bool flag(const std::string& name) { return std::find(args.begin(), args.end(), name) != args.end(); }

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::u32string> strings(const json::Value& v) {
    std::vector<std::u32string> out;
    for (const auto& x : v.items()) out.push_back(toU32(x.asString()));
    return out;
}

std::string dirOf(const std::string& path) {
    auto p = path.find_last_of("/\\");
    return p == std::string::npos ? "." : path.substr(0, p);
}

int runEval(Lexicon&, LatticeDecoder& decoder, LlamaScorer& scorer) {
    auto items = json::Value::parse(readFile(args[4]));
    const int K = int(opt("--k", 8));
    Fusion fusion;
    fusion.unigramWeight = float(opt("--w", 0.5));
    fusion.switchMargin = float(opt("--margin", 0.5));
    fusion.typoMargin = float(opt("--typo-margin", 2.0));
    const bool tolerant = !flag("--strict");
    const bool clean = flag("--clean") || !tolerant;
    std::vector<double> stepMs, latMs, ctxMs;
    int ok = 0, total = 0, segOK = 0, latticeSegOK = 0, typoFixed = 0, typoTotal = 0;
    struct Calib {
        float agree;
        bool correct, corrected;
    };
    std::vector<Calib> calib;
    for (const auto& it : items.items()) {
        const auto typed = strings(clean ? it["read"] : it["typed"]);
        const auto read = strings(it["read"]);
        const auto gold = toU32(it["gold"].asString());
        auto t0 = Clock::now();
        scorer.setContext(toU32(it["ctx"].asString()));
        ctxMs.push_back(ms(t0));
        std::u32string out;
        for (size_t i = 1; i <= typed.size(); ++i) {
            std::vector<std::u32string> prefix(typed.begin(), typed.begin() + std::ptrdiff_t(i));
            t0 = Clock::now();
            auto cands = decoder.nbest(prefix, K, tolerant);
            latMs.push_back(ms(t0));
            if (cands.size() <= 1) {
                out = cands.empty() ? U"" : cands[0].text;
                continue;
            }
            std::vector<std::u32string> texts;
            for (const auto& c : cands) texts.push_back(c.text);
            t0 = Clock::now();
            auto llm = scorer.score(texts);
            stepMs.push_back(ms(t0));
            auto a = fusion.analyze(cands, llm, int(i));
            out = cands[size_t(a.pick)].text;
            if (flag("--calibrate") && i == typed.size()) {
                auto lit = decoder.nbest(prefix, 1, false);
                auto shownPR = Composition::perReading(cands[size_t(a.pick)]);
                auto litPR = lit.empty() ? shownPR : Composition::perReading(lit[0]);
                if (shownPR.size() != gold.size()) continue;
                const float maxS = *std::max_element(a.scores.begin(), a.scores.end());
                std::vector<double> w;
                for (float s : a.scores) w.push_back(double(std::exp((s - maxS) / Composition::temperature)));
                double z = 0;
                for (double x : w) z += x;
                for (size_t r = 0; r < shownPR.size(); ++r) {
                    double g = 0;
                    for (size_t c = 0; c < cands.size(); ++c) {
                        auto p = Composition::perReading(cands[c]);
                        if (r < p.size() && p[r] == shownPR[r]) g += w[c];
                    }
                    calib.push_back({float(g / z), shownPR[r] == std::u32string(1, gold[r]),
                                     litPR.size() == shownPR.size() && litPR[r] != shownPR[r]});
                }
            }
        }
        auto top = decoder.nbest(typed, 1, tolerant);
        latticeSegOK += (!top.empty() && top[0].text == gold) ? 1 : 0;
        if (out.size() == gold.size()) {
            for (size_t c = 0; c < gold.size(); ++c) ok += out[c] == gold[c] ? 1 : 0;
        }
        total += int(gold.size());
        segOK += out == gold ? 1 : 0;
        const size_t pos = size_t(it["pos"].asInt());
        if (!clean && pos < typed.size() && typed[pos] != read[pos]) {
            ++typoTotal;
            typoFixed += (out.size() == gold.size() && out[pos] == gold[pos]) ? 1 : 0;
        }
    }
    const double n = double(items.size());
    std::printf("segments %d  chars %d  K=%d input=%s tolerant=%s\n", int(items.size()), total, K, clean ? "clean" : "typo",
                tolerant ? "yes" : "no");
    std::printf("accuracy: char %.2f%%  segment %.1f%% (lattice only %.1f%%)", 100.0 * ok / total, 100.0 * segOK / n,
                100.0 * latticeSegOK / n);
    if (typoTotal > 0) std::printf("  typo recovered %.1f%%", 100.0 * typoFixed / typoTotal);
    std::printf("\nLLM step: p50 %.1f  p90 %.1f  p99 %.1f  max %.1f ms  (n=%d)\n", pct(stepMs, 0.5), pct(stepMs, 0.9),
                pct(stepMs, 0.99), stepMs.empty() ? 0.0 : *std::max_element(stepMs.begin(), stepMs.end()), int(stepMs.size()));
    std::printf("lattice:  p50 %.2f  p99 %.2f ms;  context update p50 %.1f ms\n", pct(latMs, 0.5), pct(latMs, 0.99), pct(ctxMs, 0.5));
    if (!calib.empty()) {
        int unmarked = 0, wrong = 0, flagged = 0, hit = 0;
        for (const auto& c : calib) {
            if (c.corrected) continue;
            ++unmarked;
            if (!c.correct) ++wrong;
            if (c.agree < Composition::suspectThreshold) {
                ++flagged;
                if (!c.correct) ++hit;
            }
        }
        std::printf("marks (T=%.1f, θ=%.2f): %d unmarked chars, %d wrong; flagged %d, caught %d/%d, false %.1f%%\n",
                    Composition::temperature, Composition::suspectThreshold, unmarked, wrong, flagged, hit, wrong,
                    100.0 * (flagged - hit) / std::max(1, unmarked - wrong));
    }
    return 0;
}

int runKeys(LatticeDecoder& decoder, LlamaScorer& scorer) {
    const std::string keys = args[4];
    const std::u32string ctx = args.size() > 5 && args[5].rfind("--", 0) != 0 ? toU32(args[5]) : U"";
    Composer composer;
    std::vector<std::u32string> readings;
    for (char k : keys) {
        if (k == ' ') {
            if (auto s = composer.complete(1)) readings.push_back(*s);
            continue;
        }
        if (int tone = toneOfKey(k)) {
            if (auto s = composer.complete(tone)) readings.push_back(*s);
            continue;
        }
        if (char32_t sym = dachenSymbol(k)) composer.receive(sym);
    }
    std::string joined;
    for (const auto& r : readings) joined += toU8(r) + " ";
    std::printf("readings: %s\n", joined.c_str());
    scorer.setContext(ctx);
    const int K = int(opt("--k", 8));
    auto t0 = Clock::now();
    auto cands = decoder.nbest(readings, K, true);
    const double tLat = ms(t0);
    std::vector<std::u32string> texts;
    for (const auto& c : cands) texts.push_back(c.text);
    t0 = Clock::now();
    auto llm = cands.size() > 1 ? scorer.score(texts) : std::vector<float>(cands.size(), 0.0f);
    const double tLLM = ms(t0);
    Fusion fusion;
    auto a = fusion.analyze(cands, llm, int(readings.size()));
    for (size_t i = 0; i < cands.size(); ++i) {
        std::printf("%s %2d  %s  llm=%.2f typo=%.1f uni=%.2f\n", int(i) == a.pick ? "→" : " ", int(i), toU8(cands[i].text).c_str(),
                    llm[i], cands[i].typo, cands[i].unigram);
    }
    Composition comp;
    for (const auto& r : readings) comp.insert(r);
    comp.reconvert(decoder, K, true);
    comp.applyAI(a, comp.generation());
    if (comp.shown()) {
        auto chars = Composition::perReading(*comp.shown());
        std::printf("marks:");
        for (size_t r = 0; r < chars.size(); ++r) {
            auto m = comp.mark(int(r));
            std::printf(" %s%s(%.2f)", toU8(chars[r]).c_str(), m == Composition::Mark::Corrected ? "*" : m == Composition::Mark::Suspicious ? "?" : "",
                        r < comp.confidence().size() ? comp.confidence()[r] : 1.0f);
        }
        std::printf("\n");
        for (int r : comp.markedReadings()) {
            std::printf("  suggestions at %d:", r);
            for (const auto& e : comp.suggestions(r)) std::printf(" %s[%d-%d]", toU8(e.phrase).c_str(), e.start, e.end);
            std::printf("\n");
        }
    }
    std::printf("lattice %.2f ms, LLM %.2f ms\n", tLat, tLLM);
    return 0;
}

int runEnEval(LlamaScorer& scorer) {
    EnglishLexicon lexicon(args[4]);
    if (!flag("--no-known")) lexicon.loadKnownWords(dirOf(args[4]) + "/en_known_words.txt");
    if (!flag("--no-list")) lexicon.loadMisspellings(dirOf(args[4]) + "/en_misspellings.txt");
    EnglishCorrector corrector(lexicon);
    corrector.keepBias = float(opt("--keep", corrector.keepBias));
    corrector.costWeight = float(opt("--cost", corrector.costWeight));
    corrector.candidateLimit = size_t(opt("--limit", double(corrector.candidateLimit)));
    corrector.knownWordBias = float(opt("--known", corrector.knownWordBias));
    auto items = json::Value::parse(readFile(args[5]));
    struct Stat {
        int n = 0, fixed = 0, wrong = 0, kept = 0, oracle = 0;
    };
    std::map<std::string, Stat> stats;
    std::vector<double> stepMs, prepMs;
    for (const auto& it : items.items()) {
        const auto typed = toU32(it["typed"].asString());
        const auto gold = toU32(it["gold"].asString());
        const auto ctx = toU32(it["ctx"].asString());
        std::u32string out = typed;
        bool inList = false;
        auto t0 = Clock::now();
        if (auto fixed = corrector.ruleFix(typed)) {
            out = *fixed;
            inList = true;
        } else if (auto check = corrector.prepare(typed, ctx)) {
            prepMs.push_back(ms(t0));
            inList = std::find(check->candidates.begin(), check->candidates.end(), toU8(lower(gold))) != check->candidates.end();
            scorer.setContext(ctx);
            t0 = Clock::now();
            auto llm = scorer.score(check->texts);
            stepMs.push_back(ms(t0));
            if (auto d = corrector.decide(*check, llm)) out = *d;
        }
        auto& st = stats[it["kind"].asString()];
        ++st.n;
        if (out == typed) ++st.kept;
        else if (lower(out) == lower(gold)) ++st.fixed;
        else ++st.wrong;
        if (inList) ++st.oracle;
        if (flag("--verbose") && out != gold)
            std::printf("  [%s] %s → %s   (meant %s)\n", it["kind"].asString().c_str(), toU8(typed).c_str(), toU8(out).c_str(),
                        toU8(gold).c_str());
    }
    std::printf("keep-bias %.1f  cost-weight %.2f  known-bias %.1f  candidates %d\n", corrector.keepBias, corrector.costWeight,
                corrector.knownWordBias, int(corrector.candidateLimit));
    for (const char* kind : {"human", "keyboard", "keep"}) {
        auto f = stats.find(kind);
        if (f == stats.end()) continue;
        const auto& st = f->second;
        auto p = [&](int x) { return 100.0 * x / st.n; };
        std::printf("  %-8s n=%d  fixed %.1f%%  wrong fix %.1f%%  unchanged %.1f%%  (meant word among candidates %.1f%%)\n", kind, st.n,
                    p(st.fixed), p(st.wrong), p(st.kept), p(st.oracle));
    }
    std::printf("  LLM step p50 %.1f ms  p90 %.1f ms;  candidate search p50 %.2f ms  p99 %.2f ms\n", pct(stepMs, 0.5), pct(stepMs, 0.9),
                pct(prepMs, 0.5), pct(prepMs, 0.99));
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    args.assign(argv, argv + argc);
    if (args.size() < 5) {
        std::fprintf(stderr, "usage: imecli eval|keys|en-eval <tsi.csv> <model.gguf> ...\n");
        return 2;
    }
    auto t0 = Clock::now();
    Lexicon lexicon(args[2]);
    std::printf("lexicon: %d phrases, %d syllables, %.0f ms\n", int(lexicon.phraseCount()), int(lexicon.syllableCount()), ms(t0));
    LatticeDecoder decoder(lexicon);
    t0 = Clock::now();
    LlamaScorer::Options o;
    o.modelPath = args[3];
    o.gpu = !flag("--cpu");
    o.threads = int(opt("--threads", 4));
    auto scorer = std::make_unique<LlamaScorer>(o);
    std::printf("model: vocab %d, load %.0f ms (%s)\n", scorer->vocabSize(), ms(t0), o.gpu ? "gpu" : "cpu");
    int rc = 2;
    if (args[1] == "eval") rc = runEval(lexicon, decoder, *scorer);
    else if (args[1] == "keys") rc = runKeys(decoder, *scorer);
    else if (args[1] == "en-eval") rc = runEnEval(*scorer);
    else std::fprintf(stderr, "unknown command %s\n", args[1].c_str());
    scorer.reset();  // release GPU resources before exit
    return rc;
}
