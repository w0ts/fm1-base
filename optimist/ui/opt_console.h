/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's USB serial console in the trial (USB_CONSOLE: FELUCCA_CDC; SYSTEM > USB SERIAL, op_project.c) on fm1-base's
 * (fm1.toml usb_console, from the configuration: gen_optimist.py --conf; plat.h plat_console_*). The setting as
 * presented at every start (usb.c's usb_cdc_on: opt_con_boot, before fm1-base starts USB; no device switch since 2026-11-05). The command line is Optimist's (io/console.c cdc_task:
 * the prompt, the line editing, "help", "? (help)"), with the commands that mean something on fm1-base's platform:
 *   play | pause | stop | rec on|off | proj load N | heavy [N]   Optimist's run control (io/console_run.c, as it is)
 *   status       the version, the time, the load, the guard, the late halves, the voices, the second core
 *   params       the selected track's parameters (console.c con_params)
 *   btlog [N]    fm1-base's Bluetooth log, the newest N lines (plat_ble_log_line: its stack's own lines, what
 *                Optimist's blell / btinq / bttrace print from its own stack on its console; BLE or BT_AUDIO builds)
 *   reboot       a restart (plat_restart)
 * and Optimist's console commands that read its HAL, on what fm1-base's platform gives (plat.h):
 *   dbg          the platform's counters: late renders (plat_xruns), erases that held the sound (plat_flash_held), the
 *                messages and their age on each MIDI port (plat_midi_count, plat_midi_ago), the USB session, the battery's
 *                reading, the radios' states
 *   cpu1         the second core: it takes jobs, why not, the jobs it misread, its stack's depth, which core this is
 *                (plat_cpu2_*; Optimist's dualcfg and cpu1park wrote its set-up and parked it, fm1-base does that)
 *   keys         the buttons and keys held (plat_buttons, plat_keys), MASTER, the battery's reading, and the matrix Optimist's
 *                UI sees
 *   keylog       the last 32 key and button edges the UI's scan saw, oldest first: id (0..13 buttons, 14..40 keys), P down /
 *                R up, the time since the one before in microseconds (the main loop's scan, not Optimist's 10 kHz one)
 *   flr OFFSET [LEN<=256]  the flash as plat_data reads it (the data windows; the app's own code is not there)
 *   tabbench     the tables' read speed: SINE, main RAM, the pool (TAB_IN_POOL)
 *   sd [ls [DIR] | cat FILE | raw SECTOR]   the card: its state, a folder, a file's first bytes, a sector (SDCARD builds)
 *   sd diag      the card's clock: the one kept, what the mount's detection saw at each step (fm1-base's plat_sd_clock:
 *                Optimist's sd diag's clock lines), the CRC failures and the run-time steps down since the power-on
 *   sd clock MHZ|auto   the clock: the fastest step at or under MHZ, unchecked (plat_sd_clock_set); auto: detected;
 *                then the mount again (Optimist's sd clock)
 *   sd speed     each step tried now (plat_sd_clock_try: the CSD, the CID and blocks, CRC-checked against the floor's;
 *                Optimist's sd speed reads the CSD alone at each clock), the clock kept as it was
 *   sd irq [N]   the read run's blocks in SPI1's interrupt (fm1-base's plat_sd_irq: 1, the default) or by the main loop's polls
 *                (0, a comparison); and its numbers. RAM only
 *   sd stats [reset]   the streams' counters since the last reset: KB/s, runs, blocks, stalls, dry voices (sd_stream.c sst_st)
 * What stays with fm1-base, which owns that hardware (its own tools read it: tools/fm1_debug.py, fm1_remote.py): memory reads (memr: raw addresses, the platform's alone to read), the crash
 * and boot records (crash, boot), the second core's set-up and rails (dualcfg), the radio's calibration, link layer and
 * Bluetooth Classic diagnostics (blevm, bletrim, blell, btclk, btinq, btscan, btrx, btpage, bttrace), the SD bus modes
 * and benchmarks, the far probe, membench and the update loader's entry (uboot). Main loop only (ui/opt_ui.c
 * opt_ui_frame). */
#pragma once

/* Over USB MIDI too (FELUCCA_CON_MIDI, the builder's USB_CONSOLE; with USB audio as well, the user 2026-11-08: "no need
 * to choose between serial and usb audio"): Optimist's console.c protocol. A line in: F0 7D 46 43 'l' <ASCII> F7 (app.c
 * app_sysex -> opt_con_frame); the reply out as F0 7D 46 43 'o' <ASCII> F7 frames (plat_sysex_send), then
 * F0 7D 46 43 'e' F7 when the command is done (tools/fm1_console.py --midi). The reply goes back the way its line came */
#ifndef FELUCCA_CON_MIDI
#define FELUCCA_CON_MIDI 0
#endif
#if FELUCCA_CDC || FELUCCA_CON_MIDI
#ifndef OPT_CON_EXTRA
#define OPT_CON_EXTRA FELUCCA_CDC               /* cpu1, keylog, flr, tabbench, sd raw (below): with the serial port, as before;
                                                 * off when the console is over MIDI alone (USB audio: room is tight) */
#endif
#define CON_LINE 64u
#include "io/console_parse.h"                   /* con_word, con_num, con_run_parse (Optimist's, pure) */
#if FELUCCA_CON_MIDI
#define CON_SX_N 120u                           /* text bytes in a reply frame */
#endif
static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;                  /* stalled: the computer stopped reading, the rest of the reply dropped */
#if FELUCCA_CON_MIDI
    uint8_t via_sx;                             /* this command came as SysEx: its reply goes there */
    uint8_t sx_n;                               /* text bytes waiting in sx[] */
    uint8_t sx[5u + CON_SX_N + 1u];             /* F0 7D 46 43 'o' <text> F7 */
#endif
} con;
static uint32_t con_reboot_ms;                  /* `reboot`: when it was asked (0: not); opt_con_task restarts */

#if FELUCCA_CON_MIDI
static void con_sx_flush(void)                  /* the waiting text as one 'o' frame */
{
    if (con.sx_n && !con.stalled) {
        con.sx[0] = 0xF0, con.sx[1] = 0x7D, con.sx[2] = 0x46, con.sx[3] = 0x43, con.sx[4] = 'o';
        con.sx[5u + con.sx_n] = 0xF7;
        if (plat_sysex_send(con.sx, 6u + con.sx_n))
            con.stalled = 1;                    /* no computer, or it stopped reading: the rest of the reply dropped */
    }
    con.sx_n = 0;
}
#endif

static void con_putc(char c)                    /* (console.c's: a full ring waits for the computer, briefly) */
{
#if FELUCCA_CON_MIDI
    if (con.via_sx) {
        if (con.stalled)
            return;
        con.sx[5u + con.sx_n++] = (uint8_t)(c & 0x7F);
        if (con.sx_n >= CON_SX_N)
            con_sx_flush();
        return;
    }
#endif
#if FELUCCA_CDC
    uint32_t t0 = plat_ms();
    while (!con.stalled && !plat_console_write(&c, 1)) {
        if (!plat_console_open() || plat_ms() - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        plat_yield();
    }
#else
    (void)c;
#endif
}
static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}
static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}
static void con_kv(const char *k, int32_t v)    /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}
static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (4u * digits)) & 15u]);
}
static void con_kx(const char *k, uint32_t v)   /* "key 0xXXXXXXXX\r\n" */
{
    con_puts(k);
    con_puts(" 0x");
    con_hex(v, 8);
    con_puts("\r\n");
}
static void con_bytes(const char *k, const uint8_t *d, uint32_t n)   /* "key: XX XX ..", 16 a line */
{
    uint32_t i;
    con_puts(k);
    con_putc(':');
    for (i = 0; i < n; i++) {
        if (i % 16u == 0 && i)
            con_puts("\r\n ");
        else
            con_putc(' ');
        con_hex(d[i], 2);
    }
    con_puts("\r\n");
}
#include "io/console_run.c"                     /* play, pause, stop, rec, proj load, heavy (Optimist's run control) */

static void con_status(void)
{
    con_puts("optimist ");
    con_puts(FELUCCA_VERSION);
    con_puts(" on fm1-base\r\n");
    con_kv("uptime_ms", (int32_t)plat_ms());
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("cpu_khz", (int32_t)cpu_khz);
#if FELUCCA_CPU_GUARD
    con_kv("cpu_guard", (int32_t)cg.level);
    con_kv("cpu_guard_steps", (int32_t)cg.steps);
    con_kv("cpu_guard_shed", (int32_t)cg.sheds);
#endif
    con_kv("audio_late", (int32_t)ovl.drops);
    con_kv("voices", (int32_t)opt_core_voices());
    con_kv("playing", song.playing);
#if FELUCCA_DUAL >= 2
    con_kv("cpu1_up", opt_core_dual_stat(0));
    con_kv("cpu1_why", opt_core_dual_stat(1));
    con_kv("cpu1_jobs", opt_core_dual_stat(2));
    con_kv("cpu1_posts", opt_core_dual_stat(3));
#endif
}
static void con_params(void)                    /* (console.c's) */
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(TSEL->p[i]);
        con_puts(i % 8u == 7u || i + 1u == P_COUNT ? "\r\n" : " ");
    }
}
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
static void con_btlog(const char *p)            /* the newest N lines (default 16), the oldest first */
{
    int ok, back;
    uint32_t n = con_num(&p, &ok), total = plat_ble_log_total();
    if (!ok || !n)
        n = 16u;
    con_kv("lines", (int32_t)total);
    for (back = (int)(n < total ? n : total) - 1; back >= 0; back--) {
        const char *l = plat_ble_log_line(back);
        if (l) {
            con_puts(l);
            con_puts("\r\n");
        }
    }
}
#endif

/* ---- Optimist's HAL commands, on fm1-base's platform */
static void con_dbg(void)
{
    static const char *const PORT[3] = {"usb", "trs", "ble"};
    static const uint8_t ID[3] = {PORT_USB, PORT_TRS, PORT_BLE};
    uint32_t k, n;
    con_kv("late_renders", (int32_t)plat_xruns());
    con_kv("flash_erases_held", (int32_t)plat_flash_held());
    for (k = 0; k < 3u; k++) {
        con_puts("midi_");
        con_puts(PORT[k]);
        con_puts(" in ");
        con_dec((int32_t)plat_midi_count(ID[k], 0));
        con_puts(" out ");
        con_dec((int32_t)plat_midi_count(ID[k], 1));
        con_puts(" ago_ms ");
        n = plat_midi_ago(ID[k], 0);
        con_dec(n == 0xFFFFFFFFu ? -1 : (int32_t)n);
        con_puts("\r\n");
    }
    con_kv("usb_session", (int32_t)plat_usb_session());
    con_kv("battery_raw", plat_battery_raw());
    con_kv("battery_charging", plat_battery_charging());   /* -1 not read, 0 on the cell, 1 5 V (no charge), 2 charging */
    con_kv("ticks24", (int32_t)plat_ticks24());
    stk_report(con_kv, con_kx);                  /* the stacks: their bottom, size, room never used (system/stackmark.h) */
#if FELUCCA_BLE
    con_kv("ble_state", plat_ble_state());
    con_kv("ble_connected", plat_ble_connected());
#endif
#if FELUCCA_BT_AUDIO
    con_kv("bta_state", plat_bta_state());
#endif
}
#if OPT_CON_EXTRA                               /* (status has cpu1_up, why, jobs, posts) */
static void con_cpu1(void)
{
    con_kv("cpu1_on", plat_cpu2_on());
    con_kv("cpu1_gave_up", plat_cpu2_gave_up());          /* (0 no, 1 it misread a job, 2 one was late, 3 barred, 4 ...: plat.h) */
    con_kv("cpu1_barred", plat_cpu2_barred());
    con_kv("cpu1_bad", (int32_t)plat_cpu2_bad());
    con_kv("cpu1_stack_used", (int32_t)plat_cpu2_stack());
    con_kv("core", (int32_t)plat_core());
#if FELUCCA_DUAL >= 2
    con_kv("cpu1_up", opt_core_dual_stat(0));
    con_kv("cpu1_why", opt_core_dual_stat(1));
    con_kv("cpu1_jobs", opt_core_dual_stat(2));
    con_kv("cpu1_posts", opt_core_dual_stat(3));
    {   /* each job's time, us a block of 32 frames (0.73 ms), smoothed: the parts, then the drums, then the kept jobs */
        static const char *const D[7] = {"drums voices us", "drum bus us", "sampled loops us", "sampled inserts us",
                                         "sampled sends us", "x0x us", "drums voices B us"};
        uint32_t k;
        int32_t v;
        for (k = 4; k < 12u && (v = opt_core_dual_stat(k)) >= 0; k++) {
            con_puts("job ");
            con_dec((int32_t)(k - 4u));
            con_puts(" us ");
            con_dec(v);
            con_puts("\r\n");
        }
        for (k = 0; k < 7u; k++)                   /* (stats 20..26: 26 the drums' half B, drum_halves.c) */
            if ((v = opt_core_dual_stat(20u + k)) >= 0)
                con_kv(D[k], v);
    }
#endif
}
#endif
static void con_keys(void)
{
    con_kx("buttons", plat_buttons());
    con_kx("keys", plat_keys());
    con_kv("master", (int32_t)plat_master());
    con_kv("battery_raw", plat_battery_raw());
    con_kv("battery_charging", plat_battery_charging());   /* -1 not read, 0 on the cell, 1 5 V (no charge), 2 charging */
    con_kx("matrix_buttons", fm1_in.buttons);               /* (the ids Optimist's UI sees: opt_panel.h) */
    con_kx("matrix_keys", oin_keys);
}
/* keylog, flr, tabbench: the developer's extras, off when the console runs over MIDI alone (OPT_CON_EXTRA 0: about 1.8 KB
 * of flash with cpu1 and sd raw; the user 2026-11-08: the console must fit beside USB audio in sd-card-mots) */
#if OPT_CON_EXTRA
static void con_keylog(void)
{
    uint32_t n = fm1_klog_n, i, from = n > FM1_KLOG_N ? n - FM1_KLOG_N : 0u, prev = 0;
    for (i = from; i < n; i++) {
        uint32_t k = i % FM1_KLOG_N;
        con_kv(fm1_klog_id[k] & 0x80u ? "R" : "P", (int32_t)(fm1_klog_id[k] & 0x7Fu));
        con_kv("  dt_us", i == from ? 0 : (int32_t)((fm1_klog_t[k] - prev) / FM1_TICKS_PER_US));
        prev = fm1_klog_t[k];
    }
}
static int con_flash_peek(uint32_t a, uint8_t *v);
static void con_dump(uint32_t a, uint32_t n, uint32_t digits, int (*peek)(uint32_t, uint8_t *))
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint8_t v;
        if (i % 16u == 0) {
            con_hex(a + i, digits);
            con_putc(':');
        }
        con_putc(' ');
        if (peek(a + i, &v)) {
            con_puts("??");
            con_puts("\r\n");
            return;
        }
        con_hex(v, 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}
static int con_flash_peek(uint32_t a, uint8_t *v)
{
    const uint8_t *p = plat_data(a);
    if (!p)
        return -1;
    *v = *p;
    return 0;
}
static void con_flr(const char *p)
{
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2);
    uint8_t v;
    if (!ok) {
        con_puts("usage: flr OFFSET [LEN<=256]   (the data windows plat_data reads)\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (con_flash_peek(a, &v) || con_flash_peek(a + n - 1u, &v)) {
        con_puts("outside the data windows\r\n");
        return;
    }
    con_dump(a, n, 6, con_flash_peek);
}
static uint32_t con_tab_run(const int16_t *t, uint32_t mask, int32_t *sum)   /* (the sum is printed: every read kept) */
{
    uint32_t r, i, k, t0, d, best = 0xFFFFFFFFu;
    for (r = 0; r < 8u; r++) {
        int32_t a = 0;
        t0 = plat_ticks24();
        for (i = 0, k = 0; i < 4096u; i++, k = (k + 37u) & mask)
            a += t[k];
        d = plat_ticks24() - t0;
        *sum += a;
        if (d < best)
            best = d;
    }
    return best * 10000u / (PLAT_TICKS_PER_US * 4096u);
}
static void con_tab_line(const char *name, const void *at, int32_t *sum)
{
    con_puts(name);
    con_puts("  ");
    con_dec((int32_t)con_tab_run((const int16_t *)at, 1023u, sum));
    con_puts(" (0.1 ns a read)\r\n");
}
static void con_tabbench(void)
{
    int32_t sum = 0;
    con_tab_line("SINE      ", SINE, &sum);
    con_tab_line("main RAM  ", &trk[0], &sum);
#if FELUCCA_FLASH && FELUCCA_TAB_IN_POOL
    con_tab_line("pool      ", st_buf, &sum);
#endif
    con_puts("(checksum ");
    con_dec(sum);
    con_puts(")\r\n");
}
#endif
#if FELUCCA_SDCARD
/* the card's clock steps (plat_sd_clock, _set, _try): fm1-base's device/sd.c has them on the sd-clock topic only (7169307),
 * not yet on this line: without it a console build with the card did not link (2026-11-08). 1 once sd-clock is merged */
#ifndef OPT_CON_SD_CLOCK
#define OPT_CON_SD_CLOCK 0
#endif
#if OPT_CON_SD_CLOCK
static const char *const CON_SD_RES[6] = {"not tried", "pass", "CRC failed", "data differs from the floor's",
                                          "timeout or refused", "skipped (no high speed)"};
#endif
static void con_sd_mhz(uint32_t khz)                    /* " 15 MHz", " 8.6 MHz" (to a tenth); 0: " -" */
{
    uint32_t t = (khz + 50u) / 100u;
    if (!khz) {
        con_puts(" -");
        return;
    }
    con_putc(' ');
    con_dec((int32_t)(t / 10u));
    if (t % 10u) {
        con_putc('.');
        con_dec((int32_t)(t % 10u));
    }
    con_puts(" MHz");
}

#if OPT_CON_SD_CLOCK
/* "clock 20 MHz (auto: fastest passed 30 MHz, probe N ms) crc fails N steps down N", then a line per step */
static void con_sd_clock(void)
{
    plat_sd_clock_t c;
    uint32_t k;
    plat_sd_clock(&c);
    con_puts("clock");
    con_sd_mhz(c.khz);
    if (c.forced) {
        con_puts(" (set: sd clock)");
    } else {
        con_puts(" (auto: fastest passed");
        if (c.top + 1u < PLAT_SD_NSTEP)
            con_sd_mhz(c.step_khz[c.top]);
        else
            con_puts(" none above the floor");
        con_puts(", probe ms ");
        con_dec((int32_t)c.probe_ms);
        con_putc(')');
    }
    con_puts(" crc fails ");
    con_dec((int32_t)c.crc_fails);
    con_puts(" steps down ");
    con_dec((int32_t)c.steps_down);
    con_puts("\r\n");
    for (k = 0; k < PLAT_SD_NSTEP; k++) {
        con_puts("  step");
        con_sd_mhz(c.step_khz[k]);
        con_puts(": ");
        con_puts(CON_SD_RES[c.res[k] % 6u]);
        con_puts(k == c.step && c.khz ? "  <- kept\r\n" : "\r\n");
    }
}

static void con_sd_speed(void)
{
    plat_sd_clock_t c;
    uint32_t k;
    int r;
    plat_sd_clock(&c);
    for (k = 0; k < PLAT_SD_NSTEP; k++) {
        r = plat_sd_clock_try(k);
        con_puts("  step");
        con_sd_mhz(c.step_khz[k]);
        con_puts(": ");
        con_puts(r >= 0 && r < 6 ? CON_SD_RES[r] : r == PLAT_SD_NOCARD ? "no card (mount first: sd)"
                                                 : r == PLAT_SD_BUSY ? "busy" : "error");
        con_puts("\r\n");
        if (r < 0)
            break;
    }
    con_puts("sd speed: done, the clock as it was\r\n");
}
#endif

static void con_sd_irq(const char *p)                /* sd irq [0|1]: the interrupt reader and its numbers */
{
    static const char *const N[6] = {"irqs", "hunts", "longest hunt bytes", "blocks", "runs", "errors"};
    uint32_t c[6], i;
    int ok;
    uint32_t v = con_num(&p, &ok);
    if (ok && v > 1u) {
        con_puts("sd irq N: 1 SPI1's interrupt takes the blocks (the default), 0 the main loop polls\r\n");
        return;
    }
    con_kv("irq", (int32_t)plat_sd_irq(ok ? (int)v : -1, c));
    for (i = 0; i < 6u; i++)
        con_kv(N[i], (int32_t)c[i]);
}

#if FELUCCA_SD_STREAM                                /* (sst_st and its counters: sd_stream.c, only built with SD_STREAM) */
static void con_sd_stats(const char *p)              /* sd stats [reset]: the streams' counters since the last reset (sd_stream.c sst_st) */
{
    uint32_t ms = (uint32_t)(SD_NOW() - sst_st.t0) / SD_TICKS_MS;
    if (con_word(&p, "reset")) {
        sst_stats_reset();
        con_puts("sd stats: reset\r\n");
        return;
    }
    con_kv("ms", (int32_t)ms);
    con_kv("KB/s", (int32_t)(ms ? (uint32_t)((uint64_t)sst_st.blocks * 512u / ms) : 0u));
    con_kv("runs", (int32_t)sst_st.runs);
    con_kv("blocks", (int32_t)sst_st.blocks);
    con_kv("blocks a run x10", (int32_t)(sst_st.runs ? sst_st.blocks * 10u / sst_st.runs : 0u));
    con_kv("stalls", (int32_t)sst_st.stalls);
    con_kv("crc", (int32_t)sst_st.crc);
    con_kv("errors", (int32_t)sst_st.errors);
    con_kv("aborted", (int32_t)sst_st.aborted);
    con_kv("held frames", (int32_t)sst_st.held);
    con_kv("run max us", (int32_t)sst_st.run_max_us);
    con_kv("lead min ms", sst_st.lead_min_ms == SST_NONE ? -1 : (int32_t)sst_st.lead_min_ms);
    con_kv("dry", (int32_t)sst_dry);
    con_kv("nostream", (int32_t)sst_nostream);
    con_kv("cut", (int32_t)sst_cut);
    con_kv("irq", (int32_t)plat_sd_irq(-1, 0));
}
#endif

static void con_sd_cmd(const char *p)
{
    static const char *const ST[] = {"unmounted", "no card", "no FAT16 / FAT32 filesystem", "ready"};
    int ok;
#if OPT_CON_SD_CLOCK
    if (con_word(&p, "diag")) {
        con_sd_clock();
        return;
    }
    if (con_word(&p, "speed")) {
        con_sd_speed();
        return;
    }
#endif
    if (con_word(&p, "irq")) {
        con_sd_irq(p);
        return;
    }
#if FELUCCA_SD_STREAM
    if (con_word(&p, "stats")) {
        con_sd_stats(p);
        return;
    }
#endif
#if OPT_CON_SD_CLOCK
    if (con_word(&p, "clock")) {                         /* the fastest step at or under MHZ; anything else: auto */
        plat_sd_clock_t c;
        uint32_t mhz = con_num(&p, &ok), k, pick = 0;
        plat_sd_clock(&c);
        for (k = 0; ok && k < PLAT_SD_NSTEP && !pick; k++)
            if (c.step_khz[k] <= mhz * 1000u)
                pick = k + 1u;
        if (ok && !pick)
            pick = PLAT_SD_NSTEP;                        /* (under the floor: the floor) */
        plat_sd_clock_set(pick);
        p = "";                                          /* (then the mount again, below) */
    }
#endif
#if OPT_CON_EXTRA
    if (con_word(&p, "raw")) {                           /* a sector, through plat_sd_read */
        static uint8_t blk[512];
        uint32_t s = con_num(&p, &ok);
        if (!ok || plat_sd_read(s, blk, 1u)) {
            con_puts("sd raw SECTOR: no card, or the read failed\r\n");
            return;
        }
        con_bytes("sector", blk, 64u);
        return;
    }
#endif
    {
        uint32_t st = sd_mount();
        int sub = *p != 0;                                   /* (a folder or a file: the state only when it is not ready) */
        if (!sub || st != SD_READY) {
            con_puts("sd ");
            con_puts(st < 4u ? ST[st] : "?");
            con_puts("\r\n");
        }
        if (!sub && sd_card_type()) {
            con_kv("MB", (int32_t)sd_card_mb());
            con_kv("blocks", (int32_t)plat_sd_blocks());
            con_puts("clock");
            con_sd_mhz(plat_sd_clock_khz());
            con_puts(" (sd diag: each step)\r\n");
        }
        if (st != SD_READY)
            return;
        if (!sub) {
            con_kv("fat", (int32_t)sd_fat_bits());
            con_kv("cluster B", (int32_t)sd_cluster_bytes());
        }
    }
    if (con_word(&p, "ls")) {
        DIR d;
        FILINFO fi;
        while (*p == ' ')
            p++;
        if (sd_opendir(&d, *p ? p : "/") != FR_OK) {
            con_puts("sd: no such directory\r\n");
            return;
        }
        while (sd_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
            con_puts(fi.fattrib & AM_DIR ? "  <dir> " : "        ");
            con_puts(fi.fname);
            if (!(fi.fattrib & AM_DIR))
                con_kv("", (int32_t)fi.fsize);
            else
                con_puts("\r\n");
        }
    } else if (con_word(&p, "cat")) {
        static uint8_t b[64];
        uint32_t got = 0;
        while (*p == ' ')
            p++;
        if (!*p || sd_open(p) != FR_OK) {
            con_puts("sd: no such file\r\n");
            return;
        }
        (void)sd_read(b, sizeof b, &got);
        con_bytes(p, b, got);
    }
}
#endif

static void con_exec(const char *p)
{
    if (con_run(p))
        return;
    if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("play  pause  stop  rec on|off  proj load N  heavy [NOTES]  status  params  dbg  keys"
#if OPT_CON_EXTRA
                 "  cpu1  keylog  flr OFFSET [LEN]  tabbench"
#endif
#if FELUCCA_SDCARD
#if OPT_CON_SD_CLOCK
                 "  sd [ls [DIR] | cat FILE | diag | speed | clock MHZ|auto | irq [N] | stats [reset]]"
#else
                 "  sd [ls [DIR] | cat FILE | irq [N] | stats [reset]]"
#endif
#endif
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
                 "  btlog [N]"
#endif
                 "  reboot\r\n");
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "params"))
        con_params();
    else if (con_word(&p, "dbg"))
        con_dbg();
#if OPT_CON_EXTRA
    else if (con_word(&p, "cpu1"))
        con_cpu1();
#endif
    else if (con_word(&p, "keys"))
        con_keys();
#if OPT_CON_EXTRA
    else if (con_word(&p, "keylog"))
        con_keylog();
    else if (con_word(&p, "flr"))
        con_flr(p);
    else if (con_word(&p, "tabbench"))
        con_tabbench();
#endif
#if FELUCCA_SDCARD
    else if (con_word(&p, "sd"))
        con_sd_cmd(p);
#endif
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
    else if (con_word(&p, "btlog"))
        con_btlog(p);
#endif
    else if (con_word(&p, "reboot")) {
        con_puts("rebooting\r\n");
        con_reboot_ms = plat_ms() | 1u;         /* (opt_con_task: the reply out first, the project saved, then the restart) */
    } else if (*p)
        con_puts("? (help)\r\n");
}

#if FELUCCA_CON_MIDI
/* app.c app_sysex: a frame from USB (the bytes between F0 and F7); 1 = a console line, run and answered here */
int opt_con_frame(const uint8_t *p, uint32_t n)
{
    char line[CON_LINE];
    uint32_t i, k = 0;
    if (n < 4u || p[0] != 0x7D || p[1] != 0x46 || p[2] != 0x43)
        return 0;
    if (p[3] != 'l')
        return 1;                               /* (ours, not a line: an echo of a reply) */
    for (i = 4; i < n && k + 1u < CON_LINE; i++)
        if (p[i] >= ' ' && p[i] < 0x7F)
            line[k++] = (char)p[i];
    line[k] = 0;
    con.via_sx = 1;
    con.stalled = 0;
    con.sx_n = 0;
    con_exec(line);
    con_sx_flush();
    {
        static const uint8_t END[6] = {0xF0, 0x7D, 0x46, 0x43, 'e', 0xF7};
        (void)plat_sysex_send(END, sizeof END);
    }
    con.via_sx = 0;
    return 1;
}
#endif

/* every main loop pass: the prompt when a computer opens the port, the line typed (console.c cdc_task); a `reboot`
 * 150 ms after its reply (the reply out, over either port), the working project saved first */
static void opt_con_task(void)
{
    if (con_reboot_ms && plat_ms() - con_reboot_ms > 150u) {
        persist_flush_now();
        plat_restart();
    }
#if FELUCCA_CDC
    int c;
    if (plat_console_open() && !con.dtr_seen) {
        con.stalled = 0;
        con_puts("\r\nOptimist ");
        con_puts(FELUCCA_VERSION);
        con_puts(" console (fm1-base) - 'help'\r\n> ");
    }
    con.dtr_seen = (uint8_t)plat_console_open();
    while ((c = plat_console_read()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                       /* (the LF of a CR LF) */
            con.stalled = 0;
            con_puts("\r\n");
            con.line[con.len] = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = (char)c;
            con_putc((char)c);
        }
    }
#endif
}
/* at power-on: the console presented from this start, before fm1-base starts USB (app_init: no re-enumeration), as
 * usb.c's usb_start sets usb_cdc_on (always: the console comes with the build, no switch since 2026-11-05) */
static void opt_con_boot(void)
{
#if FELUCCA_CDC
    usb_cdc_on = 1;
    plat_console_present(1);
#endif
}
#else
static void opt_con_task(void) {}
static void opt_con_boot(void) {}
#endif
#if !FELUCCA_CON_MIDI
int opt_con_frame(const uint8_t *p, uint32_t n) { (void)p, (void)n; return 0; }
#endif
