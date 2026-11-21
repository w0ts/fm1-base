/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The switchable features' hooks (core/opt_feat_decl.h declares them; Optimist's core/feat_mem.c reads them), after the
 * UI: what each feature's setting is, whether it is idle, and its switch-off as the user's own (kept in the settings
 * record). The idle signals in one place:
 *   BT AUDIO   an AUDIO mode on, no device paging, linked, setting up, streaming or being scanned for
 *   BT MIDI    on, no computer connected, no device connected (the central)
 *   USB AUDIO  no computer has chosen an audio alt setting (no stream either way; USB MIDI may be in use)
 *   WI-FI      a mode on, not joined, or joined with no Link peer and no editor session
 *   WI-FI AP   running, no client
 *   EDITOR     no editor or Mirror request for EDITOR_IDLE_MS
 *   USB HOST   on, no device enumerated
 *   MIRROR     never idle while on (a client mirrors over Bluetooth, or asked something in the last 3 s)
 * Only the blocks of core/opt_lend.h are lendable (of_bytes: BT AUDIO's buffers, the mirror's Bluetooth ring): the
 * others are never offered. The pause of a YES to PACK: the
 * transport stopped (what sounds is faded out by feat_mem.c), the parts quiet before the pack (fx_arena.c part_quiet); UNLOAD: the PROJECT page's SAVE into the open project once stopped (op_edits.c
 * prj_save_go), then a NEW project; none open yet: nothing saved, said (SAVE FIRST) and the setting back off */
#pragma once
#if FMEM_NLEND
#define EDITOR_IDLE_MS 60000u
static uint32_t of_bytes(uint32_t f)
{
#if OPT_LEND_BTA
    if (f == OF_BTA)
        return plat_bta_mem_bytes();
#endif
#if MIR_LEND
    if (f == OF_MIR)
        return MIR_LEND_BYTES;                   /* (the ring and the encoder's segment hashes: ed_mirror.c) */
#endif
#if OPT_LEND_LINK
    if (f == OF_WIFI)
        return plat_link_mem_bytes();
#endif
#if OPT_LEND_AP
    if (f == OF_AP)
        return plat_wifi_ap_mem_bytes();
#endif
#if OPT_LEND_UAC
    if (f == OF_UAC)
        return plat_usb_audio_mem_bytes() + OPT_UA_OWN_BYTES;
#endif
    (void)f;
    return 0u;
}
#if OPT_LEND_UAC
/* USB audio's block (core/opt_lend.h L_UAC): wanted while the computer streams either way (an alt set), unless its
 * question was answered NO (or it was switched off for room) since the stream started: then not until it stops. Lent:
 * the rings (plat_usb_audio_mem) first, then the render's buffers (core/opt_core.c opt_play_p, opt_cap_p) */
static uint8_t of_uac_no;
static int of_uac_want(void)
{
    plat_usb_audio_stats_t s;
    plat_usb_audio_stats(&s);
    if (!s.in_alt && !s.out_alt) {
        of_uac_no = 0;
        return 0;
    }
    return !of_uac_no;
}
static void of_uac_lend(void *p)                 /* (the interrupts off: arb_set) */
{
    uint8_t *b = (uint8_t *)p;
    plat_usb_audio_mem(b);
    opt_play_p = b ? (int32_t *)(b + plat_usb_audio_mem_bytes()) : 0;
    opt_cap_p = b ? (int32_t *)(b + plat_usb_audio_mem_bytes()) + OPT_UA_PLAY_N : 0;
}
#endif
#if OPT_LEND_LINK || OPT_LEND_AP
/* Link's node and the access point's state (plat.h plat_link_mem, plat_wifi_ap_mem): wanted while a Wi-Fi MODE (a MODE
 * AP) is chosen, and after it while fm1-base still runs them (its plat_*_mem(0) refuses), then given back. Link starts
 * and the access point comes up only once lent (fm1-base waits for them) */
static uint8_t of_wl_lent;                       /* (a bit each: 1 Link's, 2 the access point's) */
static int of_wl_want(uint32_t bit, int mode_on, int (*mem)(void *))
{
    if (mode_on)
        return 1;
    if ((of_wl_lent & bit) && mem(0) != 0)
        return 1;                                /* (still running: let go first) */
    of_wl_lent &= (uint8_t)~bit;
    return 0;
}
static void of_wl_set(uint32_t bit, void *p, int (*mem)(void *))
{
    if (p)
        of_wl_lent |= (uint8_t)(mem(p) == 0 ? bit : 0u);
    else if (mem(0) == 0)
        of_wl_lent &= (uint8_t)~bit;
}
#endif
/* core/opt_lend.h's blocks by their index there: the size, wanted now, lent or taken back (core/arena_bufs.c) */
static const uint8_t OF_LENT[] = {
#if OPT_LEND_BTA
    OF_BTA,
#endif
#if MIR_LEND
    OF_MIR,
#endif
#if OPT_LEND_LINK
    OF_WIFI,
#endif
#if OPT_LEND_AP
    OF_AP,
#endif
#if OPT_LEND_UAC
    OF_UAC,
#endif
};
static uint32_t of_lend_bytes(uint32_t i) { return of_bytes(OF_LENT[i]); }
static int of_lend_want(uint32_t i)
{
#if OPT_LEND_BTA
    if (i == L_BTA)
        return bta_lend_want();
#endif
#if MIR_LEND
    if (i == L_MIR)
        return mir_lend_want();
#endif
#if OPT_LEND_LINK
    if (i == L_LINK)
        return of_wl_want(1u, plat_wifi_get_mode() != WIFI_MODE_OFF, plat_link_mem);
#endif
#if OPT_LEND_AP
    if (i == L_AP)
        return of_wl_want(2u, plat_wifi_get_mode() == WIFI_MODE_AP || plat_wifi_get_mode() == WIFI_MODE_AP_MAN ||
                                  plat_wifi_ap_state() == WIFI_AP_STARTING || plat_wifi_ap_state() == WIFI_AP_UP,
                          plat_wifi_ap_mem);
#endif
#if OPT_LEND_UAC
    if (i == L_UAC)
        return of_uac_want();
#endif
    return 0;
}
static void of_lend_set(uint32_t i, void *p)    /* (the interrupts off: arb_set) */
{
#if OPT_LEND_UAC
    if (i == L_UAC)
        of_uac_lend(p);
#endif
#if OPT_LEND_BTA
    if (i == L_BTA)
        bta_lend(p);
#endif
#if MIR_LEND
    if (i == L_MIR)
        mir_lend(p);
#endif
#if OPT_LEND_LINK
    if (i == L_LINK)
        of_wl_set(1u, p, plat_link_mem);
#endif
#if OPT_LEND_AP
    if (i == L_AP)
        of_wl_set(2u, p, plat_wifi_ap_mem);
#endif
}
#if OPT_LEND_BTA
static int of_bta_busy(void)
{
    uint32_t s = bta_state();
    return s == BTA_S_SCANNING || s == BTA_S_PAGING || s == BTA_S_LINKED || s == BTA_S_SETUP || s == BTA_S_STREAMING;
}
#endif
static int of_editor_idle(void)
{
#if FELUCCA_EDITOR && FELUCCA_ED_MIRROR
    return !mir.on && fm1_ms - mir.last_ms > EDITOR_IDLE_MS;
#else
    return 0;                                    /* (no signal: never idle) */
#endif
}
static int of_on(uint32_t f)
{
    switch (f) {
#if OPT_LEND_BTA
    case OF_BTA:
        return bta.mode != BTA_OFF;
#endif
#if MIR_LEND
    case OF_MIR:
        return mir_lend_on();
#endif
#if FELUCCA_BLE
    case OF_BLE:
        return ble_on != 0;
#endif
#if FELUCCA_USB_AUDIO
    case OF_UAC:
#if OPT_LEND_UAC
        return of_uac_want();                    /* (its block held only while it streams: on is that, never on and idle) */
#else
        return 1;                                /* (no setting yet: always on) */
#endif
#endif
#if FELUCCA_WIFI
    case OF_WIFI:
        return wn_st.mode != WN_MODE_OFF && wn_st.mode != WN_MODE_AP && wn_st.mode != WN_MODE_AP_MAN;
#endif
#if FELUCCA_WIFI_AP
    case OF_AP:
        return wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN;
#endif
#if FELUCCA_EDITOR
    case OF_EDITOR:
        return 1;
#endif
#if FELUCCA_USB_HOST
    case OF_USBH:
        return plat_usb_host_state() != USBH_OFF;
#endif
    default:
        return 0;
    }
}
static int of_idle(uint32_t f)
{
    if (!of_on(f))
        return 0;
    switch (f) {
#if OPT_LEND_BTA
    case OF_BTA:
        return !of_bta_busy();
#endif
#if FELUCCA_BLE
    case OF_BLE:
        return !plat_ble_connected() && plat_ble_device() != BLE_DEV_READY;
#endif
#if FELUCCA_USB_AUDIO
    case OF_UAC: {
        plat_usb_audio_stats_t s;
        plat_usb_audio_stats(&s);
        return !s.in_alt && !s.out_alt;
    }
#endif
#if FELUCCA_WIFI
    case OF_WIFI:
        return plat_wifi_step() != WIFI_STEP_UP || (plat_link_peers() == 0u && of_editor_idle());
#endif
#if FELUCCA_WIFI_AP
    case OF_AP:
        return plat_wifi_ap_clients() == 0u;
#endif
    case OF_EDITOR:
        return of_editor_idle();
#if FELUCCA_USB_HOST
    case OF_USBH:
        return plat_usb_host_state() != USBH_CONNECTED;
#endif
    default:
        return 0;
    }
}
static void of_off(uint32_t f)                   /* (only the lendable ones are ever switched off here) */
{
#if OPT_LEND_UAC
    if (f == OF_UAC) {
        of_uac_no = 1;                           /* (its question's NO: silent to the computer until the stream stops) */
        return;
    }
#endif
#if MIR_LEND
    if (f == OF_MIR) {
        mir_lend_off();                          /* (the Bluetooth mirror ends; asked again once the central has gone) */
        return;
    }
#endif
#if OPT_LEND_LINK || OPT_LEND_AP
    if ((f == OF_WIFI || f == OF_AP) && of_on(f)) {
        plat_wifi_mode(WIFI_MODE_OFF);           /* (as MODE OFF on SYSTEM > WI-FI, kept: their blocks go back by themselves) */
        return;
    }
#endif
#if OPT_LEND_BTA
    if (f != OF_BTA || !of_on(f))
        return;
    bt_enter(BT_AUD, BT_OFF);                    /* (as MODE OFF on SYSTEM > BT: its block goes back by itself) */
    sys_dirty = 1;
    settings_later = 1;                          /* (kept: the settings record, once quiet) */
#endif
    (void)f;
}
static uint8_t of_unload_st;                     /* UNLOAD asked for feature n - 1: waiting for the transport to stop */
static int of_playing(void) { return song.playing || transport_req == 1u; }
static void of_pause(void)                       /* (the voices: feat_mem.c fmem_hush, after) */
{
    if (song.playing || transport_req == 1u)
        transport_req = 2;                       /* (stopped: the sequencer's notes end) */
}
static void of_resume(void) { transport_req = 1; }
static int of_quiet(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (!part_quiet(k))
            return 0;
    return 1;
}
static void of_unload(uint32_t f)
{
    if (prj_cur < 0) {
        ui_message("SAVE FIRST");                /* (no open project to save into: nothing unloaded) */
        of_off(f);
        return;
    }
    if (song.playing || transport_req == 1u)
        transport_req = 2;
    of_unload_st = (uint8_t)(f + 1u);
}
static void of_say_full(uint32_t f)
{
    char b[24];
    str_cpy(b, "MEM FULL ", sizeof b);
    str_app(b, FMEM[f].name, sizeof b);
    ui_message(b);
}
/* every main loop pass (opt_bt_poll): an UNLOAD once stopped: saved, then a NEW project; the block placed by itself
 * (arb_poll) once the new project's parts have switched */
static void of_poll(void)
{
    uint32_t f = of_unload_st - 1u;
    if (!of_unload_st || song.playing || transport_req)
        return;
    of_unload_st = 0;
    if (prj_save_go((uint32_t)prj_cur)) {
        of_off(f);                               /* (not saved: nothing unloaded, the setting back off) */
        return;
    }
    op_project_new();
    ui_message("SAVED, NEW PROJECT");
}
#else
static void of_poll(void) {}
#endif
