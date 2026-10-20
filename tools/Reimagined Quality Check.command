#!/bin/bash
# Visible-window check for the isolated Reimagined Study only.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/check_visitor_headed.py --reimagined
