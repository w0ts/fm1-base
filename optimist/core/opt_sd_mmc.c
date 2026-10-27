/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's card driver (storage/sd/sd_mmc.c: ChaN's SPI-mode driver on Optimist's hal/fm1_sd_bus.h) in the trial:
 * the same names, on fm1-base's SD card (plat.h plat_sd_*: device/sd.c, the card on the LCD's bus; the host simulator's
 * card image), so Optimist's card code above it (sdcard.c, FatFs' disk layer sd_diskio.c, the streams sd_stream.c, the
 * recorder sd_rec.c, the SD card mode sd_mode.c) compiles unchanged. sdcard.c includes this file in sd_mmc.c's place
 * (SD_MMC_C, ui/opt_sd.h). What moved to fm1-base: the bus and its sessions (each plat_sd_* call takes the bus from
 * the LCD and gives it back: sdcard.c's sd_begin / sd_end do nothing here), the read modes and the DMA probe (fm1-base's
 * start-up; fm1_sd_mode / fm1_sd_dma stay for Optimist's code to read), the CRCs and their retries (fm1-base checks
 * every block's CRC16 and reads a failed one again; sd_crc_retry here lets the stream read it once more). Left out:
 * the 30 MHz clock (fm1-base runs the card at 20: sd_clock_probe says not tried), Petit FatFs (the trial builds full
 * FatFs only: the SD card mode's).
 *   sd_rd_*      a multi-block read: plat_sd_read_open / plat_sd_read_next (a short wait a poll, as sd_rd_poll's few
 *                token bytes) / plat_sd_close
 *   sd_wr_*      the recorder's multi-block write: plat_sd_write_open, plat_sd_write_next (0 for the block: the card's
 *                busy asked without sending), plat_sd_close (the stop, its busy waited for) */
#if !FELUCCA_SD_FATFS
#error "the trial's card: full FatFs only (SDCARD with LAYOUT 1 and FAR_OK: FELUCCA_SD_FATFS)"
#endif
#include "storage/sd/fatfs/ff.h"
#include "storage/sd/fatfs/diskio.h"

/* ---- what sdcard.c reads of hal/fm1_sd_bus.h and the LCD driver: the bus is fm1-base's */
enum { FM1_SD_DUPLEX = 0, FM1_SD_DIRRX = 1, FM1_SD_BITBANG = 2 };
#define FM1_SD_BAUD_SLOW 0u
#define FM1_SD_BAUD_FAST 1u
#define FM1_SD_BAUD_OVER 2u
static uint8_t fm1_sd_mode = FM1_SD_DUPLEX;
static uint8_t fm1_sd_dma;
static void fm1_sd_bus_begin(uint32_t baud) { (void)baud; }
static void fm1_sd_bus_end(void) {}
static void lcd_cmd(uint8_t c) { (void)c; }      /* (sd_begin's NOP: never sent, sd_lcd_live stays 0; fm1-base's own) */
static void fm1_lcd_baud(uint32_t b) { (void)b; }
#ifndef LCD_BAUD
#define LCD_BAUD 0u
#endif

#define SD_NOW() fm1_ticks()
#define SD_TICKS_MS (FM1_TICKS_PER_US * 1000u)
static DWORD sd_ms_since(DWORD t0) { return (DWORD)(SD_NOW() - t0) / SD_TICKS_MS; }

#define CT_MMC 0x01
#define CT_SD1 0x02
#define CT_SD2 0x04
#define CT_SDC (CT_SD1 | CT_SD2)
#define CT_BLOCK 0x08
static BYTE CardType;                            /* (fm1-base's card: blocks of 512 from 0 whatever its kind) */
static DWORD sd_sectors;
static BYTE sd_crc_on = 1, sd_over, sd_crc_bad;
static DWORD sd_crc_fails, sd_demotions;

static WORD sd_crc16_up(WORD c, BYTE b)          /* CRC16-CCITT from 0 (the SD block's): sdcard.c's DMA probe */
{
    UINT i;
    c ^= (WORD)(b << 8);
    for (i = 0; i < 8u; i++)
        c = (WORD)(c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1);
    return c;
}
static WORD sd_crc16(const BYTE *p, UINT n)
{
    WORD c = 0;
    while (n--)
        c = sd_crc16_up(c, *p++);
    return c;
}

static DSTATUS sd_card_init(void)
{
    if (plat_sd_init() != 0) {
        CardType = 0;
        sd_sectors = 0;
        return STA_NOINIT;
    }
    CardType = CT_SD2 | CT_BLOCK;
    sd_sectors = plat_sd_blocks();
    return 0;
}

/* a block's CRC16 failed (fails: its failures so far): 1 = read it again (once; fm1-base read it three times already) */
static BYTE sd_crc_retry(BYTE *fails)
{
    sd_crc_fails++;
    return ++*fails < 2u;
}

/* ---- whole blocks */
enum { SDR_IDLE = 0, SDR_TOKEN, SDR_DMA, SDR_READY, SDR_ERR };
#define SDR_BUSY SDR_TOKEN
#ifndef SD_RD_WAIT_US
#define SD_RD_WAIT_US 20u                         /* a poll's wait for the block (sd_rd_poll's few token bytes) */
#endif
static struct {
    BYTE *buf;
    DWORD t0;
    WORD crc;                                    /* (the last blocking read's CRC16: sdcard.c's DMA probe compares it) */
    BYTE state, multi, crc_ok;
    BYTE dma;
} sd_rd;

static DRESULT sd_rd_open(DWORD sector, UINT n)
{
    (void)n;
    sd_rd.state = SDR_IDLE;
    sd_rd.multi = plat_sd_read_open((uint32_t)sector) == 0;
    return sd_rd.multi ? RES_OK : RES_ERROR;
}

static void sd_rd_next(BYTE *buf)
{
    sd_rd.buf = buf;
    sd_rd.state = SDR_TOKEN;
    sd_rd.t0 = SD_NOW();
}

static BYTE sd_rd_poll(void)
{
    int r;
    if (sd_rd.state != SDR_TOKEN)
        return sd_rd.state;
    r = plat_sd_read_next(sd_rd.buf, SD_RD_WAIT_US);
    if (r == PLAT_SD_BUSY) {
        if (sd_ms_since(sd_rd.t0) >= 100u)
            sd_rd.state = SDR_ERR;
        return sd_rd.state;
    }
    sd_rd.crc_ok = r == 0;
    sd_rd.state = r == 0 || r == PLAT_SD_CRC ? SDR_READY : SDR_ERR;
    return sd_rd.state;
}

static DRESULT sd_rd_close(void)
{
    DRESULT r = sd_rd.state == SDR_ERR ? RES_ERROR : RES_OK;
    if (sd_rd.multi && plat_sd_close() != 0)
        r = RES_ERROR;
    sd_rd.state = SDR_IDLE;
    sd_rd.multi = 0;
    return r;
}

static DRESULT disk_readblocks(BYTE *buf, DWORD sector, UINT n)
{
    if (!n || plat_sd_read((uint32_t)sector, buf, n) != 0) {
        sd_rd.crc_ok = 0;
        return RES_ERROR;
    }
    sd_rd.crc = sd_crc16(buf + (n - 1u) * 512u, 512u);
    sd_rd.crc_ok = 1;
    return RES_OK;
}

static UINT sd_clock_probe(BYTE *buf, UINT n, BYTE force)   /* (fm1-base's 20 MHz: the faster clock not tried) */
{
    (void)buf, (void)force;
    sd_over = 0;
    return n;
}

static DRESULT disk_writeblock(const BYTE *buf, DWORD sector)
{
    return plat_sd_write((uint32_t)sector, buf, 1) == 0 ? RES_OK : RES_ERROR;
}

/* ---- the recorder's multi-block write */
enum { SDW_IDLE = 0, SDW_READY, SDW_BUSY, SDW_STOP, SDW_ERR };
static struct {
    DWORD t0;
    BYTE state;
    DWORD blocks;
} sd_wr;

static DRESULT sd_wr_open(DWORD sector)
{
    if (plat_sd_write_open((uint32_t)sector) != 0) {
        sd_wr.state = SDW_ERR;
        return RES_ERROR;
    }
    sd_wr.state = SDW_READY;
    sd_wr.blocks = 0;
    return RES_OK;
}

static BYTE sd_wr_poll(void)
{
    int r;
    if (sd_wr.state == SDW_STOP)
        return sd_wr.state = SDW_IDLE;           /* (sd_wr_stop waited for the stop's busy) */
    if (sd_wr.state != SDW_BUSY)
        return sd_wr.state;
    r = plat_sd_write_next(0);                   /* (the card's busy asked, nothing sent) */
    if (r == 0)
        return sd_wr.state = SDW_READY;
    if (r != PLAT_SD_BUSY)
        return sd_wr.state = SDW_ERR;
    return SDW_BUSY;
}

static BYTE sd_wr_block(const BYTE *buf)
{
    int r;
    if (sd_wr.state != SDW_READY)
        return sd_wr.state == SDW_ERR ? SDW_ERR : SDW_BUSY;
    while ((r = plat_sd_write_next(buf)) == PLAT_SD_BUSY)
        ;                                        /* (never long: sd_wr_poll said ready) */
    if (r != 0)
        return sd_wr.state = SDW_ERR;
    sd_wr.blocks++;
    sd_wr.t0 = SD_NOW();
    return sd_wr.state = SDW_BUSY;
}

static BYTE sd_wr_stop(void)
{
    if (sd_wr.state != SDW_READY)
        return sd_wr.state == SDW_ERR ? SDW_ERR : SDW_BUSY;
    if (plat_sd_close() != 0)
        return sd_wr.state = SDW_ERR;
    return sd_wr.state = SDW_STOP;
}

static void sd_wr_abort(void)
{
    (void)plat_sd_close();
    sd_wr.state = SDW_IDLE;
}
