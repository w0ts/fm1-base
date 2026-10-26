/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The trial app's side of Optimist's audio core (core/opt_core.c): what app.c calls. */
#pragma once
#include <stdint.h>

void opt_core_init(void);                                    /* power-on: the parts' default sounds, the drums, empty patterns */
void opt_core_render(int32_t *lr, uint32_t n, uint32_t master_q12, uint32_t keys);   /* n stereo frames (a multiple of
                                                              * opt_core_block()), 24-bit; keys: plat_keys() */
uint32_t opt_core_block(void);                               /* Optimist's mix block (CTL frames) */
void opt_core_midi(uint32_t pkt);                            /* a USB-MIDI packet in (channel messages, clock) */
void opt_core_panic(void);                                   /* every voice off */
uint32_t opt_core_voices(void);                              /* voices sounding (a check) */
const char *opt_core_engine(uint32_t part);                  /* the part's engine's name */
