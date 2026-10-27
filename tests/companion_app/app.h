/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The companion test app (tests/companion_test.py): an app with the companion services (fm1.toml companion = true,
 * device/companion.c), its own SysEx beside them (APP_SYSEX: frames F0 7D 43 50 cmd .. F7, "CP": 1 counted, 2 plays,
 * 3 stops; never answered), its own tags, and three files (APP_FILES): notes.txt (written while playing too) and
 * sample.raw kept by plat_file_* in the data area, info read only (made by the app). No sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "CPTEST"
#define APP_VERSION "1.2"
#define APP_SPLASH_RGB RGB(80, 200, 120)
#define APP_BLOCK 256u
#define APP_COMPANION_EXTRAS "editor=cptest/1;site=https://example.invalid/cptest"

enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);

#define APP_FILES
int app_file(uint32_t i, plat_file_info_t *f);
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n);
int app_file_begin(uint32_t i, uint32_t size);
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n);
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc);
int app_file_delete(uint32_t i);
int app_file_busy(void);

/* the host simulator's checks: own (the app's SysEx frames it took), commits (files made current) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
