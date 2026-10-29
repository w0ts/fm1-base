# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/fm1_ports.py, how fm1_remote.py and fm1_debug.py find the FM-1 by its USB-MIDI port name: "FM1-XXXX"
(plat_dev_name), the old "Optimist" and the stock "FM-1" match; other gear and a Bluetooth port do not."""
import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
R = importlib.import_module("fm1_ports")

fails = 0


def ok(c, msg):
    global fails
    fails += not c
    print(("  ok   " if c else "  FAIL ") + msg)


for name in ("FM1-1A2B", "FM1-1A2B MIDI 1", "FM1-ffff", "Optimist", "Optimist (Felucca)", "FM-1", "FM-1 MIDI"):
    ok(R.fm1_ports([name, "Launchkey Mini MK3"]) == [name], f"{name!r}: found, other gear not")
for name in ("Launchkey Mini MK3", "IAC Driver Bus 1", "FM1-12", "FM1-1A2G", "FM1-1A2B Bluetooth", "Optimist 112D Bluetooth"):
    ok(R.fm1_ports([name]) == [], f"{name!r}: not an FM-1 USB port")
ok(R.fm1_ports(["Launchkey", "FM1-0001", "Optimist"]) == ["FM1-0001", "Optimist"], "the order of the list is kept")
ok(R.fm1_ports(["X0X MIDI", "Launchkey"], extra=("X0X",)) == ["X0X MIDI"], "an extra name (fm1_debug.py: X0X)")
sys.exit(1 if fails else 0)
