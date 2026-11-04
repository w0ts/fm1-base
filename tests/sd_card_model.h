/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD card tests' stand-ins and card (tests/sd_test.c, tests/sd_clock_test.c; device/sd.c included after it): the
 * LCD (lcd.c), the time base and felucca-bsp's fm1_sd_hw.h as device/sd.c calls them, and a card in SPI mode byte by
 * byte (SDHC, SDSC v2, SD v1: CMD0 8 55/41 58 59 16 6 9 10 12 17 18 24 25, every command's CRC7 checked). Its clock is
 * the step of device/sd_clock.h SPI1's divider is at (the start-up's 400 kHz none: always right); the clock faults
 * (Optimist's tests/sd_card_model.h has the same switches, for its own driver's detection):
 *   ok_from      steps faster than this read wrong: a byte of every block and register flipped, its CRC unchanged
 *   dead_from    steps faster than this: nothing comes back (every byte 0xFF: timeouts)
 *   zero_from    steps faster than this: the line stuck low after the token (zeros, and a CRC16 of zeros: right)
 *   flaky_from, flaky_every   steps faster than flaky_from: every flaky_every-th block flipped
 *   bad_only     > 0: step bad_only - 1 alone reads wrong (a resonance)
 *   die_from     steps faster than this: the card stops answering for good
 *   no_hs        CMD6 answers, the card stays at default speed (30 MHz then reads wrong, as without CMD6)
 *   cmd6_crc_bad CMD6's status comes with a wrong CRC16
 * and the failure the black FM-1 showed (Optimist 47524040: garbage past the start-up):
 *   garbage_from steps faster than this: every byte read back is noise; the card still hears the commands
 *   slow_ms      every token (a block's, a register's) comes this late, at every clock
 *   wedge_from   a command at a step faster than this leaves the card deaf until a CMD0 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/plat.h"
#include "../device/sd_clock.h"

/* ---- the stand-ins: the LCD (lcd.c), the time base, fm1_sd_hw.h, the watchdog ---- */
#define LCD_BAUD 0u
#define FM1_TICKS_PER_US 24u
static uint32_t now;                                 /* ticks: each byte on the bus 0.4 us at 20 MHz (10 ticks) */
static uint32_t fm1_ticks(void) { return now += 2u; }
static uint32_t wdt_feeds, wdt_last, wdt_gap;       /* the feeds, the last one's time, the longest gap (ticks) */
static void fm1_wdt_feed(void)
{
    if (now - wdt_last > wdt_gap)
        wdt_gap = now - wdt_last;
    wdt_last = now;
    wdt_feeds++;
}
static uint8_t lcd_up = 1;
static uint32_t lcd_nops, lcd_syncs, lcd_baud_now = LCD_BAUD, lcd_cs_low = 1;
static void lcd_sync(void) { lcd_syncs++; }
static void lcd_cmd(uint8_t c) { lcd_sync(); if (c == 0) lcd_nops++; }
static void fm1_lcd_baud(uint32_t b) { lcd_baud_now = b; }
enum { FM1_SD_HW_RX = 0, FM1_SD_HW_DUPLEX = 1, FM1_SD_HW_GPIO = 2 };
#define FM1_SD_HW_BAUD_INIT 149u
#define CON_LCD 0x4021u
static uint32_t spi_con = CON_LCD, spi_baud, sel, taken, hw_mode, take_baud;   /* (take_baud: the last session's clock) */
static int t_step(void)                              /* the clock now: a step of sd_clock.h; the start-up's: past them */
{
    for (uint32_t k = 0; k < FM1B_SD_NSTEP; k++)
        if (FM1B_SD_STEP_BAUD[k] == spi_baud)
            return (int)k;
    return (int)FM1B_SD_NSTEP;
}
static uint32_t fm1_sd_hw_take(uint32_t baud, uint32_t mode)
{
    taken = 1;
    hw_mode = mode;
    spi_baud = take_baud = baud;
    lcd_baud_now = 999;
    return spi_con & ~0xC000u;
}
static void fm1_sd_hw_baud(uint32_t b) { spi_baud = b; }
static void fm1_sd_hw_sel(void) { sel = 1; lcd_cs_low = 0; }
static void card_desel(void);
static void fm1_sd_hw_desel(void) { sel = 0; lcd_cs_low = 1; card_desel(); }
static void fm1_sd_hw_give(uint32_t con, uint32_t mode)
{
    (void)mode;
    fm1_sd_hw_desel();
    spi_con = con | 0x4000u;
    taken = 0;
}

/* ---- the card ---- */
#define NBLK 4096u
static uint8_t img[NBLK][512];
static struct {
    int kind;                                        /* 0 none, 1 SDHC, 2 SDSC v2, 3 SD v1 */
    int reads_in;                                    /* the one hw mode its answers reach SPI1 by (-1: any) */
    int crc_bad;                                     /* blocks to send with a wrong CRC16 (-1: all) */
    int late;                                        /* 0xFF bytes before each read token */
    int busy;                                        /* 0x00 bytes of busy after each block written */
    int dma_bad;                                     /* a DMA read gives garbage */
    int idle, acmd, app_next, crc_on, ready41;
    uint8_t cmd[6];
    int ncmd;
    uint8_t out[1024];                               /* what DO says next */
    int nout, pos;
    int rd_multi;                                    /* a CMD18 run: the next block address */
    uint32_t rd_next;
    int wr;                                          /* 1 CMD24, 2 CMD25: taking a block */
    uint32_t wr_addr;
    uint8_t wbuf[514];
    int wn, wtok;
    int crc7_bad, cmds;
    uint32_t blocks_read, blocks_written;
    int ok_from, dead_from, zero_from, flaky_from, flaky_every, bad_only, die_from, no_hs;   /* the clock faults (above) */
    long flaky_n;
    int hs;                                          /* CMD6 switched it to high speed */
    long cmd6s, cmd10s, cmd6_switches;
    int cmd6_crc_bad, garbage_from, slow_ms, wedge_from, wedged;
    int hold_at;                                     /* out[] index of a late token (-1 none), hold_end its time */
    uint32_t hold_end;
    uint32_t noise;
} c;
static void card_desel(void)                         /* (a late token never sent: dropped with the selection) */
{
    if (c.hold_at >= 0) {
        c.nout = c.pos = 0;
        c.hold_at = -1;
    }
}
static int clock_bad(void)                           /* this clock reads wrong: past what the card takes, or 30 without CMD6 */
{
    int k = t_step();
    return k < c.ok_from || k == c.bad_only - 1 || (k < (int)FM1B_SD_NSTEP_HS && !c.hs);
}

static void say(const uint8_t *b, int n) { memcpy(c.out + c.nout, b, (size_t)n); c.nout += n; }
static void say1(uint8_t b) { c.out[c.nout++] = b; }
static uint16_t crc16(const uint8_t *p, int n)
{
    uint16_t r = 0;
    for (int i = 0; i < n; i++) {
        r ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++)
            r = (uint16_t)(r & 0x8000u ? ((uint32_t)r << 1) ^ 0x1021u : (uint32_t)r << 1);
    }
    return r;
}
static uint8_t crc7(const uint8_t *p, int n)
{
    uint8_t r = 0;
    for (int i = 0; i < n; i++)
        for (int k = 7; k >= 0; k--) {
            int bit = ((p[i] >> k) ^ (r >> 6)) & 1;
            r = (uint8_t)((r << 1) & 0x7F);
            if (bit)
                r ^= 0x09;
        }
    return (uint8_t)(r << 1 | 1);
}
static uint32_t blk_of(uint32_t a) { return c.kind == 1 ? a : a / 512u; }
static void hold(void)                              /* slow_ms: what comes next comes late */
{
    if (c.slow_ms) {
        c.hold_at = c.nout;
        c.hold_end = now + (uint32_t)c.slow_ms * 1000u * FM1_TICKS_PER_US;
    }
}
static void say_block(uint32_t b)
{
    uint8_t d[512];
    uint16_t k;
    int st = t_step();
    for (int i = 0; i < c.late; i++)
        say1(0xFF);
    hold();
    say1(0xFE);
    memcpy(d, img[b], 512);
    k = crc16(d, 512);
    if (clock_bad())
        d[100] ^= 0x10;
    if (st < c.flaky_from && c.flaky_every && ++c.flaky_n % c.flaky_every == 0)
        d[300] ^= 0x04;
    if (st < c.zero_from)
        memset(d, 0, 512), k = 0;
    say(d, 512);
    if (c.crc_bad) {
        k ^= 1;
        if (c.crc_bad > 0)
            c.crc_bad--;
    }
    say1((uint8_t)(k >> 8));
    say1((uint8_t)k);
    c.blocks_read++;
}
static void say_reg(uint8_t *r16)                   /* a register's data block (the CSD, the CID): its CRC7 last */
{
    uint16_t k16;
    r16[15] = crc7(r16, 15);
    k16 = crc16(r16, 16);
    if (clock_bad())
        r16[3] ^= 0x20;
    say1(0);
    hold();
    say1(0xFE);
    say(r16, 16);
    say1((uint8_t)(k16 >> 8)), say1((uint8_t)k16);
}
static void command(void)
{
    uint8_t *k = c.cmd, r1 = c.idle ? 1 : 0;
    uint32_t arg = (uint32_t)k[1] << 24 | (uint32_t)k[2] << 16 | (uint32_t)k[3] << 8 | k[4];
    int n = k[0] & 0x3F, app = c.app_next;
    c.app_next = 0;
    c.cmds++;
    if (k[5] != crc7(k, 5)) {                        /* (every command: the driver always sends it right) */
        c.crc7_bad++;
        if (c.crc_on || n == 0 || n == 8) {
            say1(0xFF);
            say1((uint8_t)(r1 | 0x08));
            return;
        }
    }
    say1(0xFF);                                      /* (one byte before the answer) */
    if (n == 12) {
        c.rd_multi = 0;
        c.nout = c.pos = 0;
        c.hold_at = -1;
        say1(0xFF);
        say1(r1);
        say1(0x00);
        say1(0x00);
        say1(0xFF);
        return;
    }
    if (t_step() < c.wedge_from)
        c.wedged = 1;
    if (c.wedged && n != 0)
        return;
    if (n == 0) {
        c.wedged = 0;
        c.hs = 0;
        c.idle = 1;
        c.ready41 = 0;
        say1(1);
        return;
    }
    if (n == 8) {
        if (c.kind == 3) {
            say1(0x05);
            return;
        }
        say1(r1);
        say1(0), say1(0), say1(1), say1(0xAA);
        return;
    }
    if (n == 55) {
        c.app_next = 1;
        say1(r1);
        return;
    }
    if (app && n == 41) {
        if (++c.ready41 >= 3)
            c.idle = 0;
        say1(c.idle ? 1 : 0);
        return;
    }
    if (n == 58) {
        say1(r1);
        say1(c.kind == 1 ? 0xC0 : 0x80), say1(0xFF), say1(0x80), say1(0);
        return;
    }
    if (c.idle) {
        say1(0x05);
        return;
    }
    if (n == 59) {
        c.crc_on = arg & 1u;
        say1(0);
        return;
    }
    if (n == 16) {
        say1(arg == 512 ? 0 : 0x40);
        return;
    }
    if (n == 9) {
        uint8_t csd[18] = {0};
        if (c.kind == 1) {                           /* v2: C_SIZE + 1 = NBLK / 1024 */
            csd[0] = 0x40;
            csd[9] = (uint8_t)(NBLK / 1024u - 1u);
        } else {                                     /* v1: (C_SIZE + 1) << (C_SIZE_MULT + 2 + READ_BL_LEN - 9) */
            uint32_t size = NBLK / 4u - 1u;          /* mult 0 (x4), READ_BL_LEN 9 */
            csd[5] = 9;
            csd[6] = (uint8_t)(size >> 10 & 3u);
            csd[7] = (uint8_t)(size >> 2);
            csd[8] = (uint8_t)(size << 6);
        }
        say_reg(csd);
        return;
    }
    if (n == 10) {                                   /* the CID: a SanDisk-like one */
        uint8_t cid[16] = {0x03, 'S', 'D', 'S', 'C', '1', '6', 'G', 0x80, 0x12, 0x34, 0x56, 0x78, 0x01, 0x5A, 0};
        c.cmd10s++;
        say_reg(cid);
        return;
    }
    if (n == 6) {                                    /* SWITCH_FUNC: v2 cards; mode 1, group 1 = 1: high speed */
        uint8_t st[64] = {0};
        uint16_t k16;
        c.cmd6s++;
        if (c.kind == 3) {
            say1(0x04);
            return;
        }
        if (arg >> 31) {
            c.cmd6_switches++;
            c.hs = !c.no_hs && !c.cmd6_crc_bad && (arg & 15u) == 1u;
        }
        st[13] = c.no_hs ? 0u : 2u;                  /* bits 407:400: group 1's functions supported */
        st[16] = (uint8_t)(c.no_hs ? 0u : arg & 15u);   /* bits 379:376: group 1's function now */
        k16 = (uint16_t)(crc16(st, 64) ^ (c.cmd6_crc_bad ? 1u : 0u));
        say1(0);
        say1(0xFE);
        say(st, 64);
        say1((uint8_t)(k16 >> 8)), say1((uint8_t)k16);
        return;
    }
    if (n == 17 || n == 18) {
        uint32_t b = blk_of(arg);
        if (b >= NBLK) {
            say1(0x40);
            return;
        }
        say1(0);
        say_block(b);
        if (n == 18) {
            c.rd_multi = 1;
            c.rd_next = b + 1;
        }
        return;
    }
    if (n == 24 || n == 25) {
        c.wr = n == 24 ? 1 : 2;
        c.wr_addr = blk_of(arg);
        c.wn = c.wtok = 0;
        say1(0);
        return;
    }
    say1(0x04);
}
static void wr_byte(uint8_t b)                       /* a write's data phase */
{
    if (!c.wtok) {
        if (b == 0xFE || b == 0xFC) {
            c.wtok = 1;
            c.wn = 0;
        } else if (b == 0xFD && c.wr == 2) {
            c.wr = 0;
            say1(0xFF);
            for (int i = 0; i < c.busy; i++)
                say1(0);
        }
        return;
    }
    c.wbuf[c.wn++] = b;
    if (c.wn < 514)
        return;
    c.wtok = 0;
    if (crc16(c.wbuf, 512) != ((uint16_t)c.wbuf[512] << 8 | c.wbuf[513])) {
        say1(0x0B);
    } else if (c.wr_addr >= NBLK) {
        say1(0x0D);
    } else {
        memcpy(img[c.wr_addr++], c.wbuf, 512);
        c.blocks_written++;
        say1(0x05);
    }
    for (int i = 0; i < c.busy; i++)
        say1(0);
    if (c.wr == 1)
        c.wr = 0;
}
static uint8_t card_byte(uint8_t in)                 /* one byte clocked while selected: what DO says */
{
    uint8_t o = 0xFF;
    int held = c.hold_at >= 0 && c.pos == c.hold_at && (int32_t)(now - c.hold_end) < 0;
    if (!held) {                                     /* (a late token: not before its time; the card still hears) */
        if (c.pos == c.hold_at)
            c.hold_at = -1;
        if (c.pos < c.nout)
            o = c.out[c.pos++];
        if (c.pos == c.nout)
            c.pos = c.nout = 0;
    }
    if (c.wr) {
        wr_byte(in);
        return o;
    }
    if (c.ncmd || (in & 0xC0) == 0x40) {
        c.cmd[c.ncmd++] = in;
        if (c.ncmd == 6) {
            c.ncmd = 0;
            if (!c.rd_multi || (c.cmd[0] & 0x3F) == 12)
                command();
        }
    }
    if (c.rd_multi && c.nout == 0 && c.rd_next < NBLK)
        say_block(c.rd_next++);
    return o;
}

static uint8_t fm1_sd_hw_xfer(uint8_t b, uint32_t mode)
{
    if (t_step() < c.die_from)                       /* the card gone at this clock (a crash: nothing brings it back) */
        c.kind = 0;
    if (!c.kind || !sel || t_step() < c.dead_from)   /* (too fast for the line: the card hears nothing, says nothing) */
        return 0xFF;
    uint8_t o = card_byte(b);
    if (t_step() < c.garbage_from) {
        c.noise = c.noise * 1103515245u + 12345u;
        return (uint8_t)(c.noise >> 16);
    }
    return c.reads_in < 0 || (uint32_t)c.reads_in == mode ? o : 0xFF;
}
static uint8_t *dma_p;
static uint32_t dma_n, dma_rx;
static void fm1_sd_hw_dma(const void *p, uint32_t n, uint32_t rx)
{
    dma_p = (uint8_t *)p;
    dma_n = n;
    dma_rx = rx;
}
static uint32_t fm1_sd_hw_dma_end(void)
{
    for (uint32_t i = 0; i < dma_n; i++) {
        if (dma_rx)
            dma_p[i] = (uint8_t)(fm1_sd_hw_xfer(0xFF, hw_mode) ^ (c.dma_bad ? 0x5A : 0));
        else
            (void)fm1_sd_hw_xfer(dma_p[i], hw_mode);
    }
    return 1;
}


static void card(int kind)
{
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.reads_in = -1;
    c.hold_at = -1;
    for (uint32_t b = 0; b < NBLK; b++)
        for (int i = 0; i < 512; i++)
            img[b][i] = (uint8_t)(b * 7u + (uint32_t)i * 13u + (b >> 3));
}
static int same(uint32_t b, const uint8_t *p, uint32_t n) { return !memcmp(img[b], p, n * 512u); }
