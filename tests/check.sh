#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
PYTHON=${TP_PYTHON:-python3}
"$PYTHON" -m unittest discover -s tests -v
if [[ $# -eq 2 ]]; then
  "$PYTHON" tools/prove_native.py "$1" --reference "$2"
elif [[ $# -ne 0 ]]; then
  echo 'usage: tests/check.sh [owned-GAME/MAIN.EXE reference-checkout]' >&2
  exit 2
fi
