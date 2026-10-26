/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's HAL calls (firmware/hal/fm1_*.h) onto fm1-base's plat.h, so Optimist's own files compile unchanged inside
 * an fm1-base app (the trial: optimist/core/opt_core.c, its UI in optimist/ui/opt_ui.c). Optimist's code,
 * GPL-3.0-only: never offered upstream as MPL. Only what the audio core and the UI call is here; each row of
 * docs/FM1-BASE-PORT-MAP.md (Optimist's tree) that the trial reaches gets its line. */
#pragma once
#include <stdint.h>
/* plat.h's names that Optimist's own files also define, under other names here: clamp (dsp/dsp_common.h), the panel's
 * enums (ui/panel.c: the same labels in the same order, but B_SCL for plat.h's B_SEL). The app's unit (app.c) sees
 * plat.h as it is; this unit uses PLAT_B_* / PLAT_EN_* (ui/opt_in.h) */
#define clamp plat_clamp
#define B_FX PLAT_B_FX
#define B_SEL PLAT_B_SEL
#define B_ENV PLAT_B_ENV
#define B_LFO PLAT_B_LFO
#define B_EDIT PLAT_B_EDIT
#define B_GLO PLAT_B_GLO
#define B_HOME PLAT_B_HOME
#define B_SAVE PLAT_B_SAVE
#define B_ARP PLAT_B_ARP
#define B_SEQ PLAT_B_SEQ
#define B_PLAY PLAT_B_PLAY
#define B_REC PLAT_B_REC
#define B_OCTDN PLAT_B_OCTDN
#define B_OCTUP PLAT_B_OCTUP
#define NB PLAT_NB
#define EN_SELECT PLAT_EN_SELECT
#define EN_ALGO PLAT_EN_ALGO
#define EN_PRESET PLAT_EN_PRESET
#define EN_K1 PLAT_EN_K1
#define EN_K2 PLAT_EN_K2
#define EN_K3 PLAT_EN_K3
#define EN_K4 PLAT_EN_K4
#define NE PLAT_NE
#include "plat.h"
#undef clamp
#undef B_FX
#undef B_SEL
#undef B_ENV
#undef B_LFO
#undef B_EDIT
#undef B_GLO
#undef B_HOME
#undef B_SAVE
#undef B_ARP
#undef B_SEQ
#undef B_PLAY
#undef B_REC
#undef B_OCTDN
#undef B_OCTUP
#undef NB
#undef EN_SELECT
#undef EN_ALGO
#undef EN_PRESET
#undef EN_K1
#undef EN_K2
#undef EN_K3
#undef EN_K4
#undef NE

/* ---- interrupts: plat_lock / plat_unlock (the lock nests: unlock at depth 0 is the sti) */
static inline void fm1_irq_off(void) { (void)plat_lock(); }
static inline void fm1_irq_on(void) { plat_unlock(0); }

/* ---- time: TIMER4's 24 MHz count is plat_ticks24 on both. The host simulator's plat_ticks24 is its machine's clock,
 * while its plat_ms is the simulated one (a script's "wait 80" is 80 ms of plat_ms and some microseconds of the
 * clock): Optimist times its knobs' acceleration and its buttons' holds in ticks, so on the host the ticks follow
 * plat_ms (a gap in fm1-base: docs/FM1-BASE-PORT-MAP.md) */
#define FM1_TICKS_PER_US PLAT_TICKS_PER_US
#ifdef PLAT_HOST
static inline uint32_t fm1_ticks(void) { return plat_ms() * (1000u * PLAT_TICKS_PER_US); }
#else
static inline uint32_t fm1_ticks(void) { return plat_ticks24(); }
#endif
static inline void fm1_delay_ms(uint32_t ms)       /* (a wait on plat_ms: the main loop only, never the render; the
                                                    * host's plat_ms stands still inside the app: no wait there) */
{
    uint32_t t0 = plat_ms();
#ifdef PLAT_HOST
    (void)t0, (void)ms;
    return;
#endif
    while (plat_ms() - t0 < ms)
        plat_yield();
}

/* ---- the panel as Optimist's sequencer and UI read it (hal/fm1_input.h fm1_in): the held keys, the held buttons.
 * Keys: the same numbering on both (bit k = key k, 0 = F3). Buttons: Optimist's bits are its matrix ids (ui/panel.c
 * panel.btn), fm1-base's plat.h B_*: ui/opt_in.h translates them each millisecond */
static struct {
    volatile uint32_t notes, buttons;
} fm1_in;

/* ---- the user sample slots (USR1..3, flash 0xA0000, eng_sample.c): read in place from fm1-base's data window */
#define SMP_USER_XIP(k) plat_data(SMP_USER_BASE + (k) * SMP_USER_SIZE)
