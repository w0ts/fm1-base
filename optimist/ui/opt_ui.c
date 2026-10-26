/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's UI (firmware/src/ui/optimist: its screens, pages, cursor, knobs, layers) in the trial's Optimist unit,
 * after its audio core (core/opt_core.c includes this file last: the UI reads and writes the core's state directly,
 * one unit as in Optimist's felucca.c). Optimist's files compile unchanged; around them:
 *   the screen  fm1-base's canvas (device/gfx.c, compiled in this unit) on the app's lcd_fill / lcd_blit (app.c
 *               opt_lcd_*: device/lcd.c on an FM-1, the simulator's framebuffer on the host), fm1-base's fonts
 *               (Terminus: run.sh sets X0X_FONTSET), Optimist's extras (opt_gfx.h)
 *   the panel   fm1-base's plat_buttons / plat_keys / plat_enc as Optimist's HAL gives them (opt_in.h: the matrix ids,
 *               the edges, the press times); the LEDs onto plat_lights / plat_lights_mid
 *   the stores  stand-ins (opt_stores.h): storage, the user presets, the projects are a later step; the rest left
 *               out (opt_stubs.h, and the builds' switches off: Bluetooth, the mirror, the editor, USB audio, CPU1)
 * The composition follows Optimist's host UI test (tests/ui_optimist_test.c) and its main loop (system/main.c). */

/* ---- the screen: fm1-base's canvas, its LCD in the app's unit */
static void lcd_sync(void) { opt_lcd_sync(); }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { opt_lcd_fill(x, y, w, h, c); }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { opt_lcd_blit(x, y, w, h, px); }
#include "gfx.c"                           /* fm1-base's (device/gfx.c): its fonts from build/gen/felucca_font.h */
#include "opt_gfx.h"

/* ---- the panel: Optimist's HAL (hal/fm1_input.h) on plat.h */
#include "opt_in.h"

/* ---- Optimist's UI side, in felucca.c's order */
#ifndef FELUCCA_KNOB_ACCEL
#define FELUCCA_KNOB_ACCEL 1                     /* (felucca.c's default: the knobs accelerate) */
#endif
#define palette_set opt_palette_set              /* (ui/panel.c settings_init: Optimist's palettes, opt_gfx.h) */
#include "ui/panel.c"
#undef palette_set
#include "opt_panel.h"                          /* fm1-base's panel in, the LEDs out (after panel.c: its map) */
#include "ui/knob_accel.h"
#include "core/model.c"
#include "drums/dsnd_desc.c"
#include "ui/meters.c"
#include "opt_stubs.h"                           /* the parts of Optimist not in the trial yet, as the UI sees them */
/* font_big (op_graph.c): FONT_L with FONT_S's range, made once, "made" read from .scale (Optimist's font has one,
 * fm1-base's has none): its height instead, as nonzero once copied (fm1-base's FONT_L has lower case with fm1.toml
 * font_full = ["L"], so the range it is given is its own) */
#define scale h
#include "ui/optimist/optimist.c"
#undef scale
#include "opt_stores.h"                          /* the stores the UI calls (after it: they answer with ui_message) */

/* ---- power-on (system/main.c: settings_init, panel_init, felucca_init without a stored project) */
void opt_core_init(void)
{
    uint32_t i;
    settings_init();
    panel_init();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    song.g[G_RSIZE] = RSIZE_NEW;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, trk_def_engine(i));
            apply_preset_to(t, trk_def_preset(i));
            t->p[P_REV] = 0;
            t->engine = t->eng_req;
        }
    }
    ins_defaults();                              /* (every track's insert slots; project.c's blank load does it on the FM-1) */
#if FELUCCA_MASTER_COMP
    for (i = 0; i < 3u * NTRK; i++)
        INS_CSET(i / 3u)[i % 3u] = GP[G_CRAT + i % 3u].def;
#endif
#if FELUCCA_ENG_WAVE
    for (i = 0; i < 64u && wt_wanted() >= 0 && wt_have != wt_wanted(); i++)
        wt_service();                            /* (WAVE's table: the main loop's work, done at once) */
#endif
    part_mem_boot();
#if FELUCCA_DRUMS
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    dsend_dry();
#endif
    song.sel = 0;
    song.master_q12 = 2048;
    song.g[G_VIEW] = (int16_t)settings.view;
    layers_init();
    go_home();
    ui.force = 1;
}

/* ---- the main loop's pass (system/main.c MAIN_LOOP): input, LEDs, the screen; between passes the input and meters */
void opt_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
}

void opt_ui_input(void)
{
    opt_in_scan();
    fm1_ms = plat_ms();
    ui_input();
    meter_tap();
}

void opt_ui_frame(void)
{
    usb.config = plat_battery() == 4;            /* (opt_stubs.h: a USB host powers the FM-1) */
    opt_in_scan();
    fm1_ms = plat_ms();
    ui_input();
    ui_leds();
    opt_leds_out();
    ui_draw();
    meter_tap();
#if FELUCCA_ENG_WAVE
    wt_service();
#endif
}

/* ---- the host simulator's checks (app.c app_expect): what the screen is on */
uint32_t opt_ui_scr(void) { return ui.scr; }
uint32_t opt_ui_row(void) { return ui.row[ui.scr]; }
uint32_t opt_ui_track(void) { return song.sel; }
int32_t opt_ui_param(uint32_t track, uint32_t id) { return track < NTRK && id < P_COUNT ? trk[track].p[id] : 0; }
int32_t opt_ui_bpm(void) { return song.g[G_BPM]; }
uint32_t opt_ui_playing(void) { return song.playing; }
/* the cursor row's cell k (the cards KNOB 1..4 edit): its value as its form draws it (cell_t gv), its label and text */
int32_t opt_ui_cell(uint32_t k, const char **label, char *val, uint32_t n)
{
    cell_t c;
    SCREENS[ui.scr].cell(ui.row[ui.scr], k % 4u, &c);
    if (label)
        *label = c.label ? c.label : "";
    if (val)
        str_cpy(val, c.val, n);
    return c.gv;
}
void opt_ui_row_name(char *b) { SCREENS[ui.scr].name(ui.row[ui.scr], b); }   /* (9 characters and the 0) */
