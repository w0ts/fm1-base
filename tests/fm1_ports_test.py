# SPDX-License-Identifier: MPL-2.0
"""A Bluetooth MIDI port is never an FM-1's USB port (tools/fm1ports.py): an FM-1 paired over Bluetooth shows one beside
its USB port ("FM1-XXXX Bluetooth", the old "Optimist 112D Bluetooth"); other gear is not taken by its name."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import fm1ports  # noqa: E402

fails = 0


def ok(c, msg):
    global fails
    fails += not c
    print(("  ok   " if c else "  FAIL ") + msg)


def names(ins, outs=None):
    return [i for i, _ in fm1ports.pairs(ins, outs or ins)]


for name in ("FM1-1A2B", "FM1-1A2B MIDI 1", "FM-1", "FM-1 MIDI"):
    ok(names([name, "Launchkey Mini MK3"]) == [name, "Launchkey Mini MK3"] and fm1ports.usual(name), f"{name!r}: a usual name")
for name in ("FM1-1A2B Bluetooth", "Optimist 112D Bluetooth", "FM-1 bluetooth"):
    ok(names([name]) == [], f"{name!r}: not a port to use")
ok(names(["Launchkey", "FM1-0001 Bluetooth", "FM1-0001"]) == ["Launchkey", "FM1-0001"], "the USB port beside a Bluetooth one is kept")
ok([p[0] for p in fm1ports.find(None, mido=type("M", (), {"get_input_names": staticmethod(lambda: ["FM1-0001 Bluetooth", "FM1-0001"]),
                                                          "get_output_names": staticmethod(lambda: ["FM1-0001 Bluetooth", "FM1-0001"])}))] == ["FM1-0001"],
   "find: the USB port only")
sys.exit(1 if fails else 0)
