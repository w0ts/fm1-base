/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD card's clock detection (device/sd.c sd_clock_probe; device/sd_clock.h's steps 30 20 15 12 10 8.57 7.5 6.67 6
 * MHz; plat.h "The clock") on the byte-level card (tests/sd_card_model.h: a card whose reads go wrong above a given
 * clock, time out there, read zeros with a right CRC, fail now and then, fail at one step alone, or stop answering for
 * good; the same switches and cases as Optimist's tests/sd_clock_test.c for its own driver):
 *   - every step reads, from the floor up: 30 MHz passes (CMD6 took high speed), 20 kept (no failure: the step below
 *     the top);
 *   - no high speed: 30 skipped, 20 passes, 15 kept; SDSC v2 the same walk; SD v1 and the plain pins: the floor, nothing
 *     tried; plat_sd_clock_set: a step, unchecked;
 *   - reads wrong above each step in turn: the first wrong step ends the walk, the one below it kept and checked once
 *     more (the floor when that is the floor), every faster step untried;
 *   - timeouts from 10 MHz up: 8.57 kept;
 *   - the black FM-1's failure: garbage past the start-up from 15 MHz up keeps 12, from 10 up keeps 8.57, no CMD6 sent;
 *     the card slow, a card that goes deaf at a fast command, the tokens later than the probe waits: each mounts, the probe's
 *     simulated time under 5.5 s (the watchdog 8 s), under 2 s between feeds; the CMD6 switch only when its status (CRC
 *     right) says high speed is supported; plat_sd_read and plat_sd_write feed the watchdog;
 *   - zeros with a right CRC16 above 12 MHz: caught by the floor's fingerprint (PLAT_SD_STEP_DIFF);
 *   - a block in 30 wrong from 8.57 MHz up: the second pass catches what the first missed; 7.5 kept;
 *   - the check at the step kept failing: the floor, still mounted;
 *   - one step alone wrong: the walk ends there, nothing faster tried;
 *   - the card gone at a step: PLAT_SD_ERR, not present, no clock;
 *   - every detection: nothing written, no command refused for its CRC7, the bus back with the LCD, the watchdog fed,
 *     the CRC failures it saw not counted as the card's;
 *   - at run time, by bytes and by DMA: a CRC failure above the floor drops one step and the read is redone (the data
 *     right), counted, plat_sd_read_next too; failing all the way down: at the floor twice more, then PLAT_SD_CRC;
 *   - plat_sd_clock_try (the console's sd speed): a step tried, the clock kept as it was.
 *   cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -DFM1B_CDC=1 -Idevice -o sd_clock_test tests/sd_clock_test.c */
#include "sd_card_model.h"
#include "sd.c"

static int checks, fails;
static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        printf("  FAIL %s\n", what);
        fails++;
    }
}
static int bus_back(void)
{
    return !taken && !sel && lcd_cs_low && spi_con == (CON_LCD | 0x4000u) && lcd_baud_now == LCD_BAUD && !sd.on_bus;
}
static const char *const RES[6] = {"-", "pass", "crc", "diff", "err", "nohs"};
static void steps_line(const char *what)            /* (a failure's context: what each step did) */
{
    plat_sd_clock_t k;
    plat_sd_clock(&k);
    printf("  %s:", what);
    for (uint32_t i = 0; i < PLAT_SD_NSTEP; i++)
        printf(" %u=%s", (unsigned)(k.step_khz[i] / 100u), RES[k.res[i] % 6u]);
    printf("  kept %u, top %u, failed %u, ms %u\n", (unsigned)k.step, (unsigned)k.top, (unsigned)k.failed,
           (unsigned)k.probe_ms);
}

static uint8_t buf[16 * 512 + 4] __attribute__((aligned(4)));

/* a card started (the detection inside plat_sd_init); the clean-up rules checked: -> plat_sd_init's result */
static int start(const char *what)
{
    char w[160];
    uint32_t f0 = sd.crc_fails, wd = wdt_feeds;
    int r;
    wdt_last = now;
    wdt_gap = 0;
    r = plat_sd_init();
    fm1_wdt_feed();                                  /* (the main loop's next: the gap after the last feed counted) */
    snprintf(w, sizeof w, "%s: nothing written, every CRC7 right, the bus back, the probe's CRC failures not counted", what);
    ok(c.blocks_written == 0 && !c.crc7_bad && bus_back() && sd.crc_fails == f0, w);
    snprintf(w, sizeof w, "%s: the watchdog fed while the steps were walked", what);
    ok(!(sd.v2 && sd.res[FM1B_SD_FLOOR] == PLAT_SD_STEP_PASS && sd.top != 0) || wdt_feeds > wd, w);
    return r;
}

/* the time: the watchdog's 8 s, and the bounds device/sd.c says: ~5 s in all, ~1.7 s between feeds; plat_sd_init's start-up
 * (ACMD41's own time before the first pass) in the gap */
static void bounded(const char *what, int r)
{
    char w[160];
    uint32_t gap_ms = wdt_gap / (1000u * FM1_TICKS_PER_US);
    printf("sd_clock_test: %-22s init %d, probe %5u ms, longest unfed %5u ms, kept %u kHz\n", what, r,
           (unsigned)sd.probe_ms, (unsigned)gap_ms, (unsigned)plat_sd_clock_khz());
    snprintf(w, sizeof w, "%s: mounted, the probe under 5.5 s (%u ms), under 2 s between feeds (%u ms)", what,
             (unsigned)sd.probe_ms, (unsigned)gap_ms);
    ok(r == 0 && plat_sd_present() && sd.probe_ms < 5500u && gap_ms < 2000u, w);
}

static void expect(const char *what, uint32_t top, uint32_t kept)
{
    char w[160];
    int good = sd.top == top && sd.step == kept;
    snprintf(w, sizeof w, "%s: fastest step %u (want %u), kept %u (want %u)", what, (unsigned)sd.top, (unsigned)top,
             (unsigned)sd.step, (unsigned)kept);
    ok(good, w);
    if (!good)
        steps_line(what);
    snprintf(w, sizeof w, "%s: reads right at the clock kept (%u kHz)", what, (unsigned)plat_sd_clock_khz());
    ok(plat_sd_read(100, buf, 16) == 0 && same(100, buf, 16) && take_baud == FM1B_SD_STEP_BAUD[kept] &&
       plat_sd_clock_khz() == FM1B_SD_STEP_KHZ(kept) && bus_back(), w);
}

static void walks(void)
{
    char w[80];
    plat_sd_clock_t k;

    card(1);                                         /* every step reads */
    ok(start("all good") == 0 && sd.failed == 0, "all good: started, no step failed");
    expect("all good", 0, 1);
    ok(c.hs && sd.res[0] == PLAT_SD_STEP_PASS && sd.res[1] == PLAT_SD_STEP_PASS &&
       sd.res[FM1B_SD_FLOOR] == PLAT_SD_STEP_PASS && sd.res[2] == PLAT_SD_STEP_PASS && c.cmd10s > 0,
       "all good: every step passed (30 after CMD6 high speed), the floor's reference (the CID read)");
    ok(plat_sd_clock_khz() == 20000u, "all good: 20000 kHz");
    plat_sd_clock(&k);
    ok(k.khz == 20000u && k.step == 1 && k.top == 0 && k.hs && !k.forced && k.step_khz[0] == 30000u &&
       k.step_khz[FM1B_SD_FLOOR] == 6000u && k.res[0] == PLAT_SD_STEP_PASS, "all good: plat_sd_clock says the same");

    card(1);                                         /* CMD6 refused high speed: 30 not tried */
    c.no_hs = 1;
    (void)start("no high speed");
    expect("no high speed", 1, 2);
    ok(sd.res[0] == PLAT_SD_STEP_NOHS, "no high speed: 30 skipped");

    card(2);                                         /* SDSC v2 (byte addresses): the same walk */
    (void)start("SDSC v2");
    expect("SDSC v2", 0, 1);

    card(3);                                         /* SD v1: the floor, nothing tried */
    ok(start("SD v1") == 0 && sd.step == FM1B_SD_FLOOR && sd.res[FM1B_SD_FLOOR] == PLAT_SD_STEP_UNTRIED && !c.cmd6s &&
       plat_sd_clock_khz() == 6000u, "SD v1: the floor (6000 kHz), untried, no CMD6");

    card(1);                                         /* the plain pins: no clock to pick */
    c.reads_in = FM1_SD_HW_GPIO;
    ok(start("plain pins") == 0 && sd.step == FM1B_SD_FLOOR && sd.res[FM1B_SD_FLOOR] == PLAT_SD_STEP_UNTRIED &&
       plat_sd_clock_khz() == 0u && plat_sd_read(3, buf, 2) == 0 && same(3, buf, 2),
       "the plain pins: the floor, untried, no clock shown (0), reads");

    card(1);                                         /* set by hand: unchecked, even where it reads wrong */
    c.ok_from = 4;
    plat_sd_clock_set(3);
    ok(start("set") == 0 && sd.step == 2 && plat_sd_clock_khz() == 15000u && sd.res[FM1B_SD_FLOOR] == PLAT_SD_STEP_UNTRIED,
       "plat_sd_clock_set(3): 15 MHz, nothing tried");
    plat_sd_clock(&k);
    ok(k.forced == 3, "set: plat_sd_clock says it was set");
    plat_sd_clock_set(0);

    for (int s = 1; s <= (int)FM1B_SD_FLOOR; s++) {  /* wrong above step s: the walk ends there, s kept */
        card(1);
        c.ok_from = s;
        snprintf(w, sizeof w, "wrong above %u kHz", (unsigned)FM1B_SD_STEP_KHZ(s));
        ok(start(w) == 0 && sd.failed == 1u, w);
        expect(w, (uint32_t)s, (uint32_t)s);
        ok(sd.res[s - 1] == PLAT_SD_STEP_CRC && (s == 1 || sd.res[s - 2] == PLAT_SD_STEP_UNTRIED), w);
    }

    card(1);                                         /* the line dead from 10 MHz up: a timeout */
    c.dead_from = 5;
    (void)start("timeouts");
    expect("timeouts", 5, 5);
    ok(sd.res[4] == PLAT_SD_STEP_ERR && sd.res[3] == PLAT_SD_STEP_UNTRIED, "timeouts: the walk ends there, 12 not tried");

    /* the black FM-1: garbage past the start-up. From 15 MHz up: 12 kept; from 10 up: 8.57. No CMD6: 30 never reached */
    card(1);
    c.garbage_from = 3;
    bounded("garbage from 15", start("garbage from 15"));
    expect("garbage from 15", 3, 3);
    ok(sd.res[2] != PLAT_SD_STEP_PASS && !c.cmd6s && plat_sd_clock_khz() == 12000u, "garbage from 15: 12 MHz, no CMD6 sent");
    card(1);
    c.garbage_from = 5;
    bounded("garbage from 10", start("garbage from 10"));
    expect("garbage from 10", 5, 5);
    ok(plat_sd_clock_khz() == 8571u, "garbage from 10: 8.57 MHz");

    card(1);                                         /* the card slow: every token 19 ms late, a hair under the wait */
    c.slow_ms = 19;
    bounded("slow", start("slow"));
    ok(sd.res[7] == PLAT_SD_STEP_PASS && sd.res[6] == PLAT_SD_STEP_UNTRIED, "slow: the 1 s budget ended the walk");
    expect("slow", 7, FM1B_SD_FLOOR);

    card(1);                                         /* slow and garbage: the same, the budget first */
    c.garbage_from = 3;
    c.slow_ms = 19;
    bounded("garbage + slow", start("garbage + slow"));
    expect("garbage + slow", 7, FM1B_SD_FLOOR);

    card(1);                                         /* tokens later than the probe waits: the floor, nothing tried */
    c.slow_ms = 30;
    bounded("slower than the wait", start("slower than the wait"));
    ok(sd.res[FM1B_SD_FLOOR] == PLAT_SD_STEP_ERR && sd.res[7] == PLAT_SD_STEP_UNTRIED,
       "slower than the wait: the floor's own pass gave up, nothing tried");
    expect("slower than the wait", FM1B_SD_FLOOR, FM1B_SD_FLOOR);

    card(1);                                         /* a fast command leaves it deaf: started again at 400 kHz */
    c.wedge_from = 6;
    bounded("wedged", start("wedged"));
    expect("wedged", 6, 6);

    card(1);                                         /* deaf, and slow enough for every wait to count */
    c.wedge_from = 7;
    c.slow_ms = 9;
    bounded("wedged + slow", start("wedged + slow"));
    expect("wedged + slow", 7, 7);

    card(1);                                         /* the check at the step kept failing: the floor, still mounted */
    c.ok_from = 6;
    c.worse_to = 8;
    bounded("the check fails", start("the check fails"));
    ok(sd.res[5] == PLAT_SD_STEP_CRC && sd.res[6] == PLAT_SD_STEP_PASS, "the check fails: 8.57 failed, 7.5 had passed");
    expect("the check fails", 6, FM1B_SD_FLOOR);

    card(1);                                         /* CMD6 asked, its status's CRC16 wrong: no switch */
    c.cmd6_crc_bad = 1;
    (void)start("CMD6 CRC");
    ok(!c.cmd6_switches && !sd.hs && sd.res[0] == PLAT_SD_STEP_NOHS, "CMD6's status CRC wrong: not switched, 30 skipped");
    card(1);                                         /* not supported */
    c.no_hs = 1;
    (void)start("CMD6 unsupported");
    ok(!c.cmd6_switches, "CMD6's status says high speed is not supported: not switched");
    card(1);
    (void)start("CMD6");
    ok(c.cmd6_switches == 1 && c.cmd6s == 2 && c.hs, "CMD6: asked, then switched, once, before 30 MHz");

    card(1);                                         /* a pass over the card inside one frame: fed at every call */
    (void)plat_sd_init();
    wdt_gap = 0;
    {
        uint32_t f0 = wdt_feeds;
        ok(plat_sd_read(5, buf, 1) == 0 && plat_sd_write(6, buf, 1) == 0 && wdt_feeds == f0 + 2,
           "plat_sd_read and plat_sd_write feed the watchdog (the index pass inside a frame)");
    }

    card(1);                                         /* zeros with a right CRC16 above 12 MHz */
    c.zero_from = 3;
    (void)start("zeros");
    expect("zeros", 3, 3);
    ok(sd.res[2] == PLAT_SD_STEP_DIFF, "zeros: 15 MHz differs from the floor's fingerprint");

    card(1);                                         /* a block in 30 wrong above 8.57 MHz */
    c.flaky_from = 6;
    c.flaky_every = 30;
    (void)start("now and then");
    expect("now and then", 6, 6);
    ok(sd.failed == 1 && sd.res[5] == PLAT_SD_STEP_CRC && sd.res[4] == PLAT_SD_STEP_UNTRIED,
       "now and then: 8.57 failed (a block in 30: its second pass, 24 blocks, reaches one), the walk ended there");

    card(1);                                         /* 15 alone wrong: the walk ends there, 20 not tried */
    c.bad_only = 3;
    (void)start("one step wrong");
    expect("one step wrong", 3, 3);
    ok(sd.res[1] == PLAT_SD_STEP_UNTRIED && sd.res[2] == PLAT_SD_STEP_CRC, "one step wrong: 15 failed, 20 untried");

    card(1);                                         /* the card gone at 30 MHz */
    c.die_from = 1;
    ok(plat_sd_init() == PLAT_SD_ERR && !plat_sd_present() && plat_sd_clock_khz() == 0u && sd.step == FM1B_SD_FLOOR &&
       bus_back(), "card gone: PLAT_SD_ERR, not present, no clock, the bus back");
}

/* at run time: the clock kept, then the card worse (wrong at and above the step kept) */
static void run_time(int dma)
{
    char w[120];
    uint32_t d0, f0;
    card(1);
    c.dma_bad = !dma;                                /* (a DMA that reads wrong: the byte path) */
    c.ok_from = 3;
    ok(plat_sd_init() == 0 && sd.step == 3 && sd.dma == (uint8_t)dma, dma ? "run time, DMA: 12 MHz kept"
                                                                          : "run time, bytes: 12 MHz kept");
    c.ok_from = 4;                                   /* 12 MHz goes wrong (and 10 reads) */
    d0 = sd.steps_down;
    f0 = sd.crc_fails;
    snprintf(w, sizeof w, "run time dma %d: CMD18 read right, one step down (to 10 MHz), counted", dma);
    ok(plat_sd_read(200, buf, 16) == 0 && same(200, buf, 16) && sd.step == 4 && sd.steps_down == d0 + 1 &&
       sd.crc_fails == f0 + 1 && bus_back(), w);
    c.ok_from = 5;
    snprintf(w, sizeof w, "run time dma %d: CMD17 too (to 8.57 MHz)", dma);
    ok(plat_sd_read(300, buf, 1) == 0 && same(300, buf, 1) && sd.step == 5, w);
    c.ok_from = 6;                                   /* a streaming run: PLAT_SD_CRC, the clock down, read again */
    snprintf(w, sizeof w, "run time dma %d: plat_sd_read_next: PLAT_SD_CRC and a step down, then read right", dma);
    ok(plat_sd_read_open(301) == 0 && plat_sd_read_next(buf, 1000) == PLAT_SD_CRC && sd.step == 6 && plat_sd_close() == 0 &&
       plat_sd_read_open(301) == 0 && plat_sd_read_next(buf, 1000) == 0 && same(301, buf, 1) && plat_sd_close() == 0 &&
       bus_back(), w);
    c.ok_from = (int)FM1B_SD_FLOOR;                  /* wrong everywhere above the floor: down to it, then read */
    snprintf(w, sizeof w, "run time dma %d: stepped down to the floor, read right", dma);
    ok(plat_sd_read(400, buf, 8) == 0 && same(400, buf, 8) && sd.step == FM1B_SD_FLOOR && plat_sd_clock_khz() == 6000u, w);
    c.ok_from = 0;
    c.crc_bad = -1;                                  /* at the floor a block that keeps failing: PLAT_SD_CRC */
    snprintf(w, sizeof w, "run time dma %d: at the floor: PLAT_SD_CRC, the card kept", dma);
    ok(plat_sd_read(400, buf, 8) == PLAT_SD_CRC && sd.step == FM1B_SD_FLOOR && plat_sd_present() && bus_back(), w);
    c.crc_bad = 0;
}

/* at run time: a timeout or a wrong R1 or token above the floor (a late line reads a bad token, 2026-11-04 on an FM-1, not a bad
 * CRC): a step down and read on, as for a CRC failure; at the floor a real failure; a card taken out reads as gone at once */
static void run_slip(void)
{
    uint32_t d0, t0;
    card(1);
    c.ok_from = 3;
    ok(plat_sd_init() == 0 && sd.step == 3, "slip: 12 MHz kept");
    c.ok_from = 0;
    c.dead_from = 4;                                 /* 12 MHz answers nothing */
    d0 = sd.steps_down;
    ok(plat_sd_read(200, buf, 16) == 0 && same(200, buf, 16) && sd.step == 4 && sd.steps_down == d0 + 1 && plat_sd_present() &&
       bus_back(), "slip: a session's timeout: one step down, read right, the card kept");
    c.dead_from = 0;
    c.garbage_from = 5;                              /* 10 MHz answers garbage: a wrong R1, a wrong token */
    ok(plat_sd_read(300, buf, 4) == 0 && same(300, buf, 4) && sd.step == 5 && plat_sd_present() && bus_back(),
       "slip: a session's wrong answer: one step down, read right");
    c.garbage_from = 0;
    c.dead_from = 6;
    ok(plat_sd_read_open(320) == 0 && sd.step == 6 && plat_sd_read_next(buf, 1000000) == 0 && same(320, buf, 1) &&
       plat_sd_close() == 0 && bus_back(), "slip: a run's open: one step down, then read right");
    c.dead_from = 0;
    ok(plat_sd_read_open(330) == 0 && plat_sd_read_next(buf, 1000000) == 0 && same(330, buf, 1), "slip: a run started");
    c.garbage_from = 7;                              /* 7.5 MHz goes bad inside the run: the next block read right, a step down */
    {
        int r;
        uint32_t n = 0;
        while ((r = plat_sd_read_next(buf, 1000000)) == PLAT_SD_BUSY && n++ < 8u)
            ;
        ok(r == 0 && same(331, buf, 1) && sd.step == 7 && plat_sd_present(), "slip: a run's wrong token: the next block read right");
    }
    c.garbage_from = 0;
    c.dead_from = 8;                                 /* nothing at all at 6.67 MHz: a run's silence, a step down, read on */
    {
        int r;
        uint32_t n = 0;
        while ((r = plat_sd_read_next(buf, 1000000)) == PLAT_SD_BUSY && n++ < 8u)
            ;
        ok(r == 0 && same(332, buf, 1) && sd.step == 8 && plat_sd_close() == 0 && bus_back(), "slip: a run's silence: the next block read right");
    }
    c.dead_from = 0;
    c.garbage_from = 0;
    /* the floor reached: a real failure */
    c.dead_from = (int)FM1B_SD_NSTEP;
    d0 = sd.steps_down;
    ok(plat_sd_read(400, buf, 1) == PLAT_SD_ERR && !plat_sd_present() && sd.steps_down == d0 && bus_back(), "slip: at the floor: gone");
    c.dead_from = 0;
    /* taken out, from the clock kept: gone, and soon */
    card(1);
    c.ok_from = 3;
    ok(plat_sd_init() == 0 && sd.step == 3, "slip: 12 MHz kept again");
    c.kind = 0;
    t0 = fm1_ticks();
    ok(plat_sd_read(400, buf, 1) == PLAT_SD_ERR && !plat_sd_present() && bus_back(), "slip: a card taken out reads as gone");
    t0 = (fm1_ticks() - t0) / (1000u * FM1_TICKS_PER_US);
    printf("sd_clock_test: a card taken out: gone in %u ms\n", (unsigned)t0);
    ok(t0 < 5u, "slip: gone within 5 ms");
    /* taken out inside a run: gone within the wait for a block */
    card(1);
    c.ok_from = 3;
    ok(plat_sd_init() == 0 && plat_sd_read_open(10) == 0 && plat_sd_read_next(buf, 1000000) == 0, "slip: a run on the clock kept");
    c.kind = 0;
    t0 = fm1_ticks();
    {
        int r;
        uint32_t n = 0;
        while ((r = plat_sd_read_next(buf, 1000000)) == PLAT_SD_BUSY && n++ < 8u)
            ;
        ok(r == PLAT_SD_ERR, "slip: a run with the card taken out: an error");
    }
    (void)plat_sd_close();
    ok(!plat_sd_present() && bus_back(), "slip: and gone");
    t0 = (fm1_ticks() - t0) / (1000u * FM1_TICKS_PER_US);
    printf("sd_clock_test: a card taken out in a run: gone in %u ms\n", (unsigned)t0);
    ok(t0 < 150u, "slip: within 150 ms");
}

static void tries(void)
{
    card(1);
    c.ok_from = 3;
    ok(plat_sd_init() == 0 && sd.step == 3, "sd speed: 12 MHz kept");
    ok(plat_sd_clock_try(4) == PLAT_SD_STEP_PASS && sd.step == 3 && bus_back(), "sd speed: 10 MHz tried: pass, 12 kept");
    ok(plat_sd_clock_try(1) == PLAT_SD_STEP_CRC && sd.step == 3 && plat_sd_present() && bus_back() &&
       plat_sd_read(5, buf, 2) == 0 && same(5, buf, 2), "sd speed: 20 MHz tried: CRC, the card still reads at 12");
    ok(plat_sd_clock_try(FM1B_SD_FLOOR) == PLAT_SD_STEP_PASS, "sd speed: the floor: pass");
    ok(plat_sd_clock_try(FM1B_SD_NSTEP) == PLAT_SD_ERR, "sd speed: no such step: PLAT_SD_ERR");
    ok(plat_sd_read_open(10) == 0 && plat_sd_clock_try(2) == PLAT_SD_BUSY && plat_sd_close() == 0,
       "sd speed: a run open: PLAT_SD_BUSY");
    card(1);
    c.no_hs = 1;
    ok(plat_sd_init() == 0 && plat_sd_clock_try(0) == PLAT_SD_STEP_NOHS, "sd speed: 30 MHz without high speed: skipped");
}

int main(void)
{
    walks();
    run_time(0);
    run_time(1);
    run_slip();
    tries();
    printf("sd_clock_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
