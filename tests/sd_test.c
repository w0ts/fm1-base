/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD card driver (device/sd.c) against a card in SPI mode, byte by byte, behind a stand-in for felucca-bsp's
 * fm1_sd_hw.h: the start-up of an SDHC, an SDSC v2 and a v1 card, no card, a card that answers in one read mode only,
 * reads (one block, runs, unaligned, a CRC16 failing once and always), writes (one, several), the streaming runs (a
 * token late: BUSY; a write's busy: BUSY), the CRC7 of every command checked, a DMA that reads wrong (the byte path
 * taken), and after every call the bus back with the LCD: the card deselected, SPI1's CON and the LCD's clock as they
 * were, a NOP sent to the LCD before each session.
 *   cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -Idevice -o sd_test tests/sd_test.c && ./sd_test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/plat.h"

/* ---- the stand-ins: the LCD (lcd.c), the time base, fm1_sd_hw.h ---- */
#define LCD_BAUD 0u
#define FM1_TICKS_PER_US 24u
static uint32_t now;                                 /* ticks: each byte on the bus 0.4 us at 20 MHz (10 ticks) */
static uint32_t fm1_ticks(void) { return now += 2u; }
static uint8_t lcd_up = 1;
static uint32_t lcd_nops, lcd_syncs, lcd_baud_now = LCD_BAUD, lcd_cs_low = 1;
static void lcd_sync(void) { lcd_syncs++; }
static void lcd_cmd(uint8_t c) { lcd_sync(); if (c == 0) lcd_nops++; }
static void fm1_lcd_baud(uint32_t b) { lcd_baud_now = b; }
enum { FM1_SD_HW_RX = 0, FM1_SD_HW_DUPLEX = 1, FM1_SD_HW_GPIO = 2 };
#define FM1_SD_HW_BAUD_INIT 149u
#define FM1_SD_HW_BAUD_RUN 2u
#define CON_LCD 0x4021u
static uint32_t spi_con = CON_LCD, spi_baud, sel, taken, hw_mode;
static uint32_t fm1_sd_hw_take(uint32_t baud, uint32_t mode)
{
    taken = 1;
    hw_mode = mode;
    spi_baud = baud;
    lcd_baud_now = 999;
    return spi_con & ~0xC000u;
}
static void fm1_sd_hw_baud(uint32_t b) { spi_baud = b; }
static void fm1_sd_hw_sel(void) { sel = 1; lcd_cs_low = 0; }
static void fm1_sd_hw_desel(void) { sel = 0; lcd_cs_low = 1; }
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
} c;

static void say(const uint8_t *b, int n) { memcpy(c.out + c.nout, b, (size_t)n); c.nout += n; }
static void say1(uint8_t b) { c.out[c.nout++] = b; }
static uint16_t crc16(const uint8_t *p, int n)
{
    uint16_t r = 0;
    for (int i = 0; i < n; i++) {
        r ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++)
            r = (uint16_t)(r & 0x8000u ? (r << 1) ^ 0x1021u : r << 1);
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
static void say_block(uint32_t b)
{
    uint16_t k;
    for (int i = 0; i < c.late; i++)
        say1(0xFF);
    say1(0xFE);
    say(img[b], 512);
    k = crc16(img[b], 512);
    if (c.crc_bad) {
        k ^= 1;
        if (c.crc_bad > 0)
            c.crc_bad--;
    }
    say1((uint8_t)(k >> 8));
    say1((uint8_t)k);
    c.blocks_read++;
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
        say1(0xFF);
        say1(r1);
        say1(0x00);
        say1(0x00);
        say1(0xFF);
        return;
    }
    if (n == 0) {
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
        say1(0);
        say1(0xFE);
        say(csd, 16);
        uint16_t k16 = crc16(csd, 16);
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
    if (c.pos < c.nout)
        o = c.out[c.pos++];
    if (c.pos == c.nout)
        c.pos = c.nout = 0;
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
    if (!c.kind || !sel)
        return 0xFF;
    uint8_t o = card_byte(b);
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

#include "sd.c"

/* ---- the checks ---- */
static int fails;
static void ok(int cond, const char *what)
{
    printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond)
        fails++;
}
static int bus_back(void)                            /* after a call: the LCD's bus as it was */
{
    return !taken && !sel && lcd_cs_low && spi_con == (CON_LCD | 0x4000u) && lcd_baud_now == LCD_BAUD && !sd.on_bus;
}
static void card(int kind)
{
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.reads_in = -1;
    for (uint32_t b = 0; b < NBLK; b++)
        for (int i = 0; i < 512; i++)
            img[b][i] = (uint8_t)(b * 7u + (uint32_t)i * 13u + (b >> 3));
}
static int same(uint32_t b, const uint8_t *p, uint32_t n) { return !memcmp(img[b], p, n * 512u); }

int main(void)
{
    static uint8_t buf[8 * 512 + 4] __attribute__((aligned(4)));
    static const char *KIND[] = {"", "SDHC", "SDSC v2", "SD v1"};
    uint32_t nops;
    int r;

    {
        static const uint8_t CMD0[5] = {0x40, 0, 0, 0, 0};
        static uint8_t ff[512];
        memset(ff, 0xFF, sizeof ff);
        ok(sd_crc7(CMD0, 5) == 0x95 && sd_crc16(ff, 512) == 0x7FA1, "the CRCs: CMD0's CRC7 0x95, a block of 0xFF's CRC16 0x7FA1");
    }
    card(0);
    r = plat_sd_init();
    ok(r == PLAT_SD_NOCARD && !plat_sd_present() && bus_back(), "no card: PLAT_SD_NOCARD, every read mode tried, the bus back");
    ok(plat_sd_read(0, buf, 1) == PLAT_SD_NOCARD && bus_back(), "no card: a read refused at once");

    for (int kind = 1; kind <= 3; kind++) {
        char what[160];
        card(kind);
        nops = lcd_nops;
        r = plat_sd_init();
        snprintf(what, sizeof what, "%s: started, %u blocks (its CSD), DMA proved, the CRCs on, every CRC7 right, a NOP to the LCD first",
                 KIND[kind], (unsigned)plat_sd_blocks());
        ok(r == 0 && plat_sd_present() && plat_sd_blocks() == NBLK && sd.dma && c.crc_on == (kind != 0) && !c.crc7_bad &&
           lcd_nops > nops && bus_back() && sd.block == (kind == 1), what);
        ok(plat_sd_read(5, buf, 1) == 0 && same(5, buf, 1) && bus_back(), "  one block (CMD17)");
        ok(plat_sd_read(100, buf, 8) == 0 && same(100, buf, 8) && bus_back(), "  eight blocks (CMD18, CMD12)");
        ok(plat_sd_read(NBLK - 2, buf, 2) == 0 && same(NBLK - 2, buf, 2), "  the card's last two blocks");
        ok(plat_sd_read(NBLK - 1, buf, 2) == PLAT_SD_ERR && plat_sd_present(), "  past the end: refused, the card kept");
        ok(plat_sd_read(7, buf + 1, 3) == 0 && same(7, buf + 1, 3), "  into an unaligned buffer (through the bounce block)");
        c.crc_bad = 1;
        nops = sd.crc_fails;
        ok(plat_sd_read(40, buf, 4) == 0 && same(40, buf, 4) && sd.crc_fails == nops + 1, "  a block's CRC16 wrong once: read again");
        c.crc_bad = -1;
        ok(plat_sd_read(40, buf, 1) == PLAT_SD_CRC && bus_back(), "  a CRC16 always wrong: PLAT_SD_CRC");
        c.crc_bad = 0;
        for (int i = 0; i < 512 * 3; i++)
            buf[i] = (uint8_t)(i * 31 + kind);
        c.busy = 30;
        ok(plat_sd_write(200, buf, 1) == 0 && same(200, buf, 1) && bus_back(), "  one block written (CMD24), its busy waited for");
        ok(plat_sd_write(300, buf, 3) == 0 && same(300, buf, 3) && bus_back(), "  three blocks written (CMD25, the stop token)");
        c.late = 40;
        ok(plat_sd_read_open(300) == 0 && sd.on_bus && plat_sd_read(0, buf, 1) == PLAT_SD_BUSY, "  a read run open: the bus held, other calls BUSY");
        r = plat_sd_read_next(buf + 1024, 2);
        ok(r == PLAT_SD_BUSY, "  its token late: BUSY within the wait");
        r = plat_sd_read_next(buf + 1024, 1000);
        ok(r == 0 && same(300, buf + 1024, 1), "  then the block");
        ok(plat_sd_read_next(buf + 1024, 1000) == 0 && same(301, buf + 1024, 1) && plat_sd_close() == 0 && bus_back(),
           "  the next, then closed (CMD12): the bus back");
        c.late = 0;
        ok(plat_sd_read_open(10) == 0 && (lcd_sync(), sd_lcd(), bus_back()) && plat_sd_read(10, buf, 1) == 0 && same(10, buf, 1),
           "  the LCD wanting the bus ends an open read run");
        for (int i = 0; i < 512 * 2; i++)
            buf[i] = (uint8_t)(i * 3 + 1);
        c.busy = 200;
        ok(plat_sd_write_open(500) == 0 && bus_back() && plat_sd_write_next(buf) == 0 && bus_back(),
           "  a write run: a block sent, the bus back between blocks");
        r = plat_sd_write_next(buf + 512);
        ok(r == PLAT_SD_BUSY && bus_back() && plat_sd_write_next(0) == PLAT_SD_BUSY,
           "  the next while the card writes: BUSY, nothing sent; asked with no block: BUSY");
        while (plat_sd_write_next(0) == PLAT_SD_BUSY)
            ;
        r = plat_sd_write_next(buf + 512);
        ok(r == 0 && plat_sd_close() == 0 && same(500, buf, 2) && bus_back(), "  then sent, the run closed: both blocks there");
        ok(!c.crc7_bad, "  every command's CRC7 right");
    }

    card(1);
    c.reads_in = FM1_SD_HW_DUPLEX;
    ok(plat_sd_init() == 0 && sd.mode == FM1_SD_HW_DUPLEX && plat_sd_read(3, buf, 2) == 0 && same(3, buf, 2),
       "a card SPI1 reads only shifting in while it sends: that mode found");
    card(1);
    c.reads_in = FM1_SD_HW_GPIO;
    ok(plat_sd_init() == 0 && sd.mode == FM1_SD_HW_GPIO && !sd.dma && plat_sd_read(3, buf, 2) == 0 && same(3, buf, 2),
       "a card read only by the plain pins: that mode, no DMA");
    card(1);
    c.dma_bad = 1;
    ok(plat_sd_init() == 0 && !sd.dma && plat_sd_read(9, buf, 3) == 0 && same(9, buf, 3), "a DMA that reads wrong: the byte path");
    card(1);
    ok(plat_sd_init() == 0, "a card taken out ...");
    c.kind = 0;
    ok(plat_sd_read(1, buf, 1) == PLAT_SD_ERR && !plat_sd_present() && bus_back(), "... its next read fails: gone");
    c.kind = 1;
    ok(plat_sd_read(1, buf, 1) == PLAT_SD_NOCARD && plat_sd_init() == 0 && plat_sd_read(1, buf, 1) == 0,
       "... put back: refused until plat_sd_init, then read");

    printf(fails ? "sd test FAILED (%d)\n" : "sd test passed\n", fails);
    return fails != 0;
}
