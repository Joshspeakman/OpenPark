#!/usr/bin/env python3
"""Recursive-descent LE census. Counts are candidates, not verified functions."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import capstone
from capstone import x86 as X
from le import LE


def survey(image):
    pending = image.apply_offset_fixups()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    code = [o for o in image.objects if o.flags & 4]
    def executable(addr):
        return any(o.base <= addr < o.base + o.size for o in code)
    functions = {image.entry}
    # Relocation targets are stronger evidence than arbitrary aligned data words.
    for fix in image.fixups:
        target = (image.objects[fix.target_object - 1].base + fix.target_offset) & 0xffffffff
        if fix.kind == 7 and executable(target):
            functions.add(target)
    todo = list(functions)
    visited, coverage = {}, set()
    indirect = collections.Counter()
    services = []
    while todo:
        addr = todo.pop()
        while executable(addr) and addr not in visited:
            ins = next(md.disasm(image.bytes_at(addr, 15), addr, 1), None)
            if ins is None:
                break
            visited[addr] = ins
            coverage.update(range(addr, addr + ins.size))
            mn = ins.mnemonic
            if mn in ('int', 'in', 'out', 'cli', 'sti', 'iretd', 'retf', 'lcall', 'ljmp'):
                services.append({'address': addr, 'instruction': mn, 'operands': ins.op_str})
            if mn in ('call', 'jmp') or mn.startswith('j') or mn.startswith('loop'):
                if ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                    target = ins.operands[0].imm & 0xffffffff
                    if executable(target):
                        todo.append(target)
                        if mn == 'call':
                            functions.add(target)
                else:
                    indirect[mn] += 1
                if mn == 'jmp':
                    break
            if mn in ('ret', 'retf', 'iretd', 'hlt', 'ud2', 'int3', 'ljmp'):
                break
            addr += ins.size
    mix = collections.Counter(i.mnemonic for i in visited.values())
    result = {
        'entry': image.entry, 'stack': image.stack,
        'objects': [{k: getattr(o, k) for k in ('size', 'base', 'flags', 'page_index', 'page_count')} for o in image.objects],
        'fixups': dict(collections.Counter(str(f.kind) for f in image.fixups)),
        'pending_fixups': len(pending), 'imports': 0,
        'function_candidates': len(functions), 'instructions': len(visited),
        'code_bytes': sum(o.size for o in code), 'covered_bytes': len(coverage),
        'coverage_percent': 100 * len(coverage) / sum(o.size for o in code),
        'mnemonics': dict(mix.most_common()), 'indirect': dict(indirect),
        'services': services, 'funcs': sorted(functions),
        'limitations': 'Static candidates; jump tables, hidden callbacks, code/data boundaries and service reachability remain to be verified.'}
    return result, visited


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    result, _ = survey(LE.read(args.exe))
    result['sha256'] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('sha256', 'entry', 'function_candidates', 'instructions', 'coverage_percent', 'fixups', 'pending_fixups', 'indirect')}, indent=2))

if __name__ == '__main__':
    main()
