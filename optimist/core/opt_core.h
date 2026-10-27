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
const int32_t *opt_core_capture(uint32_t n);                 /* the last render's USB capture: n frames of Optimist's 4
                                                              * channels, 24-bit; 0 = none (the computer not recording) */
void opt_core_midi(uint32_t pkt, int port);                  /* a USB-MIDI packet in (channel messages, clock), from PORT_* */
void opt_core_tick(void);                                    /* fm1-base's 1 kHz tick: the MIDI in (plat_midi_in), CLOCK OUT */
void opt_core_panic(void);                                   /* every voice off */
uint32_t opt_core_voices(void);                              /* voices sounding (a check) */
const char *opt_core_engine(uint32_t part);                  /* the part's engine's name */
void opt_core_dual_parts(uint32_t jobs);                     /* the host: a fixed set of jobs for the second core (bit p:
                                                              * part p, NPART the drums, NPART + 1 the reverb's kept block;
                                                              * 0 the split by load); core/opt_dual.c. No-op without DUAL */
int32_t opt_core_dual_stat(uint32_t k);                       /* the second core's figures for the host's checks: 0 up,
                                                              * 1 why (dual_mode.h DUAL_*), 2 jobs joined, 3 posted; -1 */

/* the UI (ui/opt_ui.c): a main loop pass of Optimist's (system/main.c) is opt_ui_frame; between frames opt_ui_input */
void opt_splash(void);                                       /* the boot screen: Optimist's logo and version */
void opt_ui_init(void);                                      /* the screen cleared, everything drawn at the next frame */
void opt_ui_frame(void);                                     /* the panel, the LEDs, the screen */
/* the web editor (ui/opt_editor.h): a SysEx frame from USB (the bytes between F0 and F7; 1 = the editor's), each main
 * loop pass (the mirror, DX7 SysEx, a restore's restart), every SysEx byte from USB (the USB interrupt: DX7 SysEx) */
int opt_ed_frame(const uint8_t *p, uint32_t n);
void opt_ed_poll(void);
void opt_sysex_byte(uint8_t b);
void opt_ui_input(void);                                     /* the panel only (and the meters) */
/* Bluetooth (ui/opt_bt.h): what fm1-base keeps (app.c app_ble_load / app_ble_save, APP_BLE_STORE: Optimist's settings
 * record's fields), and whether it starts at power-on (APP_BLE_AT_BOOT: SYSTEM > BT MIDI as saved) */
struct plat_ble_rec;
int opt_ble_load(struct plat_ble_rec *r);
int opt_ble_save(const struct plat_ble_rec *r);
int opt_ble_at_boot(void);

/* the host simulator's checks (app.c app_expect) */
uint32_t opt_ui_scr(void);                                   /* the screen shown (Optimist's SCR_*) */
uint32_t opt_ui_row(void);                                   /* its cursor row */
uint32_t opt_ui_track(void);                                 /* the track selected (0..3, 3 the drums) */
int32_t opt_ui_param(uint32_t track, uint32_t id);           /* a track's value (Optimist's P_*) */
void opt_ui_param_set(uint32_t track, uint32_t id, int32_t v);   /* the host: a track's value set as is (no hook) */
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
void opt_ui_ins(uint32_t track, uint32_t slot, uint32_t type);   /* insert type (fx_slots.c FXT_*) into a track's slot */
int32_t opt_ui_fxi(uint32_t track, uint32_t slot);           /* its instance (fxi.c FXI_*: 0 none, 1 OK, 2 WAIT, 3 FULL) */
int32_t opt_ui_beat(void);                                   /* the transport's beat (core.h clk_beat) */
void opt_ui_clock_out(uint32_t on);                          /* the HOME menu's CLOCK OUT */
int32_t opt_ui_guard(void);                                  /* the CPU guard's level (cpuguard.h CG_L_*; -1 not built) */
uint32_t opt_ui_drops(void);                                 /* late halves since power-on (overload.h ovl.drops) */
int32_t opt_ui_batt(void);                                   /* the battery's reading as SYSTEM > BATTERY has it */
int32_t opt_ui_uad(uint32_t s);                              /* USB AUDIO's source s (0 MASTER): its channels (3: 1+2); -1 none */
/* the globals' sectors 0xFC000..0xFEFFF: plat.h's PLAT_XDATA2 (app.c, on plat_data*) */
#define OPT_GLOB_LO 0xFC000u
#define OPT_GLOB_HI 0xFF000u
int opt_glob_erase(uint32_t off);
int opt_glob_write(uint32_t off, const void *src, uint32_t n);
const uint8_t *opt_glob_ptr(uint32_t off);
/* the canvas, the app's unit's (app.c: gfx.c's cv_px, n pixels or more; 0 = smaller), shared with this unit's gfx.c */
uint16_t *opt_canvas(uint32_t n);
/* the fonts this unit draws with, FONT_L and FONT_S: the app's unit's (app.c opt_font_l, opt_font_s; declared in
 * ui/opt_ui.c after device/gfx.c, whose felucca_font_t they are) */
/* the LCD, from the app's unit (app.c: the platform's lcd_fill / lcd_blit / lcd_sync) */
void opt_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
void opt_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
void opt_lcd_sync(void);

/* the companion's files (ui/opt_editor.h; app.c's APP_FILES): the backup's objects, the sample slots, BKINFO and BKCTL
 * (Optimist's io/editor/ed_comp.c), as app_file_* (0 / -1); none without the editor's companion part */
uint32_t opt_cf_count(void);
int opt_cf_info(uint32_t i, const char **name, uint32_t *kind, uint32_t *flags, uint32_t *size, uint32_t *room);
int opt_cf_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n);
int opt_cf_begin(uint32_t i, uint32_t size);
int opt_cf_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n);
int opt_cf_commit(uint32_t i);
int opt_cf_delete(uint32_t i);
int opt_cf_busy(void);
/* the companion's parameters (ui/opt_ui.c; app.c's APP_PARAMS): i 0 .. opt_cp_count() - 1 */
uint32_t opt_cp_count(void);
int opt_cp_desc(uint32_t i, uint32_t *id, uint32_t *group, int32_t *mn, int32_t *mx, int32_t *df, const char **name,
                const char *const **names);
int32_t opt_cp_get(uint32_t i);
int32_t opt_cp_set(uint32_t i, int32_t v);
