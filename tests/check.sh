#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -n ${TP_PYTHON:-} ]]; then PYTHON=$TP_PYTHON                     # (python3 -m venv .venv && .venv/bin/pip install -r requirements.txt)
elif [[ -x .venv/bin/python3 ]]; then PYTHON=.venv/bin/python3
else PYTHON=python3; fi
"$PYTHON" -m unittest discover -s tests -v
if [[ $# -eq 2 ]]; then
  "$PYTHON" tools/prove_native.py "$1" --reference "$2"
elif [[ $# -ne 0 ]]; then
  echo 'usage: tests/check.sh [owned-GAME/MAIN.EXE reference-checkout]' >&2
  exit 2
fi
