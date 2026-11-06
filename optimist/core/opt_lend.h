/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The switchable features' blocks Optimist's arena lends (its core/arena_bufs.c ARB_LEND0 + i, core/feat_mem.c), in
 * this order, those built (included by core/opt_core.c before engines/engines.c, which counts them: FMEM_NLEND):
 *   L_BTA  Bluetooth audio's queue and ring (fm1.toml bt_audio_mem = "app": plat.h plat_bta_mem; ui/opt_bta_lend.h),
 *          17.7 KB while an AUDIO mode is on or its run is let go
 *   L_MIR  the mirror's Bluetooth ring (io/editor/ed_mirror.c mbr_q, MIR_LEND), 8 KB while a Bluetooth client uses it
 *   L_LINK Ableton Link's node (fm1.toml wifi_mem = "app": plat.h plat_link_mem), 1.8 KB while a Wi-Fi MODE is on or
 *          Link still runs
 *   L_AP   the access point's state (plat_wifi_ap_mem), 1.7 KB while a MODE AP is chosen or the access point still runs
 *   L_UAC  USB audio's rings (fm1.toml usb_audio_mem = "app": plat.h plat_usb_audio_mem) and the render's own buffers of
 *          the computer's audio and the capture (opt_core.c opt_play, opt_cap), 22 KB while the computer streams (an alt
 *          set, either way); declined (the question's NO) until it stops
 * Each its size, wanted now, and lent or taken back (the interrupts off): ui/opt_feat.h of_lend_*, after the code they
 * read. Each is a feature of the questions' table too (core/opt_feat_decl.h FMEM_FEATS: its name, its size) */
#pragma once
#if defined(FM1B_BTA_MEM) && FM1B_BTA_MEM && FELUCCA_BT_AUDIO
#define OPT_LEND_BTA 1
#else
#define OPT_LEND_BTA 0
#endif
#if defined(FELUCCA_ARENA_BYTES) && FELUCCA_FX_ARENA && FELUCCA_EDITOR && FELUCCA_ED_MIRROR && FELUCCA_BLE
#define MIR_LEND 1                               /* (ed_mirror.c: the ring a pointer, lent) */
#else
#define MIR_LEND 0
#endif
#if defined(FM1B_WIFI_MEM) && FM1B_WIFI_MEM && FELUCCA_ARENA_KB
#define OPT_LEND_LINK (FM1B_LINK != 0)
#define OPT_LEND_AP (FM1B_WIFI_AP != 0)
#else
#define OPT_LEND_LINK 0
#define OPT_LEND_AP 0
#endif
#if defined(FM1B_WIFI_MEM) && FM1B_WIFI_MEM && !FELUCCA_ARENA_KB
#error "opt_lend.h: fm1.toml wifi_mem = app without Optimist's arena (ARENA): nothing lends Link's node (gen_optimist.py --conf)"
#endif
#if defined(FM1B_UAC_MEM) && FM1B_UAC_MEM && FELUCCA_USB_AUDIO && FELUCCA_ARENA_KB && !defined(PLAT_HOST)
#define OPT_LEND_UAC 1
#else
#define OPT_LEND_UAC 0
#endif
#if defined(FM1B_UAC_MEM) && FM1B_UAC_MEM && !defined(PLAT_HOST) && !OPT_LEND_UAC
#error "opt_lend.h: fm1.toml usb_audio_mem = app without Optimist's arena and USB_AUDIO: nothing lends the rings (gen_optimist.py --conf)"
#endif
#define FMEM_NLEND (OPT_LEND_BTA + MIR_LEND + OPT_LEND_LINK + OPT_LEND_AP + OPT_LEND_UAC)
#define L_BTA 0
#define L_MIR OPT_LEND_BTA
#define L_LINK (L_MIR + MIR_LEND)
#define L_AP (L_LINK + OPT_LEND_LINK)
#define L_UAC (L_AP + OPT_LEND_AP)
#if FMEM_NLEND
static uint32_t of_lend_bytes(uint32_t i);
static int of_lend_want(uint32_t i);
static void of_lend_set(uint32_t i, void *p);
#define FMEM_LEND_BYTES(i) of_lend_bytes(i)
#define FMEM_LEND_WANT(i) of_lend_want(i)
#define FMEM_LEND_SET(i, p) of_lend_set(i, p)
#endif
