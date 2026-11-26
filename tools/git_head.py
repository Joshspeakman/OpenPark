#!/usr/bin/env python3
"""git_head.py: the checkout's commit, short, read from .git itself (no git needed: MSYS2's shell may have no git of
its own, and Windows' git isn't on its path). For the launch window's NIGHTLY label (tools/build_windows.sh,
tools/build_linux.sh). Prints nothing and exits 1 if it can't tell."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    git = os.path.join(ROOT, '.git')
    if os.path.isfile(git):                                    # (a worktree: "gitdir: <path>")
        with open(git) as f:
            git = os.path.join(ROOT, f.read().split(':', 1)[1].strip())
    common = git
    if os.path.isfile(os.path.join(git, 'commondir')):         # (a worktree's refs live in the main .git)
        with open(os.path.join(git, 'commondir')) as f:
            common = os.path.normpath(os.path.join(git, f.read().strip()))
    with open(os.path.join(git, 'HEAD')) as f:
        head = f.read().strip()
    if not head.startswith('ref:'):
        print(head[:7])
        return
    ref = head.split(None, 1)[1]
    for base in (git, common):
        p = os.path.join(base, *ref.split('/'))
        if os.path.isfile(p):
            with open(p) as f:
                print(f.read().strip()[:7])
            return
    packed = os.path.join(common, 'packed-refs')
    if os.path.isfile(packed):
        with open(packed) as f:
            for line in f:
                parts = line.split()
                if len(parts) == 2 and parts[1] == ref:
                    print(parts[0][:7])
                    return
    sys.exit(1)


if __name__ == '__main__':
    main()
