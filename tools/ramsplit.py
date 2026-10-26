# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""An app's split of the SRAM between RAM code and the pool (fm1.toml ram_text_kb), on the linker script's MEMORY.

device/app.ld lays the SRAM out from its bottom: RAMTEXT (the RAM code window: .ram_text, copied at boot, the
guard's one window for code outside XIP), RAM (.data and .bss), POOL (big zeroed buffers) up to the stacks' first
guard band; then the stacks, NOINIT and the reserved top, which nothing here moves. The SRAM is fixed, so a bigger
RAMTEXT is taken from the pool: what RAMTEXT grows past RAM's start moves RAM up by as much (its size kept) and the
pool's start too, so the pool ends where it did (the stacks', NOINIT's and the top's addresses unchanged). WiFi's
layout (device/app_wifi.ld: RAMTEXT 24 KiB, 8 KiB free below RAM) gives that gap first.

  ram_text_kb = N    RAMTEXT N KiB, at least the script's own (32, 24 with WiFi); the script's own size, or no
                     ram_text_kb, links with the script as it is (the same image, byte for byte)

What it costs and what to know:
  - the pool shrinks by what RAM moves (tools/build.py check keeps the pool's spare: a split too big fails there);
  - every RAM and pool address moves up: an app's or a radio's buffer at a fixed address does not exist in fm1-base
    (the linker places them all), but a hardware reach is the app's to know (ble/shim/misc.c: a radio's buffers
    low in RAM);
  - felucca-bsp's crt0 writes the boot probe's words (icfg, psr, sp ...) at 0x01C07F00.. before the C start-up, the
    last 256 bytes of a 32 KiB RAMTEXT: in a bigger one the start-up's copy of .ram_text writes code over them (as a
    full 32 KiB one does today); nothing in fm1-base reads them.
"""
import re

MEMORY = re.compile(r"\bMEMORY\s*\{(.*?)\}", re.S)
REGION = re.compile(r"^(?P<lead>\s*)(?P<name>\w+)(?P<attr>\s*\([a-z]+\)\s*:\s*)ORIGIN\s*=\s*(?P<org>0x[0-9A-Fa-f]+|\d+)"
                    r"\s*,\s*LENGTH\s*=\s*(?P<len>0x[0-9A-Fa-f]+|\d+K?)(?P<tail>.*)$", re.M)
KIB = 1024


def number(s):
    """a linker number: 0x..., decimal, or decimal K"""
    return int(s[:-1]) * KIB if s.endswith("K") else int(s, 0)


def regions(script):
    """the MEMORY block's regions: {name: (origin, length)}"""
    m = MEMORY.search(script)
    if not m:
        raise SystemExit("ramsplit: the linker script has no MEMORY block")
    return {r["name"]: (number(r["org"]), number(r["len"])) for r in REGION.finditer(m.group(1))}


def split(script, kb):
    """-> (the script, its regions) with RAMTEXT kb KiB (see the top); kb None or the script's own: the script as it is"""
    have = regions(script)
    for name in ("RAMTEXT", "RAM", "POOL"):
        if name not in have:
            raise SystemExit(f"ramsplit: the linker script has no {name} region")
    (rt_org, rt_len), (ram_org, ram_len), (pool_org, pool_len) = have["RAMTEXT"], have["RAM"], have["POOL"]
    if not (rt_org + rt_len <= ram_org and ram_org + ram_len <= pool_org):
        raise SystemExit("ramsplit: RAMTEXT, RAM and POOL are not in that order, apart")
    if kb is not None and (not isinstance(kb, int) or isinstance(kb, bool) or kb * KIB < rt_len):
        raise SystemExit(f"fm1.toml ram_text_kb = {kb!r}: a whole number of KiB, at least {rt_len // KIB}")
    if kb is None or kb * KIB == rt_len:
        return script, have
    move = max(0, rt_org + kb * KIB - ram_org)
    if move >= pool_len:
        raise SystemExit(f"fm1.toml ram_text_kb = {kb}: RAM would move {move} B, the whole pool ({pool_len} B)")
    new = {**have, "RAMTEXT": (rt_org, kb * KIB), "RAM": (ram_org + move, ram_len),
           "POOL": (pool_org + move, pool_len - move)}
    block = MEMORY.search(script)

    def region(r):
        org, length = new[r["name"]]
        if (org, length) == have[r["name"]]:
            return r.group(0)
        return f"{r['lead']}{r['name']}{r['attr']}ORIGIN = 0x{org:08X}, LENGTH = 0x{length:X}{r['tail']}"
    body = REGION.sub(region, block.group(1))
    note = (f"/* tools/ramsplit.py, fm1.toml ram_text_kb = {kb}: RAMTEXT {kb} KiB, RAM and the pool's start {move} B "
            f"up, the pool {move} B smaller (the comments below are the script's own layout) */\n")
    return note + script[:block.start(1)] + body + script[block.end(1):], new
