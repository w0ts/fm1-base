/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist on fm1-base, the trial: the app's half of the contract with the platform (device/unity.c,
 * host/plat_host.c). Milestone 1: Optimist's audio core (core/opt_core.c) on app_render; the keys play part 1. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "OPTIMIST"
#define APP_SPLASH_RGB RGB(255, 214, 10)
#define APP_BLOCK 256u                   /* frames per audio block (8 of Optimist's 32-frame mix blocks) */

/* the store: only the platform's panel calibration for now (Optimist's projects, presets and settings: a later step) */
enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks (host/plat_host.c "expect"): voices (sounding), engine0 (part 1's engine slot),
 * blocks (rendered), keys (held, as the render saw them) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
