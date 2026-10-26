/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's web editor in the trial (EDITOR: web/editor.html, its backup and restore, the librarian, the screen mirrored
 * for the iPhone app and tools/fm1_mirror.py): its own files (io/editor/editor.c and what it includes: ed_lite.c,
 * ed_backup.c, ed_user.c, ed_mirror.c ...) compiled unchanged after the storage, as Optimist's felucca.c includes them,
 * on fm1-base's USB-MIDI SysEx instead of Optimist's io/usb/usb.c: the same frames, the same bytes.
 *   in       app.c app_sysex (fm1-base's APP_SYSEX, main loop) -> opt_ed_frame: the frame as ota_frame_get gives it to
 *            ed_service; taken (1) when the editor took it, else fm1-base's main loop goes on with it
 *   out      ota_wire_send -> plat_sysex_send (a reply, whole); the mirror's frames, which ed_mirror.c writes packet by
 *            packet into usb.c's ring (sx_out_q, so_w, mir_pkt), -> plat_sysex_begin / put / end as each packet is
 *            written (RING_PUBLISH, below); so_r, SXQ: fm1-base's ring as plat_sysex_queued / plat_sysex_room see it
 *   usb      config and resets (the editor's and the mirror's sessions) from plat_usb_session
 *   DX7      fm6_sx_byte (usb.c's hook in Optimist) on fm1-base's APP_SYSEX_BYTE (app.c app_sysex_byte), fm6_service
 *            in the main loop (system/main.c's order)
 *   restart  a restore done (ed_backup.c bk.reboot) and the mirror's test session (ed_mirror.c mir_test) restart
 *            through plat_restart (main.c fm1_reboot); the test session's mark in .noinit (felucca.c mir_test_boot),
 *            its blank flash in opt_storage.h (st_blank)
 * Main loop only, but fm6_sx_byte (the USB interrupt, as in Optimist). */
#pragma once

#if FELUCCA_EDITOR
#if FELUCCA_ED_LIVE
#error "the trial's editor: ED_LIVE off (the dev profile's: backup, librarian, mirror); the live editor is not wired"
#endif
#if FELUCCA_BLE
#error "the trial's editor: no Bluetooth (ed_mirror.c's Bluetooth link writes RING_PUBLISH's other way)"
#endif
#define FELUCCA_OTA 1                           /* (felucca.c: the editor needs the OTA SysEx plumbing, here fm1-base's) */
#define MIR_BLE 0                               /* (felucca.c: the mirror over Bluetooth needs BLE) */
#define MBR_PROJ 0

/* ---- usb.c's state as the editor reads it (opt_stubs.h usb: config, resets), each main loop pass */
static void opt_ed_usb(void)
{
    uint32_t s = plat_usb_session();
    usb.config = (uint8_t)(s != 0u);
    usb.resets = s;
}

/* ---- frames in: one at a time, the one app_sysex has (ota_frame_get / ota_frame_done, usb.c's) */
static const uint8_t *opt_rx_p;
static uint32_t opt_rx_n;
static uint8_t opt_rx_ready, sx_ready;          /* (sx_ready: a frame waits, as ed_sync9.c reads it) */
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!opt_rx_ready)
        return 0;
    *p = opt_rx_p;
    *n = opt_rx_n;
    return 1;
}
static void ota_frame_done(void) { opt_rx_ready = 0, sx_ready = 0; }
static void ota_idle(void)                      /* (a wait for the ring: the computer reads meanwhile) */
{
    plat_yield();
#ifdef PLAT_HOST
    fm1_ms++;                                   /* (the host's time stands still inside the app: opt_in.h fm1_wdt_feed) */
#endif
}

/* ---- frames out. The mirror's: ed_mirror.c mir_pkt writes each USB-MIDI packet into sx_out_q[so_w % SXQ], then
 * RING_PUBLISH, then so_w++; here the packet goes on to fm1-base's ring at RING_PUBLISH (a frame's first packet opens
 * the stream, its last, CIN 5..7, ends it). so_r: so_w less what fm1-base's ring still holds, so mir_pkt waits while
 * it is full and the computer reads (its MIR_STALL_MS without progress), mir_state_room sees its room and mir_poll its
 * being empty; a stream that fm1-base cut short (no room for PLAT_SYSEX_STALL_MS) reads full until the next pass, so
 * the mirror falls behind and catches up, as when Optimist's own ring stalls */
static uint32_t opt_sxq(void)                  /* fm1-base's ring, in packets (its room and what it holds) */
{
    static uint32_t q;
    uint32_t r = plat_sysex_room();
    if (r && (r + plat_sysex_queued()) / 3u > q)
        q = (r + plat_sysex_queued()) / 3u;
    return q > 256u ? 256u : q ? q : 1u;
}
#define SXQ opt_sxq()
static uint32_t sx_out_q[256], so_w;
static uint8_t sx_busy, opt_sx_open, opt_sx_cut;
#define so_r (so_w - (opt_sx_cut ? SXQ : plat_sysex_queued() / 3u))
static void opt_sx_publish(void)
{
    uint32_t pkt = sx_out_q[so_w % SXQ], cin = pkt & 15u, n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
    uint8_t b[3];
    b[0] = (uint8_t)(pkt >> 8), b[1] = (uint8_t)(pkt >> 16), b[2] = (uint8_t)(pkt >> 24);
    if (b[0] == 0xF0u) {                        /* (a frame's first packet: mir_wire's F0 starts it) */
        if (opt_sx_open)
            (void)plat_sysex_end();             /* (one before it left open: cut, the computer drops it at this F0) */
        opt_sx_open = plat_sysex_begin() == 0;
        opt_sx_cut = !opt_sx_open;
    }
    if (opt_sx_open && plat_sysex_put(b, n))
        opt_sx_cut = 1;
    if (cin >= 5u && opt_sx_open) {             /* (its last packet, with F7) */
        if (plat_sysex_end())
            opt_sx_cut = 1;
        opt_sx_open = 0;
    }
}
static int ota_wire_send(const uint8_t *p, uint32_t n)   /* a whole reply (ed_send) */
{
    if (opt_sx_open) {
        (void)plat_sysex_end();                 /* (a mirror frame left open: cut, as the next F0 cuts it) */
        opt_sx_open = 0;
    }
    return plat_sysex_send(p, n);
}

/* ---- the restarts (system/main.c fm1_reboot) and the test session's mark (felucca.c, ED_MIRROR) */
static void fm1_reboot(void) { plat_restart(); }
#if FELUCCA_ED_MIRROR
static volatile uint32_t mir_test_boot __attribute__((section(".noinit")));   /* MIR_TEST_MAGIC: the next start is a
                                                 * test session (opt_core_init reads and clears it at once) */
#endif

/* ---- the editor (felucca.c's order: its reply buffer in the pool, then editor.c) */
#include "io/editor/ed_out.h"
#define ED_OUT_HERE 1
static uint8_t ed_out[ED_OUT_N] __attribute__((aligned(4))) IN_POOL;
#undef RING_PUBLISH
#define RING_PUBLISH() opt_sx_publish()         /* (inside the editor's files only: ed_mirror.c mir_pkt, the one use built) */
#include "io/editor/editor.c"
#undef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* (core/core.h's) */
#if FELUCCA_ED_MIRROR
_Static_assert(MIR_TEST_MAGIC == 0x5453544Du, "the test session's mark: felucca.c's");
#endif

/* the host simulator's restart (plat_restart: "reboot") keeps the app's RAM, which an FM-1's restart clears: the
 * editor's own state forgotten there at the start (opt_core_init), so a restart asked by the editor (a restore, the
 * test session) is not asked again by what it left (mir.test_req, bk.reboot) */
static void opt_ed_forget(void)
{
#ifdef PLAT_HOST
#if FELUCCA_ED_MIRROR
    memset(&mir, 0, sizeof mir);
#endif
#if FELUCCA_FLASH && FELUCCA_BACKUP
    memset(&bk, 0, sizeof bk);
#endif
    opt_sx_open = opt_sx_cut = 0;
#endif
}

/* app.c app_sysex: a frame from USB (the bytes between F0 and F7), 1 = the editor's */
int opt_ed_frame(const uint8_t *p, uint32_t n)
{
    opt_ed_usb();
    opt_rx_p = p, opt_rx_n = n, opt_rx_ready = 1, sx_ready = 1;
    ed_service();
    if (!opt_rx_ready)
        return 1;
    opt_rx_ready = 0, sx_ready = 0;             /* (not the editor's: fm1-base's main loop goes on with it) */
    return 0;
}

/* every main loop pass (app.c app_loop): the mirror's catch-ups and STATE, MASTER from its client, a restore's
 * restart, DX7 SysEx (system/main.c: fm6_service, ed_service, bk.reboot) */
static void opt_ed_master(void)
{
#if FELUCCA_ED_MIRROR
    uint32_t pot = plat_master() / 4u, v;       /* (fm1-base's MASTER 0..4096; Optimist's pot 0..1023: main.c master_pot) */
    pot = pot > 1023u ? 1023u : pot;
    v = mir_master(pot);
    opt_master_q12 = v == pot && !(mir.on && mir.master) ? 0u : ((v * v) >> 8) + 1u;   /* (main.c: k10 * k10 >> 8) */
#endif
}
void opt_ed_poll(void)
{
    opt_ed_usb();
    if (opt_sx_open && !sx_busy) {
        (void)plat_sysex_end();                 /* (a mirror frame given up: mir_pkt let the ring go) */
        opt_sx_open = 0;
    }
    opt_sx_cut = 0;                             /* (a cut stream: read full for one pass, the mirror behind) */
    fm1_ms = plat_ms();
    fm6_service();                              /* DX7 SysEx for FM6 (fm6_sysex.c) */
    ed_service();                               /* (no frame: the mirror's catch-ups and STATE) */
    opt_ed_master();
#if FELUCCA_FLASH && FELUCCA_BACKUP
    if (bk.reboot && fm1_ms - bk.reboot_ms > 150u) {   /* a restore done (ed_backup.c): its reply out, restart */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, &FONT_S, "RESTORED: RESTART", C_WHITE, 1);
        fm1_delay_ms(50);
        bk.reboot = 0;                          /* (the host simulator's restart returns: once) */
        fm1_reboot();
    }
#endif
}
#else
static void opt_ed_forget(void) {}
int opt_ed_frame(const uint8_t *p, uint32_t n) { (void)p, (void)n; return 0; }
void opt_ed_poll(void) { fm1_ms = plat_ms(); fm6_service(); }
#endif

/* app.c app_sysex_byte: every SysEx byte from USB (the USB interrupt): DX7 voices and banks (eng_fm6.c, usb.c's call) */
void opt_sysex_byte(uint8_t b)
{
#ifdef FM6_RX
    fm6_sx_byte(b);
#else
    (void)b;
#endif
}
