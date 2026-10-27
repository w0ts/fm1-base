#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""plat_usb_audio_stats in the host simulator, as an app with APP_USB_STATS reads it (tests/usbstats_app): every field
filled (the app starts them at 0xA5..), no stream at first; while a computer records and plays half a second, both
alt settings on and the output's frames the frames played, the counters the simulator has no ring for at 0; after
both stop, the alt settings back at 0.

  tests/usb_stats_test.py HOST_EXE OUTDIR"""
import math
import struct
import subprocess
import sys
from pathlib import Path

FRAMES = 22050
SCRIPT = f"""wait 100
expect app_reads >0
expect app_out_alt 0
expect app_in_alt 0
expect app_out_frames 0
expect app_in_packets 0
usb_rec rec.wav
usb_play in.wav
wait 100
expect app_out_alt 1
expect app_in_alt 1
expect app_out_frames >0
wait 800
expect usb_play_frames {FRAMES}
expect app_out_frames {FRAMES}
expect app_out_packets 0
expect app_out_underruns 0
expect app_out_overruns 0
expect app_out_feedback 0
expect app_in_packets 0
expect app_in_underruns 0
expect app_in_overruns 0
expect app_in_missed 0
expect app_in_stalls 0
usb_play off
usb_recstop
wait 50
expect app_out_alt 0
expect app_in_alt 0
"""


def main() -> int:
    exe, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    data = b"".join(struct.pack("<hh", round(8000 * math.sin(2 * math.pi * 440 * i / 44100)), 0) for i in range(FRAMES))
    (out / "in.wav").write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                                 struct.pack("<IHHIIHH", 16, 1, 2, 44100, 44100 * 4, 4, 16) + b"data" +
                                 struct.pack("<I", len(data)) + data)
    (out / "usb_stats.txt").write_text(SCRIPT)
    r = subprocess.run([str(exe), str(out / "usb_stats.txt"), str(out)], capture_output=True, text=True)
    print(r.stdout, end="")
    expects = SCRIPT.count("\nexpect ") + SCRIPT.startswith("expect ")
    oks = sum(1 for line in r.stdout.splitlines() if line.strip().startswith("ok "))
    if r.returncode or oks != expects:
        print(f"usb-stats: FAILED (rc {r.returncode}, {oks} of {expects} checks ok)\n{r.stderr}")
        return 1
    print(f"usb-stats: all passed ({oks} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
