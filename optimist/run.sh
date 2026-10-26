#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The trial in the host simulator, built and run: optimist/run.sh (from anywhere). Builds with fm1-base's
# tools/build.py --host (Terminus: X0X_FONTSET, which fm1.toml cannot set yet), then runs tests/note.txt (milestone 1:
# the sound), tests/ui.txt (milestone 2: the UI) and the storage (milestone 3): tests/storage.txt saves through the UI
# and dumps the flash; tests/storage_restart.txt powers on with it in a new process and finds everything (then a second
# autosave, dumped for tests/storage_resume.txt). Optimist's own simulator (optsim.py: its firmware from its tree, with
# emcc and node) writes its flash doing the same steps: the trial reads it (storage_restart.txt again) and the two
# flashes are compared sector by sector (optsim.py same); it also goes over the trial's flash (optsim.py touch) and the
# trial reads the result (tests/storage_touched.txt). The screens and the flashes in optimist/build/host. PYTHON: a
# python with Pillow (fm1-base's font generator), default the first python3 on PATH. OPTSIM=0: without Optimist's
# simulator (no emcc / node).
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
H=optimist/build/host
X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
$H/optimist_host optimist/tests/note.txt $H
$H/optimist_host optimist/tests/ui.txt $H
$H/optimist_host optimist/tests/storage.txt $H
OPT_FLASH=$H/trial-flash.bin OPT_DUMP=$H/trial-flash2.bin $H/optimist_host optimist/tests/storage_restart.txt $H
OPT_FLASH=$H/trial-flash2.bin $H/optimist_host optimist/tests/storage_resume.txt $H
if [ "${OPTSIM:-1}" != 0 ]; then
    "$PY" optimist/optsim.py build
    "$PY" optimist/optsim.py write $H/optimist-flash.bin
    "$PY" optimist/optsim.py same $H/optimist-flash.bin $H/trial-flash.bin
    OPT_FLASH=$H/optimist-flash.bin OPT_DUMP=$H/optimist-flash2.bin $H/optimist_host optimist/tests/storage_restart.txt $H
    "$PY" optimist/optsim.py touch $H/trial-flash2.bin $H/touched-flash.bin
    OPT_FLASH=$H/touched-flash.bin $H/optimist_host optimist/tests/storage_touched.txt $H
fi
echo "optimist/run.sh: every check passed"
