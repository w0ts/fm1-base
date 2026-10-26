/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist on fm1-base, the trial: the app's half of the contract with the platform (device/unity.c,
 * host/plat_host.c). Milestone 1: Optimist's audio core (core/opt_core.c) on app_render; milestone 2: its UI
 * (ui/opt_ui.c) on app_frame / app_input_only, fm1-base's canvas and panel. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "OPTIMIST"
#define APP_SPLASH_RGB RGB(255, 214, 10)
#define APP_BLOCK 256u                   /* frames per audio block (8 of Optimist's 32-frame mix blocks) */

/* the store: only the platform's panel calibration for now (Optimist's projects, presets and settings: a later step).
 * Each object takes two sectors, A at its offset and B the next (device/storage.c). OBJ_PANEL at 0xD8000 / 0xD9000: free
 * in Optimist's map since its FM6 user bank went (firmware/src/storage/storage.c's flash map, 2026-10-18; nothing in
 * its tree writes there: the snapshots and the CZ collection end below 0xD8000, the drum kit bank is 0xDA000 / 0xDB000).
 * Not 0xDC000 (milestone 1's): Optimist's user preset bank A, which an FM-1's presets would have lost */
enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xD8000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks (host/plat_host.c "expect"): voices (sounding), blocks (rendered), keys (held, as the
 * render saw them); the UI's: scr (Optimist's SCR_*: 0 the mixer, 1 SOUND, 2 FX, 5 STEP, 7 TEMPO, 9 GLOBAL), row (its
 * cursor row), track (selected, 3 the drums), bpm, playing, gv1..gv4 (the cursor row's cards: their values), pT.ID
 * (track T's value P_* ID) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
/* the app's script command: "cells" prints the screen, its cursor row and its four cards (label, value) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
