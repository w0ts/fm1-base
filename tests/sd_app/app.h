/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD test app (tests/run_tests.sh): the platform's SD card (plat_sd_*) as an app uses it, in the host simulator
 * with a card image. The script's sdcheck runs the calls in turn (start-up, reads, writes, a read run, a write run) and
 * counts what came out right; sdgone checks that a card taken out is gone; expect sd_ok / sd_bad. Nothing else: no
 * sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SDTEST"
#define APP_SPLASH_RGB RGB(80, 160, 240)
#define APP_BLOCK 256u

enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator: sdcheck (the calls in turn), sdgone (no card), sd_ok and sd_bad (the calls that came out right,
 * wrong) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
