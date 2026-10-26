/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's HAL calls (firmware/hal/fm1_*.h) onto fm1-base's plat.h, so Optimist's own files compile unchanged inside
 * an fm1-base app (the trial: optimist/core/opt_core.c). Optimist's code, GPL-3.0-only: never offered upstream as MPL.
 * Only what the audio core (engines, voices, drums, effects, sequencer) calls is here; each row of
 * docs/FM1-BASE-PORT-MAP.md (Optimist's tree) that the trial reaches gets its line. */
#pragma once
#include <stdint.h>
#define clamp plat_clamp                   /* (plat.h's clamp beside Optimist's own, dsp/dsp_common.h) */
#include "plat.h"
#undef clamp

/* ---- interrupts: plat_lock / plat_unlock (the lock nests: unlock at depth 0 is the sti) */
static inline void fm1_irq_off(void) { (void)plat_lock(); }
static inline void fm1_irq_on(void) { plat_unlock(0); }

/* ---- time: TIMER4's 24 MHz count is plat_ticks24 on both */
#define FM1_TICKS_PER_US PLAT_TICKS_PER_US
static inline uint32_t fm1_ticks(void) { return plat_ticks24(); }
static inline void fm1_delay_ms(uint32_t ms)       /* (a wait on plat_ms: the main loop only, never the render) */
{
    uint32_t t0 = plat_ms();
    while (plat_ms() - t0 < ms)
        plat_yield();
}

/* ---- the panel as Optimist's sequencer reads it (hal/fm1_input.h fm1_in): the held keys, the held buttons.
 * Keys: the same numbering on both (bit k = key k, 0 = F3). Buttons: Optimist's bits are its matrix ids, fm1-base's
 * plat.h B_*; the trial leaves them at 0 (no UI yet: the layer buttons, FILL ... are the UI's) */
static struct {
    volatile uint32_t notes, buttons;
} fm1_in;

/* ---- the user sample slots (USR1..3, flash 0xA0000, eng_sample.c): read in place from fm1-base's data window */
#define SMP_USER_XIP(k) plat_data(SMP_USER_BASE + (k) * SMP_USER_SIZE)
