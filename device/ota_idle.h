/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* What every wait of the update and of the SysEx sender does while it waits (bsp chip/ota.c's ota_idle hook: ota_read's
 * tries, ota_wire_send's room in the ring, plat_fm1.c's stream packets): the watchdog fed. Their time limits all count
 * fm1_ms. Host-testable: tests/ota_stall_test.c.
 * The firmware supplies ota_idle_feed (the watchdog). */
#ifndef OTA_IDLE_H
#define OTA_IDLE_H

#include <stdint.h>

static void ota_idle_feed(void);

typedef struct {
    uint32_t ms, t24;                               /* fm1_ms last seen moving, and the crystal's count then */
    uint8_t on;
} ota_clock_t;

/* one wait's turn: ms the firmware's fm1_ms, t24 TIMER4's crystal count (fm1_ticks), t24_per_ms its ticks a ms */
static void ota_idle_do(ota_clock_t *c, uint32_t ms, uint32_t t24, uint32_t t24_per_ms)
{
    (void)c, (void)ms, (void)t24, (void)t24_per_ms;
    ota_idle_feed();
}

#endif
