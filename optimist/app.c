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
    keys_seen = plat_keys();
    opt_core_render(lr, n, master_q12, keys_seen);
#ifdef PLAT_HOST
    if (render_pcm)
        fwrite(lr, sizeof *lr, 2u * n, render_pcm);
#endif
    blocks++;
}

/* the 1 kHz tick (APP_TICK, the timer interrupt): the MIDI in, USB, TRS (and Bluetooth), into Optimist's queue (its
 * checks, its sources, its arrival times); CLOCK OUT's packets when due (core/opt_core.c opt_core_tick) */
void app_tick(void) { opt_core_tick(); }

void app_panic(void) { opt_core_panic(); }
void app_update_begin(void) { opt_update_begin(); }   /* (before app_panic: the transport stops cleanly first) */
/* the frames Bluetooth audio takes from the render (APP_BT_FRAMES): the plain output, or silence for a render corrected
 * for the FM-1's own codec (SPK LOWCUT / HP BASS, Optimist's fx/outcorr.c) */
const int32_t *app_bt_frames(const int32_t *lr, uint32_t n)
{
    (void)n;
    return opt_core_bt_frames(lr);
}
/* the USB audio input's frames, Optimist's four channels (opt_core_render made them while the computer records) */
const int32_t *app_usb_capture(const int32_t *lr, uint32_t n)
{
    (void)lr;
    return opt_core_capture(n);
}

/* the USB name (fm1.toml usb_name_fn; felucca-bsp FELUCCA_USB_NAME_FN, asked each time a host reads it): "FM1-XXXX", the
 * MIDI device and its ports, and "FM1-XXXX In" / "FM1-XXXX Out" for the audio. XXXX as the Bluetooth name has it
 * (opt_dev_id: the kept address's, else the flash ID's, which is the address fm1-base makes when none is kept) */
int opt_dev_id(void);
const char *app_usb_name(void)
{
    static char name[9];
    plat_dev_name(name, opt_dev_id());
    return name;
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
#if FM1B_CRK_DIAG
/* crk-I (crackle hunt, a measurement build): the LCD writes started with the interrupts off (the global enable, icfg
 * bit 9): how many, and the last one's caller; fm1-base's log reads them (device/main_fm1.c log_frame, "crk lcd") */
uint32_t crk_lcd_off, crk_lcd_from;
uint32_t crk_lcd_boot;                           /* (crk-J: the boot's, the splash before the interrupts are on: main_fm1.c) */
static void crk_lcd_note(uint32_t ra)
{
#ifdef PLAT_HOST
    (void)ra;                                    /* (the host: no interrupts) */
#else
    if (!(fm1_icfg() & 0x200u)) {
        crk_lcd_off++;
        crk_lcd_from = ra;
    }
#endif
}
void opt_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    crk_lcd_note((uint32_t)(uintptr_t)__builtin_return_address(0));
    lcd_fill(x, y, w, h, c);
}
void opt_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    crk_lcd_note((uint32_t)(uintptr_t)__builtin_return_address(0));
    lcd_blit(x, y, w, h, px);
}
#else
void opt_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { lcd_fill(x, y, w, h, c); }
void opt_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { lcd_blit(x, y, w, h, px); }
#endif
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

/* the rails as fm1-base's start left them (device/main_fm1.c: M-VAVE's VDC14 7 and SYSVDD 15, the 1.3 V supply on its
 * DC-DC), for SYSTEM > INTERNALS' SUPPLY line (ui/opt_supply.h): VDC14 << 4 | SYSVDD (felucca-bsp's fm1_sys.h), and
 * P3_ANA_CON0 | CON5 << 8 | CON6 << 16 | CON9 << 24 in regs (Optimist's hal/fm1_supply.h fm1_supply_regs). The host:
 * the raised levels, on the DC-DC */
uint32_t opt_supply_read(uint32_t *regs)
{
#ifdef PLAT_HOST
    *regs = 0x0Fu << 24 | 0x07u << 16 | 0x08u;
    return 0x7Fu;
#else
    *regs = fm1_p33_read(0x00u) | (uint32_t)fm1_p33_read(0x05u) << 8 | (uint32_t)fm1_p33_read(FM1_P3_ANA_CON6) << 16 |
            (uint32_t)fm1_p33_read(FM1_P3_ANA_CON9) << 24;
    return fm1_vdc14() << 4 | fm1_sysvdd();
#endif
}

/* the panel's setup as M-VAVE's own firmware sets it and Optimist's display/lcd.c sends it (FM1 Open V13, app/board/
 * lcd.inc display_setup, GPL-3.0-only): the porch, the gate voltages, VCOM, the supplies, 60 Hz and both gammas.
 * fm1-base's lcd_init leaves them at the ST7789V's reset values (VCOMS 0x20 for this panel's 0x18, GCTRL 0x35 for 0x56,
 * VRHS 0x0B for 0x1F, its own gamma): a VCOM off the panel's flickers. Sent once, before the first frame (the splash) */
#ifndef PLAT_HOST
static const uint8_t OPT_LCD_PANEL[] = {
    0xB2, 5, 0x0C, 0x0C, 0x0C, 0x00, 0x33,   /* PORCTRL */
    0xB7, 1, 0x56,                           /* GCTRL: VGH / VGL */
    0xBB, 1, 0x18,                           /* VCOMS */
    0xC0, 1, 0x2C,                           /* LCMCTRL */
    0xC2, 1, 0x01,                           /* VDVVRHEN */
    0xC3, 1, 0x1F,                           /* VRHS */
    0xC4, 1, 0x20,                           /* VDVS */
    0xC6, 1, 0x0F,                           /* FRCTRL2: 60 Hz */
    0xD0, 2, 0xA6, 0xA1,                     /* PWCTRL1 */
    0xE0, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,   /* PVGAMCTRL */
    0xE1, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,   /* NVGAMCTRL */
};
static void opt_lcd_panel(void)
{
    uint32_t r, x;
    for (r = 0; r < sizeof OPT_LCD_PANEL; r += 2u + OPT_LCD_PANEL[r + 1u]) {
        lcd_cmd(OPT_LCD_PANEL[r]);
        lcd_sync();                              /* (lcd_small free: the last data byte sent) */
        for (x = 0; x < OPT_LCD_PANEL[r + 1u]; x++)
            lcd_small[x] = OPT_LCD_PANEL[r + 2u + x];   /* (the DMA reads RAM only) */
        lcd_data(lcd_small, OPT_LCD_PANEL[r + 1u]);
    }
    lcd_sync();
}
#endif
void app_splash(void)
{
#ifndef PLAT_HOST
    opt_lcd_panel();
#endif
    opt_splash();
}
void app_ui_init(void) { opt_ui_init(); }
int app_sysex(const uint8_t *p, uint32_t n) { return opt_ed_frame(p, n); }
void app_sysex_byte(uint8_t b) { opt_sysex_byte(b); }
void app_loop(void)
{
    opt_ed_poll();
    opt_wifi_loop();                             /* Wi-Fi: the page's mirrors of fm1-base's manager, Link's MIDI clock */
}
#ifdef APP_WIFI_STORE
/* the Wi-Fi networks kept and the mode (fm1-base's own Wi-Fi): Optimist's settings record's field (ui/opt_wifi.h) */
uint8_t *app_wifi_store(void) { return opt_wifi_store(); }
void app_wifi_store_dirty(void) { opt_wifi_dirty(); }
#endif
/* what Bluetooth keeps (APP_BLE_STORE): Optimist's settings record's fields (ui/opt_bt.h) */
int app_ble_load(struct plat_ble_rec *r) { return opt_ble_load(r); }
int app_ble_save(const struct plat_ble_rec *r) { return opt_ble_save(r); }
#ifdef APP_BLE_PEERS
/* the devices one by one (plat.h): to Optimist's own list (ui/opt_bt_dev.h), main loop */
void app_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi) { opt_ble_scan_report(pdu, n, rssi); }
void app_ble_device_keys(const struct plat_ble_keys *k) { opt_ble_device_keys(k); }
#endif
void app_input_only(void) { opt_ui_input(); }
void app_frame(void) { opt_ui_frame(); }

/* fm1-base's companion services (app.h): the files (Optimist's backup objects and sample slots: core/opt_core.h opt_cf_*,
 * ui/opt_editor.h) and the parameters */
uint32_t app_file_count(void) { return opt_cf_count(); }
int app_file(uint32_t i, plat_file_info_t *f)
{
    uint32_t kind, flags, size, room;
    if (opt_cf_info(i, &f->name, &kind, &flags, &size, &room))
        return -1;
    f->kind = (uint8_t)kind, f->flags = (uint8_t)flags, f->size = size, f->room = room;
    return 0;
}
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n) { return opt_cf_read(i, off, dst, n); }
int app_file_begin(uint32_t i, uint32_t size) { return opt_cf_begin(i, size); }
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n) { return opt_cf_write(i, off, src, n); }
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc) { (void)size, (void)crc; return opt_cf_commit(i); }
int app_file_delete(uint32_t i) { return opt_cf_delete(i); }
int app_file_busy(void) { return opt_cf_busy(); }

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
    else if (!strcmp(what, "cv_cuts"))           /* the canvases cut short, both units' (CANVAS_STRIP: none, every tall band in passes) */
        *got = (int)(opt_ui_cv_cuts() + cv_cuts);
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
    else if (!strncmp(what, "fxi.", 4) && what[4] >= '0' && what[4] <= '3' && what[5] == '.')   /* fxi.T.S: track T's */
        *got = (int)opt_ui_fxi((uint32_t)(what[4] - '0'), (uint32_t)atoi(what + 6));   /* slot S's instance (1 OK) */
    else if (!strcmp(what, "beat"))            /* the transport's beat */
        *got = (int)opt_ui_beat();
    else if (!strcmp(what, "guard"))           /* the CPU guard's level (-1: not built) */
        *got = (int)opt_ui_guard();
    else if (!strcmp(what, "drops"))           /* late halves since power-on (SYSTEM > INFO DROPS) */
        *got = (int)opt_ui_drops();
    else if (!strcmp(what, "batt"))            /* SYSTEM > BATTERY's reading (song.batt_raw) */
        *got = (int)opt_ui_batt();
    else if (!strcmp(what, "lights"))          /* SYSTEM > LIGHTS' level (0 OFF .. 3 HIGH; -1 not built) */
        *got = (int)opt_ui_lights();
    else if (!strcmp(what, "link_eff"))        /* SYNC > LINK in force (MODE JAM turns OFF into ON) */
        *got = (int)opt_ui_link(1);
    else if (!strcmp(what, "link_stored"))     /* SYNC > LINK as stored (JAM never touches it) */
        *got = (int)opt_ui_link(0);
    else if (!strcmp(what, "hp"))              /* the headphone jack as the UI read it (1 in; the script's "jack") */
        *got = (int)opt_ui_hp();
    else if (!strcmp(what, "oc"))              /* the output's correction (0 none, 1 SPK LOWCUT, 2 BASS+, 3 / 4 HP BASS) */
        *got = (int)opt_ui_oc();
    else if (!strcmp(what, "usbh_mode"))       /* SYSTEM > USB's MODE: 0 DEVICE, 1 HOST (-1: no USB_HOST) */
        *got = (int)opt_ui_usbh();
    else if (!strcmp(what, "panel_hits"))      /* Optimist's object sectors on OBJ_PANEL's 0xD8000..0xD9FFF */
        *got = (int)opt_st_panel_hits();
    else if (!strcmp(what, "bta_out"))         /* Bluetooth audio: the sound to the device (1) or the FM-1's own (0) */
#if FM1B_BT_AUDIO
        *got = plat_bta_out_device();
#else
        *got = -1;                             /* (not built) */
#endif
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
void app_host_reboot(void) { opt_ui_usbh_reboot(); }   /* (what RAM would forget: SYSTEM > USB's MODE) */

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    char row[12], v[4][24];
    const char *l[4];
    uint32_t k;
    int32_t gv[4];
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "blesx")) {                 /* blesx F07D...F7: a SysEx frame from the computer over Bluetooth (the
                                                 * hex bytes in one word), as USB-MIDI packets into the tick's MIDI in
                                                 * (core/opt_core.c opt_core_midi from PORT_BLE): the mirror's requests */
        uint8_t f[96];
        uint32_t n = 0, i;
        for (; a && a[0] && a[1] && n < sizeof f; a += 2) {
            char h[3] = {a[0], a[1], 0};
            f[n++] = (uint8_t)strtoul(h, 0, 16);
        }
        for (i = 0; i < n; i += 3) {
            uint32_t m = n - i < 3u ? n - i : 3u, end = i + m == n && f[n - 1] == 0xF7u;
            uint32_t cin = end ? 4u + m : 4u, pkt = cin;
            uint32_t j;
            for (j = 0; j < m; j++)
                pkt |= (uint32_t)f[i + j] << (8u * (j + 1u));
            opt_core_midi(pkt, PORT_BLE);
        }
        return 1;
    }
    if (!strcmp(cmd, "linkset")) {               /* linkset N: SYNC > LINK stored as N (0 OFF, 1 ON, 2 ON+S/S) */
        opt_ui_link_set((uint32_t)atoi(a ? a : "0"));
        return 1;
    }
    if (!strcmp(cmd, "update_begin")) {         /* an update begins (main_fm1.c update_begin): the transport asked to stop; the host's
                                                 * time stands still inside the app (stop_first gives up waiting), so the
                                                 * next millisecond's render carries it out */
        app_update_begin();
        return 1;
    }
    if (!strcmp(cmd, "lights")) {                /* lights N: SYSTEM > LIGHTS set to level N (0 OFF .. 3 HIGH) */
        opt_ui_lights_set((uint32_t)atoi(a ? a : "0"));
        return 1;
    }
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
    if (!strcmp(cmd, "ins") || !strcmp(cmd, "clockout")) {   /* ins T S TYPE: an insert type (FXT_*) into track T's
                                                 * slot S, as the mixer's INSERT puts it; clockout 0|1: CLOCK OUT */
        if (cmd[0] == 'i' && (!a || !b || !c || !a[0] || !b[0] || !c[0])) {
            printf("FAIL ins T S TYPE\n");
            app_fails++;
        } else if (cmd[0] == 'i') {
            opt_ui_ins((uint32_t)atoi(a), (uint32_t)atoi(b), (uint32_t)atoi(c));
        } else {
            opt_ui_clock_out(a && atoi(a) != 0);
        }
        return 1;
    }
    if (!strcmp(cmd, "insert")) {                /* insert T S TYPE [V]: an insert type into track T's slot S with its amount V (default
                                                 * 64): a failure when this build lacks the type (its builder item off) */
        int rc = !a || !b || !c || !a[0] || !b[0] || !c[0] ? -2 : opt_ui_insert((uint32_t)atoi(a), (uint32_t)atoi(b),
                                                                                 (uint32_t)atoi(c), d && d[0] ? atoi(d) : 64);
        printf("%s insert %s %s %s: %s\n", rc < 0 ? "FAIL" : "  ok", a ? a : "", b ? b : "", c ? c : "",
               rc == -2 ? "usage: insert T S TYPE [V]" : rc < 0 ? "not built" : "loaded");
        app_fails += rc < 0;
        return 1;
    }
    if (!strcmp(cmd, "engine")) {                /* engine T NAME: synth track T's engine (the first built one that starts with NAME):
                                                 * a failure when this build lacks it */
        int e = !a || !b || !a[0] || !b[0] ? -2 : opt_ui_engine((uint32_t)atoi(a), b);
        printf("%s engine %s %s: %s%s\n", e < 0 ? "FAIL" : "  ok", a ? a : "", b ? b : "",
               e < 0 ? "not built" : "slot ", e < 0 ? "" : opt_ui_engine_name((uint32_t)atoi(a)));
        app_fails += e < 0;
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
