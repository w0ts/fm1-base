#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""fm1.toml boot_pack as tools/build.py makes it: pack_script moves the RAM copies' load addresses out of XIP into a
PACK region of their own (device/app.ld and app_wifi.ld; the other regions as they were), a script with nothing to
move stops the build; boot_stream runs the app's coder as `boot_pack OUT DATA RAMTEXT`, makes an empty blob for a
missing section, puts the stream after the gap and pads it to the word with 0xFF, and stops the build when the coder
fails or writes nothing. The start's decode (app_boot_unpack, device/main_fm1.c) runs only on an FM-1. Run from the
repo root:
  python3 tests/boot_pack_test.py"""
import importlib
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
build = importlib.import_module("build")

failed = 0

# the app's coder for the test: OUT = "PK", each blob's length (2 bytes) and its bytes reversed; "fail" in the data
# blob: exit 3; "none": no OUT written. The arguments go to OUT.args, one a line
CODER = '''import sys
from pathlib import Path
out, data, ram = sys.argv[1:4]
Path(out + ".args").write_text("\\n".join(sys.argv[1:]))
d, r = Path(data).read_bytes(), Path(ram).read_bytes()
if d == b"fail":
    sys.exit(3)
if d != b"none":
    Path(out).write_bytes(b"PK" + b"".join(len(x).to_bytes(2, "little") + x[::-1] for x in (d, r)))
print("coded")
'''


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def stream(tmp, data, ram, gap):
    """-> boot_stream(gap) with these blobs (None: the blob's file missing), or the build's stop message"""
    for name, blob in (("data.bin", data), ("ramtext.bin", ram)):
        (tmp / name).unlink(missing_ok=True)
        if blob is not None:
            (tmp / name).write_bytes(blob)
    try:
        return build.boot_stream(gap)
    except SystemExit as e:
        return f"stop: {e}"


with tempfile.TemporaryDirectory() as t:
    tmp = Path(t)
    build.OUT, build.APP, build.NAME = tmp, tmp, "packtest"
    build.CONF = {"boot_pack": "coder.py"}
    (tmp / "coder.py").write_text(CODER)

    for script in ("app.ld", "app_wifi.ld"):
        src = ROOT / "device" / script
        before = build.memory_regions(src.read_text())
        out = build.pack_script(src)
        text = out.read_text()
        after = build.memory_regions(text)
        ok(out == tmp / "packtest_pack.ld" and "AT > XIP" not in text and text.count("AT > PACK") ==
           src.read_text().count("AT > XIP"), f"{script}: every load address in XIP moved to PACK")
        ok(after.get("PACK") == (0x10000000, 0x100000) and {k: v for k, v in after.items() if k != "PACK"} == before,
           f"{script}: PACK at 0x10000000 (1 MiB, past anything mapped), the other regions unchanged")
    bare = tmp / "bare.ld"
    bare.write_text("MEMORY { XIP (rx) : ORIGIN = 0x02000120, LENGTH = 0x1000 }\nSECTIONS { .text : { *(.text) } > XIP }\n")
    try:
        build.pack_script(bare)
        got = "no stop"
    except SystemExit as e:
        got = str(e)
    ok("no load address in XIP" in got, f"a script with nothing to move stops the build ({got})")

    got = stream(tmp, b"\x01\x02\x03", b"\xAA\xBB", 3)
    coded = b"PK\x03\x00\x03\x02\x01\x02\x00\xBB\xAA"
    ok(got == b"\xff" * 3 + coded + b"\xff" * (-(3 + len(coded)) % 4) and len(got) % 4 == 0,
       f"the stream after a 3-byte gap, padded to the word with 0xFF ({got.hex() if isinstance(got, bytes) else got})")
    args = (tmp / "boot_pack.bin.args").read_text().split("\n")
    ok(args == [str(tmp / "boot_pack.bin"), str(tmp / "data.bin"), str(tmp / "ramtext.bin")],
       "the coder is run as `boot_pack OUT DATA RAMTEXT`")
    got = stream(tmp, b"\x05", None, 0)
    ok(got == b"PK\x01\x00\x05\x00\x00\xff", f"no .ram_text: an empty blob in its place ({got!r})")
    got = stream(tmp, b"fail", b"", 0)
    ok(isinstance(got, str) and "coder.py failed" in got, f"the coder exits non-zero: the build stops ({got})")
    got = stream(tmp, b"none", b"", 0)
    ok(isinstance(got, str) and "coder.py failed" in got, f"the coder writes nothing: the build stops ({got})")

print("boot-pack: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
