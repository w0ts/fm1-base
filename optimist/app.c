/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist on fm1-base, the trial app (milestone 2): fm1-base's platform, Optimist's audio core and UI. The render is
 * Optimist's (core/opt_core.c: engines, voices, drums, effects, sequencer); the screen, the panel and the LEDs are
 * Optimist's UI (ui/opt_ui.c) on fm1-base's canvas, plat_buttons / plat_keys / plat_enc and plat_lights. Both live in
 * one unit of their own (fm1.toml o2): this file is its door into the app's unit (device/unity.c, host/plat_host.c),
 * after the platform: gfx.c and lcd.c are in scope. */
#include "core/opt_core.h"

static volatile uint32_t blocks;         /* rendered (a check: the render runs) */
static volatile uint32_t keys_seen;      /* the keys as the last render saw them */
#ifdef PLAT_HOST
static FILE *render_pcm;                 /* the host's "pcm FILE": every sample as rendered (24-bit in int32, L R), raw */
#endif

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port))    /* USB, TRS (and Bluetooth): into Optimist's MIDI queue (its checks, its sources) */
        opt_core_midi(pkt, port);
    keys_seen = plat_keys();
    opt_core_render(lr, n, master_q12, keys_seen);
#ifdef PLAT_HOST
    if (render_pcm)
        fwrite(lr, sizeof *lr, 2u * n, render_pcm);
#endif
    blocks++;
}

void app_panic(void) { opt_core_panic(); }
/* the USB audio input's frames, Optimist's four channels (opt_core_render made them while the computer records) */
const int32_t *app_usb_capture(const int32_t *lr, uint32_t n)
{
    (void)lr;
    return opt_core_capture(n);
}

#ifdef PLAT_HOST
static int flash_load(const char *path, uint32_t *sectors);
#endif
void app_init(void)
{
#ifdef PLAT_HOST
    static uint8_t first = 1;                    /* OPT_FLASH=FILE: a power-on with that flash (a 1 MiB image at the FM-1's
                                                  * offsets, flash_dump's or Optimist's simulator's), before anything reads it */
    const char *img = getenv("OPT_FLASH");
    uint32_t n;
    if (first && img && img[0]) {
        if (flash_load(img, &n)) {
            printf("FAIL OPT_FLASH %s: no 1 MiB image there\n", img);
            exit(1);
        }
        printf("  ok power-on with the flash of %s (%u sectors)\n", img, (unsigned)n);
    }
    first = 0;
    if (getenv("OPT_DUAL_PARTS"))                /* the second core's fixed jobs (core/opt_dual.c: the host's ticks */
        opt_core_dual_parts((uint32_t)strtoul(getenv("OPT_DUAL_PARTS"), 0, 0));   /* measure no load to split by) */
#endif
    opt_core_init();
}

/* the platform's screen, for Optimist's unit (fm1-base's gfx.c is compiled there too, on this unit's canvas: opt_canvas;
 * the LCD is this unit's) */
uint16_t *opt_canvas(uint32_t n) { return n <= CV_MAX ? cv_px : 0; }
/* the two faces Optimist's unit draws with: this unit's (build/gen/felucca_font.h), one copy in the image */
const felucca_font_t *const opt_font_l = &FONT_L, *const opt_font_s = &FONT_S;
void opt_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { lcd_fill(x, y, w, h, c); }
void opt_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { lcd_blit(x, y, w, h, px); }
void opt_lcd_sync(void) { lcd_sync(); }

/* the platform's messages (safe mode, an update): over Optimist's screen, which draws itself again after */
void app_say(const char *a, const char *b)
{
    lcd_fill(0, 200, 240, 40, C_BLACK);
    draw_text_box(0, 204, 240, &FONT_S, a ? a : "", C_WHITE, 1);
    if (b)
        draw_text_box(0, 222, 240, &FONT_S, b, C_WHITE, 1);
}

/* the interrupts' global enable (icfg bit 9), for Optimist's unit (fm1_compat.h fm1_irq_off, core.h IRQ_IE_NOW) */
#ifndef PLAT_HOST
uint32_t opt_irq_enabled(void) { return (fm1_icfg() & 0x200u) != 0u; }
#endif

void app_ui_init(void) { opt_ui_init(); }
int app_sysex(const uint8_t *p, uint32_t n) { return opt_ed_frame(p, n); }
void app_sysex_byte(uint8_t b) { opt_sysex_byte(b); }
void app_loop(void) { opt_ed_poll(); }
void app_input_only(void) { opt_ui_input(); }
void app_frame(void) { opt_ui_frame(); }

/* fm1-base's companion services (app.h): the files and the parameters */
static const plat_file_t OPT_NOTES = {0x93000u, 0x94000u, 0x1000u};
#define OPT_SETTINGS 0xFC000u                    /* the settings record's copy A (opt_glob_ptr) */
#define OPT_KITS 0xDA000u                        /* the drum kit bank's copy A (ui/opt_storage.h ST_UKIT_SECTOR) */
static const uint8_t *opt_sector(uint32_t i)    /* the read-only files' bytes: a whole sector each */
{
    return i == 1u ? opt_glob_ptr(OPT_SETTINGS) : i == 2u ? plat_data(OPT_KITS) : 0;
}
int app_file(uint32_t i, plat_file_info_t *f)
{
    int n;
    switch (i) {
    case 0:
        n = plat_file_size(&OPT_NOTES);
        f->name = "notes.txt", f->kind = PLAT_FK_TEXT, f->size = n < 0 ? 0u : (uint32_t)n;
        f->flags = PLAT_FF_READ | PLAT_FF_WRITE | PLAT_FF_DELETE | PLAT_FF_PLAYING, f->room = OPT_NOTES.n - PLAT_FILE_HEAD;
        return 0;
    case 1:
    case 2:
        f->name = i == 1u ? "settings.bin" : "kits.bin", f->kind = i == 1u ? PLAT_FK_SETTINGS : PLAT_FK_BANK;
        f->flags = PLAT_FF_READ, f->size = f->room = opt_sector(i) ? 0x1000u : 0u;
        return 0;
    default:
        return -1;
    }
}
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n)
{
    const uint8_t *p = opt_sector(i);
    if (i == 0)
        return plat_file_read(&OPT_NOTES, off, dst, n);
    if (!p || off > 0x1000u || n > 0x1000u - off)
        return -1;
    memcpy(dst, p + off, n);
    return 0;
}
int app_file_begin(uint32_t i, uint32_t size) { return i == 0 ? plat_file_begin(&OPT_NOTES, size) : -1; }
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n)
{
    return i == 0 ? plat_file_write(&OPT_NOTES, off, src, n) : -1;
}
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc) { return i == 0 ? plat_file_commit(&OPT_NOTES, size, crc) : -1; }
int app_file_delete(uint32_t i) { return i == 0 ? plat_file_delete(&OPT_NOTES) : -1; }
int app_file_busy(void) { return opt_ui_playing() != 0; }

int app_param(uint32_t i, plat_param_t *p)
{
    uint32_t id, group;
    int32_t mn, mx, df;
    const char *name;
    const char *const *names;
    if (opt_cp_desc(i, &id, &group, &mn, &mx, &df, &name, &names))
        return -1;
    p->id = (uint16_t)id, p->group = (uint8_t)group, p->flags = 0;
    p->type = names ? PLAT_PT_ENUM : PLAT_PT_NUMBER;
    p->min = mn, p->max = mx, p->def = df, p->name = name, p->labels = names;
    p->unit = id == 0x100u ? "BPM" : 0;          /* (G_BPM: Optimist's F_BPM) */
    return 0;
}
const char *app_param_group(uint32_t g)
{
    static const char *const G[] = {"SONG", "TRACK 1", "TRACK 2", "TRACK 3", "DRUMS"};
    return g < sizeof G / sizeof G[0] ? G[g] : 0;
}
int32_t app_param_get(uint32_t i) { return opt_cp_get(i); }
int32_t app_param_set(uint32_t i, int32_t v) { return opt_cp_set(i, v); }

/* Optimist's globals, 0xFC000..0xFEFFF (its settings A/B, its autosave's copy B: ui/opt_storage.h): plat.h's fourth
 * window, PLAT_XDATA2 (fm1.toml xdata2 = true), on the device and in the host simulator alike. Here, in the app's
 * unit, for the UI's unit (core/opt_core.h): plat_data* with the window's bounds */
_Static_assert(OPT_GLOB_LO == PLAT_XDATA2_LO && OPT_GLOB_HI == PLAT_XDATA2_HI, "Optimist's globals are PLAT_XDATA2");
#if !FM1B_XDATA2
#error "optimist/fm1.toml: xdata2 = true (its globals are plat.h's PLAT_XDATA2)"
#endif
const uint8_t *opt_glob_ptr(uint32_t off)
{
    return plat_in(off, 1u, OPT_GLOB_LO, OPT_GLOB_HI) ? plat_data(off) : 0;
}
int opt_glob_erase(uint32_t off)
{
    return plat_in(off, 0x1000u, OPT_GLOB_LO, OPT_GLOB_HI) ? plat_data_erase(off) : -1;
}
int opt_glob_write(uint32_t off, const void *src, uint32_t n)
{
    return plat_in(off, n, OPT_GLOB_LO, OPT_GLOB_HI) ? plat_data_write(off, src, n) : -1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "voices"))
        *got = (int)opt_core_voices();
    else if (!strcmp(what, "blocks"))
        *got = (int)blocks;
    else if (!strcmp(what, "keys"))
        *got = (int)keys_seen;
    else if (!strcmp(what, "scr"))
        *got = (int)opt_ui_scr();
    else if (!strcmp(what, "row"))
        *got = (int)opt_ui_row();
    else if (!strcmp(what, "track"))
        *got = (int)opt_ui_track();
    else if (!strcmp(what, "bpm"))
        *got = (int)opt_ui_bpm();
    else if (!strcmp(what, "playing"))
        *got = (int)opt_ui_playing();
    else if (!strncmp(what, "gv", 2) && what[2] >= '1' && what[2] <= '4' && !what[3])   /* gvK: the cursor row's card K */
        *got = (int)opt_ui_cell((uint32_t)(what[2] - '1'), 0, 0, 0);
    else if (!strncmp(what, "p", 1) && what[1] >= '0' && what[1] <= '3' && what[2] == '.')   /* pT.ID: track T's value ID */
        *got = (int)opt_ui_param((uint32_t)(what[1] - '0'), (uint32_t)atoi(what + 3));
    else if (!strncmp(what, "up.", 3))           /* up.K: user preset UK (1 = U01) holds a sound */
        *got = opt_st_up_used((uint32_t)atoi(what + 3) - 1u);
    else if (!strncmp(what, "proj.", 5))         /* proj.S: project slot S (1 = the first) holds a project */
        *got = opt_st_proj_used((uint32_t)atoi(what + 5) - 1u);
    else if (!strncmp(what, "w.", 2)) {          /* w.ADDR: the flash's 32-bit word at ADDR (little-endian); -1 unread */
        uint32_t w;
        *got = opt_st_word((uint32_t)strtoul(what + 2, 0, 0), &w) ? -1 : (int)w;
    } else if (!strncmp(what, "uad.", 4))        /* uad.S: SYSTEM > USB AUDIO's source S (0 MASTER): its channels (3: 1+2) */
        *got = (int)opt_ui_uad((uint32_t)atoi(what + 4));
    else if (!strcmp(what, "panel_hits"))      /* Optimist's object sectors on OBJ_PANEL's 0xD8000..0xD9FFF */
        *got = (int)opt_st_panel_hits();
    else if (!strcmp(what, "c1up"))            /* the second core takes jobs (1); c1why: why not (dual_mode.h DUAL_*); */
        *got = (int)opt_core_dual_stat(0);     /* c1jobs: its jobs joined; c1posts: handed over (-1: no DUAL) */
    else if (!strcmp(what, "c1why"))
        *got = (int)opt_core_dual_stat(1);
    else if (!strcmp(what, "c1jobs"))
        *got = (int)opt_core_dual_stat(2);
    else if (!strcmp(what, "c1posts"))
        *got = (int)opt_core_dual_stat(3);
    else
        return 0;
    return 1;
}

/* the whole flash as Optimist's own simulator keeps it (firmware/sim: a 1 MiB image at the FM-1's offsets): what the
 * trial holds of it, the data windows (plat_data) and the globals (opt_glob_*); the rest of the image reads erased */
#define OPT_IMG_SIZE 0x100000u
static uint32_t app_fails;
static const uint8_t *img_at(uint32_t off)      /* a sector of the trial's flash, 0 = none */
{
    return PLAT_DATA_OK(off, 0x1000u) ? plat_data(off) : opt_glob_ptr(off);
}
static int flash_dump(const char *path)
{
    static uint8_t img[OPT_IMG_SIZE];
    uint32_t off;
    FILE *f = fopen(path, "wb");
    size_t n;
    for (off = 0; off < OPT_IMG_SIZE; off += 0x1000u) {
        const uint8_t *p = img_at(off);
        if (p)
            memcpy(&img[off], p, 0x1000u);
        else
            memset(&img[off], 0xFF, 0x1000u);
    }
    n = f ? fwrite(img, 1, sizeof img, f) : 0;
    if (f)
        fclose(f);
    return n == sizeof img ? 0 : -1;
}
static int flash_load(const char *path, uint32_t *sectors)
{
    static uint8_t img[OPT_IMG_SIZE];
    uint32_t off;
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(img, 1, sizeof img, f) : 0;
    if (f)
        fclose(f);
    if (n != sizeof img)
        return -1;
    *sectors = 0;
    for (off = 0; off < OPT_IMG_SIZE; off += 0x1000u) {
        int rc;
        if (!img_at(off))
            continue;                            /* (the app area, the update's staging, key_mac: not the trial's) */
        rc = PLAT_DATA_OK(off, 0x1000u) ? plat_data_erase(off) || plat_data_write(off, &img[off], 0x1000u)
                                        : opt_glob_erase(off) || opt_glob_write(off, &img[off], 0x1000u);
        if (rc)
            return -1;
        ++*sectors;
    }
    return 0;
}

int app_host_fails(void) { return (int)app_fails; }

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    char row[12], v[4][24];
    const char *l[4];
    uint32_t k;
    int32_t gv[4];
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "msg")) {                   /* msg [TEXT]: the header's last message (has TEXT, else a failure) */
        int bad = a && a[0] && !strstr(opt_ui_msg(), a);
        printf("%s msg \"%s\"%s%s\n", bad ? "FAIL" : "  ok", opt_ui_msg(), a && a[0] ? " has " : "", a ? a : "");
        app_fails += (uint32_t)bad;
        return 1;
    }
    if (!strcmp(cmd, "flash_dump") || !strcmp(cmd, "flash_load")) {   /* FILE: the 1 MiB image out, or in (then reboot) */
        int load = cmd[6] == 'l', rc;
        if (a && a[0] == '$')                    /* $NAME: the file the environment's NAME names */
            a = getenv(a + 1);
        rc = !a || !a[0] ? -1 : load ? flash_load(a, &k) : flash_dump(a);
        if (rc)
            printf("FAIL %s %s\n", cmd, a ? a : "");
        else if (load)
            printf("  ok flash_load %s: %u sectors\n", a, (unsigned)k);
        else
            printf("  ok flash_dump %s\n", a);
        app_fails += rc != 0;
        return 1;
    }
    if (!strcmp(cmd, "pcm")) {                   /* pcm FILE: the render's samples from now into FILE (a path as is); */
        if (render_pcm)                          /* pcm stop: closed */
            fclose(render_pcm);
        render_pcm = a && a[0] && strcmp(a, "stop") ? fopen(a, "wb") : 0;
        if (a && a[0] && strcmp(a, "stop") && !render_pcm) {
            printf("FAIL pcm %s: not written\n", a);
            app_fails++;
        }
        return 1;
    }
    if (!strcmp(cmd, "param")) {                 /* param T ID V: track T's value ID (Optimist's P_*) set to V, as is */
        if (!a || !b || !c || !a[0] || !b[0] || !c[0]) {
            printf("FAIL param T ID V\n");
            app_fails++;
            return 1;
        }
        opt_ui_param_set((uint32_t)atoi(a), (uint32_t)atoi(b), (int32_t)atoi(c));
        return 1;
    }
    if (strcmp(cmd, "cells"))
        return 0;
    opt_ui_row_name(row);
    for (k = 0; k < 4u; k++)
        gv[k] = opt_ui_cell(k, &l[k], v[k], sizeof v[k]);
    printf("  screen %u track %u row %u %s:", (unsigned)opt_ui_scr(), (unsigned)opt_ui_track(), (unsigned)opt_ui_row(), row);
    for (k = 0; k < 4u; k++)
        printf("  [%s %s (%d)]", l[k], v[k], (int)gv[k]);
    printf("\n");
    return 1;
}
#endif
