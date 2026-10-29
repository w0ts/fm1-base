/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's lines in fm1-base's development readout (plat.h APP_LOG: F0 7D 4C 47 F7, "LG"; tools/fm1_remote.py log),
 * after fm1-base's own (its "cache ram ..." lines: the ways' test, plat_cache_ram). Where the memory went:
 *   arena BYTES at ADDR used BYTES      the arena (core/arena.c: the free SRAM at boot, ARN_FREE), its blocks in use
 *   undo keep BYTES cache BYTES         the undo history's least in the SRAM (engines.c ARN_UNDO_KEEP: 16 KB less the
 *                                       cache RAM's) and the cache RAM it has (0: none: CACHE_RAM off or its test failed)
 *   undo ring BYTES + BYTES at ADDR     its ring now: the SRAM part (after the arena's highest block), the cache RAM
 *                                       part and where (seq/undo.c undo_h; "undo ring 0": not set up yet, no edit)
 * Hexadecimal, 8 digits. Main loop: reads only. First, on the probe branch, the GPIO probe's lines (gpio_probe.h gp_dump) */
static char *opt_lg_put(char *q, const char *t, uint32_t v)
{
    uint32_t i;
    while (*t)
        *q++ = *t++;
    for (i = 0; i < 8u; i++)
        *q++ = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    *q = 0;
    return q;
}
void app_log(void (*line)(const char *t))
{
    char b[44], *q;
#if FELUCCA_CACHE_RAM
    uint32_t cr = fm1_cache_ram_free();
#else
    uint32_t cr = 0;
#endif
    gp_dump(line, fm1_ms);                       /* the GPIO probe first (a probe branch: SYSTEM > GPIO PROBE, gpio_probe.h) */
#if PART_DROPS
    q = opt_lg_put(b, "arena ", ARN_BYTES);
    q = opt_lg_put(q, " at ", (uint32_t)(uintptr_t)arn_mem);
    opt_lg_put(q, " used ", ARN_BYTES - arn_room());
    line(b);
    q = opt_lg_put(b, "undo keep ", ARN_UNDO_KEEP);
#else
    q = opt_lg_put(b, "undo keep ", 0u);
#endif
    opt_lg_put(q, " cache ", cr);
    line(b);
#if FELUCCA_UNDO_HISTORY
    q = opt_lg_put(b, "undo ring ", undo_h.ready ? undo_h.len[0] : 0u);
    q = opt_lg_put(q, " + ", undo_h.ready ? undo_h.len[1] : 0u);
    opt_lg_put(q, " at ", undo_h.ready && undo_h.len[1] ? (uint32_t)(uintptr_t)undo_h.seg[1] : 0u);
    line(b);
#endif
}
