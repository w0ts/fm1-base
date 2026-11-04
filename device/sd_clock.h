/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD card's SPI clocks on the FM-1 (device/sd.c's clock detection; plat.h plat_sd_clock): SPI1's clock is
 * lsb / (BAUD + 1), lsb 60 MHz (felucca-bsp's fm1_sd_hw.h: BAUD 149 = 400 kHz, the card's start-up), BAUD a whole number,
 * so the clocks between the floor and the high-speed limit are these and no others:
 *   BAUD    1     2     3     4     5     6      7     8      9
 *   MHz    30    20    15    12    10    8.57   7.5   6.67   6 (the floor)
 * The steps, fastest first; the last is the floor (6 MHz: the black FM-1's first SD board reads there, where 20 and 30
 * gave garbage past the start-up). 30 MHz is past the default speed's 25: tried only on a card that took CMD6's high
 * speed (the first FM1B_SD_NSTEP_HS steps). 60 MHz (BAUD 0) is past high speed's 50 and never tried. No registers here:
 * the host tests use the table too. The same table as Optimist's firmware/hal/fm1_sd_clock.h (its own driver's, the
 * classic build's): optimist/tests/sd/sd_clock_steps.c compares the two entry by entry (optimist/run.sh's sd run). */
#pragma once
#include <stdint.h>

#define FM1B_SD_LSB_KHZ 60000u
#define FM1B_SD_NSTEP 9u
#define FM1B_SD_NSTEP_HS 1u                /* the first steps (30 MHz) only after CMD6 switched the card to high speed */
#define FM1B_SD_FLOOR (FM1B_SD_NSTEP - 1u) /* the last step: the clock every card starts its data at */
static const uint8_t FM1B_SD_STEP_BAUD[FM1B_SD_NSTEP] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
#define FM1B_SD_STEP_KHZ(k) (FM1B_SD_LSB_KHZ / (FM1B_SD_STEP_BAUD[k] + 1u))
