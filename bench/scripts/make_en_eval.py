"""Builds bench/en_eval.json: English words as typed, in context, with the word that was meant.

kinds:
  human   - real misspellings (Norvig/Birkbeck test sets) placed into sentences of big.txt
  keyboard- synthetic finger slips (adjacent key, swapped / missing / doubled letter) of common words
  keep    - correct words that are NOT in the dictionary (names, rare words): must be left alone
Only typed forms that are not dictionary words are kept (the corrector never touches those).
"""
import json, random, re, sys
from collections import defaultdict

root = sys.argv[1]  # repository root (expects bench/en_data/ from bench/scripts/fetch_en_data.sh)
random.seed(7)
words = {}
for line in open(f"{root}/data/en_freq.txt", encoding="utf-8"):
    p = line.split()
    if len(p) >= 2: words[p[0]] = int(p[1])
ranked = sorted(words, key=lambda w: -words[w])
common = set(ranked[:20000])

text = open(f"{root}/bench/en_data/big.txt", encoding="utf-8", errors="ignore").read()
text = re.sub(r"\s+", " ", text)
sentences = [s.strip() for s in re.split(r"(?<=[.!?])\s+", text) if 40 <= len(s.strip()) <= 260]
random.shuffle(sentences)
sentences = sentences[:40000]

occ = defaultdict(list)  # word -> [(sentence index, offset)]
for si, s in enumerate(sentences):
    for m in re.finditer(r"[A-Za-z']+", s):
        w = m.group()
        if m.start() < 8: continue  # leave some context before the word
        if len(occ[w.lower()]) < 5: occ[w.lower()].append((si, m.start(), w))

def ctx_of(si, off):
    prev = sentences[si - 1] + " " if si > 0 else ""
    return (prev + sentences[si][:off])[-220:]

items = []
# human misspellings
for f in ("spell-testset1.txt", "spell-testset2.txt"):
    for line in open(f"{root}/bench/en_data/{f}", encoding="utf-8"):
        if ":" not in line: continue
        right, wrongs = line.split(":", 1)
        right = right.strip()
        if right not in words or not occ.get(right): continue
        si, off, w = random.choice(occ[right])
        if w != right: continue  # keep lowercase occurrences only
        for wrong in wrongs.split():
            if wrong in words or len(wrong) < 3: continue
            items.append({"kind": "human", "ctx": ctx_of(si, off), "typed": wrong, "gold": right})

# keyboard slips
rows = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
pos = {c: (r, i + (0, 0.25, 0.75)[r]) for r, row in enumerate(rows) for i, c in enumerate(row)}
def neighbours(c):
    if c not in pos: return []
    r, x = pos[c]
    return [d for d, (r2, x2) in pos.items() if d != c and abs(r2 - r) <= 1 and abs(x2 - x) <= 1.0]
def slip(w):
    i = random.randrange(len(w))
    kind = random.choice(["sub", "sub", "swap", "drop", "insert", "double"])
    if kind == "sub" and neighbours(w[i]): return w[:i] + random.choice(neighbours(w[i])) + w[i + 1:]
    if kind == "swap" and i < len(w) - 1: return w[:i] + w[i + 1] + w[i] + w[i + 2:]
    if kind == "drop" and len(w) > 4: return w[:i] + w[i + 1:]
    if kind == "insert" and neighbours(w[i]): return w[:i] + random.choice(neighbours(w[i])) + w[i:]
    if kind == "double": return w[:i] + w[i] + w[i:]
    return None
cands = [w for w in occ if w in common and 4 <= len(w) <= 11 and w.isalpha()]
random.shuffle(cands)
n = 0
for w in cands:
    t = slip(w)
    if not t or t in words or t == w: continue
    si, off, orig = random.choice(occ[w])
    if orig != w: continue
    items.append({"kind": "keyboard", "ctx": ctx_of(si, off), "typed": t, "gold": w})
    n += 1
    if n >= 300: break

# correct words outside the dictionary: must stay
keep = [w for w in occ if w not in words and len(w) >= 3 and re.fullmatch(r"[a-z]+", w)]
random.shuffle(keep)
for w in keep[:150]:
    si, off, orig = occ[w][0]
    items.append({"kind": "keep", "ctx": ctx_of(si, off), "typed": orig, "gold": orig})
names = [w for w in occ if w.capitalize() not in words and w not in words and len(w) >= 3]
random.shuffle(names)
k = 0
for w in names:
    si, off, orig = occ[w][0]
    if not orig[0].isupper() or sentences[si][:off].rstrip().endswith((".", "!", "?")): continue
    items.append({"kind": "keep", "ctx": ctx_of(si, off), "typed": orig, "gold": orig})
    k += 1
    if k >= 100: break

json.dump(items, open(f"{root}/bench/en_eval.json", "w"), ensure_ascii=False, indent=0)
from collections import Counter
print(len(items), Counter(i["kind"] for i in items))
