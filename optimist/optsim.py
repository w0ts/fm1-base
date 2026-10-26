#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""Optimist's own simulator (its whole firmware, firmware/sim/sim.c on firmware/sim/hal: the flash a 1 MiB RAM image
at the FM-1's offsets), built from Optimist's tree with the profile the trial's copy holds as an FM-1 runs it
(build/optgen/felucca_config_profile.h, gen_optimist.py: Optimist's dev profile, its lines down to the trial's marker;
OPT_PROFILE=none: empty, Optimist's source defaults), for the trial's storage checks (optimist/run.sh):

  optsim.py build                  build/optsim/sim.wasm (emcc), its headers in build/optsim/gen
  optsim.py build-trial            build/optsim-trial/sim.wasm: the same with the trial's own switches (the profile
                                   copy whole, its trial block too: build/optgen/felucca_config.h), for the editor's
                                   checks (edproof.py: the same build on both sides)
  optsim.py ed SCRIPT [FLASH]      that one driven by a SysEx script (tests/optsim_ed.mjs), on FLASH (a 1 MiB image)
  optsim.py write OUT.bin          Optimist's firmware on a blank flash: the steps of tests/storage.txt (a sound, the
                                   tempo, KNOB COLORS, project slot 3, user preset U01, the autosave), its flash to OUT
  optsim.py touch IN.bin OUT.bin   Optimist's firmware powered on with IN (the trial's flash): the tempo 4 up, the
                                   autosave; its flash to OUT
  optsim.py idle IN.bin OUT.bin    Optimist's firmware powered on with IN, hands off for 15 s: what a power-on writes
                                   by itself (the autosave of the work resumed), to OUT
  optsim.py same OPT.bin TRIAL.bin the two flashes after the same steps: every 4 KiB sector the same byte for byte, but
                                   what this build has and the trial's lacks (Optimist's USB audio): in the section
                                   log (0x97000 format 1, 0xA0000 format 2) the FX records (their USB input track),
                                   the other records compared one by one; in the settings record (0xFC000) the USB
                                   routing ("UAR3") and the header's CRCs

The build uses Optimist's tools/sim.py (its flags, its compile) without its generate step (that writes Optimist's
tree: gen_param_help.py's copy into web/editor.html): felucca_config.h the profile's (above), the trial's generated
headers (build/optgen, gen_optimist.py) and Optimist's font and icon generators into build/optsim/gen. Nothing is
written in Optimist's tree (no bytecode either). The driving: tests/optsim.mjs (node)."""
import os
import struct
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


def gen_headers(t, gen=GEN, config="felucca_config_profile.h"):
    gen.mkdir(parents=True, exist_ok=True)
    prof = BUILD / "optgen" / config         # (gen_optimist.py: the profile as an FM-1 runs it; or the trial's own)
    if not prof.exists():
        raise SystemExit(f"optsim: {prof} missing (tools/build.py optimist --host first)")
    (gen / "felucca_config.h").write_text(prof.read_text())
    for h in SHARED:
        link = gen / h
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(BUILD / "optgen" / h)
    for tool, out in (("gen_font.py", "felucca_font.h"), ("gen_icons.py", "felucca_icons.h")):
        if not (gen / out).exists():
            subprocess.run([sys.executable, str(t / "tools" / tool), str(gen / out)], cwd=t, check=True)


TRIAL_OUT = BUILD / "optsim-trial"
TRIAL_WASM = TRIAL_OUT / "sim.wasm"


def build(trial=False):
    t = tree()
    for h in SHARED:
        if not (BUILD / "optgen" / h).exists():
            raise SystemExit(f"optsim: build/optgen/{h} missing (tools/build.py optimist --host first)")
    gen, wasm = (TRIAL_OUT / "gen", TRIAL_WASM) if trial else (GEN, WASM)
    gen_headers(t, gen, "felucca_config.h" if trial else "felucca_config_profile.h")
    sys.path.insert(0, str(t / "tools"))
    import sim                                 # Optimist's tools/sim.py: its flags and its compile
    sim.compile_sim(gen, wasm, True)
    print(f"optsim   {wasm}")


def ed(script, flash=None):
    if not TRIAL_WASM.exists():
        raise SystemExit(f"optsim: {TRIAL_WASM} missing (optsim.py build-trial)")
    r = subprocess.run(["node", str(APP / "tests" / "optsim_ed.mjs"), str(TRIAL_WASM), script, *([flash] if flash else [])])
    if r.returncode:
        raise SystemExit(f"optsim: ed {script} failed")


def drive(*args):
    if not WASM.exists():
        raise SystemExit(f"optsim: {WASM} missing (optsim.py build)")
    ui = "defaults" if os.environ.get("OPT_PROFILE") == "none" else "dev"   # (tests/optsim.mjs: the UI's layout)
    r = subprocess.run(["node", str(DRIVER), str(WASM), *args], env={**os.environ, "OPTSIM_UI": ui})
    if r.returncode:
        raise SystemExit(f"optsim: {' '.join(args)} failed")


# what the simulator's build writes and the trial's does not (Optimist's switches the trial has off: USB audio), by name:
#   the section log  the FX records (fx_rec.c: the USB input track's UT_TLV, ids 105..121 the scenes' and the autosave's,
#                    140 the project's): the other records the same, in the same order (id, project, bytes)
#   the settings     its header's CRCs and the USB audio routing (persist_rec.c PER4: ua_src .. ua_mark3, "UAR3")
LOG = {"none": (0x97000, 8), "dev": (0xA0000, 32)}  # (sec_log.c: format 1 / format 2 with no sample slots built)
FX_IDS = set(range(105, 122)) | {140}
SET_OK = set(range(16, 20)) | set(range(28, 32)) | set(range(0x2B6, 0x2DC))


def log_records(img, base, n):
    """(id, project, dead, bytes) of every record in the log's sectors, in their order (sec_log.c sec_rhead_t)"""
    out = []
    for s in range(base, base + n * 0x1000, 0x1000):
        off = 16
        while off + 16 <= 0x1000:
            m, rid, _, _, ln, proj, dead, _ = struct.unpack_from("<HBBIHBBI", img, s + off)
            if m == 0xFFFF:
                break
            out.append((rid, proj, dead, img[s + off + 16:s + off + 16 + ln]))
            off += 16 + ((ln + 3) & ~3)
    return out


def same(a, b):
    x, y = Path(a).read_bytes(), Path(b).read_bytes()
    if len(x) != 0x100000 or len(y) != 0x100000:
        raise SystemExit("optsim: same: two 1 MiB images")
    base, n = LOG["none" if os.environ.get("OPT_PROFILE") == "none" else "dev"]
    rx, ry = log_records(x, base, n), log_records(y, base, n)
    fx = [r for r in rx if r[0] in FX_IDS] + [r for r in ry if r[0] in FX_IDS]
    log_ok = [r for r in rx if r[0] not in FX_IDS] == [r for r in ry if r[0] not in FX_IDS]
    print(f"  {'ok  ' if log_ok else 'FAIL'} the section log {base:#07x}: {len(rx)} / {len(ry)} records, the same but the "
          f"FX records ({len(fx)}: ids {sorted({r[0] for r in fx})})")
    bad = 0 if log_ok else 1
    used = sum(1 for s in range(0, len(x), 0x1000) if x[s:s + 0x1000] != b"\xff" * 0x1000)
    for s in range(0, len(x), 0x1000):
        d = [i - s for i in range(s, s + 0x1000) if x[i] != y[i]]
        if not d or base <= s < base + n * 0x1000:
            continue
        why = "the settings record: its CRCs and the USB audio routing" if s == 0xFC000 and set(d) <= SET_OK else ""
        print(f"  {'ok  ' if why else 'FAIL'} sector {s:#07x}: {len(d)} bytes differ" + (f" ({why})" if why else ""))
        bad += not why
    print(f"{'FAIL' if bad else '  ok'} optsim same: {used} sectors written by Optimist, {bad} differ unexplained")
    if bad:
        raise SystemExit(1)


def main(argv):
    if argv[:1] == ["build"]:
        build()
    elif argv[:1] == ["build-trial"]:
        build(True)
    elif argv[:1] == ["ed"] and len(argv) in (2, 3):
        ed(*argv[1:])
    elif argv[:1] == ["write"] and len(argv) == 2:
        drive("write", argv[1])
    elif argv[:1] == ["touch"] and len(argv) == 3:
        drive("touch", argv[1], argv[2])
    elif argv[:1] == ["idle"] and len(argv) == 3:
        drive("idle", argv[1], argv[2])
    elif argv[:1] == ["same"] and len(argv) == 3:
        same(argv[1], argv[2])
    else:
        raise SystemExit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
