#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""Optimist's own simulator (its whole firmware, firmware/sim/sim.c on firmware/sim/hal: the flash a 1 MiB RAM image
at the FM-1's offsets), built from Optimist's tree with the switches the trial's unit has (none: Optimist's source
defaults, core/registry.h), for the trial's storage checks (optimist/run.sh):

  optsim.py build                  build/optsim/sim.wasm (emcc), its headers in build/optsim/gen
  optsim.py write OUT.bin          Optimist's firmware on a blank flash: the steps of tests/storage.txt (a sound, the
                                   tempo, KNOB COLORS, project slot 3, user preset U01, the autosave), its flash to OUT
  optsim.py touch IN.bin OUT.bin   Optimist's firmware powered on with IN (the trial's flash): the tempo 4 up, the
                                   autosave; its flash to OUT
  optsim.py same OPT.bin TRIAL.bin the two flashes after the same steps: every 4 KiB sector the same byte for byte, but
                                   the two that hold what this build has and the trial's lacks (felucca.c's own
                                   defaults: FELUCCA_USB_AUDIO 1): the section log's (0x97000: the project's FX
                                   record, id 107) and the settings record's (0xFC000: its "UAR3" USB routing)

The build uses Optimist's tools/sim.py (its flags, its compile) without its generate step (that writes Optimist's
tree: gen_param_help.py's copy into web/editor.html): an empty felucca_config.h (the source defaults, as the trial),
the trial's generated headers (build/optgen, gen_optimist.py) and Optimist's font and icon generators into
build/optsim/gen. Nothing is written in Optimist's tree (no bytecode either). The driving: tests/optsim.mjs (node)."""
import os
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True                 # (importing Optimist's tools/sim.py: no __pycache__ in its tree)
APP = Path(__file__).resolve().parent
BUILD = APP / "build"
OUT = BUILD / "optsim"
GEN = OUT / "gen"
WASM = OUT / "sim.wasm"
TREE = BUILD / "optimist"                      # Optimist's tree (gen_optimist.py links it)
DRIVER = APP / "tests" / "optsim.mjs"
# the trial's generated headers (gen_optimist.py) the firmware includes beside its fonts and icons
SHARED = ["felucca_colors.h", "felucca_tables.h", "felucca_samples.h", "felucca_drumkits.h", "x0x_drum_samples.h",
          "x0x_drum_tables.h", "felucca_wavetables.h"]


def tree():
    t = TREE.resolve()
    if not (t / "firmware" / "sim" / "sim.c").is_file():
        raise SystemExit(f"optsim: no Optimist tree at {TREE} (tools/build.py optimist --host makes the link)")
    return t


def gen_headers(t):
    GEN.mkdir(parents=True, exist_ok=True)
    (GEN / "felucca_config.h").write_text("/* empty: Optimist's source defaults (core/registry.h), as the trial's unit */\n")
    for h in SHARED:
        link = GEN / h
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(BUILD / "optgen" / h)
    for tool, out in (("gen_font.py", "felucca_font.h"), ("gen_icons.py", "felucca_icons.h")):
        if not (GEN / out).exists():
            subprocess.run([sys.executable, str(t / "tools" / tool), str(GEN / out)], cwd=t, check=True)


def build():
    t = tree()
    for h in SHARED:
        if not (BUILD / "optgen" / h).exists():
            raise SystemExit(f"optsim: build/optgen/{h} missing (tools/build.py optimist --host first)")
    gen_headers(t)
    sys.path.insert(0, str(t / "tools"))
    import sim                                 # Optimist's tools/sim.py: its flags and its compile
    sim.compile_sim(GEN, WASM, True)
    print(f"optsim   {WASM}")


def drive(*args):
    if not WASM.exists():
        raise SystemExit(f"optsim: {WASM} missing (optsim.py build)")
    r = subprocess.run(["node", str(DRIVER), str(WASM), *args])
    if r.returncode:
        raise SystemExit(f"optsim: {' '.join(args)} failed")


# the sectors the simulator's build writes more into (see the docstring): their differences shown, not failed
BUILD_DIFF = {0x97000: "the section log (the simulator's build: the project's FX record, id 107)",
              0xFC000: "the settings record (the simulator's build: its USB audio routing, UAR3)"}


def same(a, b):
    x, y = Path(a).read_bytes(), Path(b).read_bytes()
    if len(x) != 0x100000 or len(y) != 0x100000:
        raise SystemExit("optsim: same: two 1 MiB images")
    bad = 0
    used = sum(1 for s in range(0, len(x), 0x1000) if x[s:s + 0x1000] != b"\xff" * 0x1000)
    for s in range(0, len(x), 0x1000):
        n = sum(1 for i in range(s, s + 0x1000) if x[i] != y[i])
        if not n:
            continue
        why = BUILD_DIFF.get(s)
        print(f"  {'ok  ' if why else 'FAIL'} sector {s:#07x}: {n} bytes differ" + (f" ({why})" if why else ""))
        bad += not why
    print(f"{'FAIL' if bad else '  ok'} optsim same: {used} sectors written by Optimist, {bad} differ unexplained")
    if bad:
        raise SystemExit(1)


def main(argv):
    if argv[:1] == ["build"]:
        build()
    elif argv[:1] == ["write"] and len(argv) == 2:
        drive("write", argv[1])
    elif argv[:1] == ["touch"] and len(argv) == 3:
        drive("touch", argv[1], argv[2])
    elif argv[:1] == ["same"] and len(argv) == 3:
        same(argv[1], argv[2])
    else:
        raise SystemExit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
