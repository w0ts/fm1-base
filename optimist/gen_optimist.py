#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The trial app's generator (fm1.toml gen, run by tools/build.py with cwd APP_DIR before every build):

  gen_optimist.py

1. build/optimist -> Optimist's tree (the env OPTIMIST, else ../../fm1base-on-sd from this directory: the worktree
   beside the fm1-base clone): Optimist's source referenced by path, never copied, never written (its generators'
   caches and outputs come here, OPTIMIST_BUILD_DIR).
2. build/optgen: Optimist's generated headers for the audio core (tables, samples, drum kits, the 909 samples,
   wavetables), every sample set as Optimist's host tests have them (tools/build.py --host-headers' environment);
   made again only when Optimist's HEAD or its working tree changed (build/optgen/stamp)."""
import os
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
BUILD = APP / "build"
LINK = BUILD / "optimist"
GEN = BUILD / "optgen"
# Optimist's tests/run_tests.sh headers (tools/build.py HOST_ENV): every sample set, the slicer off
HOST_ENV = {"FELUCCA_SAMPLES_SKIP": "", "FELUCCA_SLICE": "0"}
# the audio core's and the UI's headers (Optimist tools/build.py generate(): its colours for the UI; its fonts are not
# used (fm1-base's canvas draws with fm1-base's), its icons nowhere, the help lines off (gen_param_help.py would also
# rewrite Optimist's web/editor.html))
GENERATORS = [("gen_tables.py", "felucca_tables.h"), ("gen_samples.py", "felucca_samples.h"),
              ("gen_drumkits.py", "felucca_drumkits.h"), ("gen_x0x_drums.py", "x0x_drum_samples.h"),
              ("gen_wavetables.py", "felucca_wavetables.h"), ("gen_colors.py", "felucca_colors.h")]


def optimist_tree():
    tree = Path(os.environ.get("OPTIMIST") or (APP / ".." / ".." / "fm1base-on-sd")).resolve()
    if not (tree / "firmware" / "src" / "felucca.c").is_file():
        raise SystemExit(f"gen_optimist: no Optimist tree at {tree} (set OPTIMIST to its checkout)")
    return tree


def link(tree):
    BUILD.mkdir(parents=True, exist_ok=True)
    if LINK.is_symlink() and LINK.resolve() == tree:
        return
    if LINK.is_symlink():
        LINK.unlink()
    elif LINK.exists():
        raise SystemExit(f"gen_optimist: {LINK} is not a link: remove it")
    LINK.symlink_to(tree, target_is_directory=True)


def stamp(tree):
    """Optimist's HEAD and its working tree's changes (a tree with edits gets new headers)"""
    def git(*a):
        r = subprocess.run(["git", "-C", str(tree), *a], capture_output=True, text=True)
        return r.stdout if r.returncode == 0 else "?"
    return git("rev-parse", "HEAD") + git("status", "--porcelain", "--", "tools", "assets", "firmware/src")


def generate(tree):
    GEN.mkdir(parents=True, exist_ok=True)
    key = stamp(tree)
    st = GEN / "stamp"
    if st.exists() and st.read_text() == key and all((GEN / out).exists() for _, out in GENERATORS):
        return
    env = {**os.environ, **HOST_ENV, "OPTIMIST_BUILD_DIR": str(GEN)}   # (gen_samples.py's cache: here, not in Optimist's tree)
    for tool, out in GENERATORS:
        cmd = [sys.executable, str(tree / "tools" / tool), str(GEN / out)]
        if tool == "gen_samples.py":
            # its cache key names each wave relative to Optimist's tree, and with OPTIMIST_BUILD_DIR here the waves
            # are not in it: the key relative to / instead (the key only; the header is the same)
            cmd = [sys.executable, "-c", "import sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); "
                   "import gen_samples as g; g.SRC = Path('/'); g.main(sys.argv[2])",
                   str(tree / "tools"), str(GEN / out)]
        r = subprocess.run(cmd, cwd=tree, env=env, capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit(f"gen_optimist: Optimist's {tool} failed")
    st.write_text(key)
    print(f"optimist {tree} (headers: {GEN})")


def main():
    tree = optimist_tree()
    link(tree)
    generate(tree)
    return 0


if __name__ == "__main__":
    sys.exit(main())
