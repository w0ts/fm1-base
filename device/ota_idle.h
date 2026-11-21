/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* What every wait of the update and of the SysEx sender does while it waits (bsp chip/ota.c's ota_idle hook: ota_read's
 * tries, ota_wire_send's room in the ring, plat_fm1.c's stream packets): the watchdog fed, as long as the clock the
 * waits count still moves. Their time limits all count fm1_ms, which only TIMER5's interrupt moves; with TIMER5
 * stopped a wait never ended and, fed, the watchdog never fired: the FM-1 silent until a power cycle (usb_poll is
 * TIMER5's too). So: fm1_ms standing still for OTA_CLOCK_STALL_MS of TIMER4's crystal count (fm1_ticks, which runs
 * whatever the interrupts do) is a stall, and the wait hands over to ota_clock_stall (the firmware's: a record, then
 * the restart). A window with the interrupts off (a 4 KiB erase: ~0.4 s at most) stays well under it.
 * Host-testable: tests/ota_stall_test.c. The firmware supplies ota_idle_feed (the watchdog) and ota_clock_stall. */
#ifndef OTA_IDLE_H
#define OTA_IDLE_H

#include <stdint.h>

#ifndef OTA_CLOCK_STALL_MS
#define OTA_CLOCK_STALL_MS 2000u
#endif

static void ota_idle_feed(void);
static void ota_clock_stall(uint32_t stood_ms);   /* fm1_ms stood still stood_ms: never returns on the FM-1 */

typedef struct {
    uint32_t ms, t24;                               /* fm1_ms last seen moving, and the crystal's count then */
    uint8_t on;
} ota_clock_t;

/* one wait's turn: ms the firmware's fm1_ms, t24 TIMER4's crystal count (fm1_ticks), t24_per_ms its ticks a ms. The
 * first look, or fm1_ms moved: the moment kept. (t24 wraps after ~179 s at 24 MHz: a stall is seen long before) */
static void ota_idle_do(ota_clock_t *c, uint32_t ms, uint32_t t24, uint32_t t24_per_ms)
{
    if (!c->on || ms != c->ms) {
        c->on = 1;
        c->ms = ms;
        c->t24 = t24;
    } else if ((uint32_t)(t24 - c->t24) >= OTA_CLOCK_STALL_MS * t24_per_ms) {
        ota_clock_stall((uint32_t)(t24 - c->t24) / t24_per_ms);
        return;                                     /* (the host test's: nothing fed) */
    }
    ota_idle_feed();
}

#endif
