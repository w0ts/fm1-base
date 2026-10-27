/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The USB audio counters test app (tests/run_tests.sh, tests/usb_stats_test.py): an app built with APP_USB_STATS,
 * reading plat_usb_audio_stats every frame as an app with its own page of them does; the script checks what the app
 * read (expect app_out_frames, app_out_alt, app_in_alt, ...) while a computer plays and records. Silent, a black
 * screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "USBSTATS"
#define APP_SPLASH_RGB RGB(240, 120, 80)
#define APP_BLOCK 256u
#define APP_USB_STATS
#define APP_HOST_EXPECT

enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_expect(const char *what, int *got);
