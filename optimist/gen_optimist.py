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
   Bluetooth and the cache RAM are the configuration's: fm1.toml's ble, bt_audio and cache_ram_ways follow its BLE,
   BT_AUDIO and CACHE_RAM (--conf, fm1.toml conf_gen).
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
# block in the profile copy says the same): never a data format or an address. Nothing today: Bluetooth (BLE,
# BT_AUDIO) and the cache RAM (CACHE_RAM, fm1-base's cache_ram_ways since feat/cache-ram) are the configuration's own,
# fm1.toml's ble / bt_audio / cache_ram_ways follow it (conf_gen, --conf below)
RELEASE_OFF = ""
MARKER = "# ---- the trial:"                   # (the copy's line where the trial's own changes start)
# the audio core's and the UI's headers (Optimist tools/build.py generate(): its colours for the UI; its fonts are not
# used (fm1-base's canvas draws with fm1-base's), its icons nowhere; the help lines' (PARAM_HELP) header alone: the
# generator's own run would also bring Optimist's web/editor.html's copy up to date, in its tree)
GENERATORS = [("gen_tables.py", "felucca_tables.h"), ("gen_samples.py", "felucca_samples.h"),
              ("gen_drumkits.py", "felucca_drumkits.h"), ("gen_x0x_drums.py", "x0x_drum_samples.h"),
              ("gen_wavetables.py", "felucca_wavetables.h"), ("gen_colors.py", "felucca_colors.h"),
              ("gen_param_help.py", "felucca_param_help.h"), ("gen_bank_order.py", "felucca_bank.h")]


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
        dim_header({}, GEN / "opt_dim.h")
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
            host_dual(0 if os.environ.get("OPT_HOST_DUAL") == "0" else configure.flags(cfg)[0].get("FELUCCA_DUAL", 0), GEN / out)
            host_sd(configure.flags(cfg)[0], GEN / out)
            slot_check(configure.flags(cfg)[0].get("FELUCCA_LAYOUT", 0), prof, tree)
            host_bt(configure.flags(cfg)[0], GEN / out)
            host_wifi(configure.flags(cfg)[0], GEN / out)
            ble_peers_define(configure.flags(cfg)[0], GEN / out)
            dim_header(configure.flags(cfg)[0], GEN / "opt_dim.h")
        if env is None:
            env = configure.flags(cfg)[1]       # (the trial's: its sample sets, SLICE)
    return env


def dim_header(flags, path):
    """build/optgen/opt_dim.h: OPT_DIM_LEVELS 1 (LIGHTS, the backlight's LOW / MID / HIGH, is in every Optimist build since
    2026-10-29): app.h then defines APP_DIM_LEVELS, fm1-base's plat_dim_ns, for the platform's units too (they see none of
    felucca_config.h). flags stays for the call sites"""
    text = ("/* the trial (gen_optimist.py dim_header): LIGHTS's levels on fm1-base's dim pulse */\n#pragma once\n"
            f"#define OPT_DIM_LEVELS 1\n")
    rows = {0: 124, 1: 62, 2: 31, 3: 16}.get(flags.get("FELUCCA_CANVAS_STRIP", 2))
    if rows:                                   # (CANVAS_STRIP: the canvas's rows for the platform's units too, app.h)
        text += f"#define OPT_CANVAS_ROWS {rows}u\n"
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


SD_APP_END = 0xCE000                           # the SD card mode's slot (LAYOUT 1): fm1-base's app_end, OBJ_PANEL above it


def slot_check(layout, prof, tree):
    """the build's app slot as the profile's flash layout (its MEMORY step) needs it: FM1B_APP_END in the environment
    = the step's release end in Optimist's one table (tools/flash_layout.py STEPS release_end: the normal slot 0x93000,
    the SD card mode's L1 0xCE000 with OBJ_PANEL just above it, every other step's end at its FM6 voices, OBJ_PANEL at
    0xD8000: app.h, ui/opt_storage.h check it); and fm1.toml app_steps (the update loader's switch rule) the table's
    every known end, so a package of any step clears what the step it replaces kept elsewhere"""
    sys.path.insert(0, str(tree / "tools"))
    import flash_layout
    if layout not in flash_layout.STEPS:
        raise SystemExit(f"gen_optimist: {prof.name}: LAYOUT {layout} has no app end ({sorted(flash_layout.STEPS)})")
    want = flash_layout.STEPS[layout].release_end
    if layout == 1 and want != SD_APP_END:
        raise SystemExit(f"gen_optimist: Optimist's L1 release end {want:#x} is not SD_APP_END {SD_APP_END:#x}")
    import tomllib
    conf = tomllib.loads((APP / "fm1.toml").read_text())
    steps = {int(e): (int(s), int(r)) for e, s, r in conf.get("app_steps", [])}
    if steps != flash_layout.known_ends() or conf.get("app_clear_top") != flash_layout.CLEAR_TOP:
        raise SystemExit("gen_optimist: fm1.toml app_steps / app_clear_top are not Optimist's tools/flash_layout.py "
                         f"known_ends() / CLEAR_TOP: {[[hex(e), s, hex(r)] for e, (s, r) in flash_layout.known_ends().items()]}")
    v = os.environ.get("FM1B_APP_END", "")
    end = int(v, 0) if v else 0x93000
    if end != want:
        raise SystemExit(f"gen_optimist: {prof.name} (LAYOUT {layout}) needs FM1B_APP_END={want:#x}"
                         f" in the environment, not {end:#x}")


SD_ITEMS = ("FELUCCA_SDCARD", "FELUCCA_LAYOUT", "FELUCCA_SD_RING_KB", "FELUCCA_SD_REC_KB")


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
    the host runs the FM-1's render path. OPT_HOST_DUAL=0: one core on the host (dualproof.py single: DUAL is retired on
    Optimist since fm1-next 4864f79a, a DUAL=0 profile builds the second core all the same)"""
    if not dual:
        return
    text = path.read_text()
    extra = ("/* the trial (gen_optimist.py host_dual): the profile's second core on the host too */\n"
             f"#if defined(PLAT_HOST) && !defined(FELUCCA_DUAL)\n#define FELUCCA_DUAL {dual}\n#endif\n")
    if not text.endswith(extra):
        path.write_text(text + extra)


BT_ITEMS = ("FELUCCA_BLE", "FELUCCA_BLE_BOND", "FELUCCA_BLE_CENTRAL", "FELUCCA_BT_AUDIO")
# (BT_AFH: retired on Optimist 2026-10-29, Bluetooth audio always hops adaptively: pinned on, as the release had it)
BT_PINNED = {"FELUCCA_BT_AFH": 1}


def ble_peers_define(flags, path):
    """OPT_BLE_PEERS 1: the build has fm1-base's central with the devices one by one (conf: ble_central; app.h
    APP_BLE_PEERS: app.c hands the scan's reports and a pairing's keys to the Optimist unit, ui/opt_bt_dev.h), on the host
    too, where the Optimist unit has no Bluetooth unless OPT_HOST_BT=1 (its stand-ins then)"""
    if not (flags.get("FELUCCA_BLE", 0) and flags.get("FELUCCA_BLE_CENTRAL", 0)):
        return
    extra = ("/* the trial (gen_optimist.py ble_peers_define): fm1-base's central, the devices one by one */\n"
             "#define OPT_BLE_PEERS 1\n")
    text = path.read_text()
    if not text.endswith(extra):
        path.write_text(text + extra)


def host_bt(flags, path):
    """OPT_HOST_BT=1: the profile's Bluetooth (BLE, BT_AUDIO ...: FM-1-only items, under __PI32V2__ alone) on the host
    too: Optimist's Bluetooth screens, settings and mirror on the host simulator's stand-ins for fm1-base's stack
    (ui/opt_bt.h; optimist/run.sh's bt run). Off by default: the host runs stay comparable with Optimist's own simulator,
    which has no Bluetooth"""
    if os.environ.get("OPT_HOST_BT") != "1":
        return
    lines = "".join(f"#define {k} {flags.get(k, 0)}\n" for k in BT_ITEMS)
    lines += "".join(f"#define {k} {v}\n" for k, v in BT_PINNED.items())
    extra = ("/* the trial (gen_optimist.py host_bt, OPT_HOST_BT=1): the profile's Bluetooth on the host too */\n"
             f"#if defined(PLAT_HOST)\n{lines}#endif\n")
    text = path.read_text()
    if not text.endswith(extra):
        path.write_text(text + extra)


WIFI_ITEMS = ("FELUCCA_WIFI", "FELUCCA_LINK", "FELUCCA_WIFI_AP")


def host_wifi(flags, path):
    """OPT_HOST_WIFI=1 (with OPT_HOST_BT=1: Wi-Fi rides on the Bluetooth radio): the profile's Wi-Fi (WIFI_LINK: an
    FM-1-only item) on the host too: Optimist's SYSTEM > WI-FI, its editor command and Link's clock on fm1-base's own
    Wi-Fi stack, which the host simulator runs over its simulated network (host/wifi_air.c; optimist/run.sh's wifi run)"""
    if os.environ.get("OPT_HOST_WIFI") != "1" or not flags.get("FELUCCA_WIFI", 0):
        return
    lines = "".join(f"#define {k} {flags.get(k, 0)}\n" for k in WIFI_ITEMS)
    extra = ("/* the trial (gen_optimist.py host_wifi, OPT_HOST_WIFI=1): the profile's Wi-Fi on the host too */\n"
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
    key += f"host_wifi {os.environ.get('OPT_HOST_WIFI') == '1'}\n"
    key += f"host_dual {os.environ.get('OPT_HOST_DUAL') != '0'}\n"   # (OPT_HOST_DUAL=0: one core on the host, dualproof.py single)
    st = GEN / "stamp"
    outs = [out for _, out in GENERATORS] + ["felucca_config.h", "felucca_config_profile.h", "opt_dim.h"]
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
        if tool == "gen_param_help.py":
            # the header only (its main also writes the editor's copy into Optimist's web/editor.html): load, validate,
            # header(table, every=False), as its main does
            cmd = [sys.executable, "-c", "import sys; sys.path.insert(0, sys.argv[1]); import gen_param_help as g; "
                   "t = g.load(); bad = g.validate(t); "
                   "sys.exit('tools/param_help.json: ' + '; '.join(bad)) if bad else open(sys.argv[2], 'w').write(g.header(t, False))",
                   str(tree / "tools"), str(GEN / out)]
        r = subprocess.run(cmd, cwd=tree, env=env, capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit(f"gen_optimist: Optimist's {tool} failed")
    st.write_text(key)
    print(f"optimist {tree} (headers: {GEN}; profile {prof.name if prof else 'none: the source defaults'})")


# The RAM functions of an engine's render path that call out of RAM or through a pointer (tools/ramcode.py), by the
# engine's switch (Optimist's core.h FAST_RAM_<name>: its builder item "on, code in RAM"): in fm1.toml's
# ram_code_allow while the engine's code is RAM code, out of it otherwise (a name there must be a RAM function)
FAST_ALLOW = {
    "analog": ["analog_render", "a2_fltb", "a2_filter"],   # (the float filter models, a2_models.c: XIP, through FAR)
    "fm6": ["fm6_render"],
    "prophet": ["prophet_render"],                    # (p5_render, its own unit: FAR, RAM code with it)
}
# the sequencer's code in RAM (Optimist's builder item SEQ_RAM, FELUCCA_SEQ_RAM: 1 FAST(seqev), 2 FAST(seq) too): the functions that
# call its cold paths in XIP through FAR (FAST_CALL(seqev, f): the arrangement, MIDI out, the generators, the locks' writes,
# the note ends ...) or the engines' note hooks through ENGINES[]; events_block's switches are tbh tables in its code too
# (read as a 'goto rN'). '?': a profile without the feature a function serves may inline it or leave it out
SEQ_ALLOW_EV = ["events_block?", "events_block_?"]   # (SEQ_RAM 1: the events; 2: and the hot core, SEQ_ALLOW too; "_": the
#                                                    inner function under CPU TIMES' timer, SYS_DEBUG)
SEQ_ALLOW = ["seq_step?", "gen_eff?", "gen_deff_of?", "lock_apply?", "sa_locks_off?", "rec_hold?",
             "seq_out_on?", "trk_note_on?", "trk_note_on_?", "trk_note_chord?", "voice_start?", "mono_play?",
             "dl_smp_fx?"]   # (dl_smp_fx: a drum hit's CUT, ds_onepole through FAR)
# the insert effects' kernels in XIP (Optimist's builder item INS_RAM 0, FELUCCA_INS_RAM: core.h FAST2(ins), FAST_CALL(insx, f)):
# the kernels that call through FAR are no RAM code then (out of the list), and the RAM code that calls them does it through
# FAR: the dispatchers (ifx_run, ifx_part, ifx_bus, ifx_st, dins_one, already there) and mix_part, a function of its own
# where a build does not inline it into mix_block / dual_run ("?")
INS_XIP_OUT = ["ifx_gater?", "ifx_lim_run", "ifx_drive?", "ifx_ts_run?"]   # (fm1.toml's names as written)
INS_XIP_IN = ["mix_part?"]


def ram_code_allow(flags):
    """fm1.toml's ram_code_allow for this configuration: the file's, the engines' names by their places (FAST_ALLOW),
    the sequencer's (SEQ_ALLOW) with its step code in RAM, the insert kernels' callers with the kernels in XIP (INS_XIP_*),
    without ut_mix when there is no USB audio (USB_AUDIO off, or the serial console)"""
    import tomllib
    allow = tomllib.loads((APP / "fm1.toml").read_text())["ram_code_allow"]
    out = list(allow)
    if "FAST_RAM_analog" in flags:                    # (an Optimist with the engines' places: theirs; an older one: the file's)
        every = {f for names in FAST_ALLOW.values() for f in names}
        out = [f for f in allow if f not in every]
        for nm, names in FAST_ALLOW.items():
            if flags.get(f"FAST_RAM_{nm}", 0):
                out += [f for f in names if f not in out]
    seq_ram = flags.get("FELUCCA_SEQ_RAM", 0)
    out += [f for f in (SEQ_ALLOW_EV if seq_ram >= 1 else []) + (SEQ_ALLOW if seq_ram >= 2 else []) if f not in out]
    if not flags.get("FELUCCA_INS_RAM", 1):
        out = [f for f in out if f not in INS_XIP_OUT] + [f for f in INS_XIP_IN if f not in out]
    # (ut_mix, the USB input track's mix, is built only with USB audio, fx.c FELUCCA_USB_AUDIO, and never beside the
    # console, which takes its endpoint: synths and synths-2 have neither, "ut_mix is no function in RAM code", 2026-11-02)
    if flags.get("FELUCCA_CDC", 0) or not flags.get("FELUCCA_USB_AUDIO", 0):
        out = [f for f in out if f != "ut_mix"]
    return out


def conf():
    """--conf (fm1.toml conf_gen, run by tools/build.py before the build): fm1.toml's ble and bt_audio from the chosen
    configuration's BLE and BT_AUDIO (a profile with neither builds without the stack; OPT_PROFILE=none: neither),
    bt_audio_mem = "app", wifi_mem = "app" and usb_audio_mem = "app" (USB_AUDIO) with an arena (ARENA: Bluetooth audio's buffers, Link's node and the access
    point's state lent by it), and
    ble_central from its BLE_CENTRAL, usb_host from its USB_HOST, cache_ram_ways from its CACHE_RAM"""
    prof = profile()
    flags = {}
    if prof is not None:
        sys.dont_write_bytecode = True
        sys.path.insert(0, str(optimist_tree() / "tools" / "builder"))
        import configure
        flags = configure.flags(configure.parse(prof.read_text())[0])[0]
    audio = bool(flags.get("FELUCCA_BT_AUDIO", 0))
    ble = bool(flags.get("FELUCCA_BLE", 0)) or audio     # (fm1-base's Bluetooth audio rides on its BLE stack, which starts
                                                         # the radio; without BT MIDI it never advertises: ui/opt_bt.h)
    print(f"ble = {str(ble).lower()}\nbt_audio = {str(audio).lower()}")
    if audio and flags.get("FELUCCA_ARENA_KB", 0):
        print('bt_audio_mem = "app"')                  # (its queue and ring lent by the arena while an AUDIO mode is on:
                                                         # core/arena_bufs.c ARB_BTA; without an arena, static as before)
    if flags.get("FELUCCA_BLE", 0) and flags.get("FELUCCA_BLE_CENTRAL", 0):
        # the devices the FM-1 connects to: fm1-base's central (ui/opt_bt_dev.h), at Optimist's level (3: no pairing)
        print("ble_central = 3" if flags["FELUCCA_BLE_CENTRAL"] == 3 else "ble_central = true")
    if flags.get("FELUCCA_BLE", 0) and flags.get("FELUCCA_BLE_BOND", 0):
        print("ble_pairing = true")                      # (BLE_BOND: a computer's pairing kept, ui/opt_bt.h opt_ble_save)
    if flags.get("FELUCCA_CACHE_RAM", 0):
        # the data cache's ways as RAM (CACHE_RAM: 2, 4 or 7): fm1-base's (felucca-bsp's fm1_cache_ram.h, plat_cache_ram;
        # the trial's fm1_compat.h), the undo history's second part, the arena that much more of the SRAM
        print(f"cache_ram_ways = {int(flags['FELUCCA_CACHE_RAM'])}")
    if flags.get("FELUCCA_WIFI", 0):
        # Wi-Fi + Link (WIFI_LINK): fm1-base's own Wi-Fi stack, which is Optimist's moved, on the radio
        # its own Bluetooth stack starts (the builder refuses WIFI_LINK without BLE), and Ableton Link on it (link/:
        # GPL-3.0-only, as this app: fm1.toml licence); ui/opt_wifi.h
        print("wifi = true", 'wifi_stack = "own"', "link = true", sep="\n")
        if flags.get("FELUCCA_WIFI_AP", 0):
            print("wifi_ap = true")                     # MODE AP: fm1-base's (device/wifi_own_ap.c, Optimist's wifi_apd.c moved)
        if flags.get("FELUCCA_ARENA_KB", 0):
            print('wifi_mem = "app"')                 # (Link's node and the access point's state lent by the arena while
                                                         # Wi-Fi is on: core/opt_lend.h L_LINK, L_AP; else static)
    if flags.get("FELUCCA_USB_HOST", 0):
        # the USB MIDI host (USB_HOST): fm1-base's (felucca-bsp's chip/usb_host.c), SYSTEM > USB's MODE HOST (ui/opt_usbh.h)
        print("usb_host = true")
    # (Optimist's BLE_DIAG and BT_DIAG items were retired 2026-10-29: fm1-base's ble_diag stays fm1.toml's false, its bt_log
    # off; both remain fm1-base's own options)
    if flags.get("FELUCCA_CDC", 0):
        # the serial console (USB_CONSOLE, ui/opt_console.h): as on Optimist, no USB audio beside it (its endpoint, EP3,
        # is the output's feedback; the split functions have no room for it)
        print("usb_console = true", "usb_audio_in = false", "usb_audio_out = false", "usb_audio_out_feedback = false",
              "usb_audio_split = false", sep="\n")
    else:
        # USB audio as Optimist's own presents it (fm1.toml): two devices, named, the output on the FM-1's clock
        print("usb_audio_out_feedback = true", "usb_audio_split = true",
              'usb_audio_in_channel_names = ["CH1", "CH2", "CH3", "CH4"]', sep="\n")   # (the devices' names follow usb_name_fn: "FM1-XXXX In" / "Out")
        if flags.get("FELUCCA_USB_AUDIO", 0) and flags.get("FELUCCA_ARENA_KB", 0):
            print('usb_audio_mem = "app"')            # (its rings lent by the arena only while the computer streams, with
                                                         # the render's own buffers: core/opt_lend.h L_UAC; else static)
    import json
    print("ram_code_allow =", json.dumps(ram_code_allow(flags)))   # (the engines' places, FAST_ALLOW; ut_mix with USB audio)
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
