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
/* CPU TIMES' SBC row (Optimist's system/dual_load.h, op_project.c): the frames fm1-base's feed encoded itself, not as jobs */
#define DUAL_TM_SBC_FEED() ((uint32_t)plat_bta_job_inline())
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

/* ---- interrupts: Optimist's fm1_irq_off / fm1_irq_on are a bare cli / sti (hal/fm1_irq.h), and its IRQ_SAVE /
 * IRQ_RESTORE (core/core.h) read the global enable first (fm1_icfg() bit 9) and turn them back on only if it was on.
 * Here on plat_lock / plat_unlock, which nest (a depth: the sti when it falls to 0), with the same effect: the unit
 * takes the lock once, only while the interrupts are on (so never inside an interrupt that runs with them off, nor
 * inside the platform's own lock), and gives it back at the next fm1_irq_on. A lock per off would leave the depth
 * up after an IRQ_SAVE that found them off (its IRQ_RESTORE does nothing): the main loop's interrupts off for good.
 * The global enable on the FM-1: app.c opt_irq_enabled (felucca-bsp's fm1_icfg, in the app's unit); the host: on */
#ifdef PLAT_HOST
static inline uint32_t opt_irq_enabled(void) { return 1u; }
#else
uint32_t opt_irq_enabled(void);
static inline uint32_t fm1_icfg(void) { return opt_irq_enabled() ? 0x200u : 0u; }   /* (core.h IRQ_IE_NOW: bit 9) */
#endif
static uint8_t opt_irq_held;                       /* this unit holds plat_lock (0 / 1) */
static uint32_t opt_irq_was;                       /* what that plat_lock returned (1: they were on), for its unlock */
static inline void fm1_irq_off(void)
{
    if (!opt_irq_held && opt_irq_enabled()) {
        opt_irq_was = plat_lock();
        opt_irq_held = 1;
    }
}
static inline void fm1_irq_on(void)
{
    if (opt_irq_held) {
        opt_irq_held = 0;
        plat_unlock(opt_irq_was);                  /* (plat_unlock(0) left them off: the main loop then ran whole frames,
                                                    * the LCD writes in them, with the audio, TIMER5 and USB held off) */
    }
}

/* ---- the core that runs this (hal/fm1_dual.h fm1_cnum: 0 or 1; FM6's scratch per core, eng_fm6.c): fm1-base's
 * plat_core (felucca-bsp's fm1_cnum on the FM-1; the host: 1 inside its simulated second core's job), through a pointer:
 * the render's RAM code calls it too (fm1.toml ram_code_allow) */
static inline uint32_t fm1_cnum(void)
{
    uint32_t (*volatile f)(void) = plat_core;
    return f();
}
/* the same, always inlined: for RAM code whose caller has no other copy (an engine's "code in RAM", eng_fm6.c fm6_bus;
 * fm1_cnum's own out-of-line copy is XIP code) */
#define FM1_CNUM_RAM 1
static inline __attribute__((always_inline)) uint32_t fm1_cnum_ram(void)
{
    uint32_t (*volatile f)(void) = plat_core;
    return f();
}
/* ---- the second core's jobs' stack (system/stackmark.h STK_C1_*: SYSTEM > INFO's RAM line): felucca-bsp's fm1_cpu1_ustack,
 * fm1.toml cpu2_stack bytes (FM1_CPU1_USTACK_BYTES, the build's flag; felucca-bsp's 2816 without), its lowest 256 B the guard;
 * plat_cpu2_stack how deep it went (0: CPU1 never started; the host: 0) */
#if FELUCCA_DUAL >= 2
#ifndef FM1_CPU1_USTACK_BYTES
#define FM1_CPU1_USTACK_BYTES 2816u
#endif
#define STK_C1_USED() plat_cpu2_stack()
#define STK_C1_BYTES ((uint32_t)FM1_CPU1_USTACK_BYTES - 256u)
#else
#define STK_C1_USED() 0u
#define STK_C1_BYTES 0u
#endif

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
/* the mirror's client's keys (hal/fm1_input.h fm1_remote_key: editor cmd 108 PANEL, ED_MIRROR), held as a finger
 * holds them: its buttons by matrix id (bit n: button n 0..13), its note keys (bit n: key n); ORed into the panel as
 * the UI reads it (ui/opt_panel.h) and into the keys the render plays (core/opt_core.c) */
static volatile uint32_t fm1_rem_btn, fm1_rem_note;

/* ---- the data cache's ways as RAM (hal/fm1_cache.h, CACHE_RAM): fm1-base's (plat_cache_ram: felucca-bsp's
 * fm1_cache_ram.h, set and tested at the start, fm1.toml cache_ram_ways from the configuration's CACHE_RAM:
 * gen_optimist.py --conf). Its part no .cache_ram object takes, 0 without its test passed: the undo history's second
 * part (seq/undo.c), the arena that much more of the SRAM (engines.c ARN_UNDO_KEEP) */
#if FELUCCA_CACHE_RAM
static inline int fm1_cache_ram_ok(void) { return plat_cache_ram()->status == PLAT_CR_OK; }
static inline uint8_t *fm1_cache_ram_lo(void) { return plat_cache_ram()->base; }
static inline uint32_t fm1_cache_ram_free(void) { return plat_cache_ram()->bytes; }
#endif

/* ---- the user sample slots (USR1..3, flash 0xA0000, eng_sample.c): read in place from fm1-base's data window */
#define SMP_USER_XIP(k) plat_data(SMP_USER_BASE + (k) * SMP_USER_SIZE)
