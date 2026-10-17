#!/usr/bin/env python3
"""Find MAIN.EXE functions that look alike: the same instructions in a similar order.

Engine survey tool (docs/PLAN.md). Before a game routine is rewritten natively
(runtime/native_*.c), its look-alikes show what to port with it: the 320x200 and
640x480 variants of a drawing routine, per-class copies, shared inlined helpers.
Works on the listing tools/survey_disasm.py writes (work/survey/all.txt);
comparison is on mnemonic sequences (operands differ between copies).

    tools/survey_similar.py work/survey/all.txt --at 15ec9c          # look-alikes of one function
    tools/survey_similar.py work/survey/all.txt --groups --floor 0.9 # families of near-identical code

The scoring (length and instruction-mix pre-filter, then a sequence ratio) is
adapted from decomp.wtf's similarity.py (MIT, (c) sam wolfs, i12bp8/decomp.wtf,
after the coddog idea); only the x86 listing reader and the grouping are new.
Output names game addresses only; it stays under work/ when saved."""
import argparse
import math
import re
from collections import Counter
from difflib import SequenceMatcher

MAX_OPS = 800                       # compare at most this many instructions of very large functions
PREFIXES = {'rep', 'repe', 'repz', 'repne', 'repnz', 'lock'}


def read_listing(path):
    """{address: [mnemonic, ...]} from a survey_disasm listing."""
    funcs, cur = {}, None
    for line in open(path, errors='replace'):
        m = re.match(r'; ---- function ([0-9a-f]+)', line)
        if m:
            cur = int(m.group(1), 16); funcs[cur] = []; continue
        parts = line.split()
        if cur is None or len(parts) < 2 or not re.fullmatch(r'[0-9a-f]{8}', parts[0]):
            continue
        op = parts[1]
        if op in PREFIXES and len(parts) > 2:
            op += ' ' + parts[2]
        funcs[cur].append(op)
    return funcs


def _cosine(a, b):
    dot = sum(v * b.get(k, 0) for k, v in a.items())
    na = math.sqrt(sum(v * v for v in a.values())); nb = math.sqrt(sum(v * v for v in b.values()))
    return dot / (na * nb) if na and nb else 0.0


def score(a, b):
    """0..1, 1 = the same instruction sequence."""
    if not a or not b:
        return 0.0
    return SequenceMatcher(None, a[:MAX_OPS], b[:MAX_OPS], autojunk=False).ratio()


def rank(target, candidates, top=5, floor=0.6, min_ops=8):
    """The `top` candidates most like `target`, best first, at least `floor` alike."""
    if len(target) < min_ops:       # tiny functions all look alike
        return []
    th, rough = Counter(target), []
    for name, ops in candidates.items():
        if not ops or not (0.6 <= len(ops) / len(target) <= 1 / 0.6):
            continue
        c = _cosine(th, Counter(ops))
        if c >= floor:
            rough.append((c, name))
    rough.sort(reverse=True)
    exact = [(name, score(target, candidates[name])) for _, name in rough[:150]]
    exact = [(n, s) for n, s in exact if s >= floor]
    exact.sort(key=lambda x: -x[1])
    return exact[:top]


def groups(funcs, floor, min_ops):
    """Families: each function joins the first earlier family whose founder it is `floor` alike."""
    fams = []
    for a in sorted(funcs, key=lambda a: -len(funcs[a])):
        ops = funcs[a]
        if len(ops) < min_ops:
            continue
        for fam in fams:
            f = funcs[fam[0]]
            if 0.85 <= len(ops) / len(f) <= 1 / 0.85 and score(ops, f) >= floor:
                fam.append(a); break
        else:
            fams.append([a])
    return [f for f in fams if len(f) > 1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('listing')
    ap.add_argument('--at', help='a function (hex): its look-alikes')
    ap.add_argument('--top', type=int, default=8)
    ap.add_argument('--floor', type=float, default=0.6)
    ap.add_argument('--min-ops', type=int, default=8)
    ap.add_argument('--groups', action='store_true', help='families of look-alike functions')
    args = ap.parse_args()
    funcs = read_listing(args.listing)
    if args.at:
        a = int(args.at, 16)
        if a not in funcs:
            raise SystemExit('no function at %x in the listing' % a)
        others = {k: v for k, v in funcs.items() if k != a}
        print('%06x: %d instructions' % (a, len(funcs[a])))
        for k, s in rank(funcs[a], others, args.top, args.floor, args.min_ops):
            print('  %06x  %.3f  %d instructions' % (k, s, len(funcs[k])))
    elif args.groups:
        fams = groups(funcs, args.floor, args.min_ops)
        for fam in sorted(fams, key=lambda f: (-len(f), f[0])):
            print('%d alike, %d instructions: %s' % (len(fam), len(funcs[fam[0]]), ' '.join('%06x' % a for a in sorted(fam))))
        print('%d families, %d functions in them, of %d' % (len(fams), sum(map(len, fams)), len(funcs)))
    else:
        ap.error('--at or --groups')


if __name__ == '__main__':
    main()
