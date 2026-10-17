import Accelerate
import CLlama
import Foundation

public enum LlamaScorerError: Error {
    case modelLoad(String)
    case contextInit
    case decode(Int32)
}

/// Scores candidate continuations with a local GGUF model through the llama.cpp C API.
///
/// KV layout (unified cache): sequence 0 = preamble + left context, followed by the
/// longest common token prefix of the current candidates ("extension", kept across calls and
/// trimmed/extended incrementally). Each candidate's divergent suffix is decoded in its own
/// sequence (1...K) that shares the prefix cells via `llama_memory_seq_cp` — no copying.
///
/// Token boundaries: the tokenizer merges a run of letters (CJK included) with one leading
/// punctuation mark or space, e.g. "，我" can be one token. Tokenizing the context and a candidate
/// separately would ask the model about a split it never saw and penalise exactly the candidates
/// that merge with the context. So the context's last pre-token ("tail") is held back and
/// tokenized together with every candidate ("token healing").
///
/// Not thread-safe: call every method from one serial queue.
public final class LlamaScorer {
    private let model: OpaquePointer
    private let ctx: OpaquePointer
    private let vocab: OpaquePointer
    private let mem: llama_memory_t
    /// Tokens before every context. A bare start (a lone "\n" or <|endoftext|>) gives a degenerate
    /// first-token distribution for this chat-tuned model (P(我) ≈ e^-22); the user-turn header is
    /// the in-distribution way to say "text a person typed starts here".
    private var preamble: [llama_token] = []
    private var tailText = ""
    public let vocabSize: Int
    public let maxCandidates: Int
    public let maxContextTokens: Int
    static let maxOutputs = 128

    private var ctxTokens: [llama_token] = []
    private var ctxLastRow: [Float] = []
    private var extTokens: [llama_token] = []
    private var extRows: [[Float]] = []
    private var batch: llama_batch
    private let batchCapacity: Int

    public init(modelPath: String, maxCandidates: Int = 16, maxContextTokens: Int = 256, gpu: Bool = true,
                preamble preambleText: String = "<|im_start|>user\n") throws {
        llama_log_set({ _, _, _ in }, nil)
        llama_backend_init()
        var mp = llama_model_default_params()
        mp.n_gpu_layers = gpu ? 999 : 0
        guard let m = llama_model_load_from_file(modelPath, mp) else { throw LlamaScorerError.modelLoad(modelPath) }
        model = m
        var cp = llama_context_default_params()
        cp.n_ctx = 1024           // context (<=256) + candidate prefix + 16 short suffixes
        cp.n_batch = 256
        cp.n_ubatch = 256
        cp.n_outputs_max = UInt32(LlamaScorer.maxOutputs)
        cp.n_seq_max = UInt32(maxCandidates + 1)
        cp.kv_unified = true
        cp.no_perf = true
        cp.n_threads = 4
        cp.n_threads_batch = 4
        guard let c = llama_init_from_model(m, cp) else { throw LlamaScorerError.contextInit }
        ctx = c
        vocab = llama_model_get_vocab(m)
        mem = llama_get_memory(c)
        vocabSize = Int(llama_vocab_n_tokens(vocab))
        self.maxCandidates = maxCandidates
        self.maxContextTokens = maxContextTokens
        batchCapacity = 256
        batch = llama_batch_init(Int32(batchCapacity), 0, Int32(maxCandidates + 1))
        preamble = tokenize(preambleText, special: true)
        if preamble.isEmpty { preamble = tokenize("\n") }
        try warmUpKernels()
    }

    /// The first decode of each shape compiles Metal pipelines (~150 ms); do it at load time.
    private func warmUpKernels() throws {
        try setContext("今天天氣很好，我們")
        _ = try score(["去公園散步", "去公元散步", "去攻園散布", "去公園散布"])
        _ = try score(["今天", "金天"])
        try setContext("")
    }

    deinit {
        llama_batch_free(batch)
        llama_free(ctx)
        llama_model_free(model)
    }

    // MARK: tokens

    public func tokenize(_ text: String, special: Bool = false) -> [llama_token] {
        let len = Int32(text.utf8.count)
        var tokens = [llama_token](repeating: 0, count: Int(len) + 8)
        var n = llama_tokenize(vocab, text, len, &tokens, Int32(tokens.count), false, special)
        if n < 0 {
            tokens = [llama_token](repeating: 0, count: Int(-n))
            n = llama_tokenize(vocab, text, len, &tokens, Int32(tokens.count), false, special)
        }
        return Array(tokens.prefix(Int(max(n, 0))))
    }

    // MARK: context (sequence 0)

    /// Makes sequence 0 hold preamble + `text` minus its tail (tail-truncated), reusing the cached
    /// common prefix; the tail is prepended to every candidate in `score`.
    public func setContext(_ text: String) throws {
        let cut = LlamaScorer.healingTailLength(text)
        tailText = String(text.suffix(cut))
        var body = tokenize(String(text.dropLast(cut)))
        let room = max(0, maxContextTokens - preamble.count)
        if body.count > room { body = Array(body.suffix(room)) }
        let toks = preamble + body
        let common = commonPrefix(ctxTokens, toks)
        if common == toks.count && common == ctxTokens.count && !ctxLastRow.isEmpty {
            return  // unchanged: keep the cached candidate prefix too
        }
        // keep [0, keep), recompute the rest (at least the last token, to get its logits)
        let keep = min(common, toks.count - 1)
        _ = llama_memory_seq_rm(mem, 0, Int32(keep), -1)
        extTokens.removeAll()
        extRows.removeAll()
        ctxTokens = Array(toks.prefix(keep))
        let rows = try decodeSequenceZero(Array(toks[keep...]), start: keep, wantRows: false)
        ctxTokens = toks
        ctxLastRow = rows.last ?? []
    }

    public var contextTokenCount: Int { ctxTokens.count }

    /// Length (in characters) of the end of `text` that could merge with following text into one
    /// pre-token: a run of letters with at most one space/punctuation mark before it, capped.
    /// Newlines and digits never merge forward. A cut inside a long run is harmless: it is shared
    /// by all candidates.
    static func healingTailLength(_ text: String, maxChars: Int = 6) -> Int {
        var n = 0
        for c in text.reversed() {
            if n >= maxChars { break }
            if c.isLetter { n += 1; continue }
            if !c.isNumber, !c.isNewline { n += 1 }
            break
        }
        return n
    }

    private func dropExtension() {
        guard !extTokens.isEmpty else { return }
        _ = llama_memory_seq_rm(mem, 0, Int32(ctxTokens.count), -1)
        extTokens.removeAll()
        extRows.removeAll()
    }

    /// Ensures sequence 0 = context + `ids`; keeps logits rows for every extension position.
    private func setExtension(_ ids: [llama_token]) throws {
        let common = commonPrefix(extTokens, ids)
        if common < extTokens.count {
            _ = llama_memory_seq_rm(mem, 0, Int32(ctxTokens.count + common), -1)
            extTokens.removeSubrange(common...)
            extRows.removeSubrange(common...)
        }
        let new = Array(ids[common...])
        guard !new.isEmpty else { return }
        let rows = try decodeSequenceZero(new, start: ctxTokens.count + common, wantRows: true)
        extTokens += new
        extRows += rows
    }

    /// Decodes `ids` into sequence 0 at positions start..., returning copied logits rows
    /// (all positions when `wantRows`, otherwise only the last one).
    private func decodeSequenceZero(_ ids: [llama_token], start: Int, wantRows: Bool) throws -> [[Float]] {
        var rows: [[Float]] = []
        var offset = 0
        while offset < ids.count {
            let chunk = ids[offset..<min(ids.count, offset + batchCapacity)]
            batch.n_tokens = 0
            for (i, t) in chunk.enumerated() {
                let isLast = offset + i == ids.count - 1
                add(token: t, pos: start + offset + i, seq: 0, logits: wantRows || isLast)
            }
            let rc = llama_decode(ctx, batch)
            guard rc == 0 else { throw LlamaScorerError.decode(rc) }
            for i in 0..<Int(batch.n_tokens) where batch.logits[i] != 0 {
                let p = llama_get_logits_ith(ctx, Int32(i))!
                rows.append(Array(UnsafeBufferPointer(start: p, count: vocabSize)))
            }
            offset += chunk.count
        }
        return rows
    }

    private func add(token: llama_token, pos: Int, seq: llama_seq_id, logits: Bool) {
        let i = Int(batch.n_tokens)
        batch.token[i] = token
        batch.pos[i] = llama_pos(pos)
        batch.n_seq_id[i] = 1
        batch.seq_id[i]![0] = seq
        batch.logits[i] = logits ? 1 : 0
        batch.n_tokens += 1
    }

    // MARK: scoring

    /// log P(candidate | context) for each candidate, up to a constant shared by all candidates.
    public func score(_ candidates: [String]) throws -> [Float] {
        precondition(candidates.count <= maxCandidates)
        var ids = candidates.map { tokenize(tailText + $0) }
        while ids.count > 2, ids.reduce(0, { $0 + max(0, $1.count - 1) }) > LlamaScorer.maxOutputs { ids.removeLast() }
        guard let shortest = ids.map(\.count).min(), shortest > 0 else { return candidates.map { _ in -.infinity } }
        var prefix = lcp(ids)
        if prefix.count == shortest { prefix.removeLast() }  // every candidate keeps >= 1 own token
        try setExtension(prefix)
        let firstRow = extRows.last ?? ctxLastRow
        let base = ctxTokens.count + extTokens.count
        let suffixes = ids.map { Array($0[prefix.count...]) }

        // targets: (candidate, row source) — row source -1 = firstRow, otherwise batch index
        var targets: [(cand: Int, batchIndex: Int, token: llama_token)] = []
        batch.n_tokens = 0
        for (c, s) in suffixes.enumerated() {
            targets.append((c, -1, s[0]))
            guard s.count > 1 else { continue }
            llama_memory_seq_cp(mem, 0, llama_seq_id(c + 1), -1, -1)
            for j in 0..<(s.count - 1) {
                targets.append((c, Int(batch.n_tokens), s[j + 1]))
                add(token: s[j], pos: base + j, seq: llama_seq_id(c + 1), logits: true)
            }
        }
        defer {
            for c in 0..<ids.count { _ = llama_memory_seq_rm(mem, llama_seq_id(c + 1), -1, -1) }
        }
        if batch.n_tokens > 0 {
            let rc = llama_decode(ctx, batch)
            guard rc == 0 else { throw LlamaScorerError.decode(rc) }
        }
        var lp = [Float](repeating: 0, count: targets.count)
        let n = vocabSize
        firstRow.withUnsafeBufferPointer { first in
            // llama_get_logits_ith may reorder outputs internally: resolve pointers on this thread
            let rows: [UnsafePointer<Float>] = targets.map {
                $0.batchIndex < 0 ? first.baseAddress! : UnsafePointer(llama_get_logits_ith(ctx, Int32($0.batchIndex))!)
            }
            lp.withUnsafeMutableBufferPointer { out in
                DispatchQueue.concurrentPerform(iterations: targets.count) { i in
                    out[i] = LlamaScorer.logProb(rows[i], n, Int(targets[i].token))
                }
            }
        }
        var scores = [Float](repeating: -.infinity, count: candidates.count)
        for c in 0..<ids.count { scores[c] = 0 }
        for (i, t) in targets.enumerated() { scores[t.cand] += lp[i] }
        return scores
    }

    /// log softmax(row)[target], vectorised.
    static func logProb(_ row: UnsafePointer<Float>, _ n: Int, _ target: Int) -> Float {
        var maxv: Float = 0
        vDSP_maxv(row, 1, &maxv, vDSP_Length(n))
        let scratch = UnsafeMutablePointer<Float>.allocate(capacity: n)
        defer { scratch.deallocate() }
        var negMax = -maxv
        vDSP_vsadd(row, 1, &negMax, scratch, 1, vDSP_Length(n))
        var count = Int32(n)
        vvexpf(scratch, scratch, &count)
        var sum: Float = 0
        vDSP_sve(scratch, 1, &sum, vDSP_Length(n))
        return row[target] - maxv - log(sum)
    }

    /// A one-token forward on the resident context, then rolled back (keeps the GPU warm and
    /// stands in for speculative pre-scoring while the user is still typing a syllable).
    public func warmUp() {
        guard let last = extTokens.last ?? ctxTokens.last else { return }
        let pos = ctxTokens.count + extTokens.count
        batch.n_tokens = 0
        add(token: last, pos: pos, seq: 0, logits: false)
        _ = llama_decode(ctx, batch)
        _ = llama_memory_seq_rm(mem, 0, Int32(pos), -1)
    }

    private func commonPrefix(_ a: [llama_token], _ b: [llama_token]) -> Int {
        var i = 0
        while i < a.count, i < b.count, a[i] == b[i] { i += 1 }
        return i
    }

    private func lcp(_ seqs: [[llama_token]]) -> [llama_token] {
        guard var p = seqs.first else { return [] }
        for s in seqs.dropFirst() { p = Array(p.prefix(commonPrefix(p, s))) }
        return p
    }
}
