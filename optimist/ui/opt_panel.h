/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The panel between fm1-base and Optimist's UI, after ui/panel.c (its panel map: panel.btn, panel.enc, panel.dir):
 * opt_in_scan reads fm1-base's panel into Optimist's HAL state (opt_in.h), opt_leds_out writes Optimist's LED columns
 * to fm1-base's lights. */
#pragma once

static const uint8_t OIN_PANEL_ID[14] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};   /* (ui/panel.c PANEL_DEFAULT) */

/* the panel now: fm1-base's labels to Optimist's matrix ids (panel.btn: the default map, or a calibration's) */
static void opt_in_scan(void)
{
    uint32_t b = plat_buttons(), k = plat_keys() | fm1_rem_note, m = fm1_rem_btn & 0x3FFFu, l, now = fm1_ticks(), ch;
    for (l = 0; l < 14u; l++)
        if ((b >> l) & 1u)
            m |= 1u << (panel.btn[l] < 14u ? panel.btn[l] : OIN_PANEL_ID[l]);
    ch = m ^ fm1_in.buttons;
    for (l = 0; l < 14u; l++)
        if ((ch >> l) & 1u)
            fm1_btn_t[(m >> l) & 1u ? 0u : 1u][l] = now;
    oin_pressed |= m & ch;
    fm1_in.buttons = m;
    oin_notes_pressed |= k & ~oin_keys;
    oin_keys = k;
    for (l = 0; l < 7u; l++)                           /* plat_enc by role; Optimist's by matrix encoder (panel.enc) */
        oin_enc[panel.enc[l] % 7u] += plat_enc((int)l) * panel.dir[l];   /* (panel_enc multiplies by dir again: 1 * 1) */
}
/* the LEDs Optimist's UI wrote (fm1_led full, fm1_led_half, fm1_led_dim; PLAY's green at column 8, row bit 1) onto
 * plat_lights (bright, dim) and plat_lights_mid (the half level) */
static uint32_t oin_lit(const uint8_t *cols, uint32_t id)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                return (cols[p] >> r) & 1u;
    return 0;
}
static void oin_side(const uint8_t *cols, uint32_t *btn, uint32_t *keys)
{
    uint32_t l, k;
    *btn = *keys = 0;
    for (l = 0; l < 14u; l++)
        *btn |= oin_lit(cols, panel.btn[l] % 14u) << l;
    for (k = 0; k < 27u; k++)
        *keys |= oin_lit(cols, 14u + k) << k;
    if ((cols[8] >> 1) & 1u)
        *btn |= LED_PLAY_GREEN;
}
static uint32_t oin_shown[6] = {~0u};
static void opt_leds_out(void)
{
    uint32_t fb, fk, db, dk, hb, hk;
    oin_side(fm1_led, &fb, &fk);
    oin_side(fm1_led_dim, &db, &dk);
    oin_side(fm1_led_half, &hb, &hk);
    if (fb == oin_shown[0] && fk == oin_shown[1] && db == oin_shown[2] && dk == oin_shown[3] && hb == oin_shown[4] &&
        hk == oin_shown[5])
        return;                                        /* (on a change only, as Optimist writes its columns) */
    plat_lights(fb, fk, db, dk, 2);
    plat_lights_mid(hb, hk);
    oin_shown[0] = fb, oin_shown[1] = fk, oin_shown[2] = db, oin_shown[3] = dk, oin_shown[4] = hb, oin_shown[5] = hk;
}
