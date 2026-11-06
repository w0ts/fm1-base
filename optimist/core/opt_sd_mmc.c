/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's card driver (storage/sd/sd_mmc.c: ChaN's SPI-mode driver on Optimist's hal/fm1_sd_bus.h) in the trial:
 * the same names, on fm1-base's SD card (plat.h plat_sd_*: device/sd.c, the card on the LCD's bus; the host simulator's
 * card image), so Optimist's card code above it (sdcard.c, FatFs' disk layer sd_diskio.c, the streams sd_stream.c, the
 * recorder sd_rec.c, the SD card mode sd_mode.c) compiles unchanged. sdcard.c includes this file in sd_mmc.c's place
 * (SD_MMC_C, ui/opt_sd.h). What moved to fm1-base: the bus and its sessions (each plat_sd_* call takes the bus from
 * the LCD and gives it back: sdcard.c's sd_begin / sd_end do nothing here), the read modes and the DMA probe (fm1-base's
 * start-up; fm1_sd_mode / fm1_sd_dma stay for Optimist's code to read), the CRCs and their retries (fm1-base checks
 * every block's CRC16 and reads a failed one again; sd_crc_retry here lets the stream read it once more), the clock
 * (fm1-base detects it at plat_sd_init with Optimist's walk on the same steps: plat.h "The clock", device/sd.c; one step
 * down there at run time on a CRC failure): sd_clock_probe here does nothing, SD_CLOCK_KHZ() is
 * plat_sd_clock_khz() (SYSTEM > SD CARD's CLOCK, sdcard.c sd_clock_khz).
 *   Petit FatFs  (SDCARD without the SD card mode's layout: core/opt_pff.c) its disk_initialize, disk_readp (a block
 *                read whole into a buffer, the part asked copied: fm1-base reads whole blocks, each one's CRC checked)
 *                and disk_writep (the block gathered, written whole at its end), at the end of this file
 *   sd_rd_*      a multi-block read: plat_sd_read_open / plat_sd_read_next (a short wait a poll, as sd_rd_poll's few
 *                token bytes) / plat_sd_close
 *   sd_wr_*      the recorder's multi-block write: plat_sd_write_open, plat_sd_write_next (0 for the block: the card's
 *                busy asked without sending), plat_sd_close (the stop, its busy waited for) */
#if FELUCCA_SD_FATFS
#include "storage/sd/fatfs/ff.h"
#include "storage/sd/fatfs/diskio.h"
#else
#include "storage/sd/diskio.h"                  /* (Petit FatFs': sdcard.c included its pff.h) */
#endif

/* ---- what sdcard.c reads of hal/fm1_sd_bus.h and the LCD driver: the bus is fm1-base's */
enum { FM1_SD_DUPLEX = 0, FM1_SD_DIRRX = 1, FM1_SD_BITBANG = 2 };
#define FM1_SD_BAUD_SLOW 0u
#define FM1_SD_BAUD_FAST 1u
#define SD_BAUD() FM1_SD_BAUD_FAST                /* (sdcard.c's sessions: fm1-base's own, at the clock it detected) */
#define SD_CLOCK_RESET() ((void)0)                /* (plat_sd_init starts every card at the floor) */
#define SD_WDT() ((void)0)                        /* (sd_diskio.c: plat_sd_read and plat_sd_write feed it) */
#define SD_CLOCK_KHZ() plat_sd_clock_khz()        /* (0: no card, the plain pins, the host) */
/* fm1-base's steps are Optimist's (hal/fm1_sd_clock.h, the classic driver's): the counts and the floor here, every step
 * by optimist/tests/sd/sd_clock_steps.c (run.sh's sd run) */
#include "../hal/fm1_sd_clock.h"
_Static_assert(FM1_SD_NSTEP == PLAT_SD_NSTEP, "fm1-base's SD clock steps: as many as Optimist's");
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
static BYTE sd_crc_on = 1, sd_crc_bad;
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

/* a timeout: fm1-base's plat_sd_read and plat_sd_read_next stepped down already (device/sd.c sd_slip): the failure stands */
static BYTE sd_time_retry(DWORD t0)
{
    (void)t0;
    return 0;
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
    sd_rd.state = SDR_IDLE;
    sd_rd.multi = plat_sd_read_open((uint32_t)sector) == 0;
    if (sd_rd.multi)
        plat_sd_read_plan((uint32_t)n);              /* (the interrupt reader stops after them) */
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

static UINT sd_clock_probe(BYTE *buf, UINT n, BYTE force)   /* (fm1-base's plat_sd_init detected it) */
{
    (void)buf, (void)n, (void)force;
    return 0;
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

#if !FELUCCA_SD_FATFS
/* ---- Petit FatFs' entry points (storage/sd/diskio.h; Optimist's sd_mmc.c streams a block's bytes past, fm1-base
 * moves whole blocks): the last block read kept, so the reads of one sector's parts read the card once */
static BYTE sd_pblk[512] __attribute__((aligned(4)));
static DWORD sd_pblk_at = 0xFFFFFFFFu;           /* the sector sd_pblk holds (none: ~0) */
static UINT sd_pwc;                              /* a write under way: the bytes of its block still to come */
DSTATUS disk_initialize(void)
{
    sd_pblk_at = 0xFFFFFFFFu;
    sd_pwc = 0;
    return sd_card_init();
}
/* Read partial sector: count bytes from offset (offset + count <= 512); buff 0: skipped. A CRC failure read again
 * (sd_crc_retry, as sd_mmc.c sd_readp) */
DRESULT disk_readp(BYTE *buff, DWORD sector, UINT offset, UINT count)
{
    BYTE fails = 0;
    UINT i;
    if (offset + count > 512u)
        return RES_PARERR;
    if (sector != sd_pblk_at) {
        sd_pblk_at = 0xFFFFFFFFu;
        while (disk_readblocks(sd_pblk, sector, 1) != RES_OK)
            if (!sd_crc_retry(&fails))
                return RES_ERROR;
        sd_pblk_at = sector;
    }
    if (buff)
        for (i = 0; i < count; i++)
            buff[i] = sd_pblk[offset + i];
    return RES_OK;
}
/* Write partial sector (pff.c's protocol): buff 0, sc a sector: a write begins there; buff, sc bytes: the next of
 * its bytes; buff 0, sc 0: the block ends (the rest zeros), written whole */
static DWORD sd_pw_at;
DRESULT disk_writep(const BYTE *buff, DWORD sc)
{
    UINT n;
    if (buff) {
        for (n = (UINT)sc; n && sd_pwc; n--, sd_pwc--)
            sd_pblk[512u - sd_pwc] = *buff++;
        return RES_OK;
    }
    if (sc) {
        sd_pblk_at = 0xFFFFFFFFu;                /* (the buffer is the block being written now) */
        sd_pw_at = sc;
        sd_pwc = 512u;
        return RES_OK;
    }
    while (sd_pwc)
        sd_pblk[512u - sd_pwc--] = 0;
    return disk_writeblock(sd_pblk, sd_pw_at);
}
#endif
