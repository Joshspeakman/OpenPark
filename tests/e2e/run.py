#!/usr/bin/env python3
"""The end-to-end steps in order (or those named): a report in work/e2e/out/report.txt."""
import sys; sys.path.insert(0, __import__('os').path.dirname(__file__)); from e2e import *
names = sys.argv[1:] or list(STEPS)
try:
    for n in names:
        if not run(n, STEPS[n]) and n in ('title', 'new_game'): break
finally:                                   # the run ended, however it ended: no instance left running, and the report written
    stop()                                 # (only the instances this harness launched: work/e2e/pids)
    open(OUT + '/report.txt', 'w').write('\n'.join(f'{"FAIL" if e else "ok"} {n} {t:.0f}s {e or ""}' for n, e, t in RESULTS) + '\n\n' + '\n'.join(EVENTS) + '\n')
    print('\n'.join(EVENTS[-20:]))
sys.exit(1 if any(e for _, e, _ in RESULTS) else 0)   # a failed step fails the run, for scripts and CI
