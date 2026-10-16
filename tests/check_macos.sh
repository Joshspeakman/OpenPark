#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DS_TEST=1 W32_AUDIO_MUTE=1 PYTHONDONTWRITEBYTECODE=1
PYTHON=${TP_PYTHON:-python3}
if [[ $(uname -m) != arm64 ]]; then
  echo 'Native verification requires Apple Silicon.' >&2
  exit 1
fi
if [[ $# -lt 2 || $# -gt 3 || ( $# -eq 3 && "${3:-}" != --park ) ]]; then
  echo 'usage: tests/check_macos.sh owned-GAME/MAIN.EXE reference-checkout [--park]' >&2
  exit 2
fi
"$PYTHON" -m unittest discover -s tests -v
"$PYTHON" tools/check_cpu.py --reference "$2"
"$PYTHON" tools/prove_native.py "$1" --reference "$2"
input_args=(--keys 60:1c0d,80:1454,82:1245,84:1f53,86:1454,88:1c0d)
trace_args=(--instructions 50000000)
native_args=(--services 12699)
if [[ "${3:-}" == --park ]]; then
  input_args=(--keys 60:1c0d,80:1454,82:1245,84:1f53,86:1454,88:1c0d,120:3b00,300:3d00
    --clicks 180:564:316,220:280:90
    --block-clicks 2450000:72:310,2650000:568:316,2800000:604:380)
  trace_args=(--quantum 1000 --blocks 8000000 --seconds 60)
  native_args=(--quantum 1000 --blocks 8000000)
fi
"$PYTHON" tools/trace_startup.py "$1" --out work/check-startup "${trace_args[@]}" "${input_args[@]}"
# Whole-program build: every statically reachable block, traced extents kept.
"$PYTHON" tools/build_game.py "$1" --reference "$2" --trace work/check-startup/trace.json \
  --survey work/main-survey.json --out work/check-native
"$PYTHON" tools/verify_startup.py "$1" --reference "$2" --trace work/check-startup/trace.json --out work/check-native \
  --library work/check-native/game.dylib --seconds 300
"$PYTHON" - <<'PY'
import json
from pathlib import Path
r=json.loads(Path('work/check-native/result.json').read_text())
t=json.loads(Path('work/check-startup/trace.json').read_text())
milestones=json.loads(Path('config/milestones.json').read_text())
assert r['status']=='service bound',r
assert r['service_boundaries_compared']==len(t['services']),r
assert t['polls']>=100,t['polls']
if t['quantum']:
    assert t['executed_blocks']==r['executed_blocks']==8000000,r
    assert r['scheduling_boundaries_compared']>0,r
    assert r['saved_files']['save/TEST.GD']==milestones['first_park']['profile_sha256'],r
assert r['frame_sha256']==milestones['first_park' if t['quantum'] else 'main_menu']['frame_sha256'],r
assert r['frame']['nonzero_pixels']>32000 and r['frame']['colors']>32,r
assert Path('work/check-native/native-frame.png').read_bytes()==Path('work/check-native/original-frame.png').read_bytes()
print('PASS: native startup, scripted input, scheduling and matching framebuffer')
PY
if [[ "${3:-}" != --park ]]; then
  verified_services=$("$PYTHON" -c 'import json; print(json.load(open("work/check-native/result.json"))["service_boundaries_compared"])')
  native_args=(--services "$verified_services")
fi
"$PYTHON" -S tools/run_native.py "$1" --library work/check-native/game.dylib --out work/check-native-only \
  "${native_args[@]}" "${input_args[@]}"
"$PYTHON" - <<'PY'
import json
from pathlib import Path
a=json.loads(Path('work/check-native/result.json').read_text())
b=json.loads(Path('work/check-native-only/result.json').read_text())
assert not b['oracle_imported'] and not b['disassembler_imported'],b
assert a['frame_sha256']==b['frame_sha256'],(a,b)
assert a['saved_files']==b['saved_files'],(a,b)
print('PASS: identical native-only frame and scratch files, without oracle/disassembler')
PY
if [[ "${3:-}" == --park ]]; then
  # The app's C DOS host, deterministic mode: the same scenario must give the golden hashes.
  clang -arch arm64 -O2 -w -I "$2/recomp/runtime" -I runtime runtime/host.c runtime/sb16.c runtime/headless.c \
    runtime/verify.c work/check-native/game.a -lz -o work/check-native/tp-headless
  overlay=$(mktemp -d work/check-native/c-host-XXXXXXXX)
  "$PYTHON" tools/export_image.py "$1" work/check-native/MAIN.TPIMG >/dev/null
  work/check-native/tp-headless --game "$(dirname "$1")" --overlay "$PWD/$overlay" --image work/check-native/MAIN.TPIMG \
    --out work/check-native/c-host "${input_args[@]}" --quantum 1000 --blocks 8000000 > work/check-native/c-host.json
  "$PYTHON" - <<'PY'
import json
from pathlib import Path
r=json.loads(Path('work/check-native/c-host.json').read_text())
m=json.loads(Path('config/milestones.json').read_text())['first_park']
assert r['frame_sha256']==m['frame_sha256'],r
assert r['saved_files']['save/TEST.GD']==m['profile_sha256'],r
print('PASS: C host reproduces the golden first-park frame and profile')
PY
fi
file work/check-native/game.dylib
otool -L work/check-native/game.dylib
