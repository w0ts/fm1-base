/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* A read run's blocks taken in SPI1's interrupt (on; the console's `sd irq 0` turns it off to compare): the card streams on while the
 * main loop is held up by the audio render (ALNK0 at priority 3, up to 5.8 ms a block), instead of waiting, its clock
 * stopped, for the main loop's next poll. plat_sd_read_open starts it after the CMD18; plat_sd_read_next takes the
 * blocks it has; plat_sd_close and a slip stop it before any command goes out by the polled path.
 *
 * The interrupt (fm1_spi1_irq, FM1_IRQ_SPI1 at SDE_PRIO, above the render and the 10 kHz tick) runs a state machine on
 * SPI1's DMA, every transfer a multiple of 4 bytes into 4-byte aligned RAM:
 *   HUNT   SDE_HUNT bytes in; scanned for the start token 0xFE (an error token, b7 clear, ends the run: SDE_ERR). The
 *          bytes after the token are the block's head (in the slot, by the CPU); the rest of the block and its CRC16
 *          come by one DMA into the slot's tail (SDE_DATA). No token: another hunt, until SD_RUN_SILENT_MS.
 *   DATA   the slot is full. The DMA's rounding bytes past the CRC16 (0 to 3) are the next hunt's first bytes (carry).
 *          A free slot: the next hunt; none, or the run's planned blocks all in: SDE_FULL, the clock stops.
 * plat_sd_read_next, main loop: the oldest full slot's head and tail into the caller's block, the CRC16 checked there
 * (the chip's CRC block stays the main loop's), the slot freed, a paused engine started again. The run's planned length
 * (plat_sd_read_plan, from the stream's run) keeps the engine from reading into the block after the run, so the CMD12
 * goes out between blocks as it does today. SDE_SLOTS slots: a whole run is read while the main loop is held up.
 * Counters for `sd irq`: interrupts, hunts, the longest hunt (bytes before a token: the card's own wait, in bytes).
 * Measured on black, 2026-11-05 (hardware/NEXT-BOARD.md "Dense kits"): under a dense kit (render 3.9 of 5.8 ms) 8-block runs
 * 366 KB/s polled, 702 with this; the heavy kit 195 dry voices in 28 s polled, 0 with this. */
#define SDE_PRIO 5u                    /* ALNK0 3, TIMER5 4 */
#define SDE_HUNT 16u                   /* bytes a hunt: 10.7 us at 12 MHz between interrupts while the card is silent */
#define SDE_SLOTS 16u                  /* blocks read ahead of the main loop: a whole run (SST_RUN 8, 16 tried) while a render holds it; 2 slots gave
                                        * +17 % under heavy on black (2026-11-05): the engine paused after two blocks for every render */
#define SDE_TAIL 520u                  /* 514 + rounding, 4-byte multiple */
enum { SDE_OFF = 0, SDE_HUNTING, SDE_DATA, SDE_FULL, SDE_ERR };

typedef struct {
    uint8_t head[SDE_HUNT];            /* the block's first k bytes, from the hunt that found its token */
    uint8_t tail[SDE_TAIL] __attribute__((aligned(4)));   /* the rest, its CRC16, the rounding */
    uint32_t k, len;                   /* head bytes; the tail DMA's length */
} sde_slot_t;

static struct {
    volatile uint8_t state;
    volatile uint8_t err;              /* SDE_ERR: the byte that ended it (0: silence) */
    uint8_t on;                        /* the switch */
    uint8_t attached;                  /* fm1_irq_attach done */
    volatile uint32_t wr, rd;          /* slots filled, taken */
    uint32_t limit;                    /* blocks this run will take (plat_sd_read_plan; 0: no limit) */
    uint32_t t0;                       /* the hunt's start */
    uint32_t carry_n, hunt_n;          /* bytes carried into the next hunt; hunts for the block in progress */
    uint8_t carry[4];
    uint8_t hunt[SDE_HUNT] __attribute__((aligned(4)));
    sde_slot_t slot[SDE_SLOTS];
    uint32_t irqs, hunts, hunt_max, blocks, runs, errs, stops_in_data;   /* sd irq's numbers */
} sde = {.on = 1};

void isr_spi1(void);                                 /* the interrupt's entry (bsp fm1_isr.S: FM1_ISR isr_spi1, fm1_spi1_irq) */
void fm1_spi1_irq(void);

static void sde_stop_hw(void)                        /* no transfer in flight, the interrupt off */
{
    fm1_sd_hw_rx_stop();
    fm1_irq_mask(FM1_IRQ_SPI1);
}

/* the bytes b[0..n) in the hunt: a token starts the block's tail DMA, an error token ends the run, nothing: a hunt more
 * (SDE_ERR silence after SD_RUN_SILENT_MS). Interrupt or main loop with the interrupt off */
static void sde_feed(const uint8_t *b, uint32_t n)
{
    uint32_t i, k;
    sde_slot_t *s = &sde.slot[sde.wr % SDE_SLOTS];
    for (i = 0; i < n; i++) {
        if (b[i] == SD_TOKEN) {
            k = n - i - 1u;
            for (i++; i < n; i++)
                s->head[i - (n - k)] = b[i];
            s->k = k;
            s->len = (514u - k + 3u) & ~3u;
            sde.state = SDE_DATA;
            fm1_sd_hw_rx_start(s->tail, s->len);
            return;
        }
        if (b[i] != 0xFFu) {
            sde.err = b[i];
            sde.state = SDE_ERR;
            sde.errs++;
            sde_stop_hw();
            return;
        }
    }
    if (sd_since(sde.t0) >= SD_MS(SD_RUN_SILENT_MS)) {
        sde.err = 0;
        sde.state = SDE_ERR;
        sde.errs++;
        sde_stop_hw();
        return;
    }
    sde.hunts++;
    sde.hunt_n++;
    fm1_sd_hw_rx_start(sde.hunt, SDE_HUNT);
}

static void sde_hunt_start(void)                     /* the next block's hunt, from the carry (interrupt, or main loop with it off) */
{
    uint32_t n = sde.carry_n;
    sde.carry_n = 0;
    sde.hunt_n = 0;
    sde.t0 = fm1_ticks();
    sde.state = SDE_HUNTING;
    sde_feed(sde.carry, n);
}

void fm1_spi1_irq(void)
{
    sde_slot_t *s;
    uint32_t i, used;
    fm1_sd_hw_rx_ack();
    sde.irqs++;
    switch (sde.state) {
    case SDE_HUNTING:
        sde_feed(sde.hunt, SDE_HUNT);
        break;
    case SDE_DATA:
        s = &sde.slot[sde.wr % SDE_SLOTS];
        used = 514u - s->k;
        for (i = used; i < s->len; i++)
            sde.carry[sde.carry_n++] = s->tail[i];
        if (sde.hunt_n > sde.hunt_max)
            sde.hunt_max = sde.hunt_n;
        sde.wr++;
        sde.blocks++;
        if (sde.wr - sde.rd < SDE_SLOTS && (!sde.limit || sde.wr < sde.limit))
            sde_hunt_start();
        else
            sde.state = SDE_FULL;
        break;
    default:
        break;
    }
}

/* the vector, once, from plat_sd_init's first call (lcd_init at boot: before main_fm1.c's fm1_guard_lock_top write-protects
 * the vectors; attached at a run instead, the first run reset the FM-1 on the write guard, black, 2026-11-05). The source
 * stays masked (ICFG, outside the guard) until a run; the interrupts as they were (off at boot) */
static void sde_attach(void)
{
    uint32_t was_on = (fm1_icfg() & 0x200u) != 0u;
    if (sde.attached)
        return;
    fm1_irq_off();
    fm1_irq_attach(FM1_IRQ_SPI1, isr_spi1, SDE_PRIO);
    fm1_irq_mask(FM1_IRQ_SPI1);
    sde.attached = 1;
    if (was_on)
        fm1_irq_on();
}

static void sde_begin(void)                          /* after a CMD18 answered: the first block's hunt */
{
    if (!sde.on || !sde.attached || !sd.dma || sd.mode == FM1_SD_HW_GPIO)   /* (the DMA path as the start-up proved it) */
        return;
    fm1_irq_off();
    sde.wr = sde.rd = 0;
    sde.limit = 0;
    sde.carry_n = 0;
    sde.err = 0;
    sde.runs++;
    fm1_irq_unmask(FM1_IRQ_SPI1);
    sde_hunt_start();
    fm1_irq_on();
}

static void sde_end(void)                            /* the engine stopped, a transfer in flight waited out: the bus is the polled path's */
{
    uint32_t n;
    if (sde.state == SDE_OFF)
        return;
    fm1_irq_off();
    if (sde.state == SDE_HUNTING || sde.state == SDE_DATA) {
        if (sde.state == SDE_DATA)
            sde.stops_in_data++;
        for (n = 0; n < 1000000u && !fm1_sd_hw_rx_done(); n++)
            ;
    }
    sde_stop_hw();
    sde.state = SDE_OFF;
    sde.limit = 0;
    fm1_irq_on();
}

static int sde_take(uint8_t *p)                      /* the oldest full slot into p: 0 right, -2 its CRC16 wrong, 1 none yet */
{
    sde_slot_t *s;
    uint32_t i, k;
    uint16_t got;
    if (sde.rd == sde.wr)
        return 1;
    s = &sde.slot[sde.rd % SDE_SLOTS];
    k = s->k;
    for (i = 0; i < k; i++)
        p[i] = s->head[i];
    for (i = k; i < 512u; i++)
        p[i] = s->tail[i - k];
    got = (uint16_t)(s->tail[512u - k] << 8 | s->tail[513u - k]);
    sde.rd++;
    fm1_irq_off();
    if (sde.state == SDE_FULL && (!sde.limit || sde.wr < sde.limit))
        sde_hunt_start();
    fm1_irq_on();
    sd.crc = sd_crc16(p, 512u);
    if (got != sd.crc) {
        sd.crc_fails++;
        return -2;
    }
    return 0;
}

void plat_sd_read_plan(uint32_t n) { sde.limit = n; }

uint32_t plat_sd_irq(int v, uint32_t *c)             /* c[6]: irqs, hunts, longest hunt (bytes), blocks, runs, errors */
{
    if (v >= 0 && sde.state == SDE_OFF)
        sde.on = (uint8_t)(v != 0);
    if (c) {
        c[0] = sde.irqs;
        c[1] = sde.hunts;
        c[2] = sde.hunt_max * SDE_HUNT;
        c[3] = sde.blocks;
        c[4] = sde.runs;
        c[5] = sde.errs;
    }
    return sde.on;
}
