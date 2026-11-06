/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD card driver (device/sd.c) against a card in SPI mode, byte by byte, behind a stand-in for felucca-bsp's
 * fm1_sd_hw.h (both: tests/sd_card_model.h; the clock's detection: tests/sd_clock_test.c): the start-up of an SDHC, an
 * SDSC v2 and a v1 card and the clock each runs at, no card, a card that answers in one read mode only,
 * reads (one block, runs, unaligned, a CRC16 failing once and always), writes (one, several), the streaming runs (a
 * token late: BUSY; a write's busy: BUSY), the CRC7 of every command checked, a DMA that reads wrong (the byte path
 * taken), and after every call the bus back with the LCD: the card deselected, SPI1's CON and the LCD's clock as they
 * were, a NOP sent to the LCD before each session.
 *   cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -Idevice -o sd_test tests/sd_test.c && ./sd_test */
#include "sd_card_model.h"                           /* the stand-ins (the LCD, the time base, fm1_sd_hw.h) and the card */
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
        ok(plat_sd_clock_khz() == (kind == 3 ? 6000u : 20000u),
           "  its clock detected: 20 MHz (30 passed after CMD6, 20 the margin); SD v1 the floor, 6 MHz");
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
