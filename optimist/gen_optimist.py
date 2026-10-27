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
   OPT_CONFIG=FILE (an absolute path): the release build's configuration, a .config of Optimist's builder (the one its
   build writes: tools/optimist.py build), instead of the profile copy: the felucca_config.h is FILE with RELEASE_OFF
   (below) appended, felucca_config_profile.h FILE as it is (the profile as Optimist's own firmware would run it)
   as Optimist's host tests have them.
   Bluetooth is the configuration's: fm1.toml's ble and bt_audio follow its BLE and BT_AUDIO (--conf, fm1.toml conf_gen).
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
PROFILE = "profiles/dev-bt.config"       # (Optimist's config/profiles/dev.config at 171e96a3, the trial's block after: with Bluetooth)
# what the release leaves off whatever the configuration says, for lack of support in fm1-base's platform (the trial's
# block in the profile copy says the same): never a data format or an address. Bluetooth (BLE, BT_AUDIO) is the
# configuration's own: fm1.toml's ble / bt_audio follow it (conf_gen, --conf below)
RELEASE_OFF = "# ---- the release: off for lack of support on fm1-base (its own app.ld and boot are its own)\nCACHE_RAM=0\n"
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
    """-> the configuration's path (OPT_CONFIG, else the profile copy's), or None (OPT_PROFILE=none: the source defaults)"""
    c = os.environ.get("OPT_CONFIG")
    if c:
        p = Path(c).resolve()
        if not p.is_file():
            raise SystemExit(f"gen_optimist: no configuration {p} (OPT_CONFIG)")
        return p
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
    if os.environ.get("OPT_CONFIG"):
        parts = ((text.rstrip("\n") + "\n" + RELEASE_OFF, "felucca_config.h"), (text, "felucca_config_profile.h"))
    elif MARKER not in text:
        raise SystemExit(f"gen_optimist: {prof} has no line {MARKER!r}")
    else:
        parts = ((text, "felucca_config.h"), (text.split(MARKER)[0], "felucca_config_profile.h"))
    env = None
    for part, out in parts:
        cfg, name = configure.parse(part)
        err, warn, _ = configure.validate(cfg)
        for w in warn:
            print(f"  warn  {out}: {w}")
        if err:
            raise SystemExit(f"gen_optimist: {prof.name} ({out}): " + "; ".join(err))
        configure.write_header(cfg, name, GEN / out)
        if out == "felucca_config.h":
            host_dual(configure.flags(cfg)[0].get("FELUCCA_DUAL", 0), GEN / out)
            host_sd(configure.flags(cfg)[0], GEN / out)
            slot_check(configure.flags(cfg)[0].get("FELUCCA_LAYOUT", 0), prof)
            host_bt(configure.flags(cfg)[0], GEN / out)
        if env is None:
            env = configure.flags(cfg)[1]       # (the trial's: its sample sets, SLICE)
    return env


SD_APP_END = 0xCE000                           # the SD card mode's slot (LAYOUT 1): fm1-base's app_end, OBJ_PANEL above it


def slot_check(layout, prof):
    """the build's app slot as the profile's flash layout needs it: Optimist's SD card mode (LAYOUT 1: its code past
    0x93000, its store from 0xD0000) on fm1-base's bigger slot, FM1B_APP_END=0xCE000 in the environment (app.h puts
    OBJ_PANEL at 0xCE000 then); the normal layout on the normal slot"""
    v = os.environ.get("FM1B_APP_END", "")
    end = int(v, 0) if v else 0x93000
    want = SD_APP_END if layout == 1 else 0x93000
    if end != want:
        raise SystemExit(f"gen_optimist: {prof.name} (LAYOUT {layout}) needs FM1B_APP_END={want:#x}"
                         f" in the environment, not {end:#x}")


SD_ITEMS = ("FELUCCA_SDCARD", "FELUCCA_LAYOUT", "FELUCCA_FAR_OK", "FELUCCA_SD_RING_KB", "FELUCCA_SD_REC_KB")


def host_sd(flags, path):
    """the profile's SD card mode (FM-1-only items: the header sets them under __PI32V2__ alone) on the host too: the
    card is fm1-base's simulated one there (host/plat_host.c, a card image), so the host runs the FM-1's card code"""
    items = [(k, flags[k]) for k in SD_ITEMS if flags.get(k)]
    if not items:
        return
    text = path.read_text()
    extra = "/* the trial (gen_optimist.py host_sd): the profile's SD card mode on the host too */\n" + "".join(
        f"#if defined(PLAT_HOST) && !defined({k})\n#define {k} {v}\n#endif\n" for k, v in items)
    if not text.endswith(extra):
        path.write_text(text + extra)


def host_dual(dual, path):
    """the profile's second core (DUAL: an FM-1-only item, its header sets FELUCCA_DUAL under __PI32V2__ alone) on the
    host too: fm1-base's simulated second core (host/plat_host.c, fm1.toml cpu2) runs core/opt_dual.c's jobs there, so
    the host runs the FM-1's render path"""
    if not dual:
        return
    text = path.read_text()
    extra = ("/* the trial (gen_optimist.py host_dual): the profile's second core on the host too */\n"
             f"#if defined(PLAT_HOST) && !defined(FELUCCA_DUAL)\n#define FELUCCA_DUAL {dual}\n#endif\n")
    if not text.endswith(extra):
        path.write_text(text + extra)


BT_ITEMS = ("FELUCCA_BLE", "FELUCCA_BLE_BOND", "FELUCCA_BLE_CENTRAL", "FELUCCA_BT_AUDIO", "FELUCCA_BT_AFH")


def host_bt(flags, path):
    """OPT_HOST_BT=1: the profile's Bluetooth (BLE, BT_AUDIO ...: FM-1-only items, under __PI32V2__ alone) on the host
    too: Optimist's Bluetooth screens, settings and mirror on the host simulator's stand-ins for fm1-base's stack
    (ui/opt_bt.h; optimist/run.sh's bt run). Off by default: the host runs stay comparable with Optimist's own simulator,
    which has no Bluetooth"""
    if os.environ.get("OPT_HOST_BT") != "1":
        return
    lines = "".join(f"#define {k} {flags.get(k, 0)}\n" for k in BT_ITEMS)
    extra = ("/* the trial (gen_optimist.py host_bt, OPT_HOST_BT=1): the profile's Bluetooth on the host too */\n"
             f"#if defined(PLAT_HOST)\n{lines}#endif\n")
    text = path.read_text()
    if not text.endswith(extra):
        path.write_text(text + extra)


def companion():
    """1: the build has fm1-base's companion services (fm1.toml companion = true; FM1B_COMPANION=0 / 1 over it, as
    tools/build.py opt_flags has it): OPT_COMPANION in the trial's header, for the Optimist unit (an o2 unit: on the host
    and the web it gets none of the platform's -D flags), whose editor then leaves its own backup to the companion's
    files (ui/opt_editor.h)"""
    import re
    env = os.environ.get("FM1B_COMPANION")
    if env in ("0", "1"):
        return int(env)
    return int(bool(re.search(r"(?m)^companion\s*=\s*true\b", (APP / "fm1.toml").read_text())))


def companion_define(path):
    text = path.read_text()
    extra = ("/* the trial (gen_optimist.py companion): fm1-base's companion services built */\n"
             f"#define OPT_COMPANION {companion()}\n")
    if not text.endswith(extra):
        path.write_text(text + extra)


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
    key += f"companion {companion()}\n"
    key += f"host_bt {os.environ.get('OPT_HOST_BT') == '1'}\n"
    st = GEN / "stamp"
    outs = [out for _, out in GENERATORS] + ["felucca_config.h", "felucca_config_profile.h"]
    if st.exists() and st.read_text() == key and all((GEN / out).exists() for out in outs):
        return
    st.unlink(missing_ok=True)
    genv = config(tree, prof)
    companion_define(GEN / "felucca_config.h")
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


def conf():
    """--conf (fm1.toml conf_gen, run by tools/build.py before the build): fm1.toml's ble and bt_audio from the chosen
    configuration's BLE and BT_AUDIO (a profile without BLE builds without the stack; OPT_PROFILE=none: neither)"""
    prof = profile()
    flags = {}
    if prof is not None:
        sys.dont_write_bytecode = True
        sys.path.insert(0, str(optimist_tree() / "tools" / "builder"))
        import configure
        flags = configure.flags(configure.parse(prof.read_text())[0])[0]
    ble = bool(flags.get("FELUCCA_BLE", 0))
    audio = ble and bool(flags.get("FELUCCA_BT_AUDIO", 0))
    print(f"ble = {str(ble).lower()}\nbt_audio = {str(audio).lower()}")
    return 0


def main():
    if sys.argv[1:] == ["--conf"]:
        return conf()
    tree = optimist_tree()
    link(tree)
    generate(tree)
    return 0


if __name__ == "__main__":
    sys.exit(main())
