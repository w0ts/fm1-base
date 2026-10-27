#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The trial in the host simulator, built and run: optimist/run.sh (from anywhere). Twice: with Optimist's source
# defaults (OPT_PROFILE=none: tests/), then with the trial's copy of Optimist's dev profile (profiles/
# dev-171e96a3.config: the device build's switches, the reference profile's data formats and addresses; tests/dev/),
# which the build is left with.
# Each run builds with fm1-base's tools/build.py --host (Terminus: X0X_FONTSET, which fm1.toml cannot set yet), then
# runs the note (milestone 1: the sound), the UI (milestone 2) and the storage (milestone 3): storage.txt saves through
# the UI and dumps the flash; storage_restart.txt powers on with it in a new process and finds everything (then a
# second autosave, dumped for storage_resume.txt). Optimist's own simulator (optsim.py: its firmware from its tree with
# the same profile, as an FM-1 runs it, with emcc and node) writes its flash doing the same steps: the trial reads it
# (storage_restart.txt again; the dev run also storage_boot.txt: a first power-on on it writes what Optimist's own
# writes (optsim.py idle), and storage_keep.txt: a setting saved keeps the settings record's fields the trial does not
# build) and the two flashes are compared (optsim.py same: sector by sector, the section log record by
# record); it also goes over the trial's flash (optsim.py touch) and the trial reads the result (storage_touched.txt).
# The dev run also plays and records USB audio (usbproof.py: fm1-base's simulator's usb_play / usb_rec, the stems, the
# routing, the USB input track), checks the parity with Optimist's own main loop, audio interrupt and timer (tests/dev/
# parity.txt: the battery, the limited inserts, CLOCK OUT, following a clock, saving while playing), renders with the
# second core (dualproof.py: fm1-base's simulated one, every split of
# the jobs the same samples, and one core's with the reverb sends at 0), runs the device build's 64-bit multiplies as
# the pass rebuilds them (mul64proof.py: mul64_pass.py's model on the host's IR, every engine, the same samples), then
# checks the web editor (edproof.py): the same SysEx to the trial and to Optimist's simulator built with
# the trial's own switches (optsim.py build-trial), every reply byte for byte; backup, restore, the mirror, DX7.
# The screens and the flashes in optimist/build/host. PYTHON: a python with Pillow (fm1-base's font generator), default
# the first python3 on PATH. OPTSIM=0: without Optimist's simulator (no emcc / node). PROFILES: the runs (default
# "none dev").
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
H=optimist/build/host

run_profile() {                     # $1: dev (the trial's profile copy) or none (Optimist's source defaults)
    if [ "$1" = none ]; then T=optimist/tests; export OPT_PROFILE=none; else T=optimist/tests/dev; unset OPT_PROFILE; fi
    echo "optimist/run.sh: profile $1"
    X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/note.txt $H
    $H/optimist_host $T/ui.txt $H
    $H/optimist_host $T/storage.txt $H
    OPT_FLASH=$H/trial-flash.bin OPT_DUMP=$H/trial-flash2.bin $H/optimist_host $T/storage_restart.txt $H
    OPT_FLASH=$H/trial-flash2.bin $H/optimist_host $T/storage_resume.txt $H
    if [ "$1" != none ]; then       # (USB audio: a computer recording and playing, fm1-base's simulator: usbproof.py)
        "$PY" optimist/usbproof.py $H/optimist_host $H/usb
        "$PY" optimist/dualproof.py $H/optimist_host $H/dual single   # (the second core: the same samples, any split)
        "$PY" optimist/mul64proof.py $H/mul64   # (the device's 64-bit multiplies as mul64_pass.py rebuilds them)
        $H/optimist_host $T/parity.txt $H   # (what Optimist's main loop, audio interrupt and timer do: trial-parity.md)
    fi
    if [ "${OPTSIM:-1}" != 0 ]; then
        "$PY" optimist/optsim.py build
        "$PY" optimist/optsim.py write $H/optimist-flash.bin
        "$PY" optimist/optsim.py same $H/optimist-flash.bin $H/trial-flash.bin
        OPT_FLASH=$H/optimist-flash.bin OPT_DUMP=$H/optimist-flash2.bin $H/optimist_host $T/storage_restart.txt $H
        if [ "$1" != none ]; then   # (the reference profile's FM-1: its first power-on, a setting saved)
            "$PY" optimist/optsim.py idle $H/optimist-flash.bin $H/optidle-flash.bin
            OPT_FLASH=$H/optimist-flash.bin OPT_DUMP=$H/boot-flash.bin $H/optimist_host $T/storage_boot.txt $H
            "$PY" optimist/optsim.py same $H/optidle-flash.bin $H/boot-flash.bin
            OPT_FLASH=$H/optimist-flash.bin $H/optimist_host $T/storage_keep.txt $H
        fi
        "$PY" optimist/optsim.py touch $H/trial-flash2.bin $H/touched-flash.bin
        OPT_FLASH=$H/touched-flash.bin $H/optimist_host $T/storage_touched.txt $H
        if [ "$1" != none ]; then   # (the editor: the trial's switches on both sides, every reply compared)
            "$PY" optimist/optsim.py build-trial
            "$PY" optimist/edproof.py $H/optimist-flash.bin $H/edproof
        fi
    fi
}

for p in ${PROFILES:-none dev}; do
    run_profile "$p"
done
unset OPT_PROFILE
echo "optimist/run.sh: every check passed (${PROFILES:-none dev})"
