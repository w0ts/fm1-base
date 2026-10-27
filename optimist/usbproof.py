#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The trial's USB audio in fm1-base's host simulator (its usb_play / usb_rec: a computer recording and playing, as WAV
files, one clock), with the trial's profile copy (optimist/run.sh, the dev run):

  usbproof.py HOST_EXE OUTDIR

tests/dev/usb_audio.txt, the checks here:
  1. the computer records (usb_rec) while T1 plays a note: Optimist's default routing, the stems T1 T2 T3 DRUMS on the
     channels 1..4: CH1 has the note, CH2..CH4 silence
  2. SYSTEM > USB AUDIO's MASTER set to 1+2 (the UI: uad.0 3), then the computer plays a second of a stereo signal
     (usb_play: 1 kHz on L, 441 Hz on R, 16-bit) through Optimist's USB input track (APP_USB_PLAY) while it records:
     every frame of it taken (usb_play_frames), CH1 / CH2 the FM-1's own output (its speaker's, the simulator's WAV of
     it: the same samples at the codec's scale, within the WAV's 16-bit rounding), and the signal itself through the
     master (Optimist's limiter and knee on it: its gain within 0.1 %, the rest under -40 dB), CH3 / CH4 silence (T3,
     DRUMS: nothing played)"""
import math
import struct
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
SCRIPT = APP / "tests" / "dev" / "usb_audio.txt"


def wav_write(path: Path, frames: list[tuple[int, int]]) -> None:
    data = b"".join(struct.pack("<hh", l, r) for l, r in frames)
    path.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                     struct.pack("<IHHIIHH", 16, 1, 2, 44100, 44100 * 4, 4, 16) + b"data" +
                     struct.pack("<I", len(data)) + data)


def wav_read(path: Path) -> list[list[int]]:
    b = path.read_bytes()
    nch, bits = struct.unpack_from("<H", b, 22)[0], struct.unpack_from("<H", b, 34)[0]
    n, w = struct.unpack_from("<I", b, 40)[0], bits // 8
    raw = b[44:44 + n]
    v = [int.from_bytes(raw[i:i + w], "little", signed=True) for i in range(0, len(raw), w)]
    return [v[i:i + nch] for i in range(0, len(v), nch)]


def fit(x: list[float], y: list[float]) -> tuple[float, float]:
    """y ~ g x: the gain and what is left (dB under the fitted signal)"""
    g = sum(a * b for a, b in zip(x, y)) / sum(a * a for a in x)
    res = sum((b - g * a) ** 2 for a, b in zip(x, y))
    return g, 10 * math.log10(res / sum((g * a) ** 2 for a in x))


def fail(msg: str) -> None:
    sys.exit(f"FAIL usbproof: {msg}")


def main() -> int:
    exe, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    n = 44100
    sig = [(round(8000 * math.sin(2 * math.pi * 1000 * i / 44100)), round(-6000 * math.sin(2 * math.pi * 441 * i / 44100)))
           for i in range(n)]
    wav_write(out / "usb-in.wav", sig)
    r = subprocess.run([str(exe), str(SCRIPT), str(out)], capture_output=True, text=True)
    sys.stdout.write("".join(f"  {ln}\n" for ln in r.stdout.splitlines() if "FAIL" in ln or "ok uad" in ln or "usb_" in ln))
    if r.returncode:
        fail(f"the simulator ({r.returncode}):\n{r.stdout}")
    st = wav_read(out / "usb-stems.wav")
    peak = [max(abs(f[c]) for f in st) for c in range(4)]
    if len(st[0]) != 4 or not peak[0] or any(peak[1:]):
        fail(f"the stems: 4 channels, T1's note on CH1 alone (peaks {peak})")
    print(f"  ok   usbproof stems: {len(st)} frames of 4 channels (24-bit): T1's note on CH1 (peak {peak[0] / 8388608:.3f}),"
          " CH2..CH4 silent (Optimist's default routing)")
    m, spk = wav_read(out / "usb-master.wav"), wav_read(out / "usb-master-out.wav")
    if len(m) != len(spk):
        fail(f"the recording and the output: {len(m)} / {len(spk)} frames")
    worst = max(abs(m[i][c] / 256 * 32767 / 65536 - spk[i][c]) for i in range(len(m)) for c in (0, 1))
    if worst > 1.0 or any(f[2] or f[3] for f in m):
        fail(f"CH1 / CH2 the FM-1's output (worst {worst:.2f} of a 16-bit step), CH3 / CH4 silent")
    print(f"  ok   usbproof master: {len(m)} frames, CH1 / CH2 the FM-1's own output (within {worst:.2f} of the WAV's 16-bit "
          "step at the codec's scale), CH3 / CH4 silent")
    s0 = next(i for i, f in enumerate(m) if f[0] or f[1]) - 1   # (sig[0] is 0, 0)
    lo, hi = 2000, n - 2000
    for c, name in ((0, "L 1 kHz"), (1, "R 441 Hz")):
        g, res = fit([sig[i][c] for i in range(lo, hi)], [m[s0 + i][c] / 256 for i in range(lo, hi)])
        if abs(g - 1) > 1e-3 or res > -40:
            fail(f"the computer's {name} through the USB input track and the master: gain {g:.5f}, the rest {res:.1f} dB")
        print(f"  ok   usbproof the computer's {name}: through Optimist's USB input track to the master and back, gain "
              f"{g:.5f}, the rest {res:.1f} dB (the master's limiter and knee)")
    print("  ok usbproof: the trial's USB audio, out and in")
    return 0


if __name__ == "__main__":
    sys.exit(main())
