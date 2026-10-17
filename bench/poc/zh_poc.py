"""
PoC: Zhuyin (Standard/大千 layout) -> Traditional Chinese conversion with
  (a) a classic unigram lattice (libchewing tsi.csv phrase frequencies),
  (b) keystroke-typo tolerance (noisy channel over neighbouring syllables),
  (c) small-LLM n-best rescoring (MLX).

Every segment (text between punctuation) is treated as one composition buffer.
Readings for the gold text come from libchewing phrases + pypinyin + particle rules.

Usage: python zh_poc.py <mlx-model> [--k 16] [--seed 0] [--typo-rate 1.0]
"""
import argparse
import json
import math
import random
import re
import statistics
import time
from collections import defaultdict

import mlx.core as mx
import opencc
from mlx_lm import load
from mlx_lm.models.cache import make_prompt_cache
from pypinyin import Style, lazy_pinyin

from latency_bench import parts, replicate

LEX = "data/libchewing-data/dict/chewing/tsi.csv"
SENTS = "data/zh_sentences.txt"

INITIALS = set("ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙ")
MEDIALS = set("ㄧㄨㄩ")
FINALS = set("ㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ")
TONE_OF = {"ˊ": 2, "ˇ": 3, "ˋ": 4, "˙": 5}
TONE_MARK = {1: "", 2: "ˊ", 3: "ˇ", 4: "ˋ", 5: "˙"}

# Standard (大千) layout
DACHEN = {
    "1": "ㄅ", "q": "ㄆ", "a": "ㄇ", "z": "ㄈ", "2": "ㄉ", "w": "ㄊ", "s": "ㄋ", "x": "ㄌ",
    "e": "ㄍ", "d": "ㄎ", "c": "ㄏ", "r": "ㄐ", "f": "ㄑ", "v": "ㄒ", "5": "ㄓ", "t": "ㄔ",
    "g": "ㄕ", "b": "ㄖ", "y": "ㄗ", "h": "ㄘ", "n": "ㄙ", "u": "ㄧ", "j": "ㄨ", "m": "ㄩ",
    "8": "ㄚ", "i": "ㄛ", "k": "ㄜ", ",": "ㄝ", "9": "ㄞ", "o": "ㄟ", "l": "ㄠ", ".": "ㄡ",
    "0": "ㄢ", "p": "ㄣ", ";": "ㄤ", "/": "ㄥ", "-": "ㄦ",
}
KEY_OF = {v: k for k, v in DACHEN.items()}
TONE_KEY = {1: " ", 2: "6", 3: "3", 4: "4", 5: "7"}
ROWS = [("1234567890-", 0.0), ("qwertyuiop", 0.5), ("asdfghjkl;", 0.75), ("zxcvbnm,./", 1.25)]
POS = {k: (i + off, r) for r, (keys, off) in enumerate(ROWS) for i, k in enumerate(keys)}


def neighbours(k):
    x, y = POS[k]
    return [k2 for k2, (x2, y2) in POS.items()
            if k2 != k and abs(y2 - y) <= 1 and (x2 - x) ** 2 + (y2 - y) ** 2 <= 1.6]


ADJ_TONES = {1: [], 2: [5], 3: [4], 4: [3], 5: [2]}  # 6<->7, 3<->4 are physically adjacent
ACCENT = [("ㄓ", "ㄗ"), ("ㄔ", "ㄘ"), ("ㄕ", "ㄙ"), ("ㄣ", "ㄥ"), ("ㄋ", "ㄌ"), ("ㄈ", "ㄏ")]
ACC = {a: b for a, b in ACCENT} | {b: a for a, b in ACCENT}

# log-penalties (nats) of the keystroke error model; hand-set for the PoC
C_TONE_ADJ, C_TONE, C_ADJ, C_ACC = -3.0, -4.5, -5.0, -4.0


def parse(s):
    ini = med = fin = ""
    tone = 1
    for ch in s:
        if ch in INITIALS:
            ini = ch
        elif ch in MEDIALS:
            med = ch
        elif ch in FINALS:
            fin = ch
        elif ch in TONE_OF:
            tone = TONE_OF[ch]
    return ini, med, fin, tone


def make(ini, med, fin, tone):
    return ini + med + fin + TONE_MARK[tone]


# ---------------------------------------------------------------- lexicon
def load_lexicon():
    lex = defaultdict(list)
    best_reading = {}
    total = 0
    for line in open(LEX, encoding="utf-8"):
        if line.startswith("#"):
            continue
        p = line.rstrip("\n").split(",")
        if len(p) < 3:
            continue
        phrase, freq, zy = p[0], int(p[1]), p[2]
        sylls = tuple(zy.split(" "))
        if len(sylls) != len(phrase) or not all(re.match(r"^[㄀-ㄯ˙ˊˇˋ]+$", s) for s in sylls):
            continue
        lex[sylls].append((phrase, freq))
        total += freq + 1
        if phrase not in best_reading or freq > best_reading[phrase][1]:
            best_reading[phrase] = (sylls, freq)
    logZ = math.log(total)
    lexlp = {k: [(ph, math.log(f + 1) - logZ) for ph, f in v] for k, v in lex.items()}
    valid = {s for k in lex for s in k}
    char_readings = defaultdict(set)
    for k, v in lex.items():
        if len(k) == 1:
            for ph, _ in v:
                char_readings[ph].add(k[0])
    return lexlp, valid, best_reading, char_readings


# ---------------------------------------------------------------- gold readings
t2s = opencc.OpenCC("t2s")


def gold_readings(seg, best_reading, char_readings):
    """Forward max-match on libchewing phrases (len>=2); singles via pypinyin + particle rules."""
    py = lazy_pinyin(t2s.convert(seg), style=Style.BOPOMOFO)
    out, i = [], 0
    while i < len(seg):
        for L in range(min(6, len(seg) - i), 1, -1):
            w = seg[i:i + L]
            if w in best_reading:
                out.extend(best_reading[w][0])
                i += L
                break
        else:
            c, r = seg[i], py[i]
            nxt = seg[i + 1] if i + 1 < len(seg) else ""
            if c in "的地著了":
                r = {"的": "ㄉㄜ˙", "地": "ㄉㄜ˙", "著": "ㄓㄜ˙", "了": "ㄌㄜ˙"}[c]
            elif c == "得":
                r = "ㄉㄜˊ" if nxt in "到了第出分知獎名" else "ㄉㄜ˙"
            if r not in char_readings.get(c, ()):  # fall back to any lexicon reading
                cands = sorted(char_readings.get(c, ()))
                if not cands:
                    return None
                r = cands[0]
            out.append(r)
            i += 1
    return out


# ---------------------------------------------------------------- typo model
def keys_of(s):
    ini, med, fin, tone = parse(s)
    return [KEY_OF[x] for x in (ini, med, fin) if x], tone


def inject_typo(s, rng):
    ini, med, fin, tone = parse(s)
    for _ in range(50):
        kind = rng.choices(["tone", "adj", "accent"], weights=[0.4, 0.35, 0.25])[0]
        if kind == "tone":
            opts = ADJ_TONES[tone] if (ADJ_TONES[tone] and rng.random() < 0.5) else [t for t in range(1, 6) if t != tone]
            return make(ini, med, fin, rng.choice(opts)), "tone"
        if kind == "adj":
            slots = [(n, x) for n, x in (("ini", ini), ("med", med), ("fin", fin)) if x]
            n, x = rng.choice(slots)
            nb = [DACHEN[k] for k in neighbours(KEY_OF[x]) if k in DACHEN]
            if not nb:
                continue
            new = rng.choice(nb)
            d = {"ini": ini, "med": med, "fin": fin}
            d[n] = ""
            # composer puts the new symbol in its own slot (may overwrite another slot)
            if new in INITIALS:
                d["ini"] = new
            elif new in MEDIALS:
                d["med"] = new
            else:
                d["fin"] = new
            t = make(d["ini"], d["med"], d["fin"], tone)
            if t != s:
                return t, "adj"
        if kind == "accent":
            cand = [x for x in (ini, fin) if x in ACC]
            if not cand:
                continue
            x = rng.choice(cand)
            t = make(ACC[x] if x == ini else ini, med, ACC[x] if x == fin else fin, tone)
            if t != s:
                return t, "accent"
    return s, "none"


def alternatives(t, valid):
    """Intended syllables that could have produced typed syllable t, with log-penalty."""
    alts = {}
    if t in valid:
        alts[t] = 0.0
    ini, med, fin, tone = parse(t)

    def add(s, c):
        if s in valid and s != t and c > alts.get(s, -1e9):
            alts[s] = c

    for tn in range(1, 6):
        if tn != tone:
            add(make(ini, med, fin, tn), C_TONE_ADJ if tn in ADJ_TONES[tone] else C_TONE)
    for slot, x in (("ini", ini), ("med", med), ("fin", fin)):
        if not x:
            continue
        for k in neighbours(KEY_OF[x]):
            if k not in DACHEN:
                continue
            new = DACHEN[k]
            d = {"ini": ini, "med": med, "fin": fin}
            d[slot] = ""
            d["ini" if new in INITIALS else "med" if new in MEDIALS else "fin"] = new
            add(make(d["ini"], d["med"], d["fin"], tone), C_ADJ)
        if x in ACC:
            add(make(ACC[x] if slot == "ini" else ini, med, ACC[x] if slot == "fin" else fin, tone), C_ACC)
    return alts


# ---------------------------------------------------------------- lattice + n-best
def nbest(typed, lexlp, valid, K, tolerant):
    n = len(typed)
    alts = [alternatives(t, valid) if tolerant else ({t: 0.0} if t in valid else {}) for t in typed]
    edges = defaultdict(list)
    for i in range(n):
        for j in range(i + 1, min(n, i + 6) + 1):
            exact = tuple(typed[i:j])
            variants = {exact: 0.0} if all(s in alts[i + q] for q, s in enumerate(exact)) else {}
            if tolerant:
                for q in range(j - i):
                    for s, c in alts[i + q].items():
                        if c < 0:
                            v = exact[:q] + (s,) + exact[q + 1:]
                            if all(x in alts[i + r] and alts[i + r][x] == 0.0 for r, x in enumerate(v) if r != q):
                                variants[v] = max(variants.get(v, -1e9), c)
            for v, c in variants.items():
                for ph, lp in lexlp.get(v, ()):
                    edges[i].append((j, ph, lp, c))
        if not any(j == i + 1 for j, *_ in edges[i]):
            edges[i].append((i + 1, "□", -20.0, 0.0))  # unconvertible syllable placeholder
    beams = [dict() for _ in range(n + 1)]
    beams[0][""] = (0.0, 0.0, 0.0)  # text -> (total, uni, typo)
    for i in range(n):
        items = sorted(beams[i].items(), key=lambda kv: -kv[1][0])[:K]
        for txt, (tot, uni, typo) in items:
            for j, ph, lp, c in edges[i]:
                key = txt + ph
                val = (tot + lp + c, uni + lp, typo + c)
                if key not in beams[j] or beams[j][key][0] < val[0]:
                    beams[j][key] = val
    return sorted(beams[n].items(), key=lambda kv: -kv[1][0])[:K]


# ---------------------------------------------------------------- LLM scorer
class Scorer:
    def __init__(self, repo):
        self.model, self.tok = load(repo)
        self.body, self.head = parts(self.model)
        self.times, self.tokcounts = [], []

    def score(self, ctx, texts):
        """log P(text | ctx) for each text, batched with a shared cached prefix."""
        t0 = time.perf_counter()
        ctx_ids = self.tok.encode(ctx)
        cache = make_prompt_cache(self.model)
        h = self.body(mx.array([ctx_ids]), cache)
        last = self.head(h[:, -1:, :])
        last = last - mx.logsumexp(last, axis=-1, keepdims=True)
        ids = [self.tok.encode(t, add_special_tokens=False) for t in texts]
        T = max(len(x) for x in ids)
        pad = [x + [0] * (T - len(x)) for x in ids]
        B = len(texts)
        bc = replicate(cache, B)
        arr = mx.array(pad)
        lp_first = last[0, 0][arr[:, 0]]
        if T > 1:
            hb = self.body(arr[:, :-1], bc)
            lg = self.head(hb)
            lg = lg - mx.logsumexp(lg, axis=-1, keepdims=True)
            tgt = arr[:, 1:]
            lp = mx.take_along_axis(lg, tgt[..., None], axis=-1)[..., 0]
            mask = mx.array([[1.0] * (len(x) - 1) + [0.0] * (T - len(x)) for x in ids])
            tot = lp_first + (lp * mask).sum(axis=1)
        else:
            tot = lp_first
        mx.eval(tot)
        self.times.append((time.perf_counter() - t0) * 1000)
        self.tokcounts.append(B * T + len(ctx_ids))
        return tot.tolist()


# ---------------------------------------------------------------- experiment
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--k", type=int, default=16)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--typo-rate", type=float, default=1.0, help="P(segment gets one typo)")
    ap.add_argument("--show", type=int, default=6)
    args = ap.parse_args()

    lexlp, valid, best_reading, char_readings = load_lexicon()
    rng = random.Random(args.seed)
    scorer = Scorer(args.model)
    scorer.score("\n", ["測試"])  # warm-up
    scorer.times.clear(); scorer.tokcounts.clear()

    data = []
    for line in open(SENTS, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        ctx = ""
        for seg, punct in re.findall(r"([^，。？！、；：]+)([，。？！、；：]?)", line):
            r = gold_readings(seg, best_reading, char_readings)
            if r is not None and len(r) == len(seg):
                typed = list(r)
                kind = "none"
                if rng.random() < args.typo_rate:
                    pos = rng.randrange(len(typed))
                    typed[pos], kind = inject_typo(typed[pos], rng)
                else:
                    pos = -1
                data.append(dict(ctx=ctx, gold=seg, read=r, typed=typed, pos=pos, kind=kind))
            ctx += seg + punct

    def evaluate(name, typed_key, tolerant, use_llm, K):
        ok_chars = tot_chars = ok_seg = typo_fixed = typo_total = 0
        changed_clean = 0
        examples = []
        for d in data:
            typed = d[typed_key]
            cands = nbest(typed, lexlp, valid, K, tolerant)
            if not cands:
                out = "□" * len(typed)
            elif not use_llm or len(cands) == 1:
                out = cands[0][0]
            else:
                texts = [c[0] for c in cands]
                lps = scorer.score("\n" + d["ctx"], texts)
                best = max(range(len(texts)), key=lambda q: lps[q] + cands[q][1][2])
                out = texts[best]
            g = d["gold"]
            n_ok = sum(a == b for a, b in zip(out, g)) if len(out) == len(g) else 0
            ok_chars += n_ok
            tot_chars += len(g)
            ok_seg += out == g
            if typed_key == "typed" and d["pos"] >= 0 and d["typed"][d["pos"]] != d["read"][d["pos"]]:
                typo_total += 1
                typo_fixed += len(out) == len(g) and out[d["pos"]] == g[d["pos"]]
            if out != g and len(examples) < args.show:
                examples.append(f"{g} → {out}  (typed: {' '.join(typed)})")
        res = dict(name=name, char_acc=round(100 * ok_chars / tot_chars, 2), seg_acc=round(100 * ok_seg / len(data), 2))
        if typo_total:
            res["typo_char_recovered"] = round(100 * typo_fixed / typo_total, 1)
        return res, examples

    for d in data:
        d["clean"] = d["read"]

    results = {"model": args.model, "segments": len(data), "chars": sum(len(d["gold"]) for d in data), "K": args.k}
    n_typo = sum(1 for d in data if d["pos"] >= 0 and d["typed"][d["pos"]] != d["read"][d["pos"]])
    results["typo_segments"] = n_typo
    results["typo_kinds"] = {k: sum(1 for d in data if d["kind"] == k) for k in ("tone", "adj", "accent", "none")}
    results["typo_invalid_syllable"] = sum(1 for d in data if d["pos"] >= 0 and d["typed"][d["pos"]] not in valid)

    # oracle coverage of the n-best list
    for K in (1, 4, 8, 16, 32):
        cov_clean = sum(any(c[0] == d["gold"] for c in nbest(d["clean"], lexlp, valid, K, False)) for d in data)
        cov_typo = sum(any(c[0] == d["gold"] for c in nbest(d["typed"], lexlp, valid, K, True)) for d in data)
        results[f"oracle@{K}"] = dict(clean_strict=round(100 * cov_clean / len(data), 1), typo_tolerant=round(100 * cov_typo / len(data), 1))

    runs = [
        ("clean | 詞頻 lattice (傳統輸入法)", "clean", False, False),
        ("clean | 詞頻 + LLM rescoring", "clean", False, True),
        ("clean | 容錯 + LLM (檢查誤改)", "clean", True, True),
        ("typo  | 詞頻 lattice (不容錯)", "typed", False, False),
        ("typo  | 容錯 lattice 無 LLM", "typed", True, False),
        ("typo  | 不容錯 + LLM", "typed", False, True),
        ("typo  | 容錯 + LLM", "typed", True, True),
    ]
    results["runs"] = []
    all_examples = {}
    for name, key, tol, llm in runs:
        scorer.times.clear(); scorer.tokcounts.clear()
        r, ex = evaluate(name, key, tol, llm, args.k)
        if llm and scorer.times:
            ts = sorted(scorer.times)
            r["llm_ms_p50"] = round(statistics.median(ts), 1)
            r["llm_ms_p90"] = round(ts[int(0.9 * len(ts)) - 1], 1)
            r["tokens_per_call_p50"] = int(statistics.median(scorer.tokcounts))
        results["runs"].append(r)
        all_examples[name] = ex
    print(json.dumps(results, ensure_ascii=False, indent=1))
    for k, v in all_examples.items():
        print("\n##", k)
        for e in v:
            print("  ", e)


if __name__ == "__main__":
    main()
