/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The output's largest |sample| a half, for the master meter (Optimist's ui/meters.c meter_tap, AUDIO_HALF_PEAK): what
 * it read of a copy of the two I2S halves (abuf, 2 x HALF_WORDS words: 4 KB of RAM) is kept here as one word a half.
 * The same value: the half's HALF_WORDS samples, those past the render's n frames 0 as the copy had them, |x| as the
 * meter takes it. Included by core/opt_core.c (the render) and tests/half_peak_test.c */
#pragma once
#include <stdint.h>
/* the half from frame f of a render of n frames (lr: 2 n words) -> its largest |sample| */
static inline int32_t opt_half_peak(const int32_t *lr, uint32_t f, uint32_t n, uint32_t half_words)
{
    uint32_t i, end = 2u * n > 2u * f ? 2u * n - 2u * f : 0u;
    int32_t pk = 0;
    if (end > half_words)
        end = half_words;
    for (i = 0; i < end; i++) {
        int32_t a = lr[2u * f + i] < 0 ? -lr[2u * f + i] : lr[2u * f + i];
        if (a > pk)
            pk = a;
    }
    return pk;                                      /* (the zeros past n: |0| never above pk) */
}
