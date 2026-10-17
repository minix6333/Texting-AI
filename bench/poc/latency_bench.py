"""
Micro-benchmarks for an LLM-assisted IME on Apple Silicon (MLX).

What we measure (all batch-1 unless noted, latency in ms, p50 / p90):
  prefill_last1(N)   fresh context of N tokens, logits only for the last position
  prefill_lastK(N)   same, but full-vocab log-probs for the last K=8 positions (scoring)
  append(L,k)        context of L tokens already cached; feed k new tokens (+ logits)
                     -> per-keystroke cost of keeping the LM in sync with typing
  rollback           cost of undoing k tokens (trim for KV caches, snapshot restore for
                     recurrent/conv states in hybrid models)
  batch_score(L,B,m) prefix of L tokens cached once; score B candidates of m tokens each
  decode             per-token greedy decode at context ~64 (rewrite-style generation)

Usage: python latency_bench.py <hf-repo-or-path> [--quick]
"""
import argparse
import json
import statistics
import sys
import time

import mlx.core as mx
from mlx_lm import load
from mlx_lm.models.cache import KVCache, RotatingKVCache, make_prompt_cache

ZH = (
    "今天下午我們在會議室討論下一季的產品規劃，大家對於新的輸入法功能都很有興趣。"
    "工程師提到，如果能在使用者打字的同時即時修正錯字，就能大幅提升輸入效率。"
    "不過，延遲必須控制在幾十毫秒以內，否則使用者會覺得卡頓。我們決定先做一個小型的原型，"
    "用本地的語言模型來測試準確率與速度，再根據結果調整架構。另外，也要考慮注音輸入時常見的錯誤，"
    "例如聲調打錯、相鄰按鍵誤觸，以及同音字選錯的問題。"
)
EN = (
    "The meeting this afternoon focused on next quarter's roadmap. Everyone was excited about "
    "the new input method, especially the idea of fixing typos in real time while the user is "
    "still typing. However, the latency has to stay within a few dozen milliseconds, otherwise "
    "the experience will feel sluggish. We agreed to build a small prototype first, measure "
    "accuracy and speed with a local language model, and then refine the architecture based on "
    "the results."
)


def parts(model):
    lm = getattr(model, "language_model", model)
    body = lm.model
    head = lm.lm_head if hasattr(lm, "lm_head") else body.embed_tokens.as_linear
    return body, head


def timeit(fn, warmup=3, iters=25):
    for _ in range(warmup):
        fn()
    ts = []
    for _ in range(iters):
        t0 = time.perf_counter()
        fn()
        ts.append((time.perf_counter() - t0) * 1000)
    ts.sort()
    return {"p50": round(statistics.median(ts), 2), "p90": round(ts[max(0, int(0.9 * len(ts)) - 1)], 2)}


def snapshot(cache):
    """Capture enough state to roll the cache back (works for KVCache + ArraysCache)."""
    snap = []
    for c in cache:
        if isinstance(c, KVCache):
            snap.append(("kv", c.offset))
        elif hasattr(c, "cache") and isinstance(c.cache, list):
            snap.append(("arr", list(c.cache)))
        elif hasattr(c, "offset"):
            snap.append(("off", c.offset))
        else:
            snap.append(("?", None))
    return snap


def restore(cache, snap):
    for c, (kind, v) in zip(cache, snap):
        if kind == "kv":
            c.trim(c.offset - v)
        elif kind == "arr":
            c.cache = list(v)
        elif kind == "off":
            c.trim(c.offset - v)


def replicate(cache, B):
    """Broadcast a batch-1 cache to batch B (for scoring B candidates sharing a prefix)."""
    out = []
    for c in cache:
        # a sliding-window cache that has not wrapped yet is just a plain KV cache
        rotating_unwrapped = isinstance(c, RotatingKVCache) and c.offset <= c.max_size
        if isinstance(c, KVCache) or rotating_unwrapped:
            n = KVCache()
            k, v = c.keys[..., : c.offset, :], c.values[..., : c.offset, :]
            n.keys = mx.repeat(k, B, axis=0)
            n.values = mx.repeat(v, B, axis=0)
            n.offset = c.offset
            out.append(n)
        elif hasattr(c, "cache") and isinstance(c.cache, list):
            n = type(c)(size=len(c.cache))
            n.cache = [None if a is None else mx.repeat(a, B, axis=0) for a in c.cache]
            out.append(n)
        else:
            raise RuntimeError(f"cannot replicate cache type {type(c).__name__}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo")
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    mx.reset_peak_memory()
    t0 = time.perf_counter()
    model, tok = load(args.repo)
    load_s = time.perf_counter() - t0
    body, head = parts(model)
    weights_mb = mx.get_active_memory() / 1e6

    zh_ids, en_ids = tok.encode(ZH), tok.encode(EN)
    res = {
        "repo": args.repo,
        "load_s": round(load_s, 2),
        "weights_mb": round(weights_mb),
        "zh_chars_per_tok": round(len(ZH) / len(zh_ids), 2),
        "en_chars_per_tok": round(len(EN) / len(en_ids), 2),
    }
    ids = (zh_ids + en_ids) * 4

    def fwd(toks, cache, k_logits):
        h = body(mx.array([toks]), cache)
        logits = head(h[:, -k_logits:, :])
        lp = logits - mx.logsumexp(logits, axis=-1, keepdims=True)
        mx.eval(lp)
        return lp

    Ns = [8, 16, 32, 64, 128, 256] if not args.quick else [16, 64]
    res["prefill_last1"] = {}
    res["prefill_last8"] = {}
    for N in Ns:
        res["prefill_last1"][N] = timeit(lambda: fwd(ids[:N], make_prompt_cache(model), 1))
        res["prefill_last8"][N] = timeit(lambda: fwd(ids[:N], make_prompt_cache(model), 8))

    # incremental append on top of a cached context, then roll back
    res["append"] = {}
    for L in ([32, 128, 512] if not args.quick else [128]):
        cache = make_prompt_cache(model)
        fwd(ids[:L], cache, 1)
        for k in [1, 2, 4]:
            snap = snapshot(cache)
            new = ids[L : L + k]

            def step():
                fwd(new, cache, k)
                restore(cache, snap)

            res["append"][f"L{L}_k{k}"] = timeit(step)

    # rollback cost alone (after appending 4 tokens)
    cache = make_prompt_cache(model)
    fwd(ids[:128], cache, 1)
    snap = snapshot(cache)
    rb = []
    for _ in range(20):
        fwd(ids[128:132], cache, 1)
        t = time.perf_counter()
        restore(cache, snap)
        mx.eval([a for c in cache for a in (getattr(c, "cache", None) or []) if a is not None] or [mx.array(0)])
        rb.append((time.perf_counter() - t) * 1000)
    res["rollback_ms_p50"] = round(statistics.median(rb), 3)

    # batched candidate scoring with a shared cached prefix
    res["batch_score"] = {}
    L, m = 64, 4
    cache = make_prompt_cache(model)
    fwd(ids[:L], cache, 1)
    for B in ([1, 4, 8, 16, 32] if not args.quick else [8, 32]):
        cands = [ids[L + i : L + i + m] for i in range(B)]

        def score():
            bc = replicate(cache, B)
            h = body(mx.array(cands), bc)
            logits = head(h)
            lp = logits - mx.logsumexp(logits, axis=-1, keepdims=True)
            mx.eval(lp)

        res["batch_score"][f"L{L}_B{B}_m{m}"] = timeit(score)

    # greedy decode per token at ~64 context
    cache = make_prompt_cache(model)
    fwd(ids[:64], cache, 1)
    nxt = mx.array([[ids[64]]])
    for _ in range(3):
        mx.eval(head(body(nxt, cache)))
    ts = []
    for _ in range(32):
        t = time.perf_counter()
        y = head(body(nxt, cache))
        nxt = mx.argmax(y[:, -1:, :], axis=-1)
        mx.eval(nxt)
        ts.append((time.perf_counter() - t) * 1000)
    res["decode_ms_per_tok_p50"] = round(statistics.median(ts), 2)
    res["peak_mem_mb"] = round(mx.get_peak_memory() / 1e6)

    print(json.dumps(res, ensure_ascii=False))


if __name__ == "__main__":
    sys.exit(main())
