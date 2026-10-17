"""
Per-syllable incremental rescoring simulation: what the IME does while the user types.

After every completed syllable the lattice yields K-best candidates for the composition
buffer; the LLM rescores them with
  * a resident context cache (committed text, prefilled once per commit),
  * a shared-prefix extension (longest common token prefix of all candidates, reused across
    keystrokes, rolled back with O(1) trims),
  * one batched forward over only the divergent suffixes.
Reports per-step latency (p50/p90/p99) and the accuracy of the final step.

Usage: python zh_incremental.py <mlx-model> [--k 8] [--w 0.5] [--tolerant]
"""
import argparse
import json
import random
import re
import statistics
import time

import mlx.core as mx
from mlx_lm import load
from mlx_lm.models.cache import KVCache, make_prompt_cache

import zh_poc as Z
from latency_bench import parts, replicate


def lcp(seqs):
    p = 0
    while all(len(s) > p for s in seqs) and len({s[p] for s in seqs}) == 1:
        p += 1
    return seqs[0][:p]


class IncrementalScorer:
    def __init__(self, repo):
        self.model, self.tok = load(repo)
        self.body, self.head = parts(self.model)
        if not all(isinstance(c, KVCache) for c in make_prompt_cache(self.model)):
            raise SystemExit("incremental scorer needs a trimmable (pure attention) model")

    def _logp(self, h):
        lg = self.head(h)
        return lg - mx.logsumexp(lg, axis=-1, keepdims=True)

    def set_context(self, text):
        ids = self.tok.encode(text)
        self.cache = make_prompt_cache(self.model)
        self.ctx_last = self._logp(self.body(mx.array([ids]), self.cache)[:, -1:, :])
        mx.eval(self.ctx_last)
        self.ext, self.ext_lp = [], []  # shared-prefix tokens currently in cache + their log-probs

    def _set_ext(self, ids):
        common = 0
        while common < min(len(ids), len(self.ext)) and ids[common] == self.ext[common]:
            common += 1
        drop = len(self.ext) - common
        if drop:
            for c in self.cache:
                c.trim(drop)
            self.ext, self.ext_lp = self.ext[:common], self.ext_lp[:common]
        new = ids[common:]
        if new:
            lp = self._logp(self.body(mx.array([new]), self.cache))
            self.ext += new
            self.ext_lp += [lp[:, i: i + 1, :] for i in range(len(new))]

    def warm(self):
        """1-token forward on the resident cache, then roll it back (speculative-work stand-in)."""
        h = self.body(mx.array([[self.tok.encode("的", add_special_tokens=False)[0]]]), self.cache)
        mx.eval(h)
        for c in self.cache:
            c.trim(1)

    def score(self, texts):
        ids = [self.tok.encode(t, add_special_tokens=False) for t in texts]
        P = lcp(ids)
        if len(P) == min(len(x) for x in ids):  # keep >=1 divergent token per candidate
            P = P[:-1]
        self._set_ext(P)
        last = self.ext_lp[-1] if self.ext_lp else self.ctx_last
        suf = [x[len(P):] for x in ids]
        T, B = max(len(s) for s in suf), len(texts)
        arr = mx.array([s + [0] * (T - len(s)) for s in suf])
        tot = last[0, 0][arr[:, 0]]
        if T > 1:
            lp = self._logp(self.body(arr[:, :-1], replicate(self.cache, B)))
            g = mx.take_along_axis(lp, arr[:, 1:, None], axis=-1)[..., 0]
            mask = mx.array([[1.0] * (len(s) - 1) + [0.0] * (T - len(s)) for s in suf])
            tot = tot + (g * mask).sum(axis=1)
        mx.eval(tot)
        return tot.tolist(), len(P), B * T


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--k", type=int, default=8)
    ap.add_argument("--w", type=float, default=0.5, help="weight of the lexicon (unigram) prior")
    ap.add_argument("--tolerant", action="store_true", help="typo-tolerant lattice on typo input")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--pace-ms", type=float, default=0.0, help="sleep between syllables (simulated typing pace)")
    ap.add_argument("--max-segments", type=int, default=0)
    ap.add_argument("--qos", choices=["default", "user-interactive", "busy"], default="default",
                    help="user-interactive: pthread QoS 0x21 (P-core preference); busy: spin instead of sleeping")
    ap.add_argument("--warm", action="store_true",
                    help="during the pace gap, run a 1-token forward at each simulated intra-syllable keystroke "
                         "(stand-in for speculative pre-scoring that also keeps the GPU clocked up)")
    args = ap.parse_args()

    if args.qos == "user-interactive":
        import ctypes
        libc = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
        rc = libc.pthread_set_qos_class_self_np(0x21, 0)  # QOS_CLASS_USER_INTERACTIVE
        assert rc == 0, rc

    def gap(sec):
        if args.qos == "busy":
            end = time.perf_counter() + sec
            while time.perf_counter() < end:
                pass
        else:
            time.sleep(sec)

    lexlp, valid, best, cr = Z.load_lexicon()
    sc = IncrementalScorer(args.model)
    sc.set_context("\n")
    sc.score(["測試一下", "測試一次"])

    rng = random.Random(args.seed)
    step_ms, ctx_ms, tok_batched, prefix_len = [], [], [], []
    ok = tot = seg_ok = segs = 0
    lattice_ok = 0
    for line in open(Z.SENTS, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        ctx = "\n"
        for seg, punct in re.findall(r"([^，。？！、；：]+)([，。？！、；：]?)", line):
            r = Z.gold_readings(seg, best, cr)
            if r is None or len(r) != len(seg):
                ctx += seg + punct
                continue
            typed = list(r)
            if args.tolerant:
                pos = rng.randrange(len(typed))
                typed[pos], _ = Z.inject_typo(typed[pos], rng)
            t0 = time.perf_counter()
            sc.set_context(ctx)
            ctx_ms.append((time.perf_counter() - t0) * 1000)
            out = ""
            if args.max_segments and segs >= args.max_segments:
                break
            for i in range(1, len(typed) + 1):
                if args.pace_ms and args.warm:
                    for _ in range(3):  # ~3 keystrokes per syllable
                        gap(args.pace_ms / 3000)
                        sc.warm()
                elif args.pace_ms:
                    gap(args.pace_ms / 1000)
                cands = Z.nbest(typed[:i], lexlp, valid, args.k, args.tolerant)
                if len(cands) < 2:
                    out = cands[0][0] if cands else "□" * i
                    continue
                texts = [c[0] for c in cands]
                t0 = time.perf_counter()
                lps, plen, ntok = sc.score(texts)
                step_ms.append((time.perf_counter() - t0) * 1000)
                tok_batched.append(ntok)
                prefix_len.append(plen)
                out = texts[max(range(len(texts)), key=lambda q: lps[q] + cands[q][1][2] + args.w * cands[q][1][1])]
            top1 = Z.nbest(typed, lexlp, valid, 1, args.tolerant)
            lattice_ok += bool(top1) and top1[0][0] == seg
            ok += sum(a == b for a, b in zip(out, seg)) if len(out) == len(seg) else 0
            tot += len(seg)
            seg_ok += out == seg
            segs += 1
            ctx += seg + punct

    q = sorted(step_ms)
    if args.pace_ms:
        third = len(step_ms) // 3
        res_extra = dict(first_third_p50=round(statistics.median(step_ms[:third]), 1),
                         last_third_p50=round(statistics.median(step_ms[-third:]), 1))
    else:
        res_extra = {}
    res = dict(model=args.model, K=args.k, w=args.w, tolerant=args.tolerant, segments=segs, llm_steps=len(q),
               step_ms_p50=round(statistics.median(q), 1), step_ms_p90=round(q[int(0.9 * len(q)) - 1], 1),
               step_ms_p99=round(q[int(0.99 * len(q)) - 1], 1), step_ms_max=round(q[-1], 1),
               context_prefill_ms_p50=round(statistics.median(ctx_ms), 1),
               batched_tokens_p50=int(statistics.median(tok_batched)),
               shared_prefix_tokens_p50=int(statistics.median(prefix_len)),
               char_acc=round(100 * ok / tot, 2), seg_acc=round(100 * seg_ok / segs, 1),
               lattice_seg_acc=round(100 * lattice_ok / segs, 1), pace_ms=args.pace_ms, warm=args.warm, qos=args.qos, **res_extra)
    print(json.dumps(res, ensure_ascii=False))


if __name__ == "__main__":
    main()
