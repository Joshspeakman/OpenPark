#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DS_TEST=1 W32_AUDIO_MUTE=1 PYTHONDONTWRITEBYTECODE=1
PYTHON=${TP_PYTHON:-python3}
if [[ $(uname -m) != arm64 ]]; then
  echo 'Native verification requires Apple Silicon.' >&2
  exit 1
fi
if [[ $# -ne 2 ]]; then
  echo 'usage: tests/check_macos.sh owned-GAME/MAIN.EXE reference-checkout' >&2
  exit 2
fi
"$PYTHON" -m unittest discover -s tests -v
"$PYTHON" tools/check_cpu.py --reference "$2"
"$PYTHON" tools/prove_native.py "$1" --reference "$2"
"$PYTHON" tools/trace_startup.py "$1" --out work/check-startup --instructions 50000000 \
  --keys 60:1c0d,80:1454,82:1245,84:1f53,86:1454,88:1c0d
"$PYTHON" tools/verify_startup.py "$1" --reference "$2" --trace work/check-startup/trace.json --out work/check-native
"$PYTHON" - <<'PY'
import json
from pathlib import Path
r=json.loads(Path('work/check-native/result.json').read_text())
t=json.loads(Path('work/check-startup/trace.json').read_text())
assert r['status']=='service bound',r
assert r['service_boundaries_compared']==len(t['services']),r
assert t['polls']>=100,t['polls']
assert r['frame']['nonzero_pixels']>32000 and r['frame']['colors']>32,r
assert Path('work/check-native/native-frame.png').read_bytes()==Path('work/check-native/original-frame.png').read_bytes()
print('PASS: native startup, scripted nickname entry and matching framebuffer')
PY
file work/check-native/startup.dylib
otool -L work/check-native/startup.dylib
