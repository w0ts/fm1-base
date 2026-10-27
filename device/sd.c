/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD card on the LCD's bus (fm1.toml sd = true: FM1B_SD; plat.h plat_sd_*, felucca-bsp docs/SD.md): a card in
 * SPI mode through felucca-bsp's fm1_sd_hw.h. Each call takes the bus from the LCD (its running transfer finished, a
 * NOP sent so it ignores what it sees meanwhile) and gives it back at the LCD's clock; a read run (plat_sd_read_open) holds it
 * until plat_sd_close, a write run gives it back between its blocks (the card keeps a multi-block write open while
 * deselected). Every command carries its CRC7 and CMD59 turns the card's CRC check on; every data block's CRC16 is
 * checked on a read and sent on a write. The start-up finds the way SPI1 reads (fm1_sd_hw.h's modes, in turn, until
 * the card answers CMD0) and whether its DMA reads right (block 0 by bytes and by DMA, the CRC16s compared). */

#define SD_CMD0 0u
#define SD_CMD8 8u
#define SD_CMD9 9u
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
} sd;
enum { SD_RUN_NONE = 0, SD_RUN_READ, SD_RUN_WRITE, SD_RUN_WBUSY };
static uint8_t sd_bounce[512] __attribute__((aligned(4)));   /* a caller's unaligned block, for the DMA */

static uint32_t sd_since(uint32_t t0) { return fm1_ticks() - t0; }

static uint16_t sd_crc16_byte(uint16_t c, uint8_t b)   /* CRC16-CCITT (x^16 + x^12 + x^5 + 1), from 0 */
{
    uint32_t i;
    c ^= (uint16_t)(b << 8);
    for (i = 0; i < 8u; i++)
        c = (uint16_t)(c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1);
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

static int sd_block_in(uint8_t *p, uint32_t dma)     /* a data block after its token: 0 = in, its CRC16 right */
{
    uint32_t i;
    uint16_t c = 0, got;
    if (dma && sd.mode != FM1_SD_HW_GPIO) {
        uint8_t *d = ((uintptr_t)p & 3u) ? sd_bounce : p;
        fm1_sd_hw_dma(d, 512u, 1);
        if (!fm1_sd_hw_dma_end())
            return -1;
        if (d != p)
            for (i = 0; i < 512u; i++)
                p[i] = d[i];
        c = sd_crc16(p, 512u);
    } else {
        for (i = 0; i < 512u; i++) {
            p[i] = sd_rx();
            c = sd_crc16_byte(c, p[i]);
        }
    }
    got = (uint16_t)(sd_rx() << 8);
    got |= sd_rx();
    if (got != c) {
        sd.crc_fails++;
        return -2;
    }
    return 0;
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
    v2 = sd_cmd(SD_CMD8, 0x1AAu) == 1u;
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
    fm1_sd_hw_baud(FM1_SD_HW_BAUD_RUN);
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
    static uint8_t a[512] __attribute__((aligned(4)));
    uint16_t c;
    sd.dma = 0;
    if (sd.mode == FM1_SD_HW_GPIO || sd_read_one(0, a, 0) != 0)
        return;
    c = sd_crc16(a, 512u);
    if (sd_read_one(0, a, 1) == 0 && sd_crc16(a, 512u) == c)
        sd.dma = 1;
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
    uint32_t k;
    int r = PLAT_SD_NOCARD;
    sd_lcd();
    sd.run = SD_RUN_NONE;
    for (k = 0; k < 3u; k++) {
        sd.mode = MODES[k];
        sd_take(FM1_SD_HW_BAUD_INIT);
        r = sd_start();
        if (r == 0)
            sd_dma_probe();
        sd_release();
        sd_give();
        if (r != PLAT_SD_NOCARD)
            break;
    }
    if (r)
        sd.mode = FM1_SD_HW_RX;
    return r;
}

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
    sd_take(FM1_SD_HW_BAUD_RUN);
    while (i < n) {
        r = sd_read_run(blk + i, d + i * 512u, n - i, &got);
        i += got;
        if (r == -2 && again++ < 2u)                 /* a block's CRC16 failed: read again from it, twice at most */
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
    sd_take(FM1_SD_HW_BAUD_RUN);
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
    sd_take(FM1_SD_HW_BAUD_RUN);
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
    sd_take(FM1_SD_HW_BAUD_RUN);
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
    sd_take(FM1_SD_HW_BAUD_RUN);
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
    sd_take(FM1_SD_HW_BAUD_RUN);
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
