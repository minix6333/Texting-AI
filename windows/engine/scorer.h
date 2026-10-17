// Scores candidate continuations with a local GGUF model through the llama.cpp C API
// (port of LlamaScorer.swift).
//
// KV layout (unified cache): sequence 0 = preamble + left context, followed by the longest common
// token prefix of the current candidates ("extension", kept across calls and trimmed/extended
// incrementally). Each candidate's own suffix is decoded in its own sequence (1...K), which shares
// the prefix cells through llama_memory_seq_cp — nothing is copied.
//
// Token healing: the tokenizer merges a run of letters (CJK included) with one leading punctuation
// mark or space. Tokenizing the context and a candidate separately would ask the model about a
// split it never saw, so the context's last pre-token ("tail") is held back and tokenized together
// with every candidate.
//
// Not thread-safe: call every method from one thread.
#pragma once

#include <string>
#include <vector>

struct llama_model;
struct llama_context;
struct llama_vocab;

namespace zl {

class LlamaScorer {
public:
    struct Options {
        std::string modelPath;
        int maxCandidates = 16;
        int maxContextTokens = 256;
        bool gpu = true;
        int threads = 4;
        /// Tokens before every context: the chat template's user-turn header is the in-distribution
        /// way to say "text a person typed starts here" (a bare start gives P(我) ≈ e^-22).
        std::string preamble = "<|im_start|>user\n";
    };

    /// Loads the model; throws std::runtime_error when it cannot.
    explicit LlamaScorer(const Options& options);
    ~LlamaScorer();
    LlamaScorer(const LlamaScorer&) = delete;
    LlamaScorer& operator=(const LlamaScorer&) = delete;

    /// Makes sequence 0 hold preamble + `text` minus its tail, reusing the cached common prefix.
    void setContext(const std::u32string& text);
    /// log P(candidate | context) for each candidate, up to a constant shared by all of them.
    std::vector<float> score(const std::vector<std::u32string>& candidates);
    /// A one-token forward on the resident context, rolled back (keeps caches warm while the user
    /// is still typing a syllable).
    void warmUp();

    int vocabSize() const { return vocabSize_; }
    int contextTokenCount() const { return int(ctxTokens_.size()); }

    /// Length (characters) of the end of `text` that could merge with following text into one
    /// pre-token: a run of letters with at most one space/punctuation mark before it, capped.
    static int healingTailLength(const std::u32string& text, int maxChars = 6);

private:
    std::vector<int> tokenize(const std::string& utf8, bool special = false) const;
    void setExtension(const std::vector<int>& ids);
    std::vector<std::vector<float>> decodeSequenceZero(const std::vector<int>& ids, int start, bool wantRows);
    void addToBatch(int token, int pos, int seq, bool logits);
    void warmUpKernels();

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    void* batch_ = nullptr;  // llama_batch*, kept opaque here
    int batchCapacity_ = 256;
    int vocabSize_ = 0;
    int maxCandidates_;
    int maxContextTokens_;
    int threads_;

    std::vector<int> preamble_;
    std::u32string tailText_;
    std::vector<int> ctxTokens_;
    std::vector<float> ctxLastRow_;
    std::vector<int> extTokens_;
    std::vector<std::vector<float>> extRows_;
};

}  // namespace zl
