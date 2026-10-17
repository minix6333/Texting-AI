#include "scorer.h"

#include <llama.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

#include "../core/text.h"

namespace zl {

namespace {
constexpr int kMaxOutputs = 128;

llama_batch& batchOf(void* p) { return *static_cast<llama_batch*>(p); }

/// exp(x) for x <= 0 to ~1e-7 relative error, written so compilers can vectorise it.
inline float fastExp(float x) {
    x = std::max(x, -87.0f);
    const float t = x * 1.4426950408889634f;  // x / ln 2
    const float fi = std::floor(t + 0.5f);
    const float y = (t - fi) * 0.6931471805599453f;  // in [-ln2/2, ln2/2]
    float p = 1.0f / 720.0f;
    p = p * y + 1.0f / 120.0f;
    p = p * y + 1.0f / 24.0f;
    p = p * y + 1.0f / 6.0f;
    p = p * y + 0.5f;
    p = p * y + 1.0f;
    p = p * y + 1.0f;
    const int32_t bits = (int32_t(fi) + 127) << 23;
    float scale;
    std::memcpy(&scale, &bits, sizeof scale);
    return p * scale;
}

/// log Σ exp(row[i]) over the vocabulary.
float logSumExp(const float* row, int n) {
    float mx = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < n; ++i) mx = row[i] > mx ? row[i] : mx;
    float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // independent lanes: vectorises without -ffast-math
    int i = 0;
    for (; i + 8 <= n; i += 8)
        for (int k = 0; k < 8; ++k) acc[k] += fastExp(row[i + k] - mx);
    float sum = 0;
    for (; i < n; ++i) sum += fastExp(row[i] - mx);
    for (float a : acc) sum += a;
    return mx + std::log(sum);
}

template <typename F>
void parallelFor(int n, int threads, F f) {
    threads = std::max(1, std::min(threads, n));
    if (threads == 1) {
        for (int i = 0; i < n; ++i) f(i);
        return;
    }
    std::atomic<int> next{0};
    auto work = [&] {
        for (int i; (i = next.fetch_add(1)) < n;) f(i);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
}

size_t commonPrefix(const std::vector<int>& a, const std::vector<int>& b) {
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    return i;
}
}  // namespace

LlamaScorer::LlamaScorer(const Options& o)
    : maxCandidates_(o.maxCandidates), maxContextTokens_(o.maxContextTokens), threads_(std::max(1, o.threads)) {
    llama_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
    llama_backend_init();
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = o.gpu ? 999 : 0;
    model_ = llama_model_load_from_file(o.modelPath.c_str(), mp);
    if (!model_) throw std::runtime_error("cannot load model " + o.modelPath);
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 1024;  // context (<= 384) + candidate prefix + 16 short suffixes
    cp.n_batch = 256;
    cp.n_ubatch = 256;
    cp.n_outputs_max = kMaxOutputs;
    cp.n_seq_max = uint32_t(maxCandidates_ + 1);
    cp.kv_unified = true;
    cp.no_perf = true;
    cp.n_threads = threads_;
    cp.n_threads_batch = threads_;
    ctx_ = llama_init_from_model(model_, cp);
    if (!ctx_) {
        llama_model_free(model_);
        throw std::runtime_error("cannot create llama context");
    }
    vocab_ = llama_model_get_vocab(model_);
    vocabSize_ = llama_vocab_n_tokens(vocab_);
    batch_ = new llama_batch(llama_batch_init(batchCapacity_, 0, maxCandidates_ + 1));
    preamble_ = tokenize(o.preamble, true);
    if (preamble_.empty()) preamble_ = tokenize("\n");
    warmUpKernels();
}

LlamaScorer::~LlamaScorer() {
    if (batch_) {
        llama_batch_free(batchOf(batch_));
        delete static_cast<llama_batch*>(batch_);
    }
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

/// The first decode of each shape compiles GPU pipelines or warms caches; do it at load time.
void LlamaScorer::warmUpKernels() {
    setContext(U"今天天氣很好，我們");
    score({U"去公園散步", U"去公元散步", U"去攻園散布", U"去公園散布"});
    score({U"今天", U"金天"});
    setContext(U"");
}

std::vector<int> LlamaScorer::tokenize(const std::string& text, bool special) const {
    std::vector<llama_token> tokens(text.size() + 8);
    int n = llama_tokenize(vocab_, text.c_str(), int32_t(text.size()), tokens.data(), int32_t(tokens.size()), false, special);
    if (n < 0) {
        tokens.resize(size_t(-n));
        n = llama_tokenize(vocab_, text.c_str(), int32_t(text.size()), tokens.data(), int32_t(tokens.size()), false, special);
    }
    return std::vector<int>(tokens.begin(), tokens.begin() + std::max(n, 0));
}

int LlamaScorer::healingTailLength(const std::u32string& text, int maxChars) {
    int n = 0;
    for (auto it = text.rbegin(); it != text.rend(); ++it) {
        if (n >= maxChars) break;
        if (isLetter(*it)) {
            ++n;
            continue;
        }
        if (!isNumber(*it) && !isNewline(*it)) ++n;
        break;
    }
    return n;
}

void LlamaScorer::addToBatch(int token, int pos, int seq, bool logits) {
    llama_batch& b = batchOf(batch_);
    const int i = b.n_tokens;
    b.token[i] = token;
    b.pos[i] = pos;
    b.n_seq_id[i] = 1;
    b.seq_id[i][0] = seq;
    b.logits[i] = logits ? 1 : 0;
    b.n_tokens += 1;
}

std::vector<std::vector<float>> LlamaScorer::decodeSequenceZero(const std::vector<int>& ids, int start, bool wantRows) {
    std::vector<std::vector<float>> rows;
    llama_batch& b = batchOf(batch_);
    size_t offset = 0;
    while (offset < ids.size()) {
        const size_t end = std::min(ids.size(), offset + size_t(batchCapacity_));
        b.n_tokens = 0;
        for (size_t i = offset; i < end; ++i) addToBatch(ids[i], start + int(i), 0, wantRows || i == ids.size() - 1);
        const int rc = llama_decode(ctx_, b);
        if (rc != 0) throw std::runtime_error("llama_decode failed: " + std::to_string(rc));
        for (int i = 0; i < b.n_tokens; ++i) {
            if (!b.logits[i]) continue;
            const float* p = llama_get_logits_ith(ctx_, i);
            rows.emplace_back(p, p + vocabSize_);
        }
        offset = end;
    }
    return rows;
}

void LlamaScorer::setContext(const std::u32string& text) {
    const int cut = healingTailLength(text);
    tailText_ = text.substr(text.size() - size_t(cut));
    std::vector<int> body = tokenize(toU8(text.substr(0, text.size() - size_t(cut))));
    const size_t room = size_t(std::max(0, maxContextTokens_ - int(preamble_.size())));
    if (body.size() > room) body.erase(body.begin(), body.end() - std::ptrdiff_t(room));
    std::vector<int> toks = preamble_;
    toks.insert(toks.end(), body.begin(), body.end());
    const size_t common = commonPrefix(ctxTokens_, toks);
    if (common == toks.size() && common == ctxTokens_.size() && !ctxLastRow_.empty()) return;  // unchanged
    // keep [0, keep), recompute the rest (at least the last token, to get its logits)
    const size_t keep = std::min(common, toks.size() - 1);
    llama_memory_t mem = llama_get_memory(ctx_);
    llama_memory_seq_rm(mem, 0, llama_pos(keep), -1);
    extTokens_.clear();
    extRows_.clear();
    ctxTokens_.assign(toks.begin(), toks.begin() + std::ptrdiff_t(keep));
    try {
        auto rows = decodeSequenceZero(std::vector<int>(toks.begin() + std::ptrdiff_t(keep), toks.end()), int(keep), false);
        ctxTokens_ = toks;
        ctxLastRow_ = rows.empty() ? std::vector<float>() : rows.back();
    } catch (...) {
        llama_memory_seq_rm(mem, 0, 0, -1);
        ctxTokens_.clear();
        ctxLastRow_.clear();
        throw;
    }
}

void LlamaScorer::setExtension(const std::vector<int>& ids) {
    const size_t common = commonPrefix(extTokens_, ids);
    if (common < extTokens_.size()) {
        llama_memory_seq_rm(llama_get_memory(ctx_), 0, llama_pos(ctxTokens_.size() + common), -1);
        extTokens_.resize(common);
        extRows_.resize(common);
    }
    if (common == ids.size()) return;
    std::vector<int> fresh(ids.begin() + std::ptrdiff_t(common), ids.end());
    auto rows = decodeSequenceZero(fresh, int(ctxTokens_.size() + common), true);
    extTokens_.insert(extTokens_.end(), fresh.begin(), fresh.end());
    for (auto& r : rows) extRows_.push_back(std::move(r));
}

std::vector<float> LlamaScorer::score(const std::vector<std::u32string>& candidates) {
    const float ninf = -std::numeric_limits<float>::infinity();
    std::vector<float> scores(candidates.size(), ninf);
    if (candidates.empty()) return scores;
    std::vector<std::vector<int>> ids;
    for (const auto& c : candidates) ids.push_back(tokenize(toU8(tailText_ + c)));
    if (int(ids.size()) > maxCandidates_) ids.resize(size_t(maxCandidates_));
    auto outputs = [&] {
        size_t n = 0;
        for (const auto& t : ids) n += t.empty() ? 0 : t.size() - 1;
        return n;
    };
    while (ids.size() > 2 && outputs() > size_t(kMaxOutputs)) ids.pop_back();
    size_t shortest = std::numeric_limits<size_t>::max();
    for (const auto& t : ids) shortest = std::min(shortest, t.size());
    if (shortest == 0 || ctxLastRow_.empty()) return scores;

    std::vector<int> prefix = ids[0];
    for (size_t c = 1; c < ids.size(); ++c) prefix.resize(commonPrefix(prefix, ids[c]));
    if (prefix.size() == shortest) prefix.pop_back();  // every candidate keeps >= 1 own token
    setExtension(prefix);
    const std::vector<float>& firstRow = extRows_.empty() ? ctxLastRow_ : extRows_.back();
    const int base = int(ctxTokens_.size() + extTokens_.size());

    struct Target {
        int cand;
        int batchIndex;  // -1: firstRow
        int token;
    };
    std::vector<Target> targets;
    llama_memory_t mem = llama_get_memory(ctx_);
    llama_batch& b = batchOf(batch_);
    b.n_tokens = 0;
    struct Cleanup {
        llama_memory_t mem;
        size_t n;
        ~Cleanup() {
            for (size_t c = 0; c < n; ++c) llama_memory_seq_rm(mem, llama_seq_id(c + 1), -1, -1);
        }
    } cleanup{mem, ids.size()};
    for (size_t c = 0; c < ids.size(); ++c) {
        const std::vector<int> s(ids[c].begin() + std::ptrdiff_t(prefix.size()), ids[c].end());
        targets.push_back({int(c), -1, s[0]});
        if (s.size() < 2) continue;
        llama_memory_seq_cp(mem, 0, llama_seq_id(c + 1), -1, -1);
        for (size_t j = 0; j + 1 < s.size(); ++j) {
            targets.push_back({int(c), b.n_tokens, s[j + 1]});
            addToBatch(s[j], base + int(j), int(c + 1), true);
        }
    }
    if (b.n_tokens > 0) {
        const int rc = llama_decode(ctx_, b);
        if (rc != 0) throw std::runtime_error("llama_decode failed: " + std::to_string(rc));
    }
    // resolve the rows on this thread (llama_get_logits_ith may reorder outputs internally)
    std::vector<const float*> rows;
    for (const auto& t : targets) rows.push_back(t.batchIndex < 0 ? firstRow.data() : llama_get_logits_ith(ctx_, t.batchIndex));
    const float firstLSE = logSumExp(firstRow.data(), vocabSize_);
    std::vector<float> lp(targets.size(), 0.0f);
    parallelFor(int(targets.size()), threads_, [&](int i) {
        const float lse = targets[size_t(i)].batchIndex < 0 ? firstLSE : logSumExp(rows[size_t(i)], vocabSize_);
        lp[size_t(i)] = rows[size_t(i)][targets[size_t(i)].token] - lse;
    });
    for (size_t c = 0; c < ids.size(); ++c) scores[c] = 0;
    for (size_t i = 0; i < targets.size(); ++i) scores[size_t(targets[i].cand)] += lp[i];
    return scores;
}

void LlamaScorer::warmUp() {
    int last;
    if (!extTokens_.empty()) last = extTokens_.back();
    else if (!ctxTokens_.empty()) last = ctxTokens_.back();
    else return;
    const int pos = int(ctxTokens_.size() + extTokens_.size());
    llama_batch& b = batchOf(batch_);
    b.n_tokens = 0;
    addToBatch(last, pos, 0, false);
    llama_decode(ctx_, b);
    llama_memory_seq_rm(llama_get_memory(ctx_), 0, pos, -1);
}

}  // namespace zl
