#!/usr/bin/env python3
"""Call graph and per-function counts from a block profile.

Engine survey tool (docs/PLAN.md). The profile is the dispatched block addresses
the headless runner records with TP_PROFILE=<second> (profile.bin, uint32 each).
A dispatch to a function start counts as a call from the previous block's function.

    tools/survey_callgraph.py work/survey/profile.bin --survey work/main-survey.json --out work/survey/callgraph.json"""
import argparse
import array
import bisect
import collections
import json
from pathlib import Path

LOAD_SHIFT = 0x100000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('profile')
    ap.add_argument('--survey', default='work/main-survey.json')
    ap.add_argument('--out', required=True)
    ap.add_argument('--top', type=int, default=25)
    args = ap.parse_args()
    funcs = sorted(f + LOAD_SHIFT for f in json.loads(Path(args.survey).read_text())['funcs'])
    fset = set(funcs)
    def owner(pc):
        i = bisect.bisect_right(funcs, pc) - 1
        return funcs[i] if i >= 0 else None
    trace = array.array('I'); trace.frombytes(Path(args.profile).read_bytes())
    own, calls, entries, prev = collections.Counter(), collections.Counter(), collections.Counter(), None
    for pc in trace:
        own[owner(pc)] += 1
        if pc in fset:
            entries[pc] += 1
            if prev is not None:
                calls[(owner(prev), pc)] += 1
        prev = pc
    Path(args.out).write_text(json.dumps({
        'self': {hex(k): v for k, v in own.items() if k},
        'entries': {hex(k): v for k, v in entries.items()},
        'calls': [[hex(a), hex(b), n] for (a, b), n in calls.items() if a]}))
    for f, n in own.most_common(args.top):
        if f:
            print('%08x  blocks %9d  calls %7d' % (f, n, entries[f]))


if __name__ == '__main__':
    main()
