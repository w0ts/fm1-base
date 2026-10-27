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
  anything else     unchanged (the float units at -O2, as Optimist builds them; fm1-base's unity.c)
  then, every unit  mul64_pass.py: each 64-bit multiply as a short piece of inline assembly with no load beside it
                    (felucca-bsp docs/CPU.md); OPT_MUL64=0 leaves them as the compiler makes them

Optimist's tree: gen_optimist.py's (the env OPTIMIST, else the worktree beside the fm1-base clone)."""
import os
import re
import shutil
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
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


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    src, dst = argv[0], argv[1]
    unit = argv[2] if len(argv) == 3 else None
    if unit == "core/opt_core.c":
        rc = size_pass(src, dst)
        if rc:
            return rc
    else:
        shutil.copyfile(src, dst)
    if os.environ.get("OPT_MUL64", "1") != "0":
        return mul64(dst, unit or "unity.c")
    return 0


def mul64(path, unit):
    """every 64-bit multiply in line and clear of loads (mul64_pass.py: felucca-bsp docs/CPU.md)"""
    sys.path.insert(0, str(APP))
    import mul64_pass
    ir, done = mul64_pass.run(Path(path).read_text())
    Path(path).write_text(ir)
    if done:
        print(f"ir_pass: {unit}: 64-bit multiplies clear of loads: {done}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
