/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD card on the LCD's bus (fm1.toml sd = true: FM1B_SD; plat.h plat_sd_*, felucca-bsp docs/SD.md): a card in
 * SPI mode through felucca-bsp's fm1_sd_hw.h. Each call takes the bus from the LCD (its running transfer finished, a
 * NOP sent so it ignores what it sees meanwhile) and gives it back at the LCD's clock; a read run (plat_sd_read_open) holds it
 * until plat_sd_close, a write run gives it back between its blocks (the card keeps a multi-block write open while
 * deselected). Every command carries its CRC7 and CMD59 turns the card's CRC check on; every data block's CRC16 is
 * checked on a read and sent on a write. The start-up finds the way SPI1 reads (fm1_sd_hw.h's modes, in turn, until
 * the card answers CMD0) and whether its DMA reads right (block 0 by bytes and by DMA, the CRC16s compared), at the
 * floor's clock; then the clock the card runs at (sd_clock_probe: a step of sd_clock.h, plat.h "The clock"), one step
 * down at run time when a block's CRC16 fails above the floor (sd_step_down). The same detection as Optimist's own
 * driver (its storage/sd/sd_mmc.c sd_clock_probe, the classic build's), on the same steps. */
#include "sd_clock.h"
_Static_assert(FM1B_SD_NSTEP == PLAT_SD_NSTEP, "sd_clock.h: plat.h's PLAT_SD_NSTEP");

#define SD_CMD0 0u
#define SD_CMD6 6u
#define SD_CMD8 8u
#define SD_CMD9 9u
#define SD_CMD10 10u
#define SD_CMD12 12u
#define SD_CMD16 16u
#define SD_CMD17 17u
#define SD_CMD18 18u
#define SD_CMD24 24u
#define SD_CMD25 25u
#define SD_CMD55 55u
#define SD_CMD58 58u
#define SD_CMD59 59u
#define SD_ACMD41 (0x80u | 41u)
#define SD_TOKEN 0xFEu                 /* a data block's start (reads, CMD24) */
#define SD_TOKEN_MULTI 0xFCu           /* a block of a CMD25 run */
#define SD_TOKEN_STOP 0xFDu            /* a CMD25 run's end */
#define SD_MS(ms) ((uint32_t)(ms) * 1000u * FM1_TICKS_PER_US)

static struct {
    uint8_t up;                        /* started: answers commands */
    uint8_t block;                     /* block addressed (SDHC / SDXC); else bytes (SDSC) */
    uint8_t mode;                      /* FM1_SD_HW_*: how SPI1 reads */
    uint8_t dma;                       /* block data by DMA (proved at the start-up) */
    uint8_t run;                       /* SD_RUN_*: a multi-block read or write open */
    uint8_t on_bus;                    /* inside a session: the bus is the card's */
    uint32_t con;                      /* SPI1's CON to give back */
    uint32_t blocks;                   /* the card's size (CSD), 512-byte blocks */
    uint32_t t0;                       /* a write run's busy start */
    uint32_t crc_fails;                /* blocks whose CRC16 failed */
    uint16_t crc;                      /* the last block's CRC16 (the clock detection's fingerprint) */
    uint8_t v2;                        /* an SD v2 card (CMD8 answered): the clock detection's */
    uint8_t step;                      /* the clock every session runs at: a step of sd_clock.h, 0 the fastest */
    uint8_t top;                       /* the fastest step the detection saw pass (FM1B_SD_FLOOR: none above it) */
    uint8_t hs;                        /* CMD6 switched the card to high speed */
    uint8_t force;                     /* plat_sd_clock_set: 0 detect, else the step + 1 */
    uint8_t res[FM1B_SD_NSTEP];        /* PLAT_SD_STEP_*: what the detection saw at each step */
    uint32_t failed;                   /* the steps the detection saw fail */
    uint32_t probe_ms;                 /* its time */
    uint32_t steps_down;               /* run-time steps down for a CRC failure */
} sd;                                  /* (step, top: set by plat_sd_init before any call reads them) */
enum { SD_RUN_NONE = 0, SD_RUN_READ, SD_RUN_WRITE, SD_RUN_WBUSY };
static uint8_t sd_bounce[512] __attribute__((aligned(4)));   /* a caller's unaligned block, for the DMA */

static uint32_t sd_since(uint32_t t0) { return fm1_ticks() - t0; }

static uint16_t sd_crc16_byte(uint16_t c, uint8_t b)   /* CRC16-CCITT (x^16 + x^12 + x^5 + 1), from 0 */
{
    uint32_t i;
    c ^= (uint16_t)(b << 8);
    for (i = 0; i < 8u; i++)
        c = (uint16_t)(c & 0x8000u ? ((uint32_t)c << 1) ^ 0x1021u : (uint32_t)c << 1);
    return c;
}

static uint16_t sd_crc16(const uint8_t *p, uint32_t n)
{
    uint16_t c = 0;
    while (n--)
        c = sd_crc16_byte(c, *p++);
    return c;
}

static uint8_t sd_crc7(const uint8_t *p, uint32_t n)   /* x^7 + x^3 + 1, the command's last byte: CRC << 1 | 1 */
{
    uint32_t i, k;
    uint8_t c = 0;
    for (k = 0; k < n; k++)
        for (i = 0; i < 8u; i++) {
            uint8_t bit = (uint8_t)(((p[k] >> (7u - i)) ^ (c >> 6)) & 1u);
            c = (uint8_t)((c << 1) & 0x7Fu);
            if (bit)
                c ^= 0x09u;
        }
    return (uint8_t)(c << 1 | 1u);
}

static uint8_t sd_x(uint8_t b) { return fm1_sd_hw_xfer(b, sd.mode); }
static uint8_t sd_rx(void) { return fm1_sd_hw_xfer(0xFFu, sd.mode); }

static void sd_take(uint32_t baud)
{
    if (sd.on_bus)
        return;
    if (lcd_up)
        lcd_cmd(0x00);                               /* NOP: the LCD ignores the bytes it sees until the next command */
    else
        lcd_sync();
    sd.con = fm1_sd_hw_take(baud, sd.mode);
    sd.on_bus = 1;
}

static void sd_give(void)
{
    if (!sd.on_bus)
        return;
    fm1_sd_hw_give(sd.con, sd.mode);
    fm1_lcd_baud(LCD_BAUD);
    sd.on_bus = 0;
}

static void sd_release(void)                         /* the card deselected; 8 clocks so it lets go of DO */
{
    fm1_sd_hw_desel();
    (void)sd_rx();
}

static uint8_t sd_cmd(uint32_t cmd, uint32_t arg)    /* the R1 answer; 0xFF none */
{
    uint8_t pk[6], r;
    uint32_t i;
    if (cmd & 0x80u) {
        r = sd_cmd(SD_CMD55, 0);
        if (r > 1u)
            return r;
        cmd &= 0x7Fu;
    }
    if (cmd != SD_CMD12) {
        sd_release();
        fm1_sd_hw_sel();
        (void)sd_rx();
    }
    pk[0] = (uint8_t)(0x40u | cmd);
    pk[1] = (uint8_t)(arg >> 24);
    pk[2] = (uint8_t)(arg >> 16);
    pk[3] = (uint8_t)(arg >> 8);
    pk[4] = (uint8_t)arg;
    pk[5] = sd_crc7(pk, 5);
    for (i = 0; i < 6u; i++)
        (void)sd_x(pk[i]);
    if (cmd == SD_CMD12)
        (void)sd_rx();                               /* (a stuff byte) */
    for (i = 0; i < 10u; i++)
        if (!((r = sd_rx()) & 0x80u))
            return r;
    return 0xFFu;
}

static uint8_t sd_wait_byte(uint32_t ms)             /* the first byte other than 0xFF within ms (0xFF: none) */
{
    uint32_t t0 = fm1_ticks();
    uint8_t b;
    do
        if ((b = sd_rx()) != 0xFFu)
            return b;
    while (sd_since(t0) < SD_MS(ms));
    return 0xFFu;
}

static int sd_wait_ready(uint32_t ms)                /* the card's busy end (DO high again): 0 = ready */
{
    uint32_t t0 = fm1_ticks();
    do
        if (sd_rx() == 0xFFu)
            return 0;
    while (sd_since(t0) < SD_MS(ms));
    return -1;
}

static int sd_crc_in(uint16_t c)                     /* the data block's CRC16 after its bytes: 0 = c, -2 not */
{
    uint16_t got = (uint16_t)(sd_rx() << 8);
    got |= sd_rx();
    sd.crc = c;
    if (got != c) {
        sd.crc_fails++;
        return -2;
    }
    return 0;
}

static int sd_bytes_in(uint8_t *p, uint32_t n)       /* n data bytes by the CPU, then their CRC16: 0 = right */
{
    uint32_t i;
    uint16_t c = 0;
    for (i = 0; i < n; i++) {
        p[i] = sd_rx();
        c = sd_crc16_byte(c, p[i]);
    }
    return sd_crc_in(c);
}

static int sd_block_in(uint8_t *p, uint32_t dma)     /* a data block after its token: 0 = in, its CRC16 right */
{
    uint32_t i;
    if (dma && sd.mode != FM1_SD_HW_GPIO) {
        uint8_t *d = ((uintptr_t)p & 3u) ? sd_bounce : p;
        fm1_sd_hw_dma(d, 512u, 1);
        if (!fm1_sd_hw_dma_end())
            return -1;
        if (d != p)
            for (i = 0; i < 512u; i++)
                p[i] = d[i];
        return sd_crc_in(sd_crc16(p, 512u));
    }
    return sd_bytes_in(p, 512u);
}

static int sd_block_out(const uint8_t *p, uint8_t token)   /* a data block: 0 = the card took it (it is busy now) */
{
    uint32_t i;
    uint16_t c = sd_crc16(p, 512u);
    (void)sd_rx();
    (void)sd_x(token);
    if (sd.dma && sd.mode != FM1_SD_HW_GPIO) {
        const uint8_t *s = p;
        if ((uintptr_t)p & 3u) {
            for (i = 0; i < 512u; i++)
                sd_bounce[i] = p[i];
            s = sd_bounce;
        }
        fm1_sd_hw_dma(s, 512u, 0);
        if (!fm1_sd_hw_dma_end())
            return -1;
    } else {
        for (i = 0; i < 512u; i++)
            (void)sd_x(p[i]);
    }
    (void)sd_x((uint8_t)(c >> 8));
    (void)sd_x((uint8_t)c);
    return (sd_rx() & 0x1Fu) == 0x05u ? 0 : -2;      /* the data response: 05 taken, 0B its CRC, 0D a write error */
}

static uint32_t sd_addr(uint32_t blk) { return sd.block ? blk : blk * 512u; }

static uint32_t sd_csd_blocks(void)                  /* the size from the CSD (v2: C_SIZE x 1024; v1: by its factors) */
{
    uint8_t csd[16];
    uint32_t i, n = 0;
    if (sd_cmd(SD_CMD9, 0) != 0 || sd_wait_byte(100) != SD_TOKEN)
        return 0;
    for (i = 0; i < 16u; i++)
        csd[i] = sd_rx();
    (void)sd_rx();
    (void)sd_rx();
    if (csd[0] >> 6 == 1u) {
        n = (((uint32_t)(csd[7] & 0x3Fu) << 16 | (uint32_t)csd[8] << 8 | csd[9]) + 1u) << 10;
    } else {
        uint32_t size = ((uint32_t)(csd[6] & 3u) << 10 | (uint32_t)csd[7] << 2 | csd[8] >> 6) + 1u;
        uint32_t mult = ((uint32_t)(csd[9] & 3u) << 1 | csd[10] >> 7) + 2u, len = csd[5] & 15u;
        n = size << (mult + len - 9u);
    }
    return n;
}

static int sd_start(void)                            /* in a session at the start-up clock: 0 = up */
{
    uint32_t i, t0;
    uint8_t r7[4], ocr[4], v2;
    sd.up = sd.block = 0;
    fm1_sd_hw_desel();
    for (i = 0; i < 10u; i++)
        (void)sd_rx();                               /* 80 clocks, deselected: the card's power-up */
    if (sd_cmd(SD_CMD0, 0) != 1u)
        return PLAT_SD_NOCARD;
    sd.v2 = v2 = sd_cmd(SD_CMD8, 0x1AAu) == 1u;
    if (v2) {
        for (i = 0; i < 4u; i++)
            r7[i] = sd_rx();
        if (r7[2] != 1u || r7[3] != 0xAAu)
            return PLAT_SD_ERR;                      /* not at 2.7-3.6 V */
    }
    t0 = fm1_ticks();
    while (sd_cmd(SD_ACMD41, v2 ? 1u << 30 : 0u) != 0)
        if (sd_since(t0) >= SD_MS(1000))
            return PLAT_SD_ERR;
    if (v2) {
        if (sd_cmd(SD_CMD58, 0) != 0)
            return PLAT_SD_ERR;
        for (i = 0; i < 4u; i++)
            ocr[i] = sd_rx();
        sd.block = (ocr[0] & 0x40u) != 0;
    }
    if (!sd.block && sd_cmd(SD_CMD16, 512u) != 0)
        return PLAT_SD_ERR;
    (void)sd_cmd(SD_CMD59, 1u);                      /* the CRCs checked (a card that refuses works without) */
    sd_release();
    fm1_sd_hw_baud(FM1B_SD_STEP_BAUD[FM1B_SD_FLOOR]);   /* (the data at the floor: sd_clock_probe picks the clock) */
    sd.blocks = sd_csd_blocks();
    sd_release();
    sd.up = 1;
    return 0;
}

static int sd_read_one(uint32_t blk, uint8_t *p, uint32_t dma)   /* CMD17, in a session */
{
    int r = -1;
    if (sd_cmd(SD_CMD17, sd_addr(blk)) == 0 && sd_wait_byte(100) == SD_TOKEN)
        r = sd_block_in(p, dma);
    sd_release();
    return r;
}

static void sd_dma_probe(void)                       /* block 0 by bytes, then by DMA: DMA only when they agree */
{
    uint8_t *a = sd_bounce;                          /* (the unaligned blocks' page: free here, aligned, so the DMA
                                                      * reads into it directly) */
    uint16_t c;
    sd.dma = 0;
    if (sd.mode == FM1_SD_HW_GPIO || sd_read_one(0, a, 0) != 0)
        return;
    c = sd_crc16(a, 512u);
    if (sd_read_one(0, a, 1) == 0 && sd_crc16(a, 512u) == c)
        sd.dma = 1;
}

/* ---- the clock (plat.h "The clock"; Optimist's sd_mmc.c sd_clock_probe, the same walk). In a session, the card up at
 * the floor: sd_clk_pass reads, at the clock set, the CSD and the CID (each block's CRC16; the register's own CRC7, its
 * last byte, goes into the fingerprint as a flag beside its bytes), SD_CLK_SINGLES blocks by CMD17 spread over the card
 * and n from block 0 by one CMD18 (each block's CRC16), by the path the card runs on (sd.dma), into sd_bounce: the
 * first failure's PLAT_SD_STEP_*, and the pass's fingerprint (a CRC16 over the registers' bytes and every block's
 * CRC16: a line stuck low reads zeros with a right CRC16, and differs). A failure sets the floor before the CMD12. */
#define SD_CLK_SINGLES 8u
#define SD_CLK_RUN 16u                 /* the CMD18's blocks (fewer on a smaller card) */
/* The time (the watchdog: 8 s, main_fm1.c fm1_wdt_arm(0x0D), fed once a main loop pass; the card mounts inside one, at
 * boot before the loop). Every wait in the probe is SD_CLK_WAIT_MS (a token, a busy end), so a pass takes at most
 * (2 registers + 8 blocks + 16) x 20 ms + 2 busy ends ~ 0.56 s however slowly the card answers, and returns at its first
 * failure; no step starts after SD_CLK_BUDGET_MS (the floor's pass and CMD6 inside it). The walk ends at the first
 * failure. Worst case, every wait at its limit, from the probe's start: steps until 1 s; the last one's two passes 1.12 s;
 * its recovery (CMD12 0.02, a pass 0.56, the start-up again: ACMD41 1 s + the CSD 0.1, a pass 0.56) 2.24 s; the check at
 * the step kept 0.56 s; that failing, a recovery again 2.24 s: ~7.2 s in all, with the watchdog fed at every pass and
 * before each start-up again, so ~1.7 s at most between two feeds (tests/sd_clock_test.c measures both on its slow card).
 * On time, a card reaches the top in 17 passes of ~26 reads each and fails no step */
#define SD_CLK_WAIT_MS 20u
#define SD_CLK_BUDGET_MS 1000u

static void sd_clk_set(uint32_t k) { fm1_sd_hw_baud(FM1B_SD_STEP_BAUD[k]); }

static uint16_t sd_sum16(uint16_t s, uint16_t v) { return sd_crc16_byte(sd_crc16_byte(s, (uint8_t)(v >> 8)), (uint8_t)v); }

static int sd_cmd6(uint32_t arg, uint8_t *st)        /* CMD6's switch status: 0 = its 64 bytes in, their CRC16 right */
{
    int r = -1;
    if (sd_cmd(SD_CMD6, arg) == 0 && sd_wait_byte(SD_CLK_WAIT_MS) == SD_TOKEN)
        r = sd_bytes_in(st, 64u);
    sd_release();
    return r;
}

/* 1 = the card is at high speed now. Asked first (mode 0, group 1 = 1): switched (mode 1) only when that status says
 * group 1 supports function 1 (bit 401: byte 13 bit 1) and would switch to it (bits 379:376 = 1: byte 16); then the
 * switch's own status says it did. Every status's CRC16 checked */
static int sd_switch_hs(void)
{
    uint8_t st[64];
    return sd_cmd6(0x00FFFFF1u, st) == 0 && (st[13] & 2u) && (st[16] & 15u) == 1u && sd_cmd6(0x80FFFFF1u, st) == 0 &&
           (st[16] & 15u) == 1u;
}

static uint32_t sd_reg_read(uint32_t cmd, uint16_t *sum)   /* the CSD (CMD9) or the CID (CMD10) */
{
    uint8_t r[16];
    uint32_t res = PLAT_SD_STEP_ERR, i;
    if (sd_cmd(cmd, 0) == 0 && sd_wait_byte(SD_CLK_WAIT_MS) == SD_TOKEN) {
        res = sd_bytes_in(r, 16u) ? PLAT_SD_STEP_CRC : PLAT_SD_STEP_PASS;
        for (i = 0; i < 16u; i++)
            *sum = sd_crc16_byte(*sum, r[i]);
        *sum = sd_crc16_byte(*sum, (uint8_t)(sd_crc7(r, 15u) == r[15]));
    }
    sd_release();
    return res;
}

static uint32_t sd_rd_crcs(uint32_t blk, uint32_t n, uint16_t *sum)   /* n blocks from blk: CMD17 for one, else CMD18 */
{
    uint32_t i, res = PLAT_SD_STEP_PASS;
    int r;
    if (sd_cmd(n == 1u ? SD_CMD17 : SD_CMD18, sd_addr(blk)) != 0) {
        sd_release();
        return PLAT_SD_STEP_ERR;
    }
    for (i = 0; i < n && res == PLAT_SD_STEP_PASS; i++) {
        if (sd_wait_byte(SD_CLK_WAIT_MS) != SD_TOKEN)
            res = PLAT_SD_STEP_ERR;
        else if ((r = sd_block_in(sd_bounce, sd.dma)) != 0)
            res = r == -2 ? PLAT_SD_STEP_CRC : PLAT_SD_STEP_ERR;
        else
            *sum = sd_sum16(*sum, sd.crc);
    }
    if (res != PLAT_SD_STEP_PASS)
        sd_clk_set(FM1B_SD_FLOOR);
    if (n > 1u && (sd_cmd(SD_CMD12, 0) & 0x80u || sd_wait_ready(SD_CLK_WAIT_MS)) && res == PLAT_SD_STEP_PASS)
        res = PLAT_SD_STEP_ERR;
    sd_release();
    return res;
}

static uint32_t sd_clk_pass(uint32_t n, uint16_t *sum)
{
    uint32_t i, r, span = sd.blocks / SD_CLK_SINGLES;
    fm1_wdt_feed();                                  /* (a pass: 0.56 s at most) */
    *sum = 0;
    if ((r = sd_reg_read(SD_CMD9, sum)) != PLAT_SD_STEP_PASS || (r = sd_reg_read(SD_CMD10, sum)) != PLAT_SD_STEP_PASS)
        return r;
    for (i = 0; i < SD_CLK_SINGLES; i++)
        if ((r = sd_rd_crcs((span * i + i * 7u) % sd.blocks, 1u, sum)) != PLAT_SD_STEP_PASS)
            return r;
    return sd_rd_crcs(0, n, sum);
}

static uint32_t sd_clk_try(uint32_t k, uint32_t n, uint16_t ref)   /* two passes at step k, each the floor's; the floor after */
{
    uint32_t r = PLAT_SD_STEP_PASS, i;
    uint16_t s;
    sd_clk_set(k);
    for (i = 0; i < 2u && r == PLAT_SD_STEP_PASS; i++)
        if ((r = sd_clk_pass(n, &s)) == PLAT_SD_STEP_PASS && s != ref)
            r = PLAT_SD_STEP_DIFF;
    sd_clk_set(FM1B_SD_FLOOR);
    return r;
}

/* after a failed step, at the floor: a CMD18 left running stopped, the card checked by a pass; failing, started again
 * (400 kHz, then the floor) and checked once more. 0: it no longer reads right at the floor */
static int sd_clk_recover(uint32_t n, uint16_t ref)
{
    uint16_t s;
    sd_release();
    fm1_sd_hw_sel();
    (void)sd_rx();
    (void)sd_cmd(SD_CMD12, 0);
    (void)sd_wait_ready(SD_CLK_WAIT_MS);
    sd_release();
    if (sd_clk_pass(n, &s) == PLAT_SD_STEP_PASS && s == ref)
        return 1;
    fm1_wdt_feed();                                  /* (the start-up again: ACMD41's 1 s at most) */
    fm1_sd_hw_baud(FM1_SD_HW_BAUD_INIT);
    sd.hs = 0;                                       /* (a start-up leaves the card at default speed) */
    if (sd_start() != 0)
        return 0;
    return sd_clk_pass(n, &s) == PLAT_SD_STEP_PASS && s == ref;
}

/* the walk, from the floor up: the reference at the floor, then each step in turn (two passes), CMD6 asked before 30 MHz
 * only. The first step that fails ends it: the clock is the step below that one, the fastest that passed, checked once
 * more there after the card was brought back at the floor (that check failing: the floor). No step failed (the top
 * reached, 30 MHz or, the card refusing high speed, 20; or the time spent, SD_CLK_BUDGET_MS): the step below the fastest
 * that passed, the margin no failure showed; so a card that reads everything runs at 20 MHz. sd.force: that step,
 * unchecked. SD v1, the plain pins (no clock to pick), a card of unknown size: the floor, nothing tried. sd.up 0 when
 * the card is lost */
static void sd_clock_probe(void)
{
    uint16_t ref, s;
    uint32_t k, r, n = sd.blocks < SD_CLK_RUN ? sd.blocks : SD_CLK_RUN, t0 = fm1_ticks();
    for (k = 0; k < FM1B_SD_NSTEP; k++)
        sd.res[k] = PLAT_SD_STEP_UNTRIED;
    sd.step = sd.top = FM1B_SD_FLOOR;
    sd.hs = 0;
    sd.failed = 0;
    if (sd.force) {
        sd.step = (uint8_t)(sd.force - 1u);
        if (sd.step < FM1B_SD_NSTEP_HS)
            sd.hs = (uint8_t)sd_switch_hs();
        return;
    }
    if (!sd.v2 || !sd.blocks || sd.mode == FM1_SD_HW_GPIO)
        return;
    r = sd.res[FM1B_SD_FLOOR] = (uint8_t)sd_clk_pass(n, &ref);
    sd.probe_ms = sd_since(t0) / SD_MS(1);
    if (r != PLAT_SD_STEP_PASS)
        return;
    for (k = FM1B_SD_FLOOR; k-- > 0u && sd_since(t0) < SD_MS(SD_CLK_BUDGET_MS);) {
        if (k < FM1B_SD_NSTEP_HS && !sd.hs && !(sd.hs = (uint8_t)sd_switch_hs())) {
            sd.res[k] = PLAT_SD_STEP_NOHS;
            break;
        }
        r = sd.res[k] = (uint8_t)sd_clk_try(k, n, ref);
        if (r == PLAT_SD_STEP_PASS) {
            sd.top = (uint8_t)k;
            continue;
        }
        sd.failed = 1;
        if (!sd_clk_recover(n, ref)) {
            sd.up = 0;
        } else if (sd.top != FM1B_SD_FLOOR) {        /* the step below the failure, checked once more */
            sd_clk_set(sd.top);
            r = sd_clk_pass(n, &s);
            sd_clk_set(FM1B_SD_FLOOR);
            if (r == PLAT_SD_STEP_PASS && s == ref)
                sd.step = sd.top;
            else if (!sd_clk_recover(n, ref))
                sd.up = 0;
        }
        break;
    }
    if (!sd.failed && sd.top != FM1B_SD_FLOOR)
        sd.step = (uint8_t)(sd.top + 1u);
    sd.probe_ms = sd_since(t0) / SD_MS(1);
}

static int sd_step_down(void)                        /* a CRC16 failed at run time: above the floor, one step down now */
{
    if (sd.step >= FM1B_SD_FLOOR)
        return 0;
    sd.step++;
    sd.steps_down++;
    sd_clk_set(sd.step);
    return 1;
}

static void sd_lcd(void)                             /* the LCD wants the bus: a session left open ends first */
{
    if (!sd.on_bus)
        return;
    if (sd.run == SD_RUN_READ) {
        (void)sd_cmd(SD_CMD12, 0);
        (void)sd_wait_ready(100);
        sd.run = SD_RUN_NONE;
    }
    sd_release();
    sd_give();
}

int plat_sd_init(void)
{
    static const uint8_t MODES[3] = {FM1_SD_HW_RX, FM1_SD_HW_DUPLEX, FM1_SD_HW_GPIO};
    uint32_t k, fails = sd.crc_fails;
    int r = PLAT_SD_NOCARD;
    sd_lcd();
    sd.run = SD_RUN_NONE;
    sd.step = sd.top = FM1B_SD_FLOOR;
    for (k = 0; k < 3u; k++) {
        sd.mode = MODES[k];
        sd_take(FM1_SD_HW_BAUD_INIT);
        r = sd_start();
        if (r == 0) {
            sd_dma_probe();
            sd_clock_probe();
            sd.crc_fails = fails;                    /* (the probes' failures are theirs: sd.res) */
            if (!sd.up)
                r = PLAT_SD_ERR;                     /* (lost at a step: no longer reads at the floor) */
        }
        sd_release();
        sd_give();
        if (r != PLAT_SD_NOCARD)
            break;
    }
    if (r) {
        sd.mode = FM1_SD_HW_RX;
        sd.step = FM1B_SD_FLOOR;
    }
    return r;
}

uint32_t plat_sd_clock_khz(void)
{
    return sd.up && sd.mode != FM1_SD_HW_GPIO ? FM1B_SD_STEP_KHZ(sd.step) : 0u;
}

void plat_sd_errs(uint32_t *crc_fails, uint32_t *steps_down)
{
    *crc_fails = sd.crc_fails;
    *steps_down = sd.steps_down;
}

#if FM1B_CDC                           /* (the console's alone: flash) */
void plat_sd_clock(plat_sd_clock_t *c)
{
    uint32_t k;
    c->khz = plat_sd_clock_khz();
    for (k = 0; k < FM1B_SD_NSTEP; k++) {
        c->step_khz[k] = FM1B_SD_STEP_KHZ(k);
        c->res[k] = sd.res[k];
    }
    c->step = sd.step;
    c->top = sd.top;
    c->forced = sd.force;
    c->hs = sd.hs;
    c->failed = sd.failed;
    c->probe_ms = sd.probe_ms;
    c->crc_fails = sd.crc_fails;
    c->steps_down = sd.steps_down;
}

void plat_sd_clock_set(uint32_t k) { sd.force = (uint8_t)(k <= FM1B_SD_NSTEP ? k : FM1B_SD_NSTEP); }
#endif

int plat_sd_present(void) { return sd.up; }

uint32_t plat_sd_blocks(void) { return sd.up ? sd.blocks : 0; }

static int sd_ready_for(uint32_t blk, uint32_t n)    /* a call may start: up, nothing open, inside the card */
{
    if (!sd.up)
        return PLAT_SD_NOCARD;
    if (sd.run != SD_RUN_NONE)
        return PLAT_SD_BUSY;
    if (sd.blocks && (blk >= sd.blocks || n > sd.blocks - blk))
        return PLAT_SD_ERR;
    return 0;
}

static int sd_fail(int r)                            /* a card that fails a call: gone (until plat_sd_init again) */
{
    if (r == -1 || r == PLAT_SD_ERR)
        sd.up = 0;
    return r < 0 ? PLAT_SD_ERR : r;
}

static int sd_read_run(uint32_t blk, uint8_t *p, uint32_t n, uint32_t *done)   /* CMD18 (CMD17 for one), in a session */
{
    int r = 0;
    *done = 0;
    if (n == 1u) {
        r = sd_read_one(blk, p, sd.dma);
        *done = r == 0;
        return r;
    }
    if (sd_cmd(SD_CMD18, sd_addr(blk)) != 0) {
        sd_release();
        return -1;
    }
    while (*done < n) {
        if (sd_wait_byte(100) != SD_TOKEN) {
            r = -1;
            break;
        }
        if ((r = sd_block_in(p + *done * 512u, sd.dma)) != 0)
            break;
        ++*done;
    }
    if ((sd_cmd(SD_CMD12, 0) & 0x80u || sd_wait_ready(100)) && !r)
        r = -1;
    sd_release();
    return r;
}

int plat_sd_read(uint32_t blk, void *p, uint32_t n)
{
    uint8_t *d = p;
    uint32_t i = 0, got, again = 0;
    int r = sd_ready_for(blk, n);
    if (r || !n)
        return r;
    fm1_wdt_feed();                                  /* (see plat.h: a pass over the card is many of these) */
    sd_take(FM1B_SD_STEP_BAUD[sd.step]);
    while (i < n) {
        r = sd_read_run(blk + i, d + i * 512u, n - i, &got);
        i += got;
        if (r == -2 && (sd_step_down() || again++ < 2u))   /* a block's CRC16 failed: read again from it, a step down
                                                      * above the floor, at the floor twice at most */
            continue;
        if (r)
            break;
    }
    sd_give();
    return r == -2 ? PLAT_SD_CRC : sd_fail(r);
}

int plat_sd_write(uint32_t blk, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    int r = sd_ready_for(blk, n);
    if (r || !n)
        return r;
    fm1_wdt_feed();                                  /* (see plat.h: a pass over the card is many of these) */
    sd_take(FM1B_SD_STEP_BAUD[sd.step]);
    if (n == 1u) {
        if (sd_cmd(SD_CMD24, sd_addr(blk)) != 0 || sd_block_out(s, SD_TOKEN) || sd_wait_ready(500))
            r = -1;
    } else if (sd_cmd(SD_CMD25, sd_addr(blk)) != 0) {
        r = -1;
    } else {
        for (i = 0; i < n && !r; i++)
            if (sd_block_out(s + i * 512u, SD_TOKEN_MULTI) || sd_wait_ready(500))
                r = -1;
        if (!r) {
            (void)sd_rx();
            (void)sd_x(SD_TOKEN_STOP);
            (void)sd_rx();
            if (sd_wait_ready(500))
                r = -1;
        }
    }
    sd_release();
    sd_give();
    return sd_fail(r);
}

int plat_sd_read_open(uint32_t blk)
{
    int r = sd_ready_for(blk, 1u);
    if (r)
        return r;
    sd_take(FM1B_SD_STEP_BAUD[sd.step]);
    if (sd_cmd(SD_CMD18, sd_addr(blk)) != 0) {
        sd_release();
        sd_give();
        return sd_fail(-1);
    }
    sd.run = SD_RUN_READ;
    return 0;
}

int plat_sd_read_next(void *p, uint32_t wait_us)
{
    uint32_t t0 = fm1_ticks();
    uint8_t b;
    int r;
    if (sd.run != SD_RUN_READ)
        return PLAT_SD_ERR;
    do {
        b = sd_rx();
        if (b == SD_TOKEN) {
            r = sd_block_in(p, sd.dma);
            if (r == -2)
                (void)sd_step_down();                /* (the caller reads it again: at the next step) */
            return r == -2 ? PLAT_SD_CRC : r ? PLAT_SD_ERR : 0;
        }
        if (b != 0xFFu)
            return PLAT_SD_ERR;                      /* an error token */
    } while (sd_since(t0) < wait_us * FM1_TICKS_PER_US);
    return PLAT_SD_BUSY;
}

int plat_sd_write_open(uint32_t blk)
{
    int r = sd_ready_for(blk, 1u);
    if (r)
        return r;
    sd_take(FM1B_SD_STEP_BAUD[sd.step]);
    r = sd_cmd(SD_CMD25, sd_addr(blk)) == 0 ? 0 : -1;
    sd_release();
    sd_give();
    if (!r)
        sd.run = SD_RUN_WRITE;
    return sd_fail(r);
}

static int sd_wbusy(void)                            /* a write run: 1 while the card writes the block before */
{
    if (sd.run != SD_RUN_WBUSY)
        return 0;
    fm1_sd_hw_sel();
    if (sd_rx() == 0xFFu) {
        sd.run = SD_RUN_WRITE;
        return 0;
    }
    if (sd_since(sd.t0) >= SD_MS(500))
        return -1;
    return 1;
}

int plat_sd_write_next(const void *p)
{
    int r;
    if (sd.run != SD_RUN_WRITE && sd.run != SD_RUN_WBUSY)
        return PLAT_SD_ERR;
    sd_take(FM1B_SD_STEP_BAUD[sd.step]);
    r = sd_wbusy();
    if (r == 0 && p) {
        fm1_sd_hw_sel();
        r = sd_block_out(p, SD_TOKEN_MULTI);
        if (r == 0) {
            sd.run = SD_RUN_WBUSY;
            sd.t0 = fm1_ticks();
        }
    }
    sd_release();
    sd_give();
    if (r == 1)
        return PLAT_SD_BUSY;
    if (r) {
        sd.run = SD_RUN_NONE;
        return sd_fail(-1);
    }
    return 0;
}

int plat_sd_close(void)
{
    int r = 0;
    if (sd.run == SD_RUN_NONE)
        return 0;
    sd_take(FM1B_SD_STEP_BAUD[sd.step]);
    if (sd.run == SD_RUN_READ) {
        if (sd_cmd(SD_CMD12, 0) & 0x80u)
            r = -1;
        if (sd_wait_ready(100))
            r = -1;
    } else {
        fm1_sd_hw_sel();
        if (sd_wait_ready(500))
            r = -1;
        (void)sd_rx();
        (void)sd_x(SD_TOKEN_STOP);
        (void)sd_rx();
        if (sd_wait_ready(500))
            r = -1;
    }
    sd.run = SD_RUN_NONE;
    sd_release();
    sd_give();
    return sd_fail(r);
}

#if FM1B_CDC
int plat_sd_clock_try(uint32_t k)                    /* (the console's sd speed) */
{
    uint16_t ref;
    uint32_t n = sd.blocks < SD_CLK_RUN ? sd.blocks : SD_CLK_RUN, fails = sd.crc_fails;
    int r = sd_ready_for(0, 1u);
    if (r)
        return r;
    if (k >= FM1B_SD_NSTEP || !sd.blocks || sd.mode == FM1_SD_HW_GPIO)
        return PLAT_SD_ERR;
    sd_take(FM1B_SD_STEP_BAUD[FM1B_SD_FLOOR]);
    if (k < FM1B_SD_NSTEP_HS && !sd.hs && !(sd.hs = (uint8_t)sd_switch_hs())) {
        sd_give();
        sd.crc_fails = fails;
        return PLAT_SD_STEP_NOHS;
    }
    r = (int)sd_clk_pass(n, &ref);
    if (r == PLAT_SD_STEP_PASS && k != FM1B_SD_FLOOR && (r = (int)sd_clk_try(k, n, ref)) != PLAT_SD_STEP_PASS &&
        !sd_clk_recover(n, ref))
        sd.up = 0;
    sd_give();
    sd.crc_fails = fails;
    return r;
}
#endif
