/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's Wi-Fi in the trial (WIFI_LINK: SYSTEM > WI-FI, the web editor's Wi-Fi tab, Link as a clock): its page (ui/optimist/op_wifi.c), its editor command (io/editor/ed_wifi.c, 112) and its settings record's field
 * compiled unchanged, on fm1-base's own Wi-Fi stack (fm1.toml wifi_stack = "own", link = true: gen_optimist.py --conf),
 * which is Optimist's moved (fm1-base's wifi/, device/wifi_own.c, link/), through plat.h, in place of Optimist's
 * wifi/wifi_conn.c, wifi_link.c and the stack under them: one copy of the Wi-Fi code in the build. MODE AP (WIFI_AP):
 * fm1-base's (fm1.toml wifi_ap = true from gen_optimist.py --conf; device/wifi_own_ap.c, which is Optimist's
 * wifi/wifi_apd.c moved), the page's and the editor's wa_* on plat_wifi_ap_*.
 *   the manager   wc, wc_req, wc_tab, wl.ip.ip (what the page and the editor read) mirrored from plat_wifi_step,
 *                 _fail, _fails, _found_ap, _joining, _joined_slot, _saved_join, _addr at every main loop pass
 *                 (opt_wifi_poll), before the UI; wc.scan_want (the page's) to plat_wifi_scanning; wc_request,
 *                 wc_enable, wc_disconnect on plat_wifi_join_ssid / _leave
 *   the networks  wn_st is the settings record's field as Optimist keeps it (storage/persist_rec.c wifi_nets,
 *                 project.c persist_boot / settings_save: wifi_nets_in / _out), handed to fm1-base as its record
 *                 (APP_WIFI_STORE: app.c app_wifi_store); fm1-base's wifi_nets.c reads and writes it (the page's
 *                 wn_find / wn_forget / wn_scan_best_for are fm1-base's functions: wifi_nets.h is the same file), and
 *                 a change it makes (a join kept, a network forgotten) is saved with the settings (settings_later)
 *   MODE AP       AP AUTO / AP MAN are the record's modes (wn_st.mode, read by fm1-base each pass); NAME, CHANNEL,
 *                 STATE, SAVE (the password), SAVE held (CLEAR KEY), the editor's ops 5 / 6 / 7 / 8: wa_name,
 *                 wa_channel, wa_status, wa_clients, wa_set_pass, wa_set_channel, wa_forget_key, wa_run on
 *                 plat_wifi_ap_name, _channel, _state, _clients, _set, _set_channel, _forget, _run
 *   Link          the session as Optimist's MIDI clock (its wifi_link.c wl_clock: 24 F8 a beat at the time Link puts
 *                 each tick, FA at the next bar line, FC at a stop) from plat_link_*, through a ring to the 1 kHz tick
 *                 (opt_link_tick: MSRC_LINK input to clock_sync.c); the FM-1's own PLAY / STOP and tempo to the session
 *                 when Link does not drive it (wl_push_own); wl_peers on plat_link_peers.
 * Main loop, but opt_link_tick (the tick). Included by ui/opt_ui.c before Optimist's UI (optimist.c: WIFI_UI) and its
 * editor (ed_opti.h: ED_HAS_WIFI), which WIFI_ON_FM1B lets have Wi-Fi with no serial console. */
#pragma once
#if FELUCCA_WIFI && FELUCCA_BLE
#define WIFI_ON_FM1B 1
#include "wifi/wifi_nets.h"                     /* (Optimist's header, fm1-base's wifi/wifi_nets.h byte for byte: the record, the
                                                 * table; their functions fm1-base's, linked from its unit) */
_Static_assert(sizeof(struct wn_ap) == sizeof(struct plat_wifi_ap) && WN_STORE_SIZE == PLAT_WIFI_STORE_SIZE,
               "wifi_nets.h: fm1-base's plat.h records");
#ifndef FELUCCA_WIFI_AP
#define FELUCCA_WIFI_AP 0                       /* (MODE AP's plat_wifi_ap_* are fm1-base's with fm1.toml wifi_ap = true, which
                                                 * gen_optimist.py --conf gives with the configuration's WIFI_AP) */
#endif
#define LK_UBEAT_US 1000000                     /* micro-beats a beat (Link's wire unit: fm1-base's link.h LK_UBEAT) */

/* ---- the networks kept: the settings record's field */
static struct wn_store wn_st;
static void wifi_nets_in(const uint8_t r[WN_STORE_SIZE])
{
    wn_load(&wn_st, r);
    if (!FELUCCA_WIFI_AP && (wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN))
        wn_st.mode = WN_MODE_OFF;                /* (a record from a build with MODE AP: this one has none) */
}
static void wifi_nets_out(uint8_t r[WN_STORE_SIZE]) { wn_save(&wn_st, r); }
uint8_t *opt_wifi_store(void) { return (uint8_t *)&wn_st; }   /* (app.c app_wifi_store: fm1-base's record) */
void opt_wifi_dirty(void) { settings_later = 1; }   /* (app.c app_wifi_store_dirty: saved with the settings once quiet) */
/* a network forgotten: through fm1-base (the joined slot follows, the record marked) */
#define wn_forget(s, i) ((void)(s), plat_wifi_forget((int)(i)))

/* ---- the manager, as the page and the editor read it (wifi_conn.c's names) */
enum { WC_OFF, WC_RADIO, WC_IDLE, WC_SCAN, WC_PMK, WC_AUTH, WC_ASSOC, WC_HS, WC_UP, WC_FAIL, WC_LINK, WC_PRE };
_Static_assert(WC_PRE == WIFI_STEP_PRE && WC_UP == WIFI_STEP_UP && WC_LINK == WIFI_STEP_JOINED, "plat.h WIFI_STEP_*");
enum { WCF_NONE, WCF_NOCAL, WCF_NOAP, WCF_AUTH, WCF_ASSOC, WCF_HS, WCF_KEY };
_Static_assert(WCF_KEY == WIFI_FAIL_KEY && WCF_NOCAL == WIFI_FAIL_RADIO, "plat.h WIFI_FAIL_*");
static struct {
    uint8_t st, fail, ch, scan_want, saved_join, joined_slot, fail_new;
    struct wn_ap ap;
} wc;
static struct {
    uint8_t on, ssid_len;
    uint8_t ssid[WN_SSID_MAX];
} wc_req;                                       /* a join by name, while its network is looked for (the page names it) */
static struct wn_scan wc_tab;
static struct { struct { uint32_t ip; } ip; } wl;   /* (wl.ip.ip: the address, host order) */
static uint32_t wc_fails_seen;
static void opt_wifi_sync(void)
{
    struct wn_ap a;
    uint32_t i, f = plat_wifi_fails();
    wc.st = (uint8_t)plat_wifi_step();
    wc.fail = (uint8_t)(plat_wifi_fail() == WIFI_FAIL_LOST ? WCF_NOAP : plat_wifi_fail());   /* (NETWORK LOST: as NOT FOUND,
                                                                                                 * the page's words) */
    if (f != wc_fails_seen) {
        wc_fails_seen = f;
        wc.fail_new = 1;                        /* (the page says it once and clears it) */
    }
    wc.saved_join = (uint8_t)plat_wifi_saved_join();
    wc.joined_slot = (uint8_t)(plat_wifi_joined_slot() + 1);
    if (plat_wifi_joining((struct plat_wifi_ap *)&a)) {
        wc.ap = a;
        wc.ch = a.ch;
    } else {
        wc.ch = 0;
    }
    for (i = 0; i < WN_SCAN_MAX && plat_wifi_found_ap((int)i, (struct plat_wifi_ap *)&wc_tab.ap[i]); i++)
        ;
    wc_tab.n = (uint8_t)i;
    wl.ip.ip = plat_wifi_addr();
    if (wc_req.on && wc.st != WC_SCAN)
        wc_req.on = 0;                          /* (found and being joined, or given up: fm1-base has it) */
}
static int wc_request(const uint8_t *ssid, uint32_t len, const char *pass)
{
    int rc = plat_wifi_join_ssid(ssid, len, pass);
    if (rc == 0) {
        memcpy(wc_req.ssid, ssid, len);
        wc_req.ssid_len = (uint8_t)len;
        wc_req.on = 1;
        wc.st = WC_SCAN;
    }
    return rc;
}
static void wc_enable(int on)
{
    if (!on)
        plat_wifi_leave();                      /* (the mode is the record's: MODE OFF stops the manager at its next pass) */
}
static void wc_disconnect(void) { plat_wifi_leave(); }

#if FELUCCA_WIFI_AP
/* ---- MODE AP, as the page and the editor call it (Optimist's wifi_apd.c names) on fm1-base's plat_wifi_ap_* */
enum { WAS_OFF = WIFI_AP_OFF, WAS_NOKEY = WIFI_AP_NOKEY, WAS_STARTING = WIFI_AP_STARTING, WAS_UP = WIFI_AP_UP,
       WAS_NOCAL = WIFI_AP_FAILED, WAS_STOPPED = WIFI_AP_STOPPED };
#define WA_CH_DEFAULT 6u                        /* (fm1-base's default channel) */
static uint32_t wa_status(void) { return (uint32_t)plat_wifi_ap_state(); }
static uint32_t wa_clients(void) { return plat_wifi_ap_clients(); }
static uint32_t wa_channel(void) { return (uint32_t)plat_wifi_ap_channel(); }
static uint32_t wa_name(uint8_t *out)           /* the network's name (no terminating zero: out holds WN_SSID_MAX) */
{
    char b[WN_SSID_MAX + 1];
    uint32_t n = (uint32_t)plat_wifi_ap_name(b);
    memcpy(out, b, n);
    return n;
}
static int wa_set_pass(const uint8_t *ssid, uint32_t len, const char *pass) { return plat_wifi_ap_set(ssid, len, pass); }
static void wa_set_channel(uint32_t ch) { (void)plat_wifi_ap_set_channel((int)ch); }
static void wa_forget_key(void) { plat_wifi_ap_forget(); }
static int wa_run(int on) { return plat_wifi_ap_run(on); }
#endif

/* ---- Link: the session as MIDI clock (Optimist's wifi_link.c wl_clock, wl_push_own, wl_midi_poll) */
#define WL_Q 16u                                /* the tick ring (the main loop writes, the 1 kHz tick reads) */
#define WL_AHEAD_US 25000                       /* ticks are queued this far before their time */
static struct {
    uint8_t feed, started, own_playing, on;
    uint32_t k;                                 /* the next MIDI clock tick: 24 a beat of the session */
    int32_t own_mpb;
    uint32_t n_ticks, n_restart, n_catchup;
} wlk;
static uint32_t wl_qp[WL_Q], wl_qt[WL_Q];
static int64_t wl_qus[WL_Q];
static volatile uint32_t wl_qw, wl_qr;
static uint32_t wl_peers(void) { return link_mode != LINK_OFF ? plat_link_peers() : 0u; }   /* (LINK OFF: no session, 0) */
/* 64-bit products in a function of their own (fm1-base's check of felucca-bsp docs/CPU.md: an inlined 32x32->64
 * multiply beside a load) */
static __attribute__((noinline)) uint64_t wl_mul(uint64_t a, uint32_t b) { return a * b; }
static int wl_q_push(uint32_t st, int64_t t)    /* 0: the ring is full, the message was NOT queued (retry next pass) */
{
    if (wl_qw - wl_qr >= WL_Q)
        return 0;
    wl_qp[wl_qw % WL_Q] = 0x0Fu | st << 8;      /* (CIN F: one system real-time byte) */
    wl_qt[wl_qw % WL_Q] = plat_link_t24(t);
    wl_qus[wl_qw % WL_Q] = t;
    RING_PUBLISH();
    wl_qw++;
    return 1;
}
/* the tick: a queued message whose time has come -> clock_sync (its time: plat_ticks24's at Link's; on the host the
 * simulator's own time decides, the stamp its tick's) */
static void opt_link_tick(void)
{
    while (wl_qr != wl_qw) {
        uint32_t k = wl_qr % WL_Q, t = wl_qt[k];
#ifdef PLAT_HOST
        if (wl_qus[k] > plat_link_now())
            return;
        t = fm1_ticks();
#else
        if ((int32_t)(fm1_ticks() - t) < 0)
            return;
#endif
        if (!midi_in_enqueue(wl_qp[k], MSRC_LINK, t))
            return;                             /* (the router's ring is full: the next tick) */
        RING_PUBLISH();
        wl_qr++;
    }
}
static void wl_clock(int64_t now)               /* Link's session as MIDI clock */
{
    int on = wl.ip.ip && link_mode != LINK_OFF && plat_link_peers() > 0;
    if (!on) {
        if (wlk.started)
            (void)wl_q_push(0xFCu, now);
        wlk.feed = wlk.started = 0;
        return;
    }
    if (!wlk.feed) {                            /* the next tick at or after now */
        int64_t ub = plat_link_beat_at(now);
        if (ub < 0)
            return;
        wlk.k = sy_div(wl_mul((uint64_t)ub, 24u), LK_UBEAT_US) + 1u;
        wlk.feed = 1;
        wlk.started = 0;
        wlk.n_restart++;
    }
    for (;;) {
        uint32_t m = wlk.k / 3u, r = wlk.k - m * 3u;
        int64_t ub = (int64_t)wl_mul(m, 125000u) + (r == 0u ? 0 : r == 1u ? 41667 : 83333), t = plat_link_time_at(ub);
        int play = link_mode == LINK_ONSS && plat_link_playing();   /* (LINK ON: tempo and beat, not the session's play / stop) */
        if (t > now + WL_AHEAD_US)
            break;
        if (t < now - 200000) {                 /* late: the main loop stalled, or the session moved */
            if (play && wlk.started && plat_link_beat_at(now) - ub < 4 * LK_UBEAT_US) {
                t = now;                        /* a bar or less behind: the missed ticks now (the sequencer catches up) */
                wlk.n_catchup++;
            } else {
                wlk.feed = 0;                   /* further (or stopped): start again from the session beat */
                return;
            }
        }
        if (play && !wlk.started && wlk.k % 96u == 0u) {   /* the next bar line (4 beats: the quantum) */
            if (!wl_q_push(0xFAu, t))
                break;
            wlk.started = 1;
        } else if (!play && wlk.started) {
            if (!wl_q_push(0xFCu, t))
                break;
            wlk.started = 0;
        }
        if (!wl_q_push(0xF8u, t))
            break;                              /* ring full: keep k, the tick goes out next pass (never dropped) */
        wlk.n_ticks++;
        wlk.k++;
    }
}
static int32_t wl_mpb_now(void) { return 60000000 / (song.g[G_BPM] > 20 ? song.g[G_BPM] : 20); }
static void wl_push_own(void)                   /* the FM-1's own PLAY / STOP and tempo, to the Link session */
{
    int32_t mpb;
    if (!wl.ip.ip || link_mode == LINK_OFF || plat_link_peers() == 0 || sy.src == MSRC_LINK)
        return;                                 /* (no session to tell yet, or Link itself drives us: no echo) */
    mpb = wl_mpb_now();
    if (mpb != wlk.own_mpb) {
        plat_link_set_tempo(mpb);
        wlk.own_mpb = mpb;
    }
    if (song.playing != wlk.own_playing) {
        if (link_mode == LINK_ONSS)             /* (LINK ON: our play / stop is not published) */
            plat_link_set_playing(song.playing);
        wlk.own_playing = song.playing;
    }
}

/* ---- the main loop's pass (app.c app_loop), before the UI reads the mirrors */
static void opt_wifi_poll(void)
{
    if (!wlk.on) {                              /* Link runs whenever Wi-Fi has an address (Optimist's: with every join) */
        wlk.on = 1;
        wlk.own_mpb = wl_mpb_now();
        wlk.own_playing = song.playing;
        plat_link_set_tempo(wlk.own_mpb);       /* (the tempo a session of its own starts with) */
    }
    plat_link_enable(link_mode != LINK_OFF);    /* (LINK OFF on the MIDI page: no session; once on, nothing: wanted at every pass, so a restart of the stack finds it) */
    opt_wifi_sync();
    plat_wifi_scanning(wc.scan_want);
    wl_clock(plat_link_now());
    wl_push_own();
}
#else
static void opt_wifi_poll(void) {}
static void opt_link_tick(void) {}
#endif
void opt_wifi_loop(void) { opt_wifi_poll(); }   /* (app.c app_loop) */
