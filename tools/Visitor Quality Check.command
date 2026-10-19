#!/bin/bash
# Run from Finder/Terminal when the agent session cannot create a Cocoa window.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/check_visitor_headed.py
