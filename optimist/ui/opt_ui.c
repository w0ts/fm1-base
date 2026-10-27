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
 *   the stores  Optimist's own storage (opt_storage.h: its formats, its addresses) on fm1-base's plat_data windows and,
 *               its globals 0xFC000..0xFEFFF in the fourth (PLAT_XDATA2, app.c opt_glob_*); its power-on (persist_boot,
 *               autosave_resume) and its main loop's saves (autosave_tick, settings_poll, sections_flush, sec_service)
 *               here; the rest left out (opt_stubs.h, and the builds' switches off: Bluetooth, CPU1; the mirror, the
 *               editor and USB audio are in: opt_editor.h, core/opt_core.c)
 * The composition follows Optimist's host UI test (tests/ui_optimist_test.c) and its main loop (system/main.c). */

/* ---- the screen: fm1-base's canvas, its LCD in the app's unit */
static void lcd_sync(void) { opt_lcd_sync(); }
#if FELUCCA_ED_MIRROR
/* the screen mirrored (editor cmd 106, io/editor/ed_mirror.c, ui/opt_editor.h): what is drawn goes to the client too, as
 * Optimist's display/lcd.c sends it */
static uint8_t mir_mute;                /* 1: what is drawn now is not mirrored (a mixer meter not due: op_mixdraw.c) */
static uint32_t mir_meters_due(void);
static uint32_t mir_meters_off(void);
static void mir_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;                   /* (clipped as lcd.c clips it: the mirror gets what the panel shows) */
    if (y + h > 240u)
        h = 240u - y;
    opt_lcd_fill(x, y, w, h, c);
    mir_fill(x, y, w, h, c);
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (!w || !h)
        return;
    opt_lcd_blit(x, y, w, h, px);
    mir_blit(x, y, w, h, px);
}
#else
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { opt_lcd_fill(x, y, w, h, c); }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { opt_lcd_blit(x, y, w, h, px); }
#endif
/* one canvas for the two units: gfx.c's cv_px (69 KiB of the pool) is the app's unit's (app.c opt_canvas), this unit's
 * gfx.c a pointer to it (set at power-on: opt_core_init, before anything draws). Both units' gfx.c are the same file
 * with the same APP_CANVAS_PX (app.h gives none): opt_canvas answers 0 for a bigger one */
#define cv_px (*opt_cv)
/* one copy of the fonts: the faces this unit draws with (FONT_L, FONT_S) are the app's unit's (app.c opt_font_*: its
 * own splash and messages use the same two), reached through pointers. build/gen/felucca_font.h's own copies here go
 * by other names that nothing reads (static: the compiler leaves them out), like the faces neither unit uses */
#define FONT_L opt_font_L_unused
#define FONT_S opt_font_S_unused
#include "gfx.c"                           /* fm1-base's (device/gfx.c): its fonts from build/gen/felucca_font.h */
#undef FONT_L
#undef FONT_S
extern const felucca_font_t *const opt_font_l, *const opt_font_s;   /* (app.c) */
#define FONT_L (*opt_font_l)
#define FONT_S (*opt_font_s)
static void opt_cv_init(void) { opt_cv = (uint16_t (*)[CV_MAX])opt_canvas(CV_MAX); }
#if FELUCCA_ED_MIRROR
static void cv_mirror(uint32_t x, uint32_t y) { mir_blit(x, y, cv_w, cv_h, cv_px); }   /* (display/gfx.c: the canvas to the
                                                                                         * mirror's client alone) */
#endif
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
#include "opt_bt.h"                              /* Optimist's Bluetooth on fm1-base's plat_ble_* / plat_bta_* (before its screens) */
/* font_big (op_graph.c): FONT_L with FONT_S's range, made once, "made" read from .scale (Optimist's font has one,
 * fm1-base's has none): its height instead, as nonzero once copied (fm1-base's FONT_L has lower case with fm1.toml
 * font_full = ["L"], so the range it is given is its own) */
#define scale h
#include "ui/optimist/optimist.c"
#undef scale
#include "opt_bt_rec.h"                          /* the settings record's Bluetooth fields (after op_bt.c, before the storage) */
#include "opt_storage.h"                         /* Optimist's storage on fm1-base's flash access (after the UI: felucca.c's order) */
#include "opt_sd.h"                              /* its SD card on fm1-base's (after the storage: felucca.c's order) */
#include "opt_editor.h"                          /* Optimist's web editor on fm1-base's SysEx (after the storage: felucca.c's) */

/* ---- the boot screen (fm1-base's APP_SPLASH, app.c app_splash: before app_init): Optimist's own, the dinghy logo,
 * "OPTIMIST" and the version (ui/splash.c, always built in Optimist: main.c boot_splash), on this unit's canvas */
#include "ui/splash.c"
void opt_splash(void)
{
    opt_cv_init();                               /* (the canvas: app_init has not run yet) */
    boot_splash();
}

/* ---- power-on (system/main.c: settings_init, panel_init, felucca_init without a stored project) */
void opt_core_init(void)
{
    uint32_t i;
#if FELUCCA_EDITOR && FELUCCA_ED_MIRROR
    st_blank = mir_test_boot == MIR_TEST_MAGIC;  /* the mirror's client asked for a test session (ed_mirror.c, 108 kind 5) */
    mir_test_boot = 0;                           /* (once: a crash in the session, and the next start is the user's) */
#endif
    opt_ed_forget();                             /* (the host's restart: what an FM-1's clears) */
    opt_bt_forget();
    cpu_khz = opt_cpu_khz();                     /* the CPU clock (main.c: before the audio), the guard's (core/opt_core.c) */
    tab_expand();                                /* the smooth tables into RAM (core/opt_core.c TAB_CODED_FORCE), first */
    opt_cv_init();                               /* the canvas (the app's unit's), before anything draws */
    persist_boot();                              /* the settings record, the stores found (before settings_init) */
    opt_bt_boot();                               /* (its Bluetooth fields: ui/opt_bt.h, main.c ble_midi_init's order) */
    fm6_boot();
#if CZ_NUSER
    czb_find();
#endif
    settings_init();
    opt_sd_boot();                               /* the card's filesystem (main.c sd_mount_boot, after the settings) */
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
    autosave_resume();                           /* the project as it was left (project.c) */
    song.g[G_VIEW] = (int16_t)settings.view;
    song.g[G_SYNC] = sync_boot;
    layers_init();
    go_home();
    ui.force = 1;
#if FELUCCA_DUAL >= 2
    opt_dual_start();                            /* the second core, after the settings (2ND CORE): core/opt_dual.c */
#endif
}

/* ---- the main loop's pass (system/main.c MAIN_LOOP): input, LEDs, the screen; between passes the input and meters */
/* the battery as main.c reads it (song.batt_raw: ADC channel 3's slow average, its counts): fm1-base's own reading of
 * the same channel (plat_battery_raw, APP_BATTERY_RAW), SLOOP's bar thresholds and the volts from it (op_project.c) */
static int32_t opt_batt_raw(void) { return plat_battery_raw(); }
/* after every ui_input, as main.c does: the effects' lines and the other buffers in the arena (fx/fx_arena.c,
 * core/arena_bufs.c), the limited inserts' instances after a change of slots or a load (fx/insfx/fxi.c: CHRS ECHO
 * JUNO FLNG XDLY on a track get an instance here, else they wait unheard), the engines' code a part asked for
 * (core/overlay.c); those a build leaves out are no-ops */
static void opt_ui_polls(void)
{
    fxm_poll();
    arb_poll();
    fxi_poll();
    rto_poll();
}

void opt_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
}

void opt_ui_input(void)
{
#if FELUCCA_DUAL >= 2
    opt_dual_loop();                             /* (a main loop pass: core/opt_dual.c's starvation check) */
#endif
    opt_in_scan();
    fm1_ms = plat_ms();
    ui_input();
    opt_ui_polls();
    meter_tap();
    opt_sd_wait();                               /* (the card's runs and the recorder's blocks between passes) */
}

void opt_ui_frame(void)
{
#if FELUCCA_EDITOR
    opt_ed_usb();                                /* (opt_stubs.h: a computer has set the FM-1 up, plat_usb_session) */
#else
    usb.config = plat_battery() == 4;            /* (opt_stubs.h: a USB host powers the FM-1) */
#endif
    opt_ua_poll();                               /* (USB audio's counters, for SYSTEM > INFO: core/opt_core.c) */
#if FELUCCA_DUAL >= 2
    opt_dual_loop();                             /* (a main loop pass: core/opt_dual.c's starvation check) */
#endif
    opt_in_scan();
    fm1_ms = plat_ms();
    song.batt_raw = opt_batt_raw();               /* SYSTEM > BATTERY, the header's icon (main.c: its own reading) */
    song.cpu_q8 = plat_cpu_pct() * 256u / 100u;   /* SYSTEM > INFO's CPU LOAD (audio.c's cpu_q8: fm1-base's, the same
                                                   * smoothing over the whole render, USB audio's work in it) */
    opt_bt_poll();                               /* Bluetooth: the audio's view, the devices list, the mirror's frames out */
    ui_input();
    opt_ui_polls();
    ui_leds();
    opt_leds_out();
    ui_draw();
    opt_sd_poll();                               /* the card's streams and recorder (opt_sd.h) */
    autosave_tick();                             /* the working project into flash, when quiet */
    opt_sd_mode_poll();                          /* the SD card mode's card after a mount, a save left pending */
#if BP23_SET
    settings_poll();                             /* a setting changed from a page */
#endif
    sections_flush();                            /* live sections / the recorded song, when quiet */
#if SEC_LOGGED
    sec_service();                               /* the next section staged for the audio (sections.c) */
#endif
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
void opt_ui_param_set(uint32_t track, uint32_t id, int32_t v)   /* (the host's "param": a value as is, no hook) */
{
    if (track < NTRK && id < P_COUNT)
        trk[track].p[id] = (int16_t)v;
}
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

/* ---- the stores, for the host simulator's checks (app.c: expect up.K, proj.S, w.ADDR, panel_hits; "msg") */
int opt_st_up_used(uint32_t k) { return (int)prj_slot_used(PR_USER, k); }        /* (as PROJECT's keys light them) */
int opt_st_proj_used(uint32_t s) { return (int)prj_slot_used(PR_PROJECT, s); }
uint32_t opt_st_panel_hits(void) { return opt_storage_hits_panel(); }
int opt_st_word(uint32_t off, uint32_t *w)       /* the 32-bit little-endian word at off, as Optimist reads it */
{
    uint8_t b[4];
    if (st_read(off, b, 4u))
        return -1;
    *w = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}
const char *opt_ui_msg(void) { return ui.msg; }   /* the header's last message */
/* the parity checks (tests/dev/parity.txt): a track's insert slots and their instances, the transport's beat, CLOCK OUT,
 * the guard's level, the late halves */
void opt_ui_ins(uint32_t track, uint32_t slot, uint32_t type) { ins_load(track, slot, type); }   /* (the mixer's INSERT) */
int32_t opt_ui_fxi(uint32_t track, uint32_t slot) { return (int32_t)fxi_state(track, slot); }   /* fxi.c FXI_*: 1 OK */
int32_t opt_ui_beat(void) { return (int32_t)clk_beat; }
void opt_ui_clock_out(uint32_t on)
{
#if FELUCCA_MIDI_CLKOUT
    clko_on = (uint8_t)(on != 0);
#else
    (void)on;
#endif
}
int32_t opt_ui_guard(void)
{
#if FELUCCA_CPU_GUARD
    return cg.level;
#else
    return -1;
#endif
}
uint32_t opt_ui_drops(void) { return ovl.drops; }
int32_t opt_ui_batt(void) { return song.batt_raw; }      /* (SYSTEM > BATTERY's reading) */
#if FELUCCA_USB_AUDIO
int32_t opt_ui_uad(uint32_t s) { return s < UAS_N ? ua_dst[s] : -1; }   /* SYSTEM > USB AUDIO: source s's channels (UAD_*) */
#else
int32_t opt_ui_uad(uint32_t s) { (void)s; return -1; }
#endif

/* ---- the companion's parameters (app.c: fm1-base's APP_PARAMS, device/companion.c): a few of Optimist's own, as its
 * tables describe them (core/params.c GP, TP): the song's BPM and SWING, each track's LVL, PAN, MUTE, DLY, REV. A set
 * writes the value as the web editor's SET does (io/editor/editor.c ED_SET: clamped, the screen drawn again) */
#define OPT_CP_G 0xFFu                          /* (a global: song.g) */
static const uint8_t OPT_CP[][2] = {
    {OPT_CP_G, G_BPM}, {OPT_CP_G, G_SWING},
#define OPT_CP_TRK(t) {t, P_LEVEL}, {t, P_PAN}, {t, P_MUTE}, {t, P_DLY}, {t, P_REV}
    OPT_CP_TRK(0), OPT_CP_TRK(1), OPT_CP_TRK(2), OPT_CP_TRK(3),
};
#define OPT_CP_N (sizeof OPT_CP / sizeof OPT_CP[0])
uint32_t opt_cp_count(void) { return OPT_CP_N; }
/* parameter i: its id (globals 0x0100 + G_*, track t's (t + 2) << 8 | P_*), its group (0 the song, 1 + t a track), its
 * range, default, label and names (0: a number) -> 0; -1 past the last */
int opt_cp_desc(uint32_t i, uint32_t *id, uint32_t *group, int32_t *mn, int32_t *mx, int32_t *df, const char **name,
                const char *const **names)
{
    const param_desc_t *d;
    if (i >= OPT_CP_N)
        return -1;
    d = OPT_CP[i][0] == OPT_CP_G ? &GP[OPT_CP[i][1]] : &TP[OPT_CP[i][1]];
    *id = OPT_CP[i][0] == OPT_CP_G ? 0x100u + OPT_CP[i][1] : (OPT_CP[i][0] + 2u) << 8 | OPT_CP[i][1];
    *group = OPT_CP[i][0] == OPT_CP_G ? 0u : 1u + OPT_CP[i][0];
    *mn = d->min, *mx = d->max, *df = d->def, *name = d->label;
    *names = d->fmt == F_ENUM ? pd_names(d) : 0;
    return 0;
}
static int16_t *opt_cp_at(uint32_t i)
{
    return OPT_CP[i][0] == OPT_CP_G ? &song.g[OPT_CP[i][1]] : &trk[OPT_CP[i][0]].p[OPT_CP[i][1]];
}
int32_t opt_cp_get(uint32_t i) { return i < OPT_CP_N ? *opt_cp_at(i) : 0; }
int32_t opt_cp_set(uint32_t i, int32_t v)
{
    const param_desc_t *d;
    if (i >= OPT_CP_N)
        return 0;
    d = OPT_CP[i][0] == OPT_CP_G ? &GP[OPT_CP[i][1]] : &TP[OPT_CP[i][1]];
    *opt_cp_at(i) = (int16_t)clamp(v, d->min, d->max);
    ui.force = 1;
    return *opt_cp_at(i);
}
