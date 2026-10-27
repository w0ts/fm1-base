#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The trial's second core in fm1-base's host simulator (its plat_cpu2_*: a job runs at once, in the render), with the
trial's profile copy (optimist/run.sh, the dev run):

  dualproof.py HOST_EXE OUTDIR [single]

tests/dual.txt (a template: the reverb sends, the file of the render's samples, the second core's expects) is run with
each split of the jobs (OPT_DUAL_PARTS: none, the split by load, which keeps every job on CPU0 on the host, T1, T2 + T3,
the drums, the reverb's kept block, all five), with the reverb sends at 0 and at 100. The checks:
  1. every split renders the same samples, 24-bit, as the render made them (core/opt_dual.c: integer sums, the order
     of the jobs does not matter; the per-core state, FM6's scratch on fm1_cnum, the LFOs' draws before the fork);
  2. the second core did the jobs it was given (c1jobs), and stayed up (c1up);
  3. with `single`: the same profile with DUAL=0 (one core, fx.c's own mix_block) built and run too: the same samples
     with the sends at 0; with them up, the reverb one mix block late on the dual build (fx.c REV_LATE, as on
     Optimist's own: docs/DUAL-CORE.md section 1): the sends' part of the output is one core's 32 frames later (what
     is left under -50 dB: the master's limiter on the sum), the rest the same. The build is
     left with the profile copy as it was (run again after)."""
import math
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
ROOT = APP.parent
TEMPLATE = APP / "tests" / "dual.txt"
PROFILE = APP / "profiles" / "dev-bt.config"
SPLITS = [None, 0x1, 0x6, 0x8, 0x10, 0x1F]       # (bit p: part p, 3 the drums, 4 the reverb's kept block)
SENDS = [0, 100]                                 # (0 first: the sends' part of the output is a take less the 0 one)
CTL = 32                                         # (Optimist's mix block, frames: the reverb's lateness)
LATE_DB = -50.0


def fail(msg: str) -> None:
    sys.exit(f"FAIL dualproof: {msg}")


def run(exe: Path, out: Path, name: str, rev: int, split: int | None, dual: bool) -> list[int]:
    pcm = out / f"{name}.pcm"
    c1 = ["expect c1up 1"] + ([f"expect c1jobs >100"] if split else []) if dual else []
    text = TEMPLATE.read_text().replace("{REV}", str(rev)).replace("{PCM}", str(pcm)).replace("{C1}", "\n".join(c1))
    script = out / f"{name}.txt"
    script.write_text(text)
    env = {k: v for k, v in os.environ.items() if k != "OPT_DUAL_PARTS"}
    if split is not None:
        env["OPT_DUAL_PARTS"] = hex(split)
    r = subprocess.run([str(exe), str(script), str(out)], env=env, capture_output=True, text=True)
    if r.returncode or "FAIL" in r.stdout:
        fail(f"{name}: {exe.name} {script.name}\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    b = pcm.read_bytes()
    return list(struct.unpack(f"<{len(b) // 4}i", b))


def wet_late(s0: list[int], s1: list[int], d0: list[int], d1: list[int]) -> float:
    """the sends' part of the output (a take with sends less the one without), left channel: the dual build's against
    one core's CTL frames earlier, the residual in dB under one core's"""
    ws = [a - b for a, b in zip(s1[0::2], s0[0::2])]
    wd = [a - b for a, b in zip(d1[0::2], d0[0::2])]
    e = sum((wd[i + CTL] - ws[i]) ** 2 for i in range(len(ws) - CTL))
    p = sum(x * x for x in ws) or 1
    return 10 * math.log10(e / p + 1e-30)


def build(profile: Path | None) -> None:
    env = dict(os.environ, X0X_FONTSET="terminus")
    if profile is not None:
        env["OPT_PROFILE"] = str(profile)
    else:
        env.pop("OPT_PROFILE", None)
    r = subprocess.run([sys.executable, "tools/build.py", "optimist", "--host"], cwd=ROOT, env=env, capture_output=True,
                       text=True)
    if r.returncode:
        fail(f"build ({profile or 'the profile copy'}):\n{r.stdout[-2000:]}{r.stderr[-2000:]}")


def main() -> int:
    exe, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    single = len(sys.argv) > 3 and sys.argv[3] == "single"
    out.mkdir(parents=True, exist_ok=True)
    dual_exe = out / "dual_host"
    shutil.copy2(exe, dual_exe)
    takes = {}
    for rev in SENDS:
        ref = None
        for split in SPLITS:
            name = f"dual_rev{rev}_{'load' if split is None else hex(split)}"
            s = run(dual_exe, out, name, rev, split, True)
            if not any(s):
                fail(f"{name}: silence")
            if ref is None:
                ref = s
            elif s != ref:
                k = next(i for i, (a, b) in enumerate(zip(s, ref)) if a != b) if len(s) == len(ref) else -1
                fail(f"{name}: not the samples of the split by load (first difference at word {k} of {len(ref)})")
            print(f"  ok {name}: {len(s) // 2} frames, the samples of the split by load")
        takes[rev] = ref
    if single:
        cfg = APP / "build" / "dual-single.config"
        text = PROFILE.read_text()
        if "\nDUAL=2\n" not in text:
            fail(f"{PROFILE.name}: no DUAL=2 line to turn off")
        cfg.write_text(text.replace("\nDUAL=2\n", "\nDUAL=0\n"))
        try:
            build(cfg)
            one_exe = out / "single_host"
            shutil.copy2(exe, one_exe)
        finally:
            build(None)
        singles = {}
        for rev in SENDS:
            s = run(one_exe, out, f"single_rev{rev}", rev, None, False)
            d = takes[rev]
            if len(s) != len(d):
                fail(f"single rev {rev}: {len(s)} words, the dual build {len(d)}")
            worst = max(abs(a - b) for a, b in zip(s, d))
            if rev == 0 and worst:
                fail(f"single rev 0: not the dual build's samples (largest difference {worst})")
            peak = max(abs(a) for a in s)
            print(f"  ok single core, sends {rev}: largest difference to the dual build {worst} of a peak {peak}")
            if rev:
                late = wet_late(singles[0], s, takes[0], d)
                if late > LATE_DB:
                    fail(f"sends {rev}: the dual build's reverb is not one core's one mix block late ({late:.1f} dB)")
                print(f"  ok sends {rev}: the dual build's reverb (its sends' part of the output) is one core's {CTL} "
                      f"frames late, {late:.1f} dB left (the master's limiter)")
            singles[rev] = s
    print("dualproof: every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
