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
static uint32_t opt_sd_late(void);               /* ui/opt_sd.h: the frame held back for the card */
static void opt_sd_strip(void);                  /* ui/opt_sd.h: the card's turn between two of the screen's strips */
#if FELUCCA_SD_STREAM
#define OPT_FILL_ROWS 16u                        /* a fill between the card's turns (a page change's clear: 21 ms of the bus otherwise) */
static void opt_fill_rows(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t r, n;
    for (r = 0; r < h; r += n) {
        n = h - r < OPT_FILL_ROWS ? h - r : OPT_FILL_ROWS;
        opt_sd_strip();
        opt_lcd_fill(x, y + r, w, n, c);
    }
}
#else
#define opt_fill_rows opt_lcd_fill
#endif
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
    opt_fill_rows(x, y, w, h, c);
    mir_fill(x, y, w, h, c);
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (!w || !h)
        return;
    opt_sd_strip();
    opt_lcd_blit(x, y, w, h, px);
    mir_blit(x, y, w, h, px);
}
#else
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { opt_fill_rows(x, y, w, h, c); }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    opt_sd_strip();
    opt_lcd_blit(x, y, w, h, px);
}
#endif
/* one canvas for the two units: gfx.c's cv_px (59,520 B of the pool; 29,760 or 14,880 B with CANVAS_STRIP) is the app's unit's (app.c opt_canvas), this unit's
 * gfx.c a pointer to it (set at power-on: opt_core_init, before anything draws). Both units' gfx.c are the same file
 * with the same APP_CANVAS_PX (app.h gives none): opt_canvas answers 0 for a bigger one */
#define cv_px (*opt_cv)
/* one copy of the fonts: the faces this unit draws with (FONT_L, FONT_S) are the app's unit's (app.c opt_font_*: its
 * own splash and messages use the same two), reached through pointers. build/gen/felucca_font.h's own copies here go
 * by other names that nothing reads (static: the compiler leaves them out), like the faces neither unit uses */
#define FONT_L opt_font_L_unused
#define FONT_S opt_font_S_unused
#ifndef FELUCCA_FONT_CACHE                   /* (Optimist's builder item FONT_CACHE: 0 off, 1 small, 2 full; a build without it: full) */
#define FELUCCA_FONT_CACHE 2
#endif
#ifndef APP_FONT_CACHE                       /* the glyphs drawn lately kept decoded (device/gfx.c font_cached; full, 6 KB and 256
                                              * slots: a STEP or a page change's frame about half the instructions, host-measured):
                                              * this unit's, which draws the screens (the app's unit, the splash and messages: none) */
#if FELUCCA_FONT_CACHE >= 2
#define APP_FONT_CACHE 6144u
#define APP_FONT_CACHE_BITS 8
#elif FELUCCA_FONT_CACHE == 1                /* small: 1,536 B and 64 slots (1,920 B in all). Host, the source defaults, a
                                              * 30 s walk over four pages while playing: full decodes 11 % of the glyph
                                              * pixels drawn, small 28 %, none all of them (2026-11-01) */
#define APP_FONT_CACHE 1536u
#define APP_FONT_CACHE_BITS 6
#endif                                       /* (0: none, each glyph decoded as it is drawn) */
#endif
#include "gfx.c"                           /* fm1-base's (device/gfx.c): its fonts from build/gen/felucca_font.h */
#ifndef FELUCCA_CANVAS_STRIP
#define FELUCCA_CANVAS_STRIP 2
#endif
_Static_assert(CV_MAX == 240u * (FELUCCA_CANVAS_STRIP == 3 ? 16u : FELUCCA_CANVAS_STRIP == 2 ? 31u : FELUCCA_CANVAS_STRIP == 1 ? 62u : 124u),
               "the canvas: app.h's rows (opt_dim.h OPT_CANVAS_ROWS) are the build's CANVAS_STRIP (felucca_config.h): "
               "gen_optimist.py dim_header");
#define CV_ROWS (CV_MAX / 240u)                    /* (as Optimist's display/gfx.c: the UI's passes, op_graph.c cv_tall) */
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
#include "opt_supply.h"                          /* SYSTEM > INTERNALS' SUPPLY line: the rails fm1-base raised */
#include "opt_bt.h"                              /* Optimist's Bluetooth on fm1-base's plat_ble_* / plat_bta_* (before its screens) */
#include "opt_wifi.h"                            /* its Wi-Fi on fm1-base's own stack, plat_wifi_* / plat_link_* (before its screens) */
/* font_big (op_graph.c): FONT_L with FONT_S's range, made once, "made" read from .scale (Optimist's font has one,
 * fm1-base's has none): its height instead, as nonzero once copied (fm1-base's FONT_L has lower case with fm1.toml
 * font_full = ["L"], so the range it is given is its own) */
#define HPJ_PLAT plat_headphones                 /* (system/hpjack.h: the jack read and debounced by fm1-base) */
#define scale h
#include "ui/optimist/optimist.c"
#undef scale
#include "opt_bt_rec.h"                          /* the settings record's Bluetooth fields (after op_bt.c, before the storage) */
#include "opt_storage.h"                         /* Optimist's storage on fm1-base's flash access (after the UI: felucca.c's order) */
#include "opt_sd.h"                              /* its SD card on fm1-base's (after the storage: felucca.c's order) */
#include "opt_editor.h"                          /* Optimist's web editor on fm1-base's SysEx (after the storage: felucca.c's) */
#ifdef ED_HAS_WIFI
#include "io/editor/ed_wifi.c"                    /* cmd 112: the Wi-Fi networks for the web editor (felucca.c's place: after the editor) */
#endif
#if WIFI_UI
#include "ui/optimist/op_wifi.c"                  /* SYSTEM > WI-FI: the page and the password entry (on ui/opt_wifi.h's mirrors) */
#endif
#include "opt_console.h"                         /* its USB serial console on fm1-base's (USB_CONSOLE: after the editor) */
#include "opt_stats.h"                           /* its part of fm1-base's debug stats (app.h APP_STATS: after the UI) */
#include "opt_update.h"                          /* an update begins: the transport stops first (app.h APP_UPDATE_BEGIN) */
#include "opt_lg.h"                              /* its lines in fm1-base's LG readout (app.h APP_LOG: the memory) */
#include "opt_feat.h"                            /* the switchable features' hooks (core/feat_mem.c: after the UI it reads) */

/* ---- the boot screen (fm1-base's APP_SPLASH, app.c app_splash: before app_init): Optimist's own, the dinghy logo,
 * "OPTIMIST" and the version (ui/splash.c, always built in Optimist: main.c boot_splash), on this unit's canvas */
#include "ui/splash.c"
#define OPT_UI_PALETTE 4u                        /* MONO: the palette ui/panel.c settings_init always sets (no COLOR setting) */
void opt_splash(void)
{
    opt_cv_init();                               /* (the canvas: app_init has not run yet) */
    opt_palette_set(OPT_UI_PALETTE);             /* the name and the version are drawn in C_HI / C_AMB, this unit's pal[]
                                                  * (fm1-base's palette_set at boot sets its own unit's copy): zero until
                                                  * settings_init (opt_core_init, after the splash), so black on black.
                                                  * Optimist's own main.c runs settings_init before boot_splash */
    boot_splash();
}

/* ---- power-on (system/main.c: settings_init, panel_init, felucca_init without a stored project) */
void opt_core_init(void)
{
    uint32_t i;
    stk_boot();                                  /* the stacks below what is live marked (main.c fm1_main's first step): SYSTEM >
                                                  * INFO's RAM line reads how deep they go (Optimist's system/stackmark.h) */
#if FELUCCA_EDITOR && FELUCCA_ED_MIRROR
    st_blank = mir_test_boot == MIR_TEST_MAGIC;  /* the mirror's client asked for a test session (ed_mirror.c, 108 kind 5) */
    mir_test_boot = 0;                           /* (once: a crash in the session, and the next start is the user's) */
#endif
    opt_ed_forget();                             /* (the host's restart: what an FM-1's clears) */
    opt_bt_forget();
    cpu_khz = opt_cpu_khz();                     /* the CPU clock (main.c: before the audio), the guard's (core/opt_core.c) */
    opt_supply_boot();                           /* the rails read back (main.c supply_boot: before the rest; ui/opt_supply.h) */
    tab_expand();                                /* the smooth tables into RAM (core/opt_core.c TAB_CODED_FORCE), first */
    opt_cv_init();                               /* the canvas (the app's unit's), before anything draws */
    persist_boot();                              /* the settings record, the stores found (before settings_init) */
    opt_bt_boot();                               /* (its Bluetooth fields: ui/opt_bt.h, main.c ble_midi_init's order) */
    fm6_boot();
#if CZ_NUSER
    czb_find();
#endif
    settings_init();
    opt_con_boot();                              /* USB SERIAL: the console presented from this start (ui/opt_console.h) */
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
            trk_def_sound(t, i);                 /* (its preset, and T2's FM6 voice: as Optimist's main.c, model.c f51dbba9) */
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
 * JUNO FLNG XDLY on a track get an instance here, else they wait unheard); those a build leaves out are no-ops */
static void opt_ui_polls(void)
{
    fxm_poll();
    arb_poll();
    fxi_poll();
}

void opt_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    glo_last = SCR_GLOBAL;                       /* (GLO opens GLOBAL after a power-on; the host's reboot runs this again: RAM only, op_global.c) */
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
    opt_con_task();                              /* the USB serial console's line (ui/opt_console.h: main.c's cdc_task) */
#if FELUCCA_DUAL >= 2
    opt_dual_loop();                             /* (a main loop pass: core/opt_dual.c's starvation check) */
#endif
    opt_in_scan();
    fm1_ms = plat_ms();
    song.batt_raw = opt_batt_raw();               /* SYSTEM > BATTERY, the header's icon (main.c: its own reading) */
    {   int chg = plat_battery_charging();
        song.batt_chg = (uint8_t)(chg > 0 ? chg : 0);   /* INFO's header: 5 V / charging (-1: not read) */
    }
    song.cpu_q8 = plat_cpu_pct() * 256u / 100u;   /* SYSTEM > INFO's CPU LOAD (audio.c's cpu_q8: fm1-base's, the same
                                                   * smoothing over the whole render, USB audio's work in it) */
    opt_bt_poll();                               /* Bluetooth: the audio's view, the devices list, the mirror's frames out */
    ui_input();
    opt_ui_polls();
    ui_leds();
    opt_leds_out();
    if (!opt_sd_late())                          /* (a stream short of data: the card first, this frame not drawn, opt_sd.h) */
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
uint32_t opt_ui_cv_cuts(void) { return cv_cuts; }   /* (gfx.c: a canvas asked taller than CV_MAX, cut short) */
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
/* the items run's checks (tests/dev/items.txt): an insert type loaded into a slot with its amount, -1 when this build lacks the
 * type (fx_slots.c ins_amt: no amount); an engine put on a synth track by its name (the first built one that starts with
 * it, any case), its slot, -1 when this build lacks it */
int opt_ui_insert(uint32_t track, uint32_t slot, uint32_t type, int32_t amount)
{
    uint32_t a;
    if (track >= NTRK)
        return -1;
    ins_load(track, slot, type);
    a = ins_amt(track, slot);
    if (a == 0xFFu || ins_slot[track][slot & 1u] != type)
        return -1;
    trk[track].p[a] = (int16_t)amount;
    return (int)a;
}
int opt_ui_engine(uint32_t track, const char *name)
{
    uint32_t e, n;
    for (n = 0; name[n]; n++)
        ;
    for (e = 0; e < NENGINES && n; e++) {
        uint32_t i;
        for (i = 0; i < n; i++) {
            char c = ENGINES[e]->name[i], d = name[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            if (d >= 'a' && d <= 'z')
                d = (char)(d - 32);
            if (!c || c != d)
                break;
        }
        if (i == n && track < NTRK - 1u) {
            set_engine_of(&trk[track], e);
            return trk[track].eng_req == e ? (int)e : -1;   /* (the arena may refuse it: part_mem_reserve) */
        }
    }
    return -1;
}
const char *opt_ui_engine_name(uint32_t track) { return track < NTRK ? ENGINES[trk[track].eng_req % NENGINES]->name : ""; }
int32_t opt_ui_fxi(uint32_t track, uint32_t slot) { return (int32_t)fxi_state(track, slot); }   /* fxi.c FXI_*: 1 OK */
int32_t opt_ui_beat(void) { return (int32_t)clk_beat; }
void opt_ui_clock_out(uint32_t on)
{
#if FELUCCA_MIDI_CLKOUT
    clko_on = on ? CLKO_BOTH : CLKO_OFF;   /* (the editor's switch: USB and BLE, the old ON) */
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
#ifdef PLAT_HOST
int32_t opt_ui_lights(void) { return -1; }   /* (no LIGHTS levels: Optimist's KEYS page and lights_lvl are gone, b9a394bc) */
int32_t opt_ui_hp(void) { return hp_in; }                          /* the jack (system/hpjack.h hp_poll) */
int32_t opt_ui_oc(void) { return oc_last; }                        /* the output's correction (fx/outcorr.c) */
void opt_ui_lights_set(uint32_t v) { (void)v; }                   /* (as above: no levels to set) */
#if FELUCCA_LINK || (defined(FELUCCA_WIFI) && FELUCCA_WIFI)
void opt_ui_link_set(uint32_t v) { link_mode = (uint8_t)(v % 3u); }   /* SYNC > LINK as the menu's step sets it (the stored value) */
int32_t opt_ui_link(int eff) { return eff ? (int32_t)link_eff() : (int32_t)link_mode; }   /* the LINK in force (MODE JAM: OFF reads ON) / the stored one */
#else
void opt_ui_link_set(uint32_t v) { (void)v; }
int32_t opt_ui_link(int eff) { (void)eff; return -1; }
#endif
#ifdef OPT_HAS_USBH
int32_t opt_ui_usbh(void) { return (int32_t)usbh_mode(); }       /* SYSTEM > USB's MODE: 0 DEVICE, 1 HOST */
void opt_ui_usbh_reboot(void) { usbh_want = 0; usbh_said = 0; }   /* (a restart: RAM cleared, MODE DEVICE) */
#else
int32_t opt_ui_usbh(void) { return -1; }                         /* (not built) */
void opt_ui_usbh_reboot(void) {}
#endif
#endif
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
