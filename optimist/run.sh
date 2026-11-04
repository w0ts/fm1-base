#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The trial in the host simulator, built and run: optimist/run.sh (from anywhere). Four times: with Optimist's source
# defaults (OPT_PROFILE=none: tests/), with the SD card mode (run_sd: tests/sd), with the dev profile's Bluetooth on the
# host too (run_bt), then with the trial's copy of Optimist's dev profile (profiles/
# dev-bt.config: the device build's switches, the reference profile's data formats and addresses; tests/dev/),
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
# the jobs the same samples, and one core's with the reverb sends at 0), then checks the web editor (edproof.py): the same SysEx to the trial and to Optimist's simulator built with
# the trial's own switches (optsim.py build-trial), every reply byte for byte; backup, restore, the mirror, DX7.
# The screens and the flashes in optimist/build/host. PYTHON: a python with Pillow (fm1-base's font generator), default
# the first python3 on PATH. OPTSIM=0: without Optimist's simulator (no emcc / node). PROFILES: the runs (default
# "none sd bt lights wifi wifi-ap usbhost console items dev"; wifi: the dev profile with Wi-Fi + Link, tests/wifi/wifi.txt (run_wifi); wifi-ap: the same with MODE AP, tests/wifi/ap.txt (run_wifi_ap); usbhost: the same with the USB MIDI host, tests/usbhost/usbhost.txt (run_usbhost); console: the dev profile with the USB serial console, run_console; items: the builder items and
# options no other run switches on, run_items; lights: the dev profile with LIGHTS, its levels gone (the dim pulse the glow's), tests/dev/lights.txt; sd: the SD card mode, run_sd below; bt: the dev profile with its Bluetooth on the host too,
# OPT_HOST_BT=1, tests/dev/bt.txt and ui.txt).
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
H=optimist/build/host

# the SD card mode (the profile copy profiles/sd-dev-07efe611.config: Optimist's card code on fm1-base's simulated
# card, fm1-base's bigger slot FM1B_APP_END=0xCE000): with no card (tests/sd/nocard.txt), then with a card image
# (optimist/sdcard.py: FAT32 by the system's formatter, Optimist's card zip, two samples; tests/sd/card.txt), the card
# read back after (sdcard.py check). Without macOS's hdiutil / newfs_msdos no card image: said, the card's checks not run.
# First, after the build linked Optimist's tree: the card's clock steps, fm1-base's and Optimist's the same
# (tests/sd/sd_clock_steps.c)
run_sd() {
    echo "optimist/run.sh: profile sd"
    export OPT_PROFILE=profiles/sd-dev-07efe611.config FM1B_APP_END=0xCE000
    X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    "${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Ioptimist/build/optimist/firmware/hal -Idevice -o $H/sd_clock_steps \
        optimist/tests/sd/sd_clock_steps.c
    $H/sd_clock_steps
    $H/optimist_host optimist/tests/note.txt $H
    $H/optimist_host optimist/tests/sd/nocard.txt $H
    $H/optimist_host optimist/tests/sd/editor_files.txt $H   # (the slots, which hold nothing, as the companion's files)
    if "$PY" optimist/sdcard.py $H/sd/card0.img; then
        cp $H/sd/card0.img $H/sd/card.img
        FM1B_SD=$H/sd/card.img $H/optimist_host optimist/tests/sd/card.txt $H
        "$PY" optimist/sdcard.py check $H/sd/card.img
        # the same layout with the serial console on (profiles/sd-console.config):
        # the parts switch engines (tests/sd/engines.txt), the console reads the card (tests/sd/console.txt, a fresh card image)
        echo "optimist/run.sh: profile sd-console (tests/sd/engines.txt, console.txt)"
        export OPT_PROFILE=profiles/sd-console.config
        X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
        $H/optimist_host optimist/tests/sd/engines.txt $H
        cp $H/sd/card0.img $H/sd/card-console.img
        FM1B_SD=$H/sd/card-console.img $H/optimist_host optimist/tests/sd/console.txt $H
    else
        echo "optimist/run.sh: the card's checks NOT run (no card image)"
    fi
    # the same layout with the editor's own backup commands (profiles/sd-live.config: ED_LIVE on, as the release's
    # fm1-next profile has it): the connect's BK_LIST and BK_READ on the slots that hold nothing (tests/sd/editor.txt)
    echo "optimist/run.sh: profile sd-live (tests/sd/editor.txt)"
    export OPT_PROFILE=profiles/sd-live.config
    X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/sd/editor.txt $H
    unset OPT_PROFILE FM1B_APP_END
}

run_bt() {                          # the dev profile with its Bluetooth on the host too (OPT_HOST_BT=1: gen_optimist.py
                                    # host_bt): Optimist's Bluetooth screens, settings and mirror on fm1-base's stand-ins
    echo "optimist/run.sh: profile dev, Bluetooth on the host (tests/dev/bt.txt)"
    unset OPT_PROFILE
    OPT_HOST_BT=1 X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/dev/bt.txt $H
    $H/optimist_host optimist/tests/dev/ui.txt $H
}

run_lights() {                      # the dev profile with LIGHTS (profiles/dev-lights.config): no backlight levels any more
                                    # (fm1-next b9a394bc): every level asked leaves the dim pulse at the glow's
    echo "optimist/run.sh: profile dev, LIGHTS (tests/dev/lights.txt)"
    export OPT_PROFILE=profiles/dev-lights.config
    X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/dev/lights.txt $H
    unset OPT_PROFILE
}

run_wifi() {                        # the dev profile with Wi-Fi + Link (profiles/dev-wifi.config), its Bluetooth
                                    # and Wi-Fi on the host too (OPT_HOST_BT=1 OPT_HOST_WIFI=1): Optimist's SYSTEM > WI-FI,
                                    # its join and Link's clock on fm1-base's own Wi-Fi stack over the simulator's network
                                    # (host/wifi_air.c), tests/wifi/wifi.txt
    echo "optimist/run.sh: profile dev-wifi, Bluetooth and Wi-Fi on the host (tests/wifi/wifi.txt)"
    export OPT_PROFILE=profiles/dev-wifi.config
    OPT_HOST_BT=1 OPT_HOST_WIFI=1 X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/note.txt $H
    $H/optimist_host optimist/tests/wifi/wifi.txt $H
    $H/optimist_host optimist/tests/wifi/jam.txt $H     # MODE JAM: an Optimist access point joined with its default password, Link forced on
    unset OPT_PROFILE
}

run_console() {                     # the dev profile with the USB serial console (profiles/dev-console.config, no USB audio beside it) and
                                    # the host's Bluetooth stand-ins: SYSTEM > SERIAL on, a restart, Optimist's command line
                                    # (tests/dev/console.txt: run control, status, dbg, cpu1, keys, keylog, memr, flr, tabbench, btlog)
    echo "optimist/run.sh: profile dev, the serial console (tests/dev/console.txt)"
    export OPT_PROFILE=profiles/dev-console.config
    OPT_HOST_BT=1 X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/dev/console.txt $H
    unset OPT_PROFILE
}

run_items() {                       # the builder items and options that nothing else switches on (profiles/dev-items.config, the host's
                                    # Bluetooth stand-ins for BT_PROJ): engines, inserts, pages
                                    # (tests/dev/items.txt)
    echo "optimist/run.sh: profile dev, the items nothing else runs (tests/dev/items.txt)"
    export OPT_PROFILE=profiles/dev-items.config
    OPT_HOST_BT=1 X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/dev/items.txt $H
    unset OPT_PROFILE
}

run_wifi_ap() {                     # MODE AP: the same profile (dev-wifi.config, WIFI_AP on), the FM-1 as the access
                                    # point (fm1-base's device/wifi_own_ap.c) and the simulator's client of it (a phone:
                                    # host/wifi_air_client.c): SYSTEM > WI-FI's AP AUTO, the password, the client's join,
                                    # its address, a Link peer over the access point, the channel, a restart, AP MAN,
                                    # CLEAR KEY; tests/wifi/ap.txt
    echo "optimist/run.sh: profile dev-wifi, MODE AP on the host (tests/wifi/ap.txt)"
    export OPT_PROFILE=profiles/dev-wifi.config
    OPT_HOST_BT=1 OPT_HOST_WIFI=1 X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/wifi/ap.txt $H
    unset OPT_PROFILE
}

run_usbhost() {                     # the USB MIDI host (profiles/dev-usbhost.config: USB_HOST on, fm1-base's usb_host): SYSTEM > USB's
                                    # MODE HOST and DEVICE line on the simulator's USB-MIDI device (usbh plug ...): a
                                    # keyboard's notes play a track, its clock is followed, CLOCK OUT and the notes go
                                    # to it and not to a computer, a lost device's notes end, back to DEVICE; tests/usbhost/usbhost.txt
    echo "optimist/run.sh: profile dev-usbhost, the USB MIDI host (tests/usbhost/usbhost.txt)"
    export OPT_PROFILE=profiles/dev-usbhost.config
    OPT_HOST_BT=1 OPT_HOST_WIFI=1 X0X_FONTSET=terminus "$PY" tools/build.py optimist --host   # (the profile's Wi-Fi: dev-wifi's)
    $H/optimist_host optimist/tests/note.txt $H
    $H/optimist_host optimist/tests/usbhost/usbhost.txt $H
    unset OPT_PROFILE
}

run_profile() {                     # $1: dev (the trial's profile copy), none (Optimist's source defaults), sd, bt, lights, wifi, wifi-ap, console, usbhost or items
    if [ "$1" = sd ]; then run_sd; return; fi
    if [ "$1" = console ]; then run_console; return; fi
    if [ "$1" = items ]; then run_items; return; fi
    if [ "$1" = bt ]; then run_bt; return; fi
    if [ "$1" = lights ]; then run_lights; return; fi
    if [ "$1" = wifi ]; then run_wifi; return; fi
    if [ "$1" = wifi-ap ]; then run_wifi_ap; return; fi
    if [ "$1" = usbhost ]; then run_usbhost; return; fi
    if [ "$1" = none ]; then T=optimist/tests; export OPT_PROFILE=none; else T=optimist/tests/dev; unset OPT_PROFILE; fi
    echo "optimist/run.sh: profile $1"
    X0X_FONTSET=terminus "$PY" tools/build.py optimist --host
    $H/optimist_host optimist/tests/note.txt $H
    $H/optimist_host $T/ui.txt $H
    $H/optimist_host $T/storage.txt $H
    OPT_FLASH=$H/trial-flash.bin OPT_DUMP=$H/trial-flash2.bin $H/optimist_host $T/storage_restart.txt $H
    OPT_FLASH=$H/trial-flash2.bin $H/optimist_host $T/storage_resume.txt $H
    $H/optimist_host $T/dbgstats.txt $H   # (SYSTEM > General > DEBUG STATS: fm1-base's stats, the setting kept)
    if [ "$1" != none ]; then       # (USB audio: a computer recording and playing, fm1-base's simulator: usbproof.py)
        "$PY" optimist/usbproof.py $H/optimist_host $H/usb
        "$PY" optimist/dualproof.py $H/optimist_host $H/dual single   # (the second core: the same samples, any split)
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

# Optimist's simulator needs emcc and node (tools/sim.py --wasm): without them its checks are said NOT run, as
# Optimist's own tests/run_tests.sh skips its simulator test, and the rest runs (OPTSIM=0 says it outright)
if [ "${OPTSIM:-1}" != 0 ] && ! { command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; }; then
    echo "optimist/run.sh: Optimist's simulator's checks NOT run (no emcc or node): optsim.py, storage_boot / keep / touched, edproof.py"
    OPTSIM=0 SIMNOTE=", Optimist's simulator NOT run"
fi
for p in ${PROFILES:-none sd bt lights wifi wifi-ap usbhost console items dev}; do
    run_profile "$p"
done
unset OPT_PROFILE
echo "optimist/run.sh: every check passed (${PROFILES:-none sd bt lights wifi wifi-ap usbhost console items dev}${SIMNOTE:-})"
