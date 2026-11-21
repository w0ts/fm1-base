#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""fm1.toml usb_reset_request as tools/build.py reads it (usb_reset_request_flags): true builds the USB reset request
(-DFELUCCA_USB_RESET_REQ=1), false or none leaves the build as it was (no flag), anything else stops the build with a
message; the flag reaches the build's own list only with the option. tools/fm1_usb_reboot.py's request matches the
bsp's magics. Run from the repo root:
  python3 tests/usb_reset_request_test.py"""
import importlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
build = importlib.import_module("build")
reboot = importlib.import_module("fm1_usb_reboot")

failed = 0


def ok(cond: bool, what: str) -> None:
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def flags(conf: dict) -> "list[str] | str":
    """-> the flags for that fm1.toml, or the build's stop message"""
    build.CONF = conf
    try:
        return build.usb_reset_request_flags()
    except SystemExit as e:
        return f"stop: {e}"


ok(flags({}) == [], "no usb_reset_request: no flag (the build as before)")
ok(flags({"usb_reset_request": False}) == [], "usb_reset_request = false: no flag")
ok(flags({"usb_reset_request": True}) == ["-DFELUCCA_USB_RESET_REQ=1"], "usb_reset_request = true: the flag")
for bad in (1, 0, "true", "yes", None):
    got = flags({"usb_reset_request": bad})
    ok(isinstance(got, str) and "usb_reset_request" in got, f"usb_reset_request = {bad!r}: the build stops ({got})")
src = (ROOT / "tools" / "build.py").read_text()
ok(src.count("-DFELUCCA_USB_RESET_REQ") == 1 and "flags += usb_reset_request_flags()" in src,
   "build.py: the define comes from usb_reset_request_flags only")
usb = (ROOT / "bsp" / "chip" / "usb.c").read_text()
want = {k: int(v, 16) for k, v in re.findall(r"#define USB_RESET_(RQ|MAGIC|APP|UBOOT) 0x([0-9A-Fa-f]+)u", usb)}
ok(len(want) == 4, f"bsp usb.c has the request's four constants ({sorted(want)})")
if len(want) == 4:
    ok(reboot.REQUEST_TYPE == want["RQ"] >> 8 and reboot.REQUEST == want["RQ"] & 0xFF,
       "fm1_usb_reboot.py: bmRequestType and bRequest as the bsp's")
    ok(reboot.MAGIC == want["MAGIC"] and reboot.KIND == {"app": want["APP"], "uboot": want["UBOOT"]},
       "fm1_usb_reboot.py: wValue and both wIndex as the bsp's")

# the note's readouts (device/hang_note.h): the `log` lines named, and the crash history's record (tests/hang_note_test.c
# prints this one) shown as a note
remote = importlib.import_module("fm1_remote")
log = ["no crash recorded", "note USB_RESET n 1 ms 123456 st 4", "note w0-2 00000013 00050002 00000007",
       "note w3-5 00120012 00400001 00000302", "gap 1 late 0 tick 2"]
named = reboot.decode_note(log)
ok(len(named) == 5 and "note USB_RESET n 1 ms 123456 st 4" in named[0]
   and "UBOOT asked" in named[1] and "SysEx ring held (sx_busy)" in named[1]
   and "host not reading MIDI (tx_deaf)" in named[1] and "SysEx out ring 5 packets, MIDI out ring 2" in named[2]
   and "streams let go 7" in named[2] and "real-time ring w 18 r 18" in named[3] and "MIDI in ring 64" in named[3]
   and "update step 2 (0 none), USB bus resets 3" in named[4], "log: a USB_RESET note's words named")
ok(reboot.decode_note(["no crash recorded"]) == [], "log: no note, nothing named")
ok(reboot.decode_note(log[1:2] + ["note w0-2 00000013 zz 00000007", log[3]]) == [], "log: a garbled word, nothing named")
recs, end = remote.parse_crash_frames([
    "crash 1/1 n 83886081 vec 4E540002 pc 00000013 rets 00050002 sp 00000007 ms 300000 emu 00120012 dbg 00400001 "
    "psr 00000302 build ABCD1234 seq 1", "end 1"])
lines = remote.format_crash(recs[0]) if recs else []
ok(end == 1 and lines and lines[0].startswith("note 1/1: USB_RESET") and "n 1 ms 300000 st 5" in lines[1]
   and any("update step 2" in ln for ln in lines), f"crashes: a kept note shown as one ({lines[:2]})")

print("usb-reset-request: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
