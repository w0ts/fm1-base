#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The trial app's generator (fm1.toml gen, run by tools/build.py with cwd APP_DIR before every build):

  gen_optimist.py

1. build/optimist -> Optimist's tree (the env OPTIMIST, else ../../fm1base-on-sd from this directory: the worktree
   beside the fm1-base clone): Optimist's source referenced by path, never written (its generators' caches and
   outputs come here, OPTIMIST_BUILD_DIR). OPT_TREE_COPY=1: build/optimist a copy of Optimist's firmware/ instead
   (src/ and hal/, the unit's sources), for the device build: fm1-base's tools/build.py gives the toolchain its
   include paths relative to fm1-base's tree (inc(): the container mounts only that), and a link out of it fails.
2. build/optgen/felucca_config.h: the build's switches, from the trial's copy of an Optimist profile (OPT_PROFILE, a
   path relative to this directory; default PROFILE: Optimist's dev) through Optimist's own builder
   (tools/builder/configure.py of the tree: parse, validate, the header, the generators' environment), as Optimist's
   tools/build.py setup_config makes it; every unit includes it first (fm1.toml o2_include). The copy's lines down to
   its trial marker are the profile as an FM-1 runs it: build/optgen/felucca_config_profile.h (optsim.py: Optimist's
   own simulator). OPT_PROFILE=none: Optimist's source defaults (core/registry.h), both headers empty, every sample set
   as Optimist's host tests have them.
3. build/optgen: Optimist's generated headers for the audio core (tables, samples, drum kits, the 909 samples,
   wavetables), in the profile's environment (its sample sets); made again only when Optimist's HEAD, its working
   tree or the profile changed (build/optgen/stamp)."""
import os
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
BUILD = APP / "build"
LINK = BUILD / "optimist"
GEN = BUILD / "optgen"
# Optimist's tests/run_tests.sh headers (tools/build.py HOST_ENV): every sample set, the slicer off (OPT_PROFILE=none)
HOST_ENV = {"FELUCCA_SAMPLES_SKIP": "", "FELUCCA_SLICE": "0"}
PROFILE = "profiles/dev-171e96a3.config"       # (Optimist's config/profiles/dev.config at 171e96a3, the trial's block after)
MARKER = "# ---- the trial:"                   # (the copy's line where the trial's own changes start)
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


def copy(tree):
    """build/optimist/firmware/src and hal: Optimist's (OPT_TREE_COPY=1), copied again at every build"""
    import shutil
    BUILD.mkdir(parents=True, exist_ok=True)
    if LINK.is_symlink():
        LINK.unlink()
    if LINK.exists():
        shutil.rmtree(LINK)
    for d in ("src", "hal"):
        shutil.copytree(tree / "firmware" / d, LINK / "firmware" / d, symlinks=True)


def link(tree):
    BUILD.mkdir(parents=True, exist_ok=True)
    if os.environ.get("OPT_TREE_COPY") == "1":
        return copy(tree)
    if LINK.is_symlink() and LINK.resolve() == tree:
        return
    if LINK.is_symlink():
        LINK.unlink()
    elif LINK.exists():
        raise SystemExit(f"gen_optimist: {LINK} is not a link (a copy: OPT_TREE_COPY=1's): remove it")
    LINK.symlink_to(tree, target_is_directory=True)


def profile():
    """-> the profile copy's path, or None (OPT_PROFILE=none: the source defaults)"""
    v = os.environ.get("OPT_PROFILE", PROFILE)
    if v == "none":
        return None
    p = (APP / v).resolve()
    if not p.is_file():
        raise SystemExit(f"gen_optimist: no profile {p} (OPT_PROFILE)")
    return p


def config(tree, prof):
    """the two headers and the generators' environment (Optimist's tools/build.py setup_config, its configure.py)"""
    if prof is None:
        for h in ("felucca_config.h", "felucca_config_profile.h"):
            (GEN / h).write_text("/* empty: Optimist's source defaults (core/registry.h), OPT_PROFILE=none */\n#pragma once\n")
        return dict(HOST_ENV)
    sys.dont_write_bytecode = True             # (no __pycache__ in Optimist's tree)
    sys.path.insert(0, str(tree / "tools" / "builder"))
    import configure
    text = prof.read_text()
    if MARKER not in text:
        raise SystemExit(f"gen_optimist: {prof} has no line {MARKER!r}")
    env = None
    for part, out in ((text, "felucca_config.h"), (text.split(MARKER)[0], "felucca_config_profile.h")):
        cfg, name = configure.parse(part)
        err, warn, _ = configure.validate(cfg)
        for w in warn:
            print(f"  warn  {out}: {w}")
        if err:
            raise SystemExit(f"gen_optimist: {prof.name} ({out}): " + "; ".join(err))
        configure.write_header(cfg, name, GEN / out)
        if env is None:
            env = configure.flags(cfg)[1]       # (the trial's: its sample sets, SLICE)
    return env


def stamp(tree):
    """Optimist's HEAD and its working tree's changes (a tree with edits gets new headers)"""
    def git(*a):
        r = subprocess.run(["git", "-C", str(tree), *a], capture_output=True, text=True)
        return r.stdout if r.returncode == 0 else "?"
    return git("rev-parse", "HEAD") + git("status", "--porcelain", "--", "tools", "assets", "firmware/src")


def generate(tree):
    GEN.mkdir(parents=True, exist_ok=True)
    prof = profile()
    key = stamp(tree) + (f"profile {prof}\n" + prof.read_text() if prof else "profile none\n")
    st = GEN / "stamp"
    outs = [out for _, out in GENERATORS] + ["felucca_config.h", "felucca_config_profile.h"]
    if st.exists() and st.read_text() == key and all((GEN / out).exists() for out in outs):
        return
    st.unlink(missing_ok=True)
    genv = config(tree, prof)
    env = {**os.environ, **genv, "OPTIMIST_BUILD_DIR": str(GEN)}   # (gen_samples.py's cache: here, not in Optimist's tree)
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
    print(f"optimist {tree} (headers: {GEN}; profile {prof.name if prof else 'none: the source defaults'})")


def main():
    tree = optimist_tree()
    link(tree)
    generate(tree)
    return 0


if __name__ == "__main__":
    sys.exit(main())
