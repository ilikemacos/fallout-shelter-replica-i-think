#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not found. Install Python 3.11+ (e.g. via python.org or Homebrew)."
    exit 1
fi
python3 -m pip install --upgrade pip pygame pyinstaller pillow
python3 scripts/build_macos.py
