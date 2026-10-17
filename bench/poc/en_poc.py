"""
PoC: word-level English typo correction at the word boundary (what an IME can do when the
user presses space / punctuation), physical-keyboard noisy channel + small-LLM context.

Methods
  E0  SymSpell top-1 for out-of-vocabulary words (classic autocorrect, no context)
  E1  keyboard-aware error model + unigram prior (no context)
  E2  keyboard-aware error model + LLM P(word | left context)          <- word-boundary IME
  E3  keyboard-aware error model + LLM P(word + next 3 words | left)    <- delayed decision
A "keep" bias delta (nats) is added to the typed word; we sweep it to trade recall for
precision. Clean sentences (#CLEAN) measure over-correction of rare/technical words.

Usage: python en_poc.py <mlx-model> [--seed 0]
"""
import argparse
import importlib.resources
import json
import math
import random
import re
import statistics
import time

import mlx.core as mx
from mlx_lm import load
from mlx_lm.models.cache import make_prompt_cache
from symspellpy import SymSpell, Verbosity

from latency_bench import parts, replicate

ROWS = [("1234567890-=", 0.0), ("qwertyuiop[]", 0.5), ("asdfghjkl;'", 0.75), ("zxcvbnm,./", 1.25)]
POS = {k: (i + off, r) for r, (keys, off) in enumerate(ROWS) for i, k in enumerate(keys)}


def adjacent(a, b):
    if a not in POS or b not in POS or a == b:
        return False
    (x1, y1), (x2, y2) = POS[a], POS[b]
    return abs(y1 - y2) <= 1 and (x1 - x2) ** 2 + (y1 - y2) ** 2 <= 1.6


LETTERS = "abcdefghijklmnopqrstuvwxyz"
NEIGH = {c: [d for d in LETTERS if adjacent(c, d)] for c in LETTERS}

# error-model costs in nats (hand-set from Grudin 1983 / Dhakal 2018 error-type mix)
SUB_ADJ, SUB, DEL, INS_ADJ, INS, TRANS = 3.5, 7.0, 4.5, 3.5, 6.0, 3.5


def err_cost(typed, intended):
    """Weighted Damerau-Levenshtein: cost of turning `intended` into `typed`."""
    a, b = intended, typed
    n, m = len(a), len(b)
    D = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        D[i][0] = D[i - 1][0] + DEL
    for j in range(1, m + 1):
        D[0][j] = D[0][j - 1] + INS
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            if a[i - 1] == b[j - 1]:
                sub = 0.0
            else:
                sub = SUB_ADJ if adjacent(a[i - 1], b[j - 1]) else SUB
            prev = b[j - 2] if j >= 2 else ""
            ins = INS_ADJ if (prev and (prev == b[j - 1] or adjacent(prev, b[j - 1]))) else INS
            D[i][j] = min(D[i - 1][j - 1] + sub, D[i - 1][j] + DEL, D[i][j - 1] + ins)
            if i > 1 and j > 1 and a[i - 1] == b[j - 2] and a[i - 2] == b[j - 1]:
                D[i][j] = min(D[i][j], D[i - 2][j - 2] + TRANS)
    return D[n][m]


def inject(word, rng):
    w = word
    for _ in range(50):
        kind = rng.choices(["sub", "del", "ins", "trans", "dbl"], weights=[35, 20, 20, 20, 5])[0]
        i = rng.randrange(len(w))
        if kind == "sub" and w[i] in NEIGH:
            out = w[:i] + rng.choice(NEIGH[w[i]]) + w[i + 1:]
        elif kind == "del" and len(w) > 3:
            out = w[:i] + w[i + 1:]
        elif kind == "ins" and w[i] in NEIGH:
            out = w[:i + 1] + rng.choice(NEIGH[w[i]] + [w[i]]) + w[i + 1:]
        elif kind == "trans" and i < len(w) - 1 and w[i] != w[i + 1]:
            out = w[:i] + w[i + 1] + w[i] + w[i + 2:]
        elif kind == "dbl":
            dbl = [k for k in range(len(w) - 1) if w[k] == w[k + 1]]
            out = (w[:dbl[0]] + w[dbl[0] + 1:]) if dbl else (w[:i + 1] + w[i] + w[i + 1:])
        else:
            continue
        if out != w:
            return out, kind
    return w, "none"


class LM:
    def __init__(self, repo):
        self.model, self.tok = load(repo)
        self.body, self.head = parts(self.model)
        V = self.body.embed_tokens.weight.shape[0] if hasattr(self.body.embed_tokens, "weight") else None
        pieces = self.tok.convert_ids_to_tokens(list(range(len(self.tok.get_vocab()))))
        bnd = [bool(p) and (p[0] in "ĠĊ▁\n .,;:!?\"')(-" or p.startswith("<")) for p in pieces]
        if V and V > len(bnd):
            bnd += [False] * (V - len(bnd))
        self.bmask = mx.array(bnd)
        self.times = []

    def score(self, ctx, conts, boundary=True):
        """log P(cont | ctx) (+ log P(next token starts a new word)) for each continuation."""
        t0 = time.perf_counter()
        cache = make_prompt_cache(self.model)
        h = self.body(mx.array([self.tok.encode(ctx)]), cache)
        last = self.head(h[:, -1:, :])
        last = last - mx.logsumexp(last, axis=-1, keepdims=True)
        ids = [self.tok.encode(c, add_special_tokens=False) for c in conts]
        T = max(len(x) for x in ids)
        arr = mx.array([x + [0] * (T - len(x)) for x in ids])
        bc = replicate(cache, len(conts))
        lg = self.head(self.body(arr, bc))
        lg = lg - mx.logsumexp(lg, axis=-1, keepdims=True)
        first = last[0, 0][arr[:, 0]]
        rest = mx.take_along_axis(lg[:, :-1], arr[:, 1:, None], axis=-1)[..., 0] if T > 1 else None
        out = []
        mx.eval(first, lg) if rest is None else mx.eval(first, rest, lg)
        for b, x in enumerate(ids):
            s = first[b].item() + (rest[b, : len(x) - 1].sum().item() if len(x) > 1 else 0.0)
            if boundary:
                p = lg[b, len(x) - 1]
                s += mx.logsumexp(mx.where(self.bmask, p, -1e9)).item()
            out.append(s)
        self.times.append((time.perf_counter() - t0) * 1000)
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--maxcand", type=int, default=8)
    args = ap.parse_args()
    rng = random.Random(args.seed)

    sym = SymSpell(max_dictionary_edit_distance=2, prefix_length=7)
    sym.load_dictionary(str(importlib.resources.files("symspellpy") / "frequency_dictionary_en_82_765.txt"), 0, 1)
    counts = sym.words
    logN = math.log(sum(counts.values()))
    uni = lambda w: math.log(counts.get(w, 0.5)) - logN

    lm = LM(args.model)
    lm.score("\nHello", [" world"])  # warm-up
    lm.times.clear()

    sents, clean = [], False
    for line in open("data/en_sentences.txt", encoding="utf-8"):
        line = line.rstrip("\n")
        if line == "#CLEAN":
            clean = True
            continue
        toks = re.findall(r"[A-Za-z']+|[^A-Za-z']+", line)
        typo_at, typed = -1, list(toks)
        if not clean:
            cand = [i for i, t in enumerate(toks) if re.fullmatch(r"[a-z]{3,}", t)]
            typo_at = rng.choice(cand)
            typed[typo_at], kind = inject(toks[typo_at], rng)
        sents.append(dict(gold=toks, typed=typed, typo_at=typo_at, clean=clean))

    def candidates(w):
        lw = w.lower()
        sugg = sym.lookup(lw, Verbosity.ALL, max_edit_distance=2, include_unknown=False)
        cs = {s.term for s in sugg} | {lw}
        scored = sorted(cs, key=lambda c: err_cost(lw, c) - uni(c))
        keep = scored[: args.maxcand]
        if lw not in keep:
            keep = keep[:-1] + [lw]
        return keep

    def match_case(src, w):
        return w.capitalize() if src[:1].isupper() else w

    # precompute per word: candidates, error costs, LM scores (E2 left ctx, E3 +3 words)
    records = []
    for s in sents:
        for i, w in enumerate(s["typed"]):
            if not re.fullmatch(r"[A-Za-z']+", w) or (w[:1].isupper() and i > 0) or "'" in w:
                continue
            cs = candidates(w)
            if len(cs) == 1:
                records.append(dict(s=s, i=i, cands=cs, err=[0.0], uni=[uni(cs[0])], e2=[0.0], e3=[0.0], sym=cs[0]))
                continue
            left = "".join(s["gold"][:i])  # assume earlier words already correct
            lead = " " if i > 0 else ""
            ctx = "\n" + left.rstrip(" ") if i > 0 else "\n"
            conts = [lead + match_case(w, c) for c in cs]
            e2 = lm.score(ctx, conts, boundary=True)
            right = "".join(s["gold"][i + 1: i + 7])  # ~3 words of lookahead
            e3 = lm.score(ctx, [c + right for c in conts], boundary=False)
            top = sym.lookup(w.lower(), Verbosity.TOP, max_edit_distance=2)
            records.append(dict(s=s, i=i, cands=cs, err=[err_cost(w.lower(), c) for c in cs],
                                uni=[uni(c) for c in cs], e2=e2, e3=e3,
                                sym=top[0].term if top else w.lower()))

    def run(method, delta):
        fixed = typos = changes = good_changes = clean_words = false_changes = 0
        for r in records:
            s, i = r["s"], r["i"]
            typed = s["typed"][i].lower()
            gold = s["gold"][i].lower()
            if method == "E0":
                out = r["sym"] if typed not in counts else typed
            else:
                prior = {"E1": r["uni"], "E2": r["e2"], "E3": r["e3"]}[method]
                best = max(range(len(r["cands"])),
                           key=lambda k: prior[k] - r["err"][k] + (delta if r["cands"][k] == typed else 0.0))
                out = r["cands"][best]
            is_typo = i == s["typo_at"] and typed != gold
            if is_typo:
                typos += 1
                fixed += out == gold
            else:
                clean_words += 1
                false_changes += out != typed
            if out != typed:
                changes += 1
                good_changes += out == gold
        return dict(method=method, delta=delta, typo_fixed=round(100 * fixed / typos, 1),
                    false_change_per_1k=round(1000 * false_changes / clean_words, 1),
                    precision=round(100 * good_changes / max(1, changes), 1), changes=changes)

    res = {"model": args.model, "typos": sum(1 for s in sents if s["typo_at"] >= 0),
           "words_checked": len(records), "llm_calls": len(lm.times)}
    ts = sorted(lm.times)
    res["llm_ms_p50"], res["llm_ms_p90"] = round(statistics.median(ts), 1), round(ts[int(0.9 * len(ts)) - 1], 1)
    res["runs"] = [run("E0", 0)] + [run(m, d) for m in ("E1", "E2", "E3") for d in (0.0, 1.0, 2.0, 3.0, 4.0)]
    print(json.dumps(res, ensure_ascii=False, indent=1))
    # show a few decisions of E2 at delta=2
    shown = 0
    for r in records:
        s, i = r["s"], r["i"]
        typed, gold = s["typed"][i].lower(), s["gold"][i].lower()
        best = max(range(len(r["cands"])), key=lambda k: r["e2"][k] - r["err"][k] + (2.0 if r["cands"][k] == typed else 0))
        out = r["cands"][best]
        if (out != gold or typed != gold) and shown < 25:
            print(f"  {'OK ' if out == gold else 'BAD'} typed={typed:14s} gold={gold:14s} E2={out:14s} cands={r['cands']}")
            shown += 1


if __name__ == "__main__":
    main()
