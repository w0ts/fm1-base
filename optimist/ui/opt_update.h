/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* an update (the upgrade command) or the soft key's UBOOT from the host begins (app.h APP_UPDATE_BEGIN: device/main_fm1.c
 * update_begin, before anything is staged or erased and before the restart): the transport stops first, as on Optimist's
 * own firmware (firmware/src/system/stop_first.h: transport_req 2, carried out by the audio block's seq_stop: song stop,
 * notes off, MIDI OUT all-notes-off, MIDI STOP when CLOCK OUT is on), waited for 100 ms at most; nothing playing: back
 * at once. Main loop, the render running meanwhile (fm1_delay_ms waits on plat_ms, fm1_wdt_feed yields: ui/opt_in.h;
 * the platform feeds the watchdog around it) */
#include "system/stop_first.h"
void opt_update_begin(void) { stop_first(); }
