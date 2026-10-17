#!/usr/bin/env python3
"""Disassemble MAIN.EXE's code, function by function, from a C-host checkpoint.

Engine survey tool (docs/PLAN.md). Function starts come from the survey
(tools/survey.py, base 0x10000) moved to where DOS/4GW loads the code (0x110000).

    tools/survey_disasm.py work/survey/rt-001.ckpt --survey work/main-survey.json --out work/survey/all.txt
    tools/survey_disasm.py work/survey/rt-001.ckpt --at 11b837 --count 80
Output stays under work/ (it is the game's code)."""
import argparse
import json
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).resolve().parent))
from spotcheck import load

LOAD_SHIFT = 0x100000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('checkpoint')
    ap.add_argument('--survey', default='work/main-survey.json')
    ap.add_argument('--out')
    ap.add_argument('--at', help='one address (hex) instead of everything')
    ap.add_argument('--count', type=int, default=60)
    args = ap.parse_args()
    mem = load(args.checkpoint)[2]
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    if args.at:
        a = int(args.at, 16)
        for k, i in enumerate(cs.disasm(mem[a:a + args.count * 8], a)):
            if k >= args.count:
                break
            print('%08x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
        return
    funcs = sorted({f + LOAD_SHIFT for f in json.loads(Path(args.survey).read_text())['funcs']})
    lines = []
    for k, f in enumerate(funcs):
        end = funcs[k + 1] if k + 1 < len(funcs) else f + 0x400
        lines.append('; ---- function %08x' % f)
        lines += ['%08x %s %s' % (i.address, i.mnemonic, i.op_str) for i in cs.disasm(mem[f:end], f)]
    Path(args.out).write_text('\n'.join(lines))
    print('%d functions, %d lines -> %s' % (len(funcs), len(lines), args.out))


if __name__ == '__main__':
    main()
