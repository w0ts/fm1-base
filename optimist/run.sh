#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The trial in the host simulator, built and run: optimist/run.sh (from anywhere). Builds with fm1-base's
# tools/build.py --host (Terminus: X0X_FONTSET, which fm1.toml cannot set yet), then runs tests/note.txt (milestone 1:
# the sound) and tests/ui.txt (milestone 2: the UI); the screens in optimist/build/host/*.png. PYTHON: a python with
# Pillow (fm1-base's font generator), default the first python3 on PATH.
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
optimist/build/host/optimist_host optimist/tests/note.txt optimist/build/host
optimist/build/host/optimist_host optimist/tests/ui.txt optimist/build/host
