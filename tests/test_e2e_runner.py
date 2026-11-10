#!/usr/bin/env python3
"""tests/e2e/run.py's exit status: 0 only when every step passed. A stand-in for e2e.py replaces the app, so no game is needed."""
import os, pathlib, shutil, subprocess, sys, tempfile, unittest

FAKE = """
import os, sys
OUT = os.environ['FAKE_OUT']; RESULTS = []; EVENTS = []
FAILS = set(os.environ.get('FAKE_FAILS', '').split(','))
STEPS = {n: n for n in ('title', 'new_game', 'panels', 'save')}
def stop(): open(OUT + '/stopped', 'w').close()
def run(name, fn):
    err = 'failed' if name in FAILS else None
    RESULTS.append((name, err, 0.0)); print(name, 'FAIL' if err else 'ok')
    return not err
"""


class Runner(unittest.TestCase):
    def go(self, fails=''):
        d = pathlib.Path(tempfile.mkdtemp(prefix='openpark-e2e-')); self.addCleanup(shutil.rmtree, d)
        shutil.copy(pathlib.Path(__file__).parent / 'e2e' / 'run.py', d / 'run.py'); (d / 'e2e.py').write_text(FAKE)
        r = subprocess.run([sys.executable, str(d / 'run.py')], capture_output=True, text=True, env=dict(os.environ, FAKE_OUT=str(d), FAKE_FAILS=fails))
        self.assertTrue((d / 'stopped').exists(), 'the app is stopped however the run ends'); self.assertTrue((d / 'report.txt').exists())
        return r

    def test_all_pass(self):
        r = self.go(); self.assertEqual(r.returncode, 0, r.stdout + r.stderr); self.assertIn('save ok', r.stdout)

    def test_a_critical_failure_stops_early_and_fails(self):
        r = self.go('title'); self.assertEqual(r.returncode, 1); self.assertNotIn('new_game', r.stdout)

    def test_a_later_failure_runs_the_rest_and_fails(self):
        r = self.go('panels'); self.assertEqual(r.returncode, 1); self.assertIn('save ok', r.stdout)


if __name__ == '__main__':
    unittest.main()
