/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The switchable features' table as Optimist's core/feat_mem.c reads it (included by core/opt_core.c before fx/fx.c,
 * which includes core/arena_bufs.c and feat_mem.c): the features and their hooks are declared here and defined after
 * the UI (ui/opt_feat.h), in the one unit. The lendable ones (core/opt_lend.h: Bluetooth audio's buffers, the
 * mirror's Bluetooth ring) hold a block of the arena only while on; the other features are in the table with their
 * idle signals (one place: SYSTEM > INFO may show what an idle one would free once its buffers are lendable too) and
 * never offered (bytes 0) */
#pragma once
#if FMEM_NLEND
enum { OF_BTA, OF_BLE, OF_UAC, OF_WIFI, OF_AP, OF_EDITOR, OF_USBH, OF_MIR, OF_N };
static uint32_t of_bytes(uint32_t f);
static int of_on(uint32_t f);
static int of_idle(uint32_t f);
static void of_off(uint32_t f);
#define FMEM_BYTES(f) of_bytes(f)
#define FMEM_ON(f) of_on(f)
#define FMEM_IDLE(f) of_idle(f)
#define FMEM_OFF(f) of_off(f)
#define OF_K(on, i) ((on) ? ARB_K(ARB_LEND0 + (i)) : FMEM_NO_K)   /* (a lent block's arena client, core/opt_lend.h) */
#define FMEM_FEATS                                                                                                      \
    {"BT AUDIO", "SYSTEM > BT", OF_K(OPT_LEND_BTA, L_BTA)}, {"BT MIDI", "SYSTEM > BT", FMEM_NO_K},                 \
    {"USB AUDIO", "SYSTEM > USB", OF_K(OPT_LEND_UAC, L_UAC)}, {"WI-FI", "SYSTEM > WI-FI", OF_K(OPT_LEND_LINK, L_LINK)},             \
    {"WI-FI AP", "SYSTEM > WI-FI", OF_K(OPT_LEND_AP, L_AP)}, {"EDITOR", "SYSTEM > USB", FMEM_NO_K},                               \
    {"USB HOST", "SYSTEM > USB", FMEM_NO_K}, {"MIRROR", "", OF_K(MIR_LEND, L_MIR)}
/* the pause of a YES to a feature's PACK, and the UNLOAD of a NOFIT (ui/opt_feat.h) */
static int of_playing(void);
static void of_pause(void);
static void of_resume(void);
static int of_quiet(void);
static void of_unload(uint32_t f);
static void of_say_full(uint32_t f);
#define FMEM_PLAYING() of_playing()
#define FMEM_PAUSE() of_pause()
#define FMEM_RESUME() of_resume()
#define FMEM_QUIET() of_quiet()
#define FMEM_UNLOAD(f) of_unload(f)
#define FMEM_SAY_FULL(f) of_say_full(f)
#endif
