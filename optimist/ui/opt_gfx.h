/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Henri David */
/* Optimist's drawing on fm1-base's canvas (device/gfx.c, included before this): what Optimist's own gfx.c
 * (firmware/src/display/gfx.c) has beside the 20 functions both share, taken from it as they are there: the status
 * colours, ink_on, cv_frame, cv_box, and Optimist's five palettes (fm1-base's gfx.c has X0X's: neutral greys under
 * the theme's two brightest steps). Optimist's code, GPL-3.0-only. */
#pragma once

/* the status colours, the web editor's (tools/colors.json "status": build/optgen/felucca_colors.h, gen_optimist.py) */
#include "felucca_colors.h"
#define C_OK COL_ST_OK               /* saved, fine, playing */
#define C_WARN COL_ST_WARN           /* a notice: another build, MISSING, EXPERIMENTAL */
#define C_ERR COL_ST_ERR             /* an error: damaged, full, clipping; recording */
#define C_AUTO COL_MK_AUTO           /* a step with automation (a lock or motion): STEP's grid, the mixer's strips */
#define C_NOTE COL_MK_NOTE           /* the note SELECT picked on a held step of several: STEP's roll */
static const uint16_t C_STATUS[4] = {0, C_OK, C_WARN, C_ERR};

/* Optimist's palettes (display/gfx.c PALETTES): five steps of one colour on black, step 1 bright enough to read as a
 * line. fm1-base's palette_set sets X0X's; the UI's (ui/panel.c settings_init) is this one (opt_ui.c: the name) */
static const uint16_t OPT_PALETTES[5][5] = {
    {RGB(0, 58, 18), RGB(0, 104, 38), RGB(16, 150, 58), RGB(56, 200, 92), RGB(120, 255, 146)},     /* GREEN */
    {RGB(84, 36, 0), RGB(136, 62, 0), RGB(180, 88, 0), RGB(225, 120, 8), RGB(255, 166, 40)},       /* AMBER */
    {RGB(0, 44, 72), RGB(0, 78, 120), RGB(16, 120, 170), RGB(56, 172, 222), RGB(140, 222, 255)},   /* CYAN */
    {RGB(74, 12, 12), RGB(124, 24, 18), RGB(180, 40, 28), RGB(226, 64, 48), RGB(255, 112, 92)},    /* RED */
    {RGB(58, 58, 58), RGB(104, 104, 104), RGB(146, 146, 146), RGB(190, 190, 190), RGB(228, 228, 228)},   /* MONO */
};
_Static_assert(NPALETTES == 5u, "fm1-base's gfx.c: as many palettes as Optimist's (ui/panel.c checks the index against NPALETTES)");
static void opt_palette_set(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < 5u; k++)
        pal[k] = OPT_PALETTES[i % 5u][k];
}

/* the text colour on a filled background bg: black or white, whichever contrasts more (display/gfx.c ink_on) */
#define INK_LIN(x) ((3u * (x) * (x) + (x) * (x) / 1024u * (x)) / 4096u)
static uint16_t ink_on(uint16_t bg)
{
    uint32_t r = (bg >> 11) * 1024u / 31u, g = ((bg >> 5) & 63u) * 1024u / 63u, b = (bg & 31u) * 1024u / 31u;
    return 2126u * INK_LIN(r) + 7152u * INK_LIN(g) + 722u * INK_LIN(b) > 184u * 10000u ? C_BLACK : C_WHITE;
}

static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1 px outline */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}
/* a tile, a card, a cell: filled in its colour when it has one; black is an outline in C_LINE */
static void cv_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    if (c == C_BLACK)
        cv_frame(x, y, w, h, C_LINE);
    else
        cv_rect(x, y, w, h, c);
}
