/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist on fm1-base, the trial: the app's half of the contract with the platform (device/unity.c,
 * host/plat_host.c). Milestone 1: Optimist's audio core (core/opt_core.c) on app_render; milestone 2: its UI
 * (ui/opt_ui.c) on app_frame / app_input_only, fm1-base's canvas and panel. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "opt_dim.h"                     /* (build/optgen, gen_optimist.py dim_header: OPT_DIM_LEVELS 1: LIGHTS is always built) */

#define APP_NAME "OPTIMIST"
#define APP_SPLASH_RGB RGB(255, 214, 10)
#define APP_BLOCK 256u                   /* frames per audio block (8 of Optimist's 32-frame mix blocks) */
/* the canvas: Optimist's own, 240 x 124 (display/gfx.c CV_MAX: its graph strip; every screen of its UI draws in bands
 * of at most that, as on its own firmware), not fm1-base's 240 x 144: 9,600 B of the SRAM. fm1-base's own screens draw
 * 64 rows at most (main_fm1.c splash, sys_text.c). Optimist's builder item CANVAS_STRIP (OPT_CANVAS_ROWS, opt_dim.h:
 * gen_optimist.py from FELUCCA_CANVAS_STRIP): 240 x 62, 240 x 31 or 240 x 16, 29,760 / 44,640 B of the pool less, its tall bands
 * drawn in passes */
#ifndef OPT_CANVAS_ROWS
#define OPT_CANVAS_ROWS 31u
#endif
#define APP_CANVAS_PX (240u * OPT_CANVAS_ROWS)

/* the store: only the platform's panel calibration (fm1-base's device/storage.c). Optimist's own data (its projects,
 * section log, presets, kits, snapshots, settings, autosave, drum records) is Optimist's storage at Optimist's addresses
 * (ui/opt_storage.h), beside it. Each object takes two sectors, A at its offset and B the next (device/storage.c).
 * OBJ_PANEL at 0xD8000 / 0xD9000: free in Optimist's map since its FM6 user bank went (firmware/src/storage/storage.c's
 * flash map, 2026-10-18; nothing in its tree writes there: the snapshots and the CZ collection end below 0xD8000, the
 * drum kit bank is 0xDA000 / 0xDB000: opt_storage.h checks it, and tests/storage.txt "panel_hits"). Not 0xDC000
 * (milestone 1's): Optimist's user preset bank A, which an FM-1's presets would have lost */
/* With Optimist's SD card mode (its LAYOUT 1: the store from 0xD0000, the presets' FM6 voices at 0xD8000) the build has
 * fm1-base's bigger slot (app_end, FM1B_APP_END=0xCE000: gen_optimist.py checks it) and OBJ_PANEL takes the two sectors
 * just above it, 0xCE000 / 0xCF000 (ui/opt_storage.h checks it). Every other MEMORY step (its LAYOUT 2..8: the slot to
 * its FM6 voices, the store stacked above up to 0xD8000 at most) keeps OBJ_PANEL at 0xD8000, so the calibration
 * survives a switch between them (Optimist's docs/SD-MODE-DESIGN.md 9.1) */
enum { OBJ_PANEL, OBJ_NOBJ };
#if FM1B_APP_END == 0xCE000u
#define APP_STORE_MAP {FM1B_APP_END}
#else
#define APP_STORE_MAP {0xD8000u}
#endif
/* its one object is 32 bytes (device/panel.c panel_t): the store's RAM one 256-byte page, not a whole payload (feat/
 * slim-store-bounce: 3,584 B of the SRAM; the same flash, the same records) */
#define APP_STORE_BOUNCE 1
#define FM1B_UAC_DEEP 1             /* its 4 channels' ring a half deeper: renders past 40 % of a half */

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
/* an update (the upgrade command) or the soft key's UBOOT from the host begins: the transport stops first (song stop,
 * notes off, MIDI OUT all-notes-off, MIDI STOP with CLOCK OUT on: ui/opt_update.h), then app_panic */
#define APP_UPDATE_BEGIN
void app_update_begin(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
/* Optimist's web editor (ui/opt_editor.h): its frames from USB (APP_SYSEX), every SysEx byte (APP_SYSEX_BYTE: DX7
 * SysEx, as Optimist's usb.c hands them to FM6), each main loop pass (APP_LOOP: the mirror, a restore's restart) */
#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);
#define APP_SYSEX_BYTE
void app_sysex_byte(uint8_t b);
#define APP_LOOP
void app_loop(void);
#define APP_RESTART                      /* plat_restart: a restore done, the mirror's test session */
#define APP_TICK                         /* 1 kHz, the timer interrupt: the MIDI in (core/opt_core.c) */
#define APP_TICK_FAST                    /* 10 kHz, the timer interrupt: CLOCK OUT's ticks to USB on time (plat_midi_rt) */
/* Bluetooth (fm1.toml ble_stack = "own": Optimist's own stack, fm1-base's): what it keeps lives in Optimist's settings
 * record, as Optimist keeps it (APP_BLE_STORE: app.c app_ble_load / app_ble_save, ui/opt_bt.h), so no OBJ_BLE; it
 * starts at power-on when SYSTEM > BT MIDI was left on (Optimist's: off by default); it advertises as Optimist does,
 * "Optimist XXXX" (XXXX from its address: the same name on Optimist's firmware) */
#define APP_BLE_STORE
/* Wi-Fi (fm1.toml wifi_stack = "own", from the configuration's WIFI_LINK: gen_optimist.py --conf): the networks kept and
 * the mode live in Optimist's settings record as Optimist keeps them (persist_rec.c wifi_nets: app.c app_wifi_store,
 * ui/opt_wifi.h), so no OBJ_WIFI */
#if defined(FM1B_WIFI_OWN) && FM1B_WIFI_OWN
#define APP_WIFI_STORE
uint8_t *app_wifi_store(void);
void app_wifi_store_dirty(void);
#endif
/* the USB MIDI host (fm1.toml usb_host = true, from the configuration's USB_HOST: gen_optimist.py --conf): SYSTEM > USB's
 * DEVICE line names the device by its product string (plat_usb_host_name; ui/opt_usbh.h) */
#if defined(FM1B_USB_HOST) && FM1B_USB_HOST
#define APP_USB_HOST_NAME
#endif
#define APP_BLE_AT_BOOT (opt_ble_at_boot() != 0)
int opt_ble_at_boot(void);
#define BLE_DEVICE_NAME "FM1-"                   /* the name "FM1-XXXX": ble/ble_cfg.h BLE_NAME_SEP "" (plat.h plat_dev_id) */
#define BLE_NAME_SEP ""
/* Optimist's own list of the Bluetooth MIDI devices (BLE_CENTRAL: ui/opt_bt_dev.h, its LAST and remembered devices with
 * their bonds) on fm1-base's central: the devices one by one (plat.h), the scan's reports and a pairing's keys to it */
#if defined(FM1B_BLE_CENTRAL) && FM1B_BLE_CENTRAL
#define APP_BLE_PEERS
void opt_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi);
struct plat_ble_keys;
void opt_ble_device_keys(const struct plat_ble_keys *k);
#endif
void app_tick(void);
void app_tick_fast(void);
#if OPT_DIM_LEVELS
#define APP_DIM_LEVELS                   /* plat_dim_ns: LIGHTS's LOW / MID / HIGH as the dim layer's brightness (ui/opt_panel.h) */
#endif
#define APP_BATTERY_RAW                  /* plat_battery_raw: SYSTEM > BATTERY's volts and charge (ui/opt_ui.c) */
#define APP_CPU_KHZ                      /* plat_cpu_khz: SYSTEM > INFO's CPU MHz, the CPU guard's clock (core/opt_core.c) */
#define APP_SPLASH                       /* the boot screen: Optimist's (ui/splash.c: the dinghy, the version) */
void app_splash(void);
/* USB audio (fm1.toml usb_audio_*): the computer's audio is Optimist's USB input track (APP_USB_PLAY: the render takes
 * it, plat_usb_audio_play), and the USB audio input is Optimist's four capture channels by SYSTEM > USB AUDIO's
 * routing (APP_USB_CAPTURE: app_usb_capture, core/opt_core.c) */
#define APP_USB_PLAY
#define APP_USB_CAPTURE
/* Bluetooth audio takes the plain output, never the FM-1's own codec's correction (SPK LOWCUT / HP BASS: APP_BT_FRAMES,
 * app_bt_frames, core/opt_core.c) */
#define APP_BT_FRAMES
const int32_t *app_bt_frames(const int32_t *lr, uint32_t n);
#define APP_USB_STATS                    /* plat_usb_audio_stats: SYSTEM > INFO's USB counters (core/opt_core.c) */
/* the debug stats (plat.h plat_stats_enable: SYSTEM > DEBUG > DEBUG STATS, Optimist's core/dbgstats.h): each record's
 * 7 bytes of Optimist's own (ui/opt_stats.h app_stats): CPU0's longest half in the last 10 s and 1 min (% of a
 * half), the CPU guard's steps, the late halves, MIDI in lost (the stacks are the platform's own: plat_stack_left);
 * written at a stop (APP_STATS_QUIET: the transport stopped) */
#define APP_STATS
#define APP_STATS_LAYOUT "c10 1 cmax 1 guard 1 drops 2 midi 2"
#define APP_STATS_QUIET
/* the development readout over USB-MIDI (plat.h APP_LOG: F0 7D 4C 47 F7, tools/fm1_remote.py log): Optimist's lines
 * after fm1-base's, where its memory went: the arena, the undo history's two parts (ui/opt_lg.h app_log) */
#define APP_LOG
void app_log(void (*line)(const char *t));
/* the second core (fm1.toml cpu2, cpu2_fallback: Optimist's DUAL on fm1-base's, core/opt_dual.c): a job not back within
 * two of Optimist's 32-frame mix blocks retires it, as Optimist's own join bound (system/dual.c DUAL_JOIN_US: 1,451 us at
 * 44.1 kHz; opt_dual.c checks the two agree), instead of felucca-bsp's 8 ms default (chip/fm1_cpu1.h, a bound per job:
 * the trial hands it one a mix block, eight a render) */
#define FM1_CPU1_TIMEOUT_US 1451u
const int32_t *app_usb_capture(const int32_t *lr, uint32_t n);

/* fm1-base's companion services (fm1.toml companion = true; docs/COMPANION.md), app.c: the files are Optimist's own
 * backup objects and user sample slots, each by its tag (SETT, PJ01, UPR1, UKIT, USR1 ...), read and written through
 * Optimist's own paths, with BKINFO (their kinds, the build's caps) and BKCTL (the restore's session: end, restart,
 * check only) beside them (Optimist's io/editor/ed_comp.c: in place of its editor's cmds 43..48 and 11..15; web/
 * editor.html uses them); and 22 parameters from Optimist's tables (ui/opt_ui.c opt_cp_*): BPM, SWING, each track's
 * LVL, PAN, MUTE, DLY, REV. Busy while it plays (song.playing): only BKCTL is written then */
#define APP_COMPANION_EXTRAS "editor=optimist"
#define APP_FILES
int app_file(uint32_t i, plat_file_info_t *f);
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n);
int app_file_begin(uint32_t i, uint32_t size);
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n);
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc);
int app_file_delete(uint32_t i);
int app_file_busy(void);
#define APP_FILE_COUNT                   /* (the objects' sizes are costly: a project's walks the section log) */
uint32_t app_file_count(void);
#define APP_PARAMS
#define APP_PARAM_MAX 24
int app_param(uint32_t i, plat_param_t *p);
const char *app_param_group(uint32_t g);
int32_t app_param_get(uint32_t i);
int32_t app_param_set(uint32_t i, int32_t v);

/* the host simulator's checks (host/plat_host.c "expect"): voices (sounding), blocks (rendered), keys (held, as the
 * render saw them); the UI's: scr (Optimist's SCR_*: 0 the mixer, 1 SOUND, 2 FX, 5 STEP, 7 TEMPO, 9 GLOBAL), row (its
 * cursor row), track (selected, 3 the drums), bpm, playing, gv1..gv4 (the cursor row's cards: their values), pT.ID
 * (track T's value P_* ID), lights (SYSTEM > LIGHTS' level; the command "lights N" sets it) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
/* the stores' (milestone 3): up.K (user preset UK holds a sound), proj.S (project slot S holds a project), w.ADDR (the
 * flash word at ADDR, little-endian, as Optimist reads it), panel_hits (Optimist's object sectors on OBJ_PANEL: 0);
 * USB audio's: uad.S (SYSTEM > USB AUDIO's source S, 0 MASTER: its channels, Optimist's UAD_*: 3 = 1+2) */
/* the app's script commands: "cells" prints the screen, its cursor row and its four cards (label, value); "msg [TEXT]"
 * the header's last message (without TEXT in it: a failure); "flash_dump FILE" / "flash_load FILE" the flash as a
 * 1 MiB image at the FM-1's offsets (Optimist's simulator's: firmware/sim), the data windows and Optimist's globals
 * (a load: then "reboot"; FILE "$NAME": the environment's NAME). On the host, OPT_FLASH=FILE: the first power-on with
 * that flash (app.c app_init) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_FAILS
int app_host_fails(void);
/* what a "reboot" forgets that RAM would: SYSTEM > USB's MODE (ui/opt_usbh.h: never kept, every boot is DEVICE) */
#define APP_HOST_REBOOT
void app_host_reboot(void);
