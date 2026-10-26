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

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port))    /* USB, TRS (and Bluetooth): into Optimist's MIDI queue (its checks, its sources) */
        opt_core_midi(pkt, port);
    keys_seen = plat_keys();
    opt_core_render(lr, n, master_q12, keys_seen);
    blocks++;
}

void app_panic(void) { opt_core_panic(); }

void app_init(void) { opt_core_init(); }

/* the platform's screen, for Optimist's unit (its canvas is fm1-base's gfx.c, compiled there; the LCD is this unit's) */
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

void app_ui_init(void) { opt_ui_init(); }
void app_input_only(void) { opt_ui_input(); }
void app_frame(void) { opt_ui_frame(); }

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
    else
        return 0;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    char row[12], v[4][24];
    const char *l[4];
    uint32_t k;
    int32_t gv[4];
    (void)a, (void)b, (void)c, (void)d;
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
