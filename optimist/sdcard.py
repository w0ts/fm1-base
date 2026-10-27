#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The SD card image for the trial's sd run (run.sh, tests/sd): a FAT32 card as a computer makes it, with what SD card
mode needs on it, for fm1-base's simulated card (host/plat_host.c, FM1B_SD=FILE):

  sdcard.py OUT.img
  sdcard.py check IMG    after tests/sd/card.txt: the card read back as a computer reads it (mounted read-only): the
                         take RECORD/REC-0001.WAV a WAV of the master (44.1 kHz 16-bit stereo, a second at least, not
                         silent), the project PROJECTS/PROJECT/PROJECT.OPP, the samples' index SYSTEM/INDEX.BIN

  the card       64 MiB, formatted FAT32 by the system's own formatter (macOS: newfs_msdos on an hdiutil disk image,
                 as Optimist's tests/fixtures/sd/make_images.sh makes its test cards), a disk image file, never a real card
  OPTIMIST/      Optimist's SD card zip unzipped at its root (its tools/sd_zip.py: the card's folders, every factory
                 preset as a file, README.TXT)
  OPTIMIST/SAMPLES/kick.wav, loops/tone.wav   two samples made here (44.1 kHz 16-bit mono: a decaying 55 Hz kick, a
                 two-second 440 Hz tone), so SAMPLES lists a file and a folder
Exit 2 (and the reason) where the system has no such formatter: run.sh then says the card's checks were not run.
Optimist's tree: gen_optimist.py's (the env OPTIMIST)."""
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

APP = Path(__file__).resolve().parent
MB = 64


def wav(path: Path, frames: list) -> None:
    """a 44.1 kHz 16-bit mono WAV of the samples (-1..1)"""
    data = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32000)) for s in frames)
    head = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 44100,
                                                                                  88200, 2, 16)
    path.write_bytes(head + b"data" + struct.pack("<I", len(data)) + data)


def samples(root: Path) -> None:
    smp = root / "OPTIMIST" / "SAMPLES"
    (smp / "loops").mkdir(parents=True, exist_ok=True)
    wav(smp / "kick.wav", [math.exp(-i / 4000.0) * math.sin(2 * math.pi * 55.0 * i / 44100.0) for i in range(22050)])
    wav(smp / "loops" / "tone.wav", [0.5 * math.sin(2 * math.pi * 440.0 * i / 44100.0) for i in range(88200)])


def card_tree(work: Path) -> Path:
    """Optimist's SD card zip, unzipped, and the two samples: the files the card gets"""
    sys.path.insert(0, str(APP))
    import gen_optimist
    tree = gen_optimist.optimist_tree()
    zp = work / "card.zip"
    subprocess.run([sys.executable, str(tree / "tools" / "sd_zip.py"), str(zp)], check=True, cwd=tree)
    root = work / "card"
    with zipfile.ZipFile(zp) as z:
        z.extractall(root)
    samples(root)
    return root


def run(*cmd: str) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def make(out: Path) -> int:
    if not (shutil.which("hdiutil") and shutil.which("newfs_msdos")):
        print("sdcard.py: no hdiutil / newfs_msdos (macOS): no card image")
        return 2
    with tempfile.TemporaryDirectory(dir=APP / "build") as w:
        work = Path(w)
        root = card_tree(work)
        img = work / "card.img"
        with open(img, "wb") as f:
            f.truncate(MB << 20)
        dev = run("hdiutil", "attach", "-imagekey", "diskimage-class=CRawDiskImage", "-nomount", str(img)).split()[0]
        try:
            run("newfs_msdos", "-F", "32", "-c", "1", "-v", "FM1SD", dev)
        finally:
            run("hdiutil", "detach", dev)
        mnt = work / "mnt"
        mnt.mkdir()
        dev = run("hdiutil", "attach", "-imagekey", "diskimage-class=CRawDiskImage", "-mountpoint", str(mnt),
                  str(img)).split()[0]
        try:
            for p in sorted(root.rglob("*")):
                dst = mnt / p.relative_to(root)
                if p.is_dir():
                    dst.mkdir(exist_ok=True)
                else:
                    shutil.copyfile(p, dst)
            for junk in (".fseventsd", ".Spotlight-V100", ".Trashes"):
                shutil.rmtree(mnt / junk, ignore_errors=True)
        finally:
            run("hdiutil", "detach", dev)
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(img, out)
    print(f"sdcard.py: {out} ({MB} MiB FAT32: Optimist's card zip, kick.wav, loops/tone.wav)")
    return 0


def check(img: Path) -> int:
    import wave
    if not shutil.which("hdiutil"):
        print("sdcard.py: no hdiutil (macOS): the card not read back")
        return 2
    bad = 0
    with tempfile.TemporaryDirectory(dir=APP / "build") as w:
        mnt = Path(w) / "mnt"
        mnt.mkdir()
        dev = run("hdiutil", "attach", "-readonly", "-imagekey", "diskimage-class=CRawDiskImage", "-mountpoint",
                  str(mnt), str(img)).split()[0]
        try:
            opt = mnt / "OPTIMIST"
            with wave.open(str(opt / "RECORD" / "REC-0001.WAV")) as wf:
                n = wf.getnframes()
                fmt = (wf.getnchannels(), wf.getframerate(), wf.getsampwidth())
                pcm = wf.readframes(n)
            peak = max(abs(v) for (v,) in struct.iter_unpack("<h", pcm)) if pcm else 0
            for what, ok in ((f"RECORD/REC-0001.WAV: {fmt[0]} ch {fmt[1]} Hz {8 * fmt[2]}-bit, {n} frames, peak {peak}",
                              fmt == (2, 44100, 2) and n >= 44100 and peak > 300),
                             ("PROJECTS/PROJECT/PROJECT.OPP", (opt / "PROJECTS" / "PROJECT" / "PROJECT.OPP").is_file()),
                             ("SYSTEM/INDEX.BIN", (opt / "SYSTEM" / "INDEX.BIN").is_file())):
                print(f"  {'ok  ' if ok else 'FAIL'} the card: {what}")
                bad += not ok
        finally:
            run("hdiutil", "detach", dev)
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "check":
        sys.exit(check(Path(sys.argv[2]).resolve()))
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(make(Path(sys.argv[1]).resolve()))
