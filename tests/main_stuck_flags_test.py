#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""fm1.toml main_stuck_ms / main_stuck_reset as tools/build.py reads them (main_stuck_flags): main_stuck_ms = N (1000 ..
600000) watches the main thread from TIMER5 (-DFM1B_MAIN_STUCK_MS=Nu, device/main_watch.h), main_stuck_reset = true
restarts the FM-1 after the record (-DFM1B_MAIN_STUCK_RESET=1, only with main_stuck_ms); none: no flag, the build as
before; anything else stops the build with a message. Run from the repo root:
  python3 tests/main_stuck_flags_test.py"""
import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
build = importlib.import_module("build")

failed = 0


def ok(cond: bool, what: str) -> None:
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def flags(conf: dict) -> "list[str] | str":
    """-> the flags for that fm1.toml, or the build's stop message"""
    build.CONF = conf
    try:
        return build.main_stuck_flags()
    except SystemExit as e:
        return f"stop: {e}"


ok(flags({}) == [], "neither key: no flag (the build as before)")
ok(flags({"main_stuck_ms": 60000}) == ["-DFM1B_MAIN_STUCK_MS=60000u"], "main_stuck_ms = 60000: the watch, no restart")
ok(flags({"main_stuck_ms": 60000, "main_stuck_reset": True}) == ["-DFM1B_MAIN_STUCK_MS=60000u",
                                                                 "-DFM1B_MAIN_STUCK_RESET=1"],
   "main_stuck_ms + main_stuck_reset = true: the watch and the restart")
ok(flags({"main_stuck_ms": 60000, "main_stuck_reset": False}) == ["-DFM1B_MAIN_STUCK_MS=60000u"],
   "main_stuck_reset = false: no restart flag")
for bad in (0, 999, 600001, -1, True, "60000", 1.5):
    got = flags({"main_stuck_ms": bad})
    ok(isinstance(got, str) and "main_stuck_ms" in got, f"main_stuck_ms = {bad!r}: the build stops")
for bad in (1, "true", None):
    got = flags({"main_stuck_ms": 60000, "main_stuck_reset": bad})
    ok(isinstance(got, str) and "main_stuck_reset" in got, f"main_stuck_reset = {bad!r}: the build stops")
got = flags({"main_stuck_reset": True})
ok(isinstance(got, str) and "main_stuck_ms" in got, "main_stuck_reset without main_stuck_ms: the build stops")
src = (ROOT / "tools" / "build.py").read_text()
ok(src.count('"-DFM1B_MAIN_STUCK_RESET=1"') == 1 and "flags += main_stuck_flags()" in src,
   "build.py: the defines come from main_stuck_flags only")
sys.exit(1 if failed else 0)
