/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's panel HAL (firmware/hal/fm1_input.h: what its UI calls) on fm1-base's plat.h. fm1-base gives the held
 * buttons (bit B_*), the held keys and the knobs' detents; Optimist's UI wants its matrix ids (ui/panel.c panel.btn),
 * the presses since it last looked, each button's last press and release time, the keys pressed since it last looked,
 * and the LED columns it writes (fm1_led*) turned into lights. opt_in_scan reads the panel (every millisecond: the
 * simulator's app_input_only and app_frame; on an FM-1, every main loop pass), so a press between two UI frames keeps
 * its edge and its time to the millisecond (the device's scan stamps it to 0.1 ms). */
#pragma once

/* plat.h's B_* (PLAT_B_* here, fm1_compat.h) and Optimist's labels (ui/panel.c) are the same list in the same order */
_Static_assert(PLAT_B_FX == 0 && PLAT_B_SEL == 1 && PLAT_B_SAVE == 7 && PLAT_B_PLAY == 10 && PLAT_B_OCTDN == 12 &&
               PLAT_B_OCTUP == 13 && PLAT_NB == 14 && PLAT_EN_SELECT == 0 && PLAT_EN_PRESET == 2 && PLAT_EN_K4 == 6 &&
               PLAT_NE == 7, "plat.h's panel order (Optimist's labels map one to one)");

/* key ids at (physical column, packed row bit) as hal/fm1_input.h has them: the LEDs' places (op_input.c ui_leds) */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_half[FM1_NCOL];
/* the backlight (menu LIGHTS: ui/lights.c, op_input.c): its LEDs and pulse (hal/fm1_input.h fm1_led_bg, fm1_led_bg_ns;
 * 0 off). fm1-base has no level of that width (LOW .. HIGH 0.5 .. 2 us a frame, under its glow's 3.2 us): any level is
 * its dim layer (ui/opt_panel.h opt_leds_out) */
static uint8_t fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static void fm1_led_key(uint32_t id, int on)            /* (hal/fm1_input.h: the LED of key id, 0..13 a button, 14..40 a key) */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}

static uint32_t fm1_btn_t[2][14];                      /* each matrix button's last press [0] / release [1] (ticks) */
static uint32_t oin_pressed, oin_notes_pressed, oin_keys;
static int32_t oin_enc[7];                             /* detents per matrix encoder, not taken yet */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = oin_pressed;
    if (released)
        *released = 0;                                 /* (nobody asks: op_input.c reads presses only) */
    oin_pressed = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t p = oin_notes_pressed;
    oin_notes_pressed = 0;
    return p;
}
#define FM1_NENC 7u
#if FELUCCA_ED_MIRROR
static int16_t fm1_turned[FM1_NENC];      /* the steps taken since the mirror's last STATE (ed_mirror.c mir_state) */
/* the mirror's client (hal/fm1_input.h, ED_MIRROR): its key id (0..40) down or up, its knob e turned s detents, all
 * of its keys up */
static void fm1_remote_key(uint32_t id, uint32_t down)
{
    uint32_t bit = id >= 14u ? 1u << (id - 14u) : 1u << id;
    volatile uint32_t *m = id >= 14u ? &fm1_rem_note : &fm1_rem_btn;
    *m = down ? *m | bit : *m & ~bit;
}
static void fm1_remote_enc(uint32_t e, int32_t s) { oin_enc[e % 7u] += s; }
static void fm1_remote_clear(void) { fm1_rem_btn = fm1_rem_note = 0; }
#endif
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = oin_enc[e % 7u];
    oin_enc[e % 7u] = 0;
#if FELUCCA_ED_MIRROR
    fm1_turned[e % 7u] = (int16_t)(fm1_turned[e % 7u] + s);
#endif
    return s;
}
/* a wait inside the UI (SYSTEM > CALIBRATE, ui/optimist/op_input.c panel_setup: it loops on fm1_ms and the panel until a
 * press or 30 s): the time and the panel read again. The host simulator's time stands still inside the app's code
 * (plat_yield runs no simulated millisecond), so there the wait's clock moves a millisecond a call, as Optimist's own
 * host test has it: the wait ends by its 30 s timeout (no input can come in meanwhile) instead of hanging */
static void opt_in_scan(void);
static void fm1_wdt_feed(void)
{
    plat_yield();
    opt_in_scan();
#ifdef PLAT_HOST
    fm1_ms++;
#else
    fm1_ms = plat_ms();
#endif
}

