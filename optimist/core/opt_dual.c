/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's second core in the trial (FELUCCA_DUAL 2: the profile's DUAL; docs/DUAL-CORE.md of Optimist's tree):
 * system/dual.c's render split (mix_block_dual, dual_split, dual_run, dual_job, dual_lfo_draw) on fm1-base's second
 * core (plat.h plat_cpu2_*, fm1.toml cpu2: felucca-bsp's chip/fm1_cpu1.h) in place of Optimist's own (hal/
 * fm1_dual.h: its mailbox, its start, its RAM loop). Part of core/opt_core.c, after fx.c (fx.c leaves mix_block to
 * this file with DUAL_RENDER, as it leaves it to dual.c in Optimist's own firmware).
 *
 * The same split as Optimist's: per 32-frame mix block, the jobs (the three synth parts, the drum track, the reverb's
 * kept block, REV_LATE) go each block to the core with less measured load, longest first (dual_split); CPU1 renders
 * its parts into its own accumulators (dual_acc[1]) and the drums onto the buses CPU0 leaves alone; CPU0 renders the
 * rest, waits, adds CPU1's sums (integer: the samples of one core), runs the buses and the master. What runs before the
 * fork on CPU0 alone, as there: the events, the macros, the duck, the busy voices, the LFOs' random draws, the old
 * sounds' fade (pold_in).
 *
 * fm1-base's part, used as it is (the comparison: Optimist's tree, docs/FM1-BASE-PORT-MAP.md): the start (in app_init,
 * before the audio and the timers, the PC limits open around it), the mailbox (one job at a time, a check word over the
 * function, the argument and the job's number, csync around each word, CPU1's loop in RAM: .c1_text), the misread and
 * fault handling, the bar after a failure, the hold at a reset, the supplies (main_fm1.c, every boot: as Optimist's
 * STOCK_SUPPLY). With fm1.toml cpu2_fallback (Optimist's rule: never a restart for the second core) a failure retires
 * CPU1 and CPU0 renders everything from then on (one click), the reason here (dual.why, SYSTEM > INFO's 2ND CORE line).
 * Optimist's own, kept here: the boot canary (32 words read 64 times by CPU1 before its first job, hal/
 * fm1_dual_guard.h), the join bound of two mix blocks (app.h FM1_CPU1_TIMEOUT_US), the main loop's starvation
 * (OVERLOAD: CPU1 taken down when the main loop had no pass for DUAL_STARVE_MS), CPU1's load (dual_load.h).
 *
 * The host: fm1-base's simulated second core runs a job at once, in order (plat_host.c); its ticks follow plat_ms, so
 * every job measures 0 and the split keeps them all on CPU0: OPT_DUAL_PARTS (app.c, the host's environment) gives a
 * fixed set of jobs to CPU1 instead (bit p: job p; Optimist's DUAL_PARTS). */
#include "system/dual_mode.h"
#include "../hal/fm1_dual_guard.h"                 /* (the boot canary's table and scan: pure, Optimist's) */
#include "../app.h"                                /* (FM1_CPU1_TIMEOUT_US: the join bound, as Optimist's DUAL_JOIN_US) */

#define DUAL_JOIN_US ((2u * CTL * 1000000u) / FS)  /* (dual.c: two mix blocks, 1.45 ms) */
_Static_assert(FM1_CPU1_TIMEOUT_US == DUAL_JOIN_US, "app.h FM1_CPU1_TIMEOUT_US: two mix blocks, as dual.c's DUAL_JOIN_US");
#define DUAL_STARVE_MS 1500u                       /* (dual.c: the main loop this long without a pass: CPU1 down) */
#define DUAL_SLEEPS_NOW 0                          /* (CPU1 spins: fm1-base's loop; op_project.c's INTERNALS) */
#define DUAL_JOB_DRUMS (1u << NPART)               /* a job's bit: the drum track (slicer_drums, dbus_run) */
#if REV_LATE
#define DUAL_JOB_REV (1u << (NPART + 1u))          /* the reverb's kept block (fx.c rev_job) */
#define DUAL_JOBS (NPART + 2u)
#else
#define DUAL_JOB_REV 0u
#define DUAL_JOBS (NPART + 1u)
#endif
#define DUAL_ALLOWED ((1u << DUAL_JOBS) - 1u)

/* the ticks (plat_ticks24) from the render's RAM code on either core: through a pointer (fm1.toml ram_code_allow:
 * dual_run, dual_job). The host: fm1_compat.h's fm1_ticks (on plat_ms) */
#ifdef PLAT_HOST
#define dual_ticks() fm1_ticks()
#else
#define dual_ticks() FAR(plat_ticks24)()
#endif

/* the state SYSTEM > INFO reads (op_project.c, as dual.c's): up, why it is down, the jobs, the stop record */
static struct {
    uint8_t up;                                    /* CPU1 takes jobs */
    uint8_t why;                                   /* DUAL_*: why it is down (dual_mode.h) */
    uint8_t down_pend;                             /* (dual.c: a wake pending; CPU1 spins here: 0) */
    uint32_t jobs, wait_max, rewakes, posts;       /* jobs joined; longest CPU0 wait (ticks); (no wakes: 0); posted */
    uint32_t t_start;                              /* ticks when CPU1 was started */
    dual_stop_t stop;
} dual;
static uint8_t dual_mode;                          /* the 2ND CORE setting (dual_mode.h; settings_word.c bits 27..28) */
/* INTERNALS' CPU1 lines (op_project.c sys_c1_cell) on what fm1-base tells: how far it got (C1S_*), its jobs; fm1-base
 * keeps its faults to itself (gave_up FAULT names one; no pc for this page): NONE */
static struct {
    uint32_t stage, count, wakes, icfg;
} fm1_dual_mb;
static struct {
    uint32_t count, dbg, emu, cpu, pc;
} fm1_dual_exc;

static mixacc_t dual_acc[2];                       /* [0] CPU0's parts, [1] CPU1's */
static volatile uint32_t dual_c1_ticks;            /* CPU1: ticks inside its jobs, a running total (dual_load.h) */
static uint32_t dual_cost[DUAL_JOBS];              /* each job's render time (ticks x 8, smoothed; dual.c) */
static uint32_t dual_fixed;                        /* the host's fixed set of jobs for CPU1 (OPT_DUAL_PARTS), 0: the split */
static volatile uint32_t dual_loop_ms;             /* plat_ms at the main loop's last pass (opt_dual_loop) */
static volatile uint32_t dual_canary_bad;          /* CPU1: the canary words it read wrong, + 1 (0: not run) */
static volatile uint32_t dual_canary_tab[FM1_DUAL_CANARY_N];

AINL void dual_clear(mixacc_t *A, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        A->send_c[i] = A->send_d[i] = A->send_r[i] = A->mix_l[i] = A->mix_r[i] = 0;
}

/* the jobs of mask, on the core that calls it, into A (the parts) and the buses (the drums); each job timed (dual.c) */
static HOT void dual_run(uint32_t mask, uint32_t n, mixacc_t *A)
{
    uint32_t p, t0, t1;
    for (p = 0; p < DUAL_JOBS; p++)
        if ((mask >> p) & 1u) {
            t0 = dual_ticks();
            if (p < NPART) {
                mix_part(&trk[p], n, A);
            } else if (p == NPART) {
#if FELUCCA_DRUMS
                SCOPE_DRUMS(scope_drums_mark);     /* (the SCOPE on the drums: their part of the mix, as mix_block does) */
                slicer_drums(mix_l, mix_r, send_r, n);
                dbus_run(n);                       /* the drum bus's inserts and sends (fx.c; the drums alone) */
                SCOPE_DRUMS(scope_drums_take);
#endif
            } else {
#if REV_LATE
                if (!rvj.done)                     /* (CPU1 retired after it ran it: not twice) */
                    rev_job();                     /* the reverb's block kept from the last block (fx.c) */
#endif
            }
            t1 = dual_ticks() - t0;
            dual_cost[p] += t1 - (dual_cost[p] >> 3);
        }
}

/* CPU1: its jobs of the block (RAM code: fm1-base's loop calls it from .c1_text, plat_cpu2_run) */
static HOT void dual_job(uint32_t mask)
{
    mixacc_t *A = &dual_acc[1];
    uint32_t t0 = dual_ticks();
    dual_clear(A, CTL);
    dual_run(mask & DUAL_ALLOWED, CTL, A);
    dual_c1_ticks += dual_ticks() - t0;            /* (before its complete: fm1-base's csync orders it) */
}

/* CPU0, before the fork: CPU1's jobs. The longest first, each onto the core with less so far (ties: CPU0); CPU1 starts
 * a little behind (its loop's look at the mailbox). Integer compares only (dual.c dual_split) */
#define DUAL_WAKE_COST 64u
static HOT uint32_t dual_split(uint32_t jobs)
{
    uint32_t ord[DUAL_JOBS], i, j, k, c0 = 0, c1 = DUAL_WAKE_COST, mask = 0;
    if (dual_fixed)
        return dual_fixed & jobs;
    for (i = 0; i < DUAL_JOBS; i++) {              /* (insertion sort, longest first: 4 or 5 jobs) */
        for (j = i; j && dual_cost[ord[j - 1u]] < dual_cost[i]; j--)
            ord[j] = ord[j - 1u];
        ord[j] = i;
    }
    for (i = 0; i < DUAL_JOBS; i++) {
        k = ord[i];
        if (!((jobs >> k) & 1u))
            continue;
        if (c1 < c0) {
            c1 += dual_cost[k];
            mask |= 1u << k;
        } else {
            c0 += dual_cost[k];
        }
    }
    return mask;
}

/* CPU0, before the fork: the rng() draws of the parts' LFOs that wrap in this block, in part order (dual.c) */
static HOT void dual_lfo_draw(void)
{
    uint32_t p;
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        if ((uint32_t)(t->lfo_ph + lfo_inc(t)) < t->lfo_ph)   /* (voice.c: the same step) */
            dual_lfo_rnd[p] = rng();
    }
}

/* fm1-base's reason (plat_cpu2_gave_up) as Optimist's (dual_mode.h DUAL_*) */
static uint32_t dual_why_of(int gave_up)
{
    switch (gave_up) {
    case 1:
        return DUAL_MISREAD;
    case 2:
        return DUAL_TIMEOUT;
    case 3:
        return DUAL_EARLY;                         /* (barred: it failed before, or CPU0 crashed beside it, this power-on) */
    case 4:
        return DUAL_FAULT;
    case 5:
        return DUAL_NO_HELLO;
    default:
        return DUAL_TIMEOUT;
    }
}

/* CPU1 down (a failure fm1-base retired it for, the canary, a starved main loop): the record SYSTEM > INFO shows */
static void dual_down(uint32_t why, uint32_t in_job, uint32_t wait_us)
{
    dual.stop.stopped = 1;
    dual.stop.in_job = (uint8_t)in_job;
    dual.stop.took = (uint8_t)in_job;
    dual.stop.at = 0;
    dual.stop.no_clock = (uint8_t)(fm1_ms == 0u);
    dual.stop.c1_ms = (dual_ticks() - dual.t_start) / (1000u * FM1_TICKS_PER_US);
    dual.stop.job = dual.jobs;
    dual.stop.wait_us = wait_us;
    dual.up = 0;
    dual.why = (uint8_t)why;
}

/* CPU1, its first job: the canary CPU0 wrote, read FM1_DUAL_CANARY_PASSES times (dual.c fm1_cpu1_hello) */
static void dual_canary_job(uint32_t arg)
{
    (void)arg;
    dual_canary_bad = 1u + fm1_dual_canary_scan(dual_canary_tab, FM1_DUAL_CANARY_PASSES);
}

/* power-on (opt_core_init, from app_init: the one time fm1-base starts it), after the settings: CPU1 started unless
 * 2ND CORE is OFF; fm1-base refuses after a failed boot (bootguard) or while barred. Then the canary */
static void opt_dual_start(void)
{
    int r;
    memset(&dual, 0, sizeof dual);
    memset(&fm1_dual_mb, 0, sizeof fm1_dual_mb);
    dual.why = (uint8_t)dual_boot_why(dual_mode, 0u);
    dual_loop_ms = plat_ms();
    if (dual.why != DUAL_UP)
        return;                                    /* (2ND CORE OFF: CPU1 stays held, as at power-on) */
    fm1_dual_canary_fill(dual_canary_tab);
    dual_canary_bad = 0;
    dual.t_start = dual_ticks();
    r = plat_cpu2_set(1);
    if (r) {                                       /* (one core: barred, a failed boot before, or no answer) */
        dual.why = (uint8_t)(plat_cpu2_gave_up() ? dual_why_of(plat_cpu2_gave_up()) : DUAL_EARLY);
        return;
    }
    fm1_dual_mb.stage = C1S_HELLO;
    if (!plat_cpu2_run(dual_canary_job, 0u) || plat_cpu2_wait() || (plat_cpu2_sync(), dual_canary_bad != 1u)) {
        if (plat_cpu2_on())
            plat_cpu2_set(0);                      /* (held: the main loop's, no job outstanding) */
        dual_down(plat_cpu2_gave_up() ? dual_why_of(plat_cpu2_gave_up()) : DUAL_CANARY, 0u, 0u);
        return;
    }
    fm1_dual_mb.stage = C1S_LOOP;
    dual.up = 1;
}

/* every main loop pass (app_loop, app_yield): its time, for the starvation check; a CPU1 put down by the render, held */
static void opt_dual_loop(void)
{
    dual_loop_ms = plat_ms();
    fm1_dual_mb.count = dual.jobs;
    if (!dual.up && plat_cpu2_on())
        plat_cpu2_set(0);                          /* (OVERLOAD, CANARY: no job outstanding from the main loop) */
}

/* CPU0, once an audio half: CPU1's load (dual_load.h), from the ticks of its jobs since the last half */
static void dual_load_half(void)
{
    static uint32_t last;
    uint32_t b = dual_c1_ticks;
    dual_load_take((b - last) / FM1_TICKS_PER_US, dual.up);
    last = b;
}

/* mix_block (fx.c) with the jobs dual_split gives CPU1 (dual.c mix_block_dual) */
static HOT void mix_block(int32_t *out, uint32_t n)
{
    mixacc_t *A = &dual_acc[0], *B = &dual_acc[1];
    uint32_t i, mask = 0, got = 0, post = 0, jobs, sw, t0;
#if FELUCCA_USB_AUDIO
    capture_clear(n);                              /* the stems (each part writes its own; fx.c) */
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    dual_clear(A, n);
    FAR(events_block)(n);                          /* (the sequencer stays in XIP) */
    BENCH_BLOCK();
#if FELUCCA_MACROS
    FAR(mac_pre)();                                /* the macros' values in (macro.c), for both cores */
#endif
    if (FELUCCA_FX_DUCK)
        duck_block(n * (uint32_t)song.g[G_BPM]);
    dual_vbusy = voices_busy();                    /* the swarm's copies: the same on both cores */
    dual_lfo_draw();                               /* the LFOs' random values (the render draws none) */
    sw = pold_any() ? FAR(pold_in)() : 0u;         /* the parts fading after a sound load: their old sound, both cores */
#if FELUCCA_DRUMS
    drums.a0 = TDRUM->att;                         /* the drum track's mute / solo fade (before the fork: either */
    drums.a1 = 32767 - gain_next(TDRUM);           /* core may render the drums) */
    jobs = (1u << (NPART + 1u)) - 1u;              /* the parts and the drums */
#else
    jobs = (1u << NPART) - 1u;
#endif
    if (dual.up && fm1_ms - dual_loop_ms > DUAL_STARVE_MS)
        FAR(dual_down)(DUAL_OVERLOAD, 0u, 0u);        /* (the main loop starved: one core; opt_dual_loop holds it) */
#if REV_LATE
    rev_late = (uint8_t)(dual.up && n == CTL);     /* CPU1 up: the reverb one block late (fx.c fx_buses) */
    jobs |= rvj.pend ? DUAL_JOB_REV : 0u;          /* (its kept block, on either core) */
#endif
    if (dual.up && n == CTL) {
        mask = dual_split(jobs);
        if (mask) {
            post = (uint32_t)FAR(plat_cpu2_run)(dual_job, mask);
            if (!post) {                           /* (retired as it looked: a misread or a fault while idle) */
                FAR(dual_down)(FAR(dual_why_of)(FAR(plat_cpu2_gave_up)()), 0u, 0u);
                mask = 0;
            } else {
                dual.posts++;
            }
        }
    }
    dual_run(jobs & ~mask, n, A);                  /* CPU0's jobs: those CPU1 was not given */
    if (post) {
        t0 = dual_ticks();
        got = FAR(plat_cpu2_wait)() == 0;
        t0 = dual_ticks() - t0;
        if (got) {
            FAR(plat_cpu2_sync)();                 /* CPU1's sums after its complete */
            if (t0 > dual.wait_max)
                dual.wait_max = t0;
            dual.jobs++;
            fm1_dual_mb.stage = C1S_DONE;
        } else {                                   /* retired: its jobs here (a click, once) */
            FAR(dual_down)(FAR(dual_why_of)(FAR(plat_cpu2_gave_up)()), 1u, t0 / FM1_TICKS_PER_US);
            if (mask & DUAL_JOB_DRUMS)             /* (whatever CPU1 left on the buses: the drums again) */
                for (i = 0; i < n; i++)
                    send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
            dual_run(mask, n, A);
        }
    }
    for (i = 0; i < n; i++) {
        send_c[i] += A->send_c[i];
        send_d[i] += A->send_d[i];
        send_r[i] += A->send_r[i];
        mix_l[i] += A->mix_l[i];
        mix_r[i] += A->mix_r[i];
    }
    if (got)
        for (i = 0; i < n; i++) {
            send_c[i] += B->send_c[i];
            send_d[i] += B->send_d[i];
            send_r[i] += B->send_r[i];
            mix_l[i] += B->mix_l[i];
            mix_r[i] += B->mix_r[i];
        }
    if (sw)
        FAR(pold_out)(sw);                         /* (and the new one back) */
#if FELUCCA_USB_AUDIO
    ut_mix(n);                                     /* the USB input, a track (fx.c) */
#endif
    mix_finish(out, n);
#if FELUCCA_MACROS
    FAR(mac_post)();
#endif
}
