/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist on fm1-base, the trial app (milestone 1): fm1-base's platform, Optimist's audio core. The render is
 * Optimist's (core/opt_core.c: engines, voices, drums, effects, sequencer); the keys and MIDI in play it; the screen
 * is a placeholder on fm1-base's gfx.c (Optimist's UI is a later step). Part of the unity build (device/unity.c,
 * host/plat_host.c), after the platform: gfx.c is in scope. */
#include "core/opt_core.h"

static volatile uint32_t blocks;         /* rendered (a check: the render runs) */
static volatile uint32_t keys_seen;      /* the keys as the last render saw them */

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port))    /* USB, TRS (and Bluetooth): into Optimist's MIDI queue (usb.c's ring) */
        opt_core_midi(pkt);
    keys_seen = plat_keys();
    opt_core_render(lr, n, master_q12, keys_seen);
    blocks++;
}

void app_panic(void) { opt_core_panic(); }

void app_init(void)
{
    opt_core_init();
}

void app_say(const char *a, const char *b)
{
    lcd_fill(0, 200, 240, 40, C_BLACK);
    draw_text_box(0, 204, 240, &FONT_S, a ? a : "", C_WHITE, 1);
    if (b)
        draw_text_box(0, 222, 240, &FONT_S, b, C_WHITE, 1);
}

static uint32_t shown_voices = 0xFFFFFFFFu;
static void draw(void)
{
    char line[32] = "PART 1 ";
    uint32_t i = 7, k;
    const char *e = opt_core_engine(0);
    for (k = 0; e[k] && i < sizeof line - 1u; k++)
        line[i++] = e[k];
    line[i] = 0;
    lcd_fill(0, 0, 240, 200, C_BLACK);
    draw_text_box(0, 60, 240, &FONT_M, "OPTIMIST", C_WHITE, 1);
    draw_text_box(0, 100, 240, &FONT_S, "ON FM1-BASE  (TRIAL)", C_WHITE, 1);
    draw_text_box(0, 130, 240, &FONT_S, line, C_WHITE, 1);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw();
}

void app_input_only(void) {}

void app_frame(void)
{
    uint32_t v = opt_core_voices();
    if (v != shown_voices) {             /* the voices sounding, as a check on the screen */
        char b[16] = "VOICES ";
        b[7] = (char)('0' + (v / 10u) % 10u);
        b[8] = (char)('0' + v % 10u);
        b[9] = 0;
        shown_voices = v;
        lcd_fill(0, 160, 240, 20, C_BLACK);
        draw_text_box(0, 160, 240, &FONT_S, b, C_WHITE, 1);
    }
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
    else
        return 0;
    return 1;
}
#endif
