#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""mul64_pass.py checked by running it, in fm1-base's host simulator:

  mul64proof.py OUTDIR

The device build turns each 64-bit multiply of the C into a short piece of pi32v2 assembly (mul64_pass.py). The host
cannot run that, so here the pass runs on the host's own IR of core/opt_core.c in its model mode: each piece of
assembly as the plain IR of what it computes, from the operands, signs, sums and shift the pass took apart (and the
64-bit operands' low and high words, as on the device). Three host builds play the same script (tests/dev/mul64.txt:
every engine of the profile on T1..T3 at once, through the delay, the reverb, the chorus, a JUNO CHORUS and a GATE
insert, the drum kits, MIDI chords and hits) and keep the render's samples (24-bit):

  plain    tools/build.py's host build as it is
  control  core/opt_core.c through its unoptimised IR and back, the pass off (OPT_MUL64=0)
  model    the same with the pass on, in its model mode

model must render control's samples, every one: the pass rebuilt every product as the C wrote it. (plain against
control is printed too: the IR's way back alone.) The host's FELUCCA_ASM=0 C stands in for Optimist's asm loops
(their FELUCCA_MUL64_CLEAR forms are not in this check: Optimist's FELUCCA_ASM_CHECK compares those on the device).
The build is left as the plain one. Run from anywhere; OPTIMIST and PATH as for optimist/run.sh."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
ROOT = APP.parent
SCRIPT = APP / "tests" / "dev" / "mul64.txt"
HOST = APP / "build" / "host" / "optimist_host"
UNIT = "opt_core.c"


def fail(msg: str) -> None:
    sys.exit(f"FAIL mul64proof: {msg}")


def cc(args: list[str]) -> int:
    """the C compiler for the host build (CC = the shim beside OUTDIR's): core/opt_core.c by way of its IR and the
    pass (MUL64PROOF: 'control' the pass off, 'model' on, model mode); anything else as given"""
    real = os.environ.get("MUL64PROOF_CC", "cc")
    if not any(a.endswith(UNIT) for a in args) or "-c" not in args:
        return subprocess.run([real, *args]).returncode
    out = args[args.index("-o") + 1]
    src = next(a for a in args if a.endswith(UNIT))
    rest = [a for i, a in enumerate(args) if a != src and a != "-o" and (i == 0 or args[i - 1] != "-o")]
    ll, pll = out + ".ll", out + ".pass.ll"
    r = subprocess.run([real, *rest, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", src, "-o", ll])
    if r.returncode:
        return r.returncode
    ir = Path(ll).read_text()
    if os.environ.get("MUL64PROOF") == "model":
        sys.path.insert(0, str(APP))
        import mul64_pass
        ir, done = mul64_pass.run(ir, model=True)
        Path(out + ".done").write_text(done + "\n")
    Path(pll).write_text(ir)
    flags = [a for i, a in enumerate(rest) if a != "-include" and (i == 0 or rest[i - 1] != "-include")]
    flags = [a for a in flags if not a.startswith(("-I", "-D", "-std", "-W"))]
    return subprocess.run([real, *flags, "-Wno-override-module", "-x", "ir", pll, "-o", out]).returncode


def build(out: Path, mode: str | None) -> None:
    env = dict(os.environ, X0X_FONTSET="terminus")
    if mode:
        shim = out / "cc.sh"
        shim.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{Path(__file__).resolve()}" cc "$@"\n')
        shim.chmod(0o755)
        env.update(CC=str(shim), MUL64PROOF=mode, MUL64PROOF_CC=os.environ.get("CC", "cc"))
    r = subprocess.run([sys.executable, "tools/build.py", "optimist", "--host"], cwd=ROOT, env=env,
                       capture_output=True, text=True)
    if r.returncode:
        fail(f"build {mode or 'plain'}:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")


def play(out: Path, name: str) -> bytes:
    pcm = out / f"{name}.pcm"
    script = out / f"{name}.txt"
    script.write_text(SCRIPT.read_text().replace("{PCM}", str(pcm)))
    r = subprocess.run([str(HOST), str(script), str(out)], capture_output=True, text=True)
    if r.returncode or "FAIL" in r.stdout:
        fail(f"{name}: {script.name}\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    (out / f"{name}.log").write_text(r.stdout)
    return pcm.read_bytes()


def first_diff(a: bytes, b: bytes) -> str:
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
    return f"{len(a)} and {len(b)} bytes, first difference at sample {n // 4}"


def main(argv: list[str]) -> int:
    if argv[:1] == ["cc"]:
        return cc(argv[1:])
    if len(argv) != 1:
        sys.exit(__doc__)
    out = Path(argv[0]).resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    pcm, done = {}, None
    for mode in ("model", "control", None):      # (plain last: the build left as it is)
        build(out, mode)
        if mode == "model":
            done = next(iter(sorted((APP / "build").rglob("*opt_core*.done"))), None)
            done = done.rename(out / "model.done") if done else None
        pcm[mode or "plain"] = play(out, mode or "plain")
    if not pcm["control"] or len(pcm["control"]) < 48000 * 8:
        fail(f"control rendered {len(pcm['control'])} bytes")
    if pcm["model"] != pcm["control"]:
        fail(f"the pass's model renders other samples than control: {first_diff(pcm['model'], pcm['control'])}")
    quiet = sum(1 for i in range(0, len(pcm["model"]), 4) if pcm["model"][i:i + 4] == b"\0\0\0\0")
    print(f"  ok mul64 model: the same {len(pcm['model']) // 4} samples as control ({quiet} zero), "
          f"the pass: {done.read_text().strip() if done else '?'}")
    print(f"  {'ok' if pcm['plain'] == pcm['control'] else 'note'} plain build against control (the IR's way back "
          f"alone): {'the same samples' if pcm['plain'] == pcm['control'] else first_diff(pcm['plain'], pcm['control'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
