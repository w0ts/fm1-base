/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The SD card's clock steps: fm1-base's (device/sd_clock.h, the release's detection in device/sd.c) and Optimist's
 * (firmware/hal/fm1_sd_clock.h, its classic driver's, storage/sd/sd_mmc.c) the same table, entry by entry: the divider
 * of each step, the clock it makes, how many need CMD6's high speed, the floor (run.sh's sd run, after the build linked
 * Optimist's tree into optimist/build/optimist).
 *   cc -std=c99 -Wall -Wextra -Werror -Ioptimist/build/optimist/firmware/hal -Idevice -o T optimist/tests/sd/sd_clock_steps.c */
#include <stdio.h>
#include "fm1_sd_clock.h"
#include "sd_clock.h"

int main(void)
{
    unsigned k, bad = 0;
    if (FM1_SD_NSTEP != FM1B_SD_NSTEP || FM1_SD_NSTEP_HS != FM1B_SD_NSTEP_HS || FM1_SD_LSB_KHZ != FM1B_SD_LSB_KHZ ||
        FM1_SD_FLOOR_BAUD != FM1B_SD_STEP_BAUD[FM1B_SD_FLOOR]) {
        printf("sd_clock_steps: the counts differ: steps %u / %u, high speed %u / %u, lsb %u / %u, floor %u / %u\n",
               (unsigned)FM1_SD_NSTEP, (unsigned)FM1B_SD_NSTEP, (unsigned)FM1_SD_NSTEP_HS, (unsigned)FM1B_SD_NSTEP_HS,
               (unsigned)FM1_SD_LSB_KHZ, (unsigned)FM1B_SD_LSB_KHZ, (unsigned)FM1_SD_FLOOR_BAUD,
               (unsigned)FM1B_SD_STEP_BAUD[FM1B_SD_FLOOR]);
        return 1;
    }
    for (k = 0; k < FM1B_SD_NSTEP; k++)
        if (FM1_SD_STEP_BAUD[k] != FM1B_SD_STEP_BAUD[k] || FM1_SD_STEP_KHZ(k) != FM1B_SD_STEP_KHZ(k)) {
            printf("sd_clock_steps: step %u differs: BAUD %u (Optimist) / %u (fm1-base)\n", k,
                   (unsigned)FM1_SD_STEP_BAUD[k], (unsigned)FM1B_SD_STEP_BAUD[k]);
            bad++;
        }
    printf(bad ? "sd_clock_steps: FAILED\n" : "sd_clock_steps: the %u steps the same (Optimist's and fm1-base's)\n",
           (unsigned)FM1B_SD_NSTEP);
    return bad != 0;
}
