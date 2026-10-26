/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The trial app's side of Optimist's unit (core/opt_core.c: the audio core, then its UI, ui/opt_ui.c): what app.c
 * calls, and what it gives the unit (the LCD, opt_lcd_*: app.c). */
#pragma once
#include <stdint.h>

void opt_core_init(void);                                    /* power-on: the parts' default sounds, the drums, empty patterns,
                                                              * the settings, the UI on its home screen (the mixer) */
void opt_core_render(int32_t *lr, uint32_t n, uint32_t master_q12, uint32_t keys);   /* n stereo frames (a multiple of
                                                              * opt_core_block()), 24-bit; keys: plat_keys() */
uint32_t opt_core_block(void);                               /* Optimist's mix block (CTL frames) */
void opt_core_midi(uint32_t pkt, int port);                  /* a USB-MIDI packet in (channel messages, clock), from PORT_* */
void opt_core_panic(void);                                   /* every voice off */
uint32_t opt_core_voices(void);                              /* voices sounding (a check) */
const char *opt_core_engine(uint32_t part);                  /* the part's engine's name */

/* the UI (ui/opt_ui.c): a main loop pass of Optimist's (system/main.c) is opt_ui_frame; between frames opt_ui_input */
void opt_ui_init(void);                                      /* the screen cleared, everything drawn at the next frame */
void opt_ui_frame(void);                                     /* the panel, the LEDs, the screen */
void opt_ui_input(void);                                     /* the panel only (and the meters) */

/* the host simulator's checks (app.c app_expect) */
uint32_t opt_ui_scr(void);                                   /* the screen shown (Optimist's SCR_*) */
uint32_t opt_ui_row(void);                                   /* its cursor row */
uint32_t opt_ui_track(void);                                 /* the track selected (0..3, 3 the drums) */
int32_t opt_ui_param(uint32_t track, uint32_t id);           /* a track's value (Optimist's P_*) */
int32_t opt_ui_bpm(void);
uint32_t opt_ui_playing(void);
int32_t opt_ui_cell(uint32_t k, const char **label, char *val, uint32_t n);   /* the cursor row's card k (0..3): its value
                                                              * as drawn (its form's), its label, its text into val */
void opt_ui_row_name(char *b);                               /* the cursor row's name (10 bytes) */

/* the stores (ui/opt_ui.c, after ui/opt_storage.h), for the host's checks */
int opt_st_up_used(uint32_t k);                              /* user preset slot k (0 = U01) holds a sound */
int opt_st_proj_used(uint32_t s);                            /* project slot s (0 = the first) holds a project */
uint32_t opt_st_panel_hits(void);                            /* storage.c's object sectors inside 0xD8000..0xD9FFF (0) */
int opt_st_word(uint32_t off, uint32_t *w);                  /* the flash word at off through st_read; 0 = read */
const char *opt_ui_msg(void);                                /* the header's last message */
/* the globals' sectors 0xFC000..0xFEFFF (app.c: felucca-bsp's driver; the host's model) */
#define OPT_GLOB_LO 0xFC000u
#define OPT_GLOB_HI 0xFF000u
int opt_glob_erase(uint32_t off);
int opt_glob_write(uint32_t off, const void *src, uint32_t n);
const uint8_t *opt_glob_ptr(uint32_t off);
/* the LCD, from the app's unit (app.c: the platform's lcd_fill / lcd_blit / lcd_sync) */
void opt_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
void opt_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
void opt_lcd_sync(void);
