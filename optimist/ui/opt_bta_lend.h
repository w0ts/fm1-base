/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Bluetooth audio's buffers lent by Optimist's arena (fm1.toml bt_audio_mem = "app": plat.h plat_bta_mem; Optimist's
 * core/arena_bufs.c ARB_BTA, core/feat_mem.c): held while an AUDIO mode is on (OPT_BTA_ON, the includer's: opt_bt.h's
 * bta.mode), and after it while fm1-base still reads them (its run being let go: plat_bta_mem(0) refuses), then given
 * back. The radio starts only once the block is lent (opt_bta_pend: opt_bt.h bta_start, opt_bt_poll). Included by
 * ui/opt_bt.h and by Optimist's tests/feat_mem_test.c (on a stand-in plat_bta_mem) */
#pragma once
static uint8_t opt_bta_lent, opt_bta_pend;
static uint32_t bta_lend_bytes(void) { return plat_bta_mem_bytes(); }
static int bta_lend_want(void)                  /* (core/arena_bufs.c arb_want: the main loop) */
{
    if (OPT_BTA_ON())
        return 1;
    if (opt_bta_lent && plat_bta_mem(0) != 0)
        return 1;                               /* (the run still being let go: the radio reads it) */
    opt_bta_lent = 0;
    return 0;
}
static void bta_lend(void *p)                   /* (core/arena_bufs.c arb_set, the interrupts off) */
{
    if (p)
        opt_bta_lent = (uint8_t)(plat_bta_mem(p) == 0);
    else if (plat_bta_mem(0) == 0)
        opt_bta_lent = 0;
}
