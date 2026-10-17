"""
Multi-model evaluation for the Zhuyin PoC.

Candidates (K=16 n-best per segment and condition) are built once and cached, so every
model rescores exactly the same lists. K and the unigram weight are swept offline.

Usage: python zh_eval.py <mlx-model> [--prefix "\n"] [--tag name]
"""
import argparse
import json
import os
import random
import re
import statistics
import time

import zh_poc as Z

CAND = "results/zh_candidates.json"
CONDS = [("clean_strict", "read", False), ("clean_tol", "read", True),
         ("typo_strict", "typed", False), ("typo_tol", "typed", True)]


def build_candidates(seed=0, K=16):
    lexlp, valid, best, cr = Z.load_lexicon()
    rng = random.Random(seed)
    data = []
    for line in open(Z.SENTS, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        ctx = ""
        for seg, punct in re.findall(r"([^，。？！、；：]+)([，。？！、；：]?)", line):
            r = Z.gold_readings(seg, best, cr)
            if r is not None and len(r) == len(seg):
                typed = list(r)
                pos = rng.randrange(len(typed))
                typed[pos], kind = Z.inject_typo(typed[pos], rng)
                d = dict(ctx=ctx, gold=seg, read=r, typed=typed, pos=pos, kind=kind,
                         typo_valid=typed[pos] in valid)
                for cond, key, tol in CONDS:
                    d[cond] = [[t, v[1], v[2]] for t, v in Z.nbest(d[key], lexlp, valid, K, tol)]
                data.append(d)
            ctx += seg + punct
    os.makedirs("results", exist_ok=True)
    json.dump(data, open(CAND, "w"), ensure_ascii=False)
    return data


def trie_tokens(tok, texts):
    nodes = set()
    for t in texts:
        ids = tok.encode(t, add_special_tokens=False)
        for i in range(1, len(ids) + 1):
            nodes.add(tuple(ids[:i]))
    return len(nodes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--prefix", default="\n")
    ap.add_argument("--tag", default=None)
    args = ap.parse_args()
    data = json.load(open(CAND)) if os.path.exists(CAND) else build_candidates()

    sc = Z.Scorer(args.model)
    sc.score(args.prefix, ["測試一下", "測試"])  # warm-up

    # ---- accuracy: score the union of candidates per segment (chunks of 16)
    t0 = time.perf_counter()
    for d in data:
        texts = sorted({c[0] for cond, *_ in CONDS for c in d[cond]})
        d["llm"] = {}
        for i in range(0, len(texts), 16):
            chunk = texts[i:i + 16]
            for t, s in zip(chunk, sc.score(args.prefix + d["ctx"], chunk)):
                d["llm"][t] = s
    score_wall = time.perf_counter() - t0

    # ---- latency: realistic calls on the typo-tolerant lists, B=8 and B=16
    lat = {}
    for B in (4, 8, 16):
        sc.times.clear(); sc.tokcounts.clear()
        for d in data[:60]:
            texts = [c[0] for c in d["typo_tol"][:B]]
            if len(texts) > 1:
                sc.score(args.prefix + d["ctx"], texts)
        ts = sorted(sc.times)
        lat[f"B{B}"] = dict(p50=round(statistics.median(ts), 1), p90=round(ts[int(0.9 * len(ts)) - 1], 1),
                           padded_tokens_p50=int(statistics.median(sc.tokcounts)))
    trie_ratio = statistics.median(
        trie_tokens(sc.tok, [c[0] for c in d["typo_tol"][:16]]) /
        max(1, 16 * max(len(sc.tok.encode(c[0])) for c in d["typo_tol"][:16]))
        for d in data if len(d["typo_tol"]) >= 16)

    # ---- offline sweep
    sweep = {}
    for cond, *_ in CONDS:
        for K in (1, 4, 8, 16):
            for w in (0.0, 0.25, 0.5, 1.0):
                if K == 1 and w > 0:
                    continue
                ok = tot = seg_ok = fixed = typo_n = help_ = harm = 0
                for d in data:
                    cands = d[cond][:K]
                    if not cands:
                        out = "□" * len(d["gold"])
                    elif K == 1:
                        out = cands[0][0]
                    else:
                        out = max(cands, key=lambda c: d["llm"][c[0]] + c[2] + w * c[1])[0]
                    g = d["gold"]
                    ok += sum(a == b for a, b in zip(out, g)) if len(out) == len(g) else 0
                    tot += len(g)
                    seg_ok += out == g
                    top1 = d[cond][0][0] if d[cond] else ""
                    help_ += (top1 != g and out == g)
                    harm += (top1 == g and out != g)
                    if cond.startswith("typo") and d["typed"][d["pos"]] != d["read"][d["pos"]]:
                        typo_n += 1
                        fixed += len(out) == len(g) and out[d["pos"]] == g[d["pos"]]
                key = f"{cond}|K{K}|w{w}"
                sweep[key] = dict(char_acc=round(100 * ok / tot, 2), seg_acc=round(100 * seg_ok / len(data), 1),
                                  helped=help_, harmed=harm)
                if typo_n:
                    sweep[key]["typo_recovered"] = round(100 * fixed / typo_n, 1)

    out = dict(model=args.model, prefix=args.prefix, segments=len(data), score_wall_s=round(score_wall, 1),
               latency=lat, trie_over_padded=round(trie_ratio, 2), sweep=sweep)
    tag = args.tag or args.model.split("/")[-1]
    json.dump(out, open(f"results/zh_eval_{tag}.json", "w"), ensure_ascii=False, indent=1)
    best = lambda cond: max(((k, v) for k, v in sweep.items() if k.startswith(cond + "|") and "|K1|" not in k),
                            key=lambda kv: kv[1]["char_acc"])
    print(f"== {tag}  latency {lat}  trie/padded={out['trie_over_padded']}")
    for cond, *_ in CONDS:
        base = sweep[f"{cond}|K1|w0.0"]
        k, v = best(cond)
        dflt = sweep[f"{cond}|K16|w0.0"]
        print(f"  {cond:13s} lattice={base['char_acc']:6.2f}/{base['seg_acc']:5.1f}  "
              f"LLM(K16,w0)={dflt['char_acc']:6.2f}/{dflt['seg_acc']:5.1f}  best={k.split('|',1)[1]}:{v['char_acc']:.2f}/{v['seg_acc']} "
              f"help/harm={v['helped']}/{v['harmed']}" + (f" typo_rec={v.get('typo_recovered')}" if 'typo_recovered' in v else ""))


if __name__ == "__main__":
    main()
