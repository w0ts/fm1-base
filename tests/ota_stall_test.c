/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The update's waits when the clock they count stops (device/ota_idle.h, bsp/chip/ota.c on the host).
 *
 * Every wait of the update (ota_read's three 5 s tries, the session's 120 s, ota_wire_send's 200 ms for room in the
 * SysEx ring) counts fm1_ms, which only TIMER5's interrupt moves (device/main_fm1.c fm1_timer5_irq, from TIMER4's
 * crystal count), and feeds the watchdog while it waits (ota_idle). TIMER5 stopped (masked, its interrupts off, never
 * acknowledged...) leaves fm1_ms standing: the wait never ends, the watchdog never fires, and nothing of USB runs either
 * (usb_poll is TIMER5's too): the FM-1 silent until a power cycle, which is the freeze seen on two FM-1s on 2026-11-21
 * (the installer: "stopped answering after 1 requests"; 7 minutes silent). Here, on the host:
 *   1. a host that answers the first read, then TIMER5 stops: the session must not wait forever. ota_idle stops
 *      feeding the watchdog (it hands over to ota_clock_stall: on the FM-1 a restart that keeps a record) once fm1_ms
 *      has not moved for OTA_CLOCK_STALL_MS of the crystal's count; before this it fed it forever (a hang, caught by the
 *      test's own limit);
 *   2. with the clock running nothing changes: a host that goes silent ends the session within its tries (15 s), a slow
 *      host (4.9 s a reply) is waited for, a SysEx ring nobody drains gives the send up after 200 ms; no stall then;
 *   3. a gap of up to OTA_CLOCK_STALL_MS with the interrupts off (a flash erase, 4 KiB: at most ~0.4 s) is no stall.
 *   cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O1 -o ota_stall_test tests/ota_stall_test.c */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FELUCCA_ID "FM-1_900"
#define FELUCCA_OTA_DRYRUN 1
#define T24_PER_MS 24000u                           /* TIMER4: 24 MHz */
#define TEST_CAP_MS 3600000u                        /* an hour of the crystal's time in one session: hung */
#include "../device/ota_idle.h"
#include "../device/wait_rec.h"
#include "../device/main_watch.h"
static wr_rec_t rec;                                /* the firmware's fm1b_wait_rec (.noinit) */

/* ---- the clocks: TIMER4's count always runs; fm1_ms only while TIMER5's interrupt does */
static uint32_t t24, fm1_ms, ms_frac;
static uint64_t crystal_us;                         /* the crystal's time, whole (t24 wraps at ~179 s) */
static int timer5_on = 1;
static void tick_us(uint32_t us)                    /* time passes (the crystal), TIMER5 moves fm1_ms if it runs */
{
    uint32_t d = us * (T24_PER_MS / 1000u);
    t24 += d;
    crystal_us += us;
    if (!timer5_on)
        return;
    ms_frac += d;
    fm1_ms += ms_frac / T24_PER_MS;
    ms_frac %= T24_PER_MS;
}

/* ---- the host: answers the first `answer` reads (after `delay_ms`), then nothing; TIMER5 stops after reply `stop_after` */
static uint32_t answer, stop_after, delay_ms, requests, served, reply_due;
static int ring_stuck;                              /* the SysEx ring full, nobody draining it */
static uint8_t rx[700];
static uint32_t rx_len, rx_full;
static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        for (nb += 8u; nb >= 7u; nb -= 7u, acc >>= 7)
            out[o++] = (uint8_t)(acc & 0x7Fu);
    }
    if (nb)
        out[o++] = (uint8_t)(acc & 0x7Fu);
    return o;
}
static uint32_t req_addr, req_len;
static void host_reply(void)                        /* cmd 0x30's answer: addr, len, the data (zeros) */
{
    static uint8_t m[8 + 15 + 512];
    uint32_t i, s = 0, body = req_len + 8u;
    memset(m, 0, sizeof m);
    m[1] = 0x59, m[2] = 0x30, m[3] = (uint8_t)body, m[4] = (uint8_t)(body >> 8), m[5] = 0;
    m[7] = (uint8_t)req_addr, m[8] = (uint8_t)(req_addr >> 8), m[9] = (uint8_t)(req_addr >> 16);
    m[10] = (uint8_t)(req_addr >> 24);
    m[11] = (uint8_t)req_len, m[12] = (uint8_t)(req_len >> 8);
    for (i = 6; i < 14u + req_len; i++)
        s += m[i];
    m[14u + req_len] = (uint8_t)~s;
    rx_len = pack7(m, 15u + req_len, rx);
    rx_full = 1;
    served++;
    if (served == stop_after)
        timer5_on = 0;                              /* the freeze: TIMER5 stops from here */
}

/* ---- chip/ota.c's hooks, as device/unity.c and bsp/chip/usb.c supply them */
static uint32_t ota_now_ms(void) { return fm1_ms; }
static ota_clock_t ota_clk;
static uint32_t feeds;
static jmp_buf out;
static uint32_t stall_ms;
static void ota_idle_feed(void) { feeds++; }
static void ota_clock_stall(uint32_t stood_ms)      /* the FM-1: a record, then the watchdog's restart */
{
    static const uint32_t snap[WR_SNAP] = {12345u, 0u, 7u, 1u, 40u, 52u, 3u, 0u, 0xACu, 0u, 6u};   /* TIMER5 off */
    stall_ms = stood_ms;
    wr_note(&rec, WR_OTA_CLOCK, 4u, 1u, 0, stood_ms, fm1_ms, snap);   /* (main_fm1.c ota_clock_stall: stage 4 USB) */
    longjmp(out, 2);
}
static void ota_idle(void)
{
    tick_us(1000u);                                 /* (each turn of a wait: a millisecond of the crystal's time) */
    if (crystal_us / 1000u > TEST_CAP_MS)
        longjmp(out, 1);                            /* still waiting after an hour: hung */
    if (reply_due && fm1_ms >= reply_due) {
        reply_due = 0;
        host_reply();
    }
    ota_idle_do(&ota_clk, fm1_ms, t24, T24_PER_MS);
}
static int ota_wire_send(const uint8_t *p, uint32_t n)   /* usb.c's: room in the ring within 200 ms, else -1 */
{
    uint32_t t0 = ota_now_ms(), acc = 0, nb = 0, o = 0, i;
    uint8_t d[48];
    while (ring_stuck) {
        if (ota_now_ms() - t0 > 200u)
            return -1;
        ota_idle();
    }
    for (i = 1; i + 1u < n; i++) {                  /* the request, decoded: cmd 0x30, addr, len */
        acc |= (uint32_t)(p[i] & 0x7Fu) << nb;
        for (nb += 7u; nb >= 8u && o < sizeof d; nb -= 8u, acc >>= 8)
            d[o++] = (uint8_t)acc;
    }
    if (o >= 14u && d[2] == 0x30) {
        requests++;
        req_addr = (uint32_t)d[7] | (uint32_t)d[8] << 8 | (uint32_t)d[9] << 16 | (uint32_t)d[10] << 24;
        req_len = (uint32_t)d[11] | (uint32_t)d[12] << 8;
        if (requests <= answer && timer5_on)
            reply_due = fm1_ms + delay_ms;          /* (the host answers what reaches it) */
    }
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!rx_full)
        return 0;
    *p = rx;
    *n = rx_len;
    return 1;
}
static void ota_frame_done(void) { rx_full = 0; }
static int ota_erase(uint32_t off) { (void)off; return 0; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off, (void)p, (void)n; return 0; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; memset(p, 0xFF, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step, (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }

#include "../bsp/chip/ota.c"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

/* one session: 0 it returned (rc in *rc), 1 hung (the test's hour), 2 stalled (ota_clock_stall) */
static int session(uint32_t ans, uint32_t stop, uint32_t delay, int stuck, int *rc, uint32_t *took_ms)
{
    volatile uint64_t t0;
    int r;
    t24 = fm1_ms = ms_frac = 0;
    crystal_us = 0;
    timer5_on = 1;
    answer = ans, stop_after = stop, delay_ms = delay, ring_stuck = stuck;
    requests = served = reply_due = 0;
    rx_full = 0;
    feeds = stall_ms = 0;
    memset(&ota_clk, 0, sizeof ota_clk);
    t0 = crystal_us;
    r = setjmp(out);
    if (r == 0) {
        *rc = ota_session();
        *took_ms = (uint32_t)((crystal_us - t0) / 1000u);
        return 0;
    }
    *took_ms = (uint32_t)((crystal_us - t0) / 1000u);
    return r;
}

int main(void)
{
    int rc = 0, r;
    uint32_t took = 0;

    printf("1. TIMER5 stops after the first reply (the 2026-11-21 freeze)\n");
    r = session(1000u, 1u, 10u, 0, &rc, &took);
    printf("     requests %u, served %u, feeds %u, %s after %u ms\n", (unsigned)requests, (unsigned)served,
           (unsigned)feeds, r == 0 ? "returned" : r == 1 ? "HUNG" : "stalled", (unsigned)took);
    want("the session does not wait forever with the watchdog fed (fm1_ms stood still)", r != 1);
    want("ota_idle hands over to ota_clock_stall once fm1_ms stood still OTA_CLOCK_STALL_MS", r == 2);
    want("... within OTA_CLOCK_STALL_MS + 1 s of the crystal's time after the stop", r == 2 && took <= 4000u);
    want("one request answered, the second sent (the installer's \"after 1 requests\")", requests == 2u && served == 1u);

    printf("2. the clock running: the waits end by themselves, no stall\n");
    r = session(1u, 0u, 10u, 0, &rc, &took);
    want("a host silent after the first read: the session gives up (ota_read's 3 x 5 s), no stall",
         r == 0 && rc < 0 && took >= 15000u && took <= 16000u && requests == 4u);
    r = session(1000u, 0u, 4900u, 0, &rc, &took);
    want("a slow host (4.9 s a reply): both header reads waited for (then the zeros fail the header's CRC), no stall",
         r == 0 && rc == -2 && requests == 2u && served == 2u && took >= 9800u);
    r = session(1000u, 0u, 10u, 1, &rc, &took);
    want("a SysEx ring nobody drains: the send gives up after 200 ms, the session ends, no stall",
         r == 0 && rc < 0 && took <= 300u);

    printf("3. an interrupts-off window shorter than OTA_CLOCK_STALL_MS is no stall\n");
    {
        uint32_t i;
        int stalled = 0;
        t24 = fm1_ms = ms_frac = 0;
    crystal_us = 0;
        timer5_on = 1;
        memset(&ota_clk, 0, sizeof ota_clk);
        if (setjmp(out) == 0) {
            for (i = 0; i < 100u; i++)
                ota_idle();
            timer5_on = 0;                          /* a 4 KiB erase with the interrupts off: 400 ms */
            tick_us(400000u);
            ota_idle();
            timer5_on = 1;
            for (i = 0; i < 100u; i++)
                ota_idle();
        } else {
            stalled = 1;
        }
        want("400 ms of fm1_ms standing still (a flash erase): no stall", !stalled);
    }
    printf("4. the record (device/wait_rec.h): what `fm1_remote.py log` prints\n");
    {
        char b[220];
        uint32_t i;
        memset(&rec, 0xA5, sizeof rec);             /* (power-on RAM) */
        wr_init(&rec);
        want("power-on: no entries, no counts line", wr_entries(&rec) == 0u && wr_counts(&rec, b, sizeof b) == 0u);
        r = session(1000u, 1u, 10u, 0, &rc, &took);
        wr_line(&rec, 0, b, sizeof b);
        printf("     %s\n", b);
        want("a stall writes one entry: its id, the time fm1_ms stood, the stage, the step, the snapshot",
             r == 2 && wr_entries(&rec) == 1u &&
             !strncmp(b, "stalled wait: OTA_CLOCK after 2000 ms at stage 4 step 1 rc 0 (ms ", 64) &&
             strstr(b, " t5 12345 au 0 su 7 rs 1 rx 40 tx 52 sxq 3 sxw 0 fl 0xAC held 0 irq 0x6)") != NULL);
        wr_counts(&rec, b, sizeof b);
        want("the counts line: OTA_CLOCK 1", !strcmp(b, "stalled waits: OTA_CLOCK 1"));
        wr_init(&rec);                              /* a restart (the watchdog's): the record kept */
        r = session(1000u, 1u, 10u, 0, &rc, &took);
        wr_counts(&rec, b, sizeof b);
        want("kept over a restart; the next stall counts 2", wr_entries(&rec) == 2u && !strcmp(b, "stalled waits: OTA_CLOCK 2"));
        for (i = 0; i < 5u; i++)
            wr_note(&rec, WR_OTA_SESSION, 4u, 1u, -1, 15000u + i, 100u, NULL);
        wr_line(&rec, 0, b, sizeof b);
        want("the ring keeps the last WR_RING, the oldest first", wr_entries(&rec) == WR_RING &&
             strstr(b, "OTA_SESSION after 15001 ms") != NULL && strstr(b, "rc -1") != NULL);
        wr_counts(&rec, b, sizeof b);
        want("the counts go on past the ring", !strcmp(b, "stalled waits: OTA_CLOCK 2 OTA_SESSION 5"));
        want("a line fits the firmware's 200-byte buffer", wr_line(&rec, 3, b, sizeof b) < 200u);
    }
    printf("5. the main thread watched from TIMER5 (device/main_watch.h)\n");
    {
        main_watch_t w;
        uint32_t ms, beat = 0, due = 0, first = 0;
        memset(&w, 0, sizeof w);
        for (ms = 0; ms < 120000u; ms++) {          /* the main loop moves every ms for 10 s, then stands still */
            if (ms < 10000u)
                beat++;
            if (main_watch_due(&w, beat, ms, 30000u)) {
                due++;
                first = first ? first : ms;
            }
        }
        want("a beat standing still 30 s: due once, 30 s after its last move", due == 1u && first == 9999u + 30000u);
        memset(&w, 0, sizeof w);
        for (due = 0, ms = 0; ms < 120000u; ms++) {
            if (ms % 20000u == 0u)
                beat++;                             /* (a long step: 20 s between passes) */
            due += (uint32_t)main_watch_due(&w, beat, ms, 30000u);
        }
        want("passes 20 s apart: never due", due == 0u);
    }
    printf(fails ? "FAILED: %d\n" : "all ok\n", fails);
    return fails != 0;
}
