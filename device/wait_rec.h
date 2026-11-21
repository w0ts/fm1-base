/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The waits that gave up (or stalled), kept where a restart finds them (.noinit: the firmware's wait_rec; a power
 * cycle clears it): the last WR_RING of them and a count per wait, read by `fm1_remote.py log` as
 *   stalled wait: <ID> after <ms> ms at stage <n> step <s> rc <rc> (ms <since boot> <snapshot fields>)
 *   stalled waits: <ID> <count> ... (the counts since the record began, nonzero ones only)
 * Written from the path that gave up (RAM only: never the flash from there). One format for every writer: the update's
 * waits (ota_idle.h's clock stall, a session that gave up), the SysEx sender's, the app's stop before an update, and a
 * restart a host asks for over EP0 (WR_USB_RESET_REQ, reserved for it). Host-testable: tests/ota_stall_test.c.
 * The snapshot (WR_SNAP words, wr_snap's order; WR_FIELD names them):
 *   t5    TIMER5's interrupts so far (fm1b_dbg.timer_irqs): stands still when TIMER5 stopped
 *   au    the render running (fm1b_dbg.in_audio: 1 inside it; usb_poll waits for 0)
 *   su rs EP0's setups and the bus resets (usb.c): moving = the poll ran
 *   rx tx EP1's packets in and out
 *   sxq   SysEx packets queued, not sent (so_w - so_r); sxw frames received, not taken (sx_made - sx_taken)
 *   fl    bit 0 sx_busy, 1 an app's stream open (sxs.on), 2 tx_deaf, 3 usb.config, 4 usb.suspended, 5 usb.ota_req,
 *         6 usb.uboot_req, 7 usb.up, 8 the card's read ahead on
 *   held  streams let go (sxs.held)
 *   irq   the interrupt controller's enable bits: bit 0 TIMER5, 1 ALNK0 (the audio), 2 SPI1 (the card) (0xF00 with
 *         them not known: a felucca-bsp without fm1_irq_enabled); bit 3 the writer's own interrupts on (icfg bit 9) */
#ifndef WAIT_REC_H
#define WAIT_REC_H

#include <stdint.h>

enum {
    WR_NONE,
    WR_OTA_CLOCK,                                   /* an update's wait: fm1_ms stood still (ota_idle.h) */
    WR_OTA_SESSION,                                 /* an update's session gave up (ota.c's rc; nothing written) */
    WR_SYSEX_TX,                                    /* a stream's packet found no room in PLAT_SYSEX_STALL_MS */
    WR_SYSEX_HOLD,                                  /* an app's stream let go after PLAT_SYSEX_HOLD_MS */
    WR_UPDATE_BEGIN_CAP,                            /* the app's stop before an update took PLAT_UPDATE_BEGIN_MS */
    WR_USB_RESET_REQ,                               /* a restart the host asked for over EP0 (reserved) */
    WR_MAIN_STUCK,                                  /* the main thread held FM1B_MAIN_STUCK_MS, TIMER5 running (main_watch.h) */
    WR_IDS
};
#define WR_RING 4u
#define WR_SNAP 11u
#define WR_MAGIC 0x57524331u                        /* "WRC1" */

typedef struct {
    uint8_t id, stage, step;
    int8_t rc;
    uint32_t elapsed_ms, at_ms;
    uint32_t s[WR_SNAP];
} wr_entry_t;

typedef struct {
    uint32_t magic, n;                              /* n: entries written (the ring holds the last WR_RING) */
    uint32_t count[WR_IDS];
    wr_entry_t e[WR_RING];
} wr_rec_t;

static const char *const WR_NAME[WR_IDS] = {"?", "OTA_CLOCK", "OTA_SESSION", "SYSEX_TX", "SYSEX_HOLD",
                                            "UPDATE_BEGIN_CAP", "USB_RESET_REQ", "MAIN_STUCK"};
static const char *const WR_FIELD[WR_SNAP] = {"t5", "au", "su", "rs", "rx", "tx", "sxq", "sxw", "fl", "held", "irq"};
#define WR_HEX_FIELDS ((1u << 8) | (1u << 10))      /* fl and irq in hex */

static void wr_init(wr_rec_t *r)                    /* at the start: a record from before a restart kept, else cleared */
{
    uint32_t i;
    if (r->magic == WR_MAGIC && r->n < 0x10000000u)
        return;
    for (i = 0; i < sizeof *r; i++)
        ((uint8_t *)r)[i] = 0;
    r->magic = WR_MAGIC;
}

static void wr_note(wr_rec_t *r, uint32_t id, uint32_t stage, uint32_t step, int32_t rc, uint32_t elapsed_ms,
                    uint32_t at_ms, const uint32_t *snap)
{
    wr_entry_t *e;
    uint32_t i;
    wr_init(r);
    if (id >= WR_IDS)
        id = WR_NONE;
    r->count[id]++;
    e = &r->e[r->n % WR_RING];
    e->id = (uint8_t)id, e->stage = (uint8_t)stage, e->step = (uint8_t)step;
    e->rc = (int8_t)(rc < -128 ? -128 : rc > 127 ? 127 : rc);
    e->elapsed_ms = elapsed_ms, e->at_ms = at_ms;
    for (i = 0; i < WR_SNAP; i++)
        e->s[i] = snap ? snap[i] : 0u;
    r->n++;
}

static char *wr_puts(char *q, char *end, const char *t)
{
    while (*t && q < end)
        *q++ = *t++;
    return q;
}
static char *wr_putu(char *q, char *end, uint32_t v, int hex)
{
    char d[12];
    uint32_t k = 0;
    if (hex)
        q = wr_puts(q, end, "0x");
    do {
        d[k++] = "0123456789ABCDEF"[hex ? v & 15u : v % 10u];
        v = hex ? v >> 4 : v / 10u;
    } while (v);
    while (k && q < end)
        *q++ = d[--k];
    return q;
}

/* entries in the ring, the oldest first: k 0 .. wr_entries()-1 */
static uint32_t wr_entries(const wr_rec_t *r) { return r->magic != WR_MAGIC ? 0u : r->n < WR_RING ? r->n : WR_RING; }

/* entry k's line into b (cap bytes with its 0): its length */
static uint32_t wr_line(const wr_rec_t *r, uint32_t k, char *b, uint32_t cap)
{
    const wr_entry_t *e;
    char *q = b, *end = b + cap - 1u;
    uint32_t i;
    if (!cap)
        return 0;
    if (k >= wr_entries(r)) {
        *b = 0;
        return 0;
    }
    e = &r->e[(r->n - wr_entries(r) + k) % WR_RING];
    q = wr_puts(q, end, "stalled wait: ");
    q = wr_puts(q, end, WR_NAME[e->id < WR_IDS ? e->id : 0]);
    q = wr_puts(q, end, " after ");
    q = wr_putu(q, end, e->elapsed_ms, 0);
    q = wr_puts(q, end, " ms at stage ");
    q = wr_putu(q, end, e->stage, 0);
    q = wr_puts(q, end, " step ");
    q = wr_putu(q, end, e->step, 0);
    q = wr_puts(q, end, " rc ");
    if (e->rc < 0)
        q = wr_puts(q, end, "-");
    q = wr_putu(q, end, (uint32_t)(e->rc < 0 ? -e->rc : e->rc), 0);
    q = wr_puts(q, end, " (ms ");
    q = wr_putu(q, end, e->at_ms, 0);
    for (i = 0; i < WR_SNAP; i++) {
        q = wr_puts(q, end, " ");
        q = wr_puts(q, end, WR_FIELD[i]);
        q = wr_puts(q, end, " ");
        q = wr_putu(q, end, e->s[i], (int)(WR_HEX_FIELDS >> i & 1u));
    }
    q = wr_puts(q, end, ")");
    *q = 0;
    return (uint32_t)(q - b);
}

/* the counts' line ("stalled waits: OTA_CLOCK 1 ..."): its length, 0 when none */
static uint32_t wr_counts(const wr_rec_t *r, char *b, uint32_t cap)
{
    char *q = b, *end = b + cap - 1u;
    uint32_t i, any = 0;
    if (!cap)
        return 0;
    *b = 0;
    if (r->magic != WR_MAGIC)
        return 0;
    q = wr_puts(q, end, "stalled waits:");
    for (i = 1; i < WR_IDS; i++) {
        if (!r->count[i])
            continue;
        any = 1;
        q = wr_puts(q, end, " ");
        q = wr_puts(q, end, WR_NAME[i]);
        q = wr_puts(q, end, " ");
        q = wr_putu(q, end, r->count[i], 0);
    }
    if (!any) {
        *b = 0;
        return 0;
    }
    *q = 0;
    return (uint32_t)(q - b);
}

#endif
