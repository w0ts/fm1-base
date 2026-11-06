#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The trial's pass over each unit's LLVM IR (fm1.toml ir_pass, tools/build.py: `ir_pass.py IN.ll OUT.ll [UNIT]`):

  core/opt_core.c   Optimist's own tools/size_fns.py (Optimist's tools/build.py does the same to felucca.c): the
                    main-loop functions of its UI, stores and editor files get LLVM's minsize, never what the sound
                    side reaches. Its roots of the sound side are the interrupts (*_irq) and the RAM code (.ram*):
                    here the audio path's RAM code is .app_ram (core/opt_core.c HOT) and the audio interrupt enters
                    the unit at ROOTS (app.c app_render): those are given to it as roots too. Then every function left
                    without minsize gets optsize, so the unit is built as Optimist builds felucca.c (-Os, the UI at
                    minsize) though fm1-base compiles o2 units at -O2 (OPT_IR_OS=0: the sound side at -O2 instead)
  core/opt_ff.c, core/opt_ffuni.c, core/opt_pff.c   FatFs and Petit FatFs (the SD card): every function at optsize, as
                    Optimist builds them (-Os)
  unity.c           (no UNIT: fm1-base's unit) its Bluetooth stack, Optimist's own files (bt/ble, bt/br: the same as
                    Optimist's ble/, bt/), as Optimist builds them: size_fns.py's lists cut to those files, so their
                    files built for size there (SIZE_FILES: the pairing's arithmetic, the radio's start, the links'
                    main-loop halves, bti_poll ...) get minsize, but what an interrupt (*_irq) or RAM code (.ram_text,
                    .app_ram, .c1_text) reaches, which stays at -Os by the same guard; and fm1-base's cold files and
                    functions (FM1B_SIZE_FILES, FM1B_SIZE_FNS below), the trial's glue in core/opt_core.c's unit too
                    (TRIAL_SIZE_FILES), with the same guard (OPT_IR_BT=0: unity.c unchanged; OPT_IR_FM1B=0: those not)
  anything else     unchanged (the float units at -O2, as Optimist builds them)

Optimist's tree: gen_optimist.py's (the env OPTIMIST, else the worktree beside the fm1-base clone)."""
import os
import re
import shutil
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
FM1B = APP.parent
# fm1-base's cold code (unity.c) built for size as Optimist builds its own counterparts (SIZE_FILES there: the stores,
# the companion glue, the update session, the Bluetooth glue's main-loop side, the panel's set-up): whole files, then
# single functions of the main loop's file that run once or never in a session (the safe mode, the splash, a crash
# record, the update mode's entry, the radio log readout, the remote control's frames). What an interrupt or RAM code
# reaches stays at -Os by size_fns.py's guard. Left at -Os on purpose: the main loop itself (fm1_main and its tick:
# Optimist's rule, a main loop at minsize is slower), USB, the flash driver, the LCD and the canvas, the SD card's
# transfers, the radio's start-up program (fm1_ble_rf.h, fm1_rf_cal.h: timed against the flash cache). OPT_IR_FM1B=0: none
FM1B_SIZE_FILES = ["device/companion.c", "device/storage.c", "device/panel.c", "device/announce.c", "device/sys_text.c",
                   "device/bt_own.c", "device/bt_own_audio.c", "device/bt_own_central.c", "bsp/chip/ota.c"]
FM1B_SIZE_FNS = ["safe_main", "splash", "fm1_fault", "enter_uboot", "log_frame", "log_line_send", "log_dec", "hexs",
                 "remote_frame"]
# the trial's own glue in Optimist's unit (ui/): the Bluetooth screens' and records' side, the stores, the editor and the
# console on fm1-base's, the SD card mode's: main loop only, as the Optimist files they wrap (SIZE_FILES there)
TRIAL_SIZE_FILES = ["ui/opt_bt.h", "ui/opt_bt_dev.h", "ui/opt_bt_rec.h", "ui/opt_storage.h", "ui/opt_editor.h",
                    "ui/opt_sd.h", "ui/opt_console.h", "ui/opt_supply.h", "ui/opt_stubs.h"]


def with_extra(size_fns, root, files, fns=()):
    """size_fns.py's SIZE_FILES with files of another tree (root; their functions by size_fns.py's own definition
    finder) and the functions fns"""
    orig = size_fns.names_in
    extra = set(fns)
    for f in files:
        p = root / f
        if p.exists():
            extra.update(size_fns.definitions(p.read_text()))
    key = "<fm1-base and the trial's>"

    def names_in(fs):
        return orig([f for f in fs if f != key]) | (extra if key in fs else set())
    size_fns.names_in = names_in
    size_fns.SIZE_FILES = [*size_fns.SIZE_FILES, key]
ROOTS = ("opt_core_render", "opt_core_midi", "opt_core_block", "opt_core_panic",   # (called inside the audio interrupt,
         "opt_core_tick")                                                             # and the timer's: app.c app_tick)
RAM_ROOT = ' section ".ram_trial_root"'        # (the mark size_fns.py's root test reads: a section named .ram*)


def tree():
    sys.path.insert(0, str(APP))
    import gen_optimist
    return gen_optimist.optimist_tree()


def size_pass(src, dst):
    sys.dont_write_bytecode = True             # (no __pycache__ in Optimist's tree)
    sys.path.insert(0, str(tree() / "tools"))
    import size_fns
    side = size_fns.main_loop_side

    def trial_side(funcs, globs):              # the trial's roots beside Optimist's (see the top)
        marked = {n: (head + RAM_ROOT if n in ROOTS or 'section ".app_ram' in head else head, body)
                  for n, (head, body) in funcs.items()}
        return side(marked, globs)
    size_fns.main_loop_side = trial_side
    if os.environ.get("OPT_IR_FM1B", "1") != "0":
        with_extra(size_fns, APP, TRIAL_SIZE_FILES)
    rc = size_fns.main([str(src), str(dst)])
    if rc or os.environ.get("OPT_IR_OS", "1") == "0":
        return rc
    ir = Path(dst).read_text()
    n = 0

    def optsize(m):                            # (a definition's attribute group reference: optsize beside it)
        nonlocal n
        if re.search(r"\b(minsize|optnone|optsize)\b", m.group(1)):
            return m.group(0)
        n += 1
        return m.group(1) + " optsize" + m.group(3)
    ir = size_fns.IR_DEF.sub(optsize, ir)
    Path(dst).write_text(ir)
    print(f"ir_pass: {n} functions of the unit's sound side and main loop at optsize (-Os, as Optimist's felucca.c)")
    return 0


def os_pass(src, dst):
    """every definition at optsize (a unit Optimist builds at -Os)"""
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(tree() / "tools"))
    import size_fns
    ir, n = Path(src).read_text(), 0

    def optsize(m):
        nonlocal n
        if re.search(r"\b(minsize|optnone|optsize)\b", m.group(1)):
            return m.group(0)
        n += 1
        return m.group(1) + " optsize" + m.group(3)
    Path(dst).write_text(size_fns.IR_DEF.sub(optsize, ir))
    print(f"ir_pass: {n} functions at optsize (-Os, as Optimist's FatFs units)")
    return 0


def bt_pass(src, dst):
    """unity.c: Optimist's size_fns.py on the Bluetooth stack's functions alone (see the top)"""
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(tree() / "tools"))
    import size_fns
    bt = ("ble/", "bt/")
    size_fns.SIZE_FILES = [f for f in size_fns.SIZE_FILES if f.startswith(bt)]
    size_fns.AUDIO_FILES = [f for f in size_fns.AUDIO_FILES if f.startswith(bt)]
    size_fns.OS_FILES = [f for f in size_fns.OS_FILES if f.startswith(bt)]
    functions = size_fns.ir_functions

    def unity_functions(ir):                   # fm1-base's RAM code as roots too (its sections: .app_ram, .c1_text)
        return {n: (head + RAM_ROOT if re.search(r'section "\.(?:app_ram|c1_text)', head) else head, body)
                for n, (head, body) in functions(ir).items()}
    size_fns.ir_functions = unity_functions
    if os.environ.get("OPT_IR_FM1B", "1") != "0":
        with_extra(size_fns, FM1B, FM1B_SIZE_FILES, FM1B_SIZE_FNS)
    return size_fns.main([str(src), str(dst)])


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    src, dst = argv[0], argv[1]
    unit = argv[2] if len(argv) == 3 else None
    if unit == "core/opt_core.c":
        return size_pass(src, dst)
    if unit in ("core/opt_ff.c", "core/opt_ffuni.c", "core/opt_pff.c"):
        return os_pass(src, dst)
    if unit is None and os.environ.get("OPT_IR_BT", "1") != "0":
        return bt_pass(src, dst)
    shutil.copyfile(src, dst)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
