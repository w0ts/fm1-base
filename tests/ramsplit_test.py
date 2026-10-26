#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/ramsplit.py, an app's RAM split (fm1.toml ram_text_kb), on device/app.ld and device/app_wifi.ld: no
ram_text_kb or the script's own size links the script as it is; a bigger RAMTEXT moves RAM up (its size kept) and
the pool's start, the pool ending where it did and nothing else moved; WiFi's 8 KiB gap below RAM goes first; a size
below the script's own, not whole, or eating the pool fails. Run from the repo root:
  python3 tests/ramsplit_test.py"""
import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
ramsplit = importlib.import_module("ramsplit")
APP = (ROOT / "device" / "app.ld").read_text()
WIFI = (ROOT / "device" / "app_wifi.ld").read_text()
KIB = 1024

failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def fails(script, kb):
    try:
        ramsplit.split(script, kb)
    except SystemExit as e:
        return str(e)
    return ""


r = ramsplit.regions(APP)
ok(r["RAMTEXT"] == (0x01C00000, 32 * KIB) and r["RAM"] == (0x01C08000, 96 * KIB) and r["POOL"] == (0x01C20000, 0x54000),
   f"app.ld read: RAMTEXT 32 KiB, RAM 96 KiB, the pool 0x54000 ({r})")
for kb in (None, 32):
    text, lay = ramsplit.split(APP, kb)
    ok(text == APP and lay == r, f"ram_text_kb = {kb}: the script as it is")

text, lay = ramsplit.split(APP, 48)
ok(lay["RAMTEXT"] == (0x01C00000, 48 * KIB) and lay["RAM"] == (0x01C0C000, 96 * KIB)
   and lay["POOL"] == (0x01C24000, 0x54000 - 16 * KIB), f"48 KiB: RAM and the pool 16 KiB up, the pool 16 KiB less ({lay})")
ok(sum(lay["POOL"]) == sum(r["POOL"]) and lay["XIP"] == r["XIP"] and lay["NOINIT"] == r["NOINIT"],
   "the pool ends where it did; XIP and NOINIT unchanged")
ok(ramsplit.regions(text) == lay, "the new script reads back as the layout it says")
rest = [ln for ln in text.splitlines()[1:] if not any(n in ln for n in ("RAMTEXT (", "RAM    (", "POOL   ("))]
ok(rest == [ln for ln in APP.splitlines() if not any(n in ln for n in ("RAMTEXT (", "RAM    (", "POOL   ("))],
   "only the three regions' lines change (and a first line says so)")

w = ramsplit.regions(WIFI)
_, lay = ramsplit.split(WIFI, 32)
ok(lay["RAM"] == w["RAM"] and lay["POOL"] == w["POOL"] and lay["RAMTEXT"][1] == 32 * KIB,
   "app_wifi.ld 32 KiB: the 8 KiB gap below RAM, nothing moves")
_, lay = ramsplit.split(WIFI, 40)
ok(lay["RAM"] == (w["RAM"][0] + 8 * KIB, w["RAM"][1]) and lay["POOL"][1] == w["POOL"][1] - 8 * KIB,
   "app_wifi.ld 40 KiB: the gap, then 8 KiB of the pool")

ok("at least 32" in fails(APP, 16), "below the script's own size fails")
ok("whole number" in fails(APP, 40.5), "not a whole number of KiB fails")
ok("whole number" in fails(APP, True), "true is no size")
ok("the whole pool" in fails(APP, 32 + 0x54000 // KIB), "a RAMTEXT that would take the whole pool fails")
ok("no MEMORY" in fails("SECTIONS { }", 48), "a script without MEMORY fails")

print("ramsplit: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
