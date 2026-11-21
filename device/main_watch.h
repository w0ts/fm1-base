/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The main thread watched from TIMER5 (FM1B_MAIN_STUCK_MS, 0: off): the main loop's passes and the update's waits each
 * move a beat (main_beat); the beat standing still that long while TIMER5 runs means the main thread is held somewhere
 * else, the watchdog fed from there (a loop that feeds it and never ends), USB's poll alive but nothing answering over
 * MIDI. Then: a record (wait_rec.h WR_MAIN_STUCK: the stage and the update's step it was in) and, with
 * FM1B_MAIN_STUCK_RESET, a restart from the tick. Host-testable: tests/ota_stall_test.c. */
#ifndef MAIN_WATCH_H
#define MAIN_WATCH_H

#include <stdint.h>

typedef struct {
    uint32_t beat, t;                               /* the beat last seen moving, and fm1_ms then */
    uint8_t on, noted;
} main_watch_t;

/* each tick (or less often): 1 once the beat has stood still limit_ms (then not again until it moves) */
static int main_watch_due(main_watch_t *w, uint32_t beat, uint32_t ms, uint32_t limit_ms)
{
    if (!w->on || beat != w->beat) {
        w->on = 1;
        w->beat = beat;
        w->t = ms;
        w->noted = 0;
        return 0;
    }
    if (w->noted || (uint32_t)(ms - w->t) < limit_ms)
        return 0;
    w->noted = 1;
    return 1;
}

#endif
