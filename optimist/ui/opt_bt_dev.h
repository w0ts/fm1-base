/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's Bluetooth MIDI devices in the trial (BLE_CENTRAL 1 or 3: SYSTEM > BT MIDI's list, LAST and the other
 * remembered devices with their bonds, the search for LAST, a pick's connection and its pairing, the passkey):
 * io/midi/ble_devices.c and its ble_connect.c compiled unchanged, as Optimist's midi_ble.c includes them, on fm1-base's
 * central (fm1.toml ble_central, from the configuration: gen_optimist.py --conf), which is the same stack's (bt/ble,
 * Optimist's), through plat.h's devices one by one (APP_BLE_PEERS, app.h): here the stack calls those two files make,
 * under their own names, on plat_ble_*:
 *   the scan       ble_ll_scan on plat_ble_scan; its reports, which fm1-base hands the app (app.c app_ble_scan_report,
 *                  main loop), queued for ble_ll_scan_take (ble_devices_poll, the main loop): Optimist's own table
 *                  (ble/ble_scan.c) builds the list from them, as on its own stack
 *   connecting     ble_central_connect / cancel on plat_ble_connect_peer / plat_ble_disconnect (struct ble_peer is
 *                  plat.h's plat_ble_peer), the state, why it failed, the pairing's flags and the passkey from
 *                  plat_ble_device, plat_ble_device_fail, plat_ble_device_flags and plat_ble_passkey; a private address
 *                  resolved by plat_ble_rpa_resolve
 *   the keys       a pairing's keys (app.c app_ble_device_keys, the main loop) to ble_connect.c's ble_app_central_keys
 *   the hold       fm1_ble_irqs_hold: nothing (fm1-base's calls hold the stack's interrupts themselves)
 * fm1-base keeps one link at a time (bt_own_central.c): connected to a device, the FM-1 does not advertise. Main loop. */
#pragma once

#if FELUCCA_BLE && BLE_CENTRAL
#include "ble/ble.h"                            /* (Optimist's: struct ble_peer, ble_keys, BLE_CS_*, BLE_CF_*: plat.h's) */
#include "ble/ble_ll.h"
#include "ble/ble_scan.c"                       /* (Optimist's: the nearby devices' table, from the reports) */
_Static_assert(sizeof(struct ble_peer) == sizeof(struct plat_ble_peer) && sizeof(struct ble_keys) ==
               sizeof(struct plat_ble_keys), "plat.h plat_ble_peer / plat_ble_keys: ble/ble.h's ble_peer / ble_keys");
_Static_assert(BLE_CF_NEED_PAIR == PLAT_BLE_CF_NEED_PAIR && BLE_CF_NEED_MITM == PLAT_BLE_CF_NEED_MITM &&
               BLE_PEER_MITM == PLAT_BLE_PEER_MITM && BLE_KEYS_AUTH == PLAT_BLE_KEYS_AUTH, "plat.h: ble/ble.h's codes");

static void fm1_ble_irqs_hold(int on) { (void)on; }

/* ---- the scan's reports (app.c app_ble_scan_report -> opt_ble_scan_report, the main loop), for ble_ll_scan_take */
#define OBS_N 16u
static uint8_t obs_pdu[OBS_N][2u + 37u], obs_len[OBS_N];
static uint16_t obs_rssi[OBS_N];
static uint32_t obs_w, obs_r;
void opt_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi)
{
    uint32_t i;
    if (n > sizeof obs_pdu[0] || obs_w - obs_r >= OBS_N)
        return;                                 /* (a full queue: the scan repeats itself) */
    for (i = 0; i < n; i++)
        obs_pdu[obs_w % OBS_N][i] = pdu[i];
    obs_len[obs_w % OBS_N] = (uint8_t)n;
    obs_rssi[obs_w % OBS_N] = (uint16_t)rssi;
    obs_w++;
}
static uint8_t ble_ll_scan_take(uint8_t *pdu, uint16_t *rssi)
{
    uint32_t i, n;
    if (obs_r == obs_w)
        return 0;
    n = obs_len[obs_r % OBS_N];
    for (i = 0; i < n; i++)
        pdu[i] = obs_pdu[obs_r % OBS_N][i];
    *rssi = obs_rssi[obs_r % OBS_N];
    obs_r++;
    return (uint8_t)n;
}
static void ble_ll_scan(int on)
{
    if (!on)
        obs_r = obs_w;                          /* (what was heard before the stop: not news any more) */
    plat_ble_scan(on);
}
static int ble_ll_scanning(void) { return plat_ble_device() == BLE_DEV_SCANNING; }
static int ble_ll_initiating(void) { return plat_ble_device() == BLE_DEV_CONNECTING; }
static int ble_ll_central(void)                 /* (connected, and we are the master) */
{
    int d = plat_ble_device();
    return d == BLE_DEV_SEARCHING || d == BLE_DEV_READY;
}

/* ---- connecting out (ble/ble.h's central, on plat.h's) */
static int ble_central_connect(const struct ble_peer *p)
{
    return plat_ble_connect_peer((const struct plat_ble_peer *)(const void *)p) == 0;
}
static void ble_central_cancel(void) { plat_ble_disconnect(); }
static uint8_t ble_central_state(void)
{
    switch (plat_ble_device()) {
    case BLE_DEV_CONNECTING:
        return BLE_CS_CONNECTING;
    case BLE_DEV_SEARCHING:
        return BLE_CS_SETUP;
    case BLE_DEV_READY:
        return BLE_CS_READY;
    default:
        return BLE_CS_IDLE;
    }
}
static uint8_t ble_central_fail(void) { return (uint8_t)plat_ble_device_fail(0); }
static uint8_t ble_central_code(void)
{
    uint32_t c;
    (void)plat_ble_device_fail(&c);
    return (uint8_t)c;
}
#if BLE_CEN_PAIR
static int ble_central_pairing(void) { return (plat_ble_device_flags() & PLAT_BLE_DF_PAIRING) != 0; }
static int ble_central_prompted(void) { return (plat_ble_device_flags() & PLAT_BLE_DF_PROMPTED) != 0; }
static int ble_central_mitm(void) { return (plat_ble_device_flags() & PLAT_BLE_DF_MITM) != 0; }
static uint32_t ble_central_passkey(void)
{
    int k = plat_ble_passkey();
    return k < 0 ? BLE_NO_PASSKEY : (uint32_t)k;
}
static int ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6]) { return plat_ble_rpa_resolve(irk, addr); }
#endif

#include "io/midi/ble_devices.c"                /* SYSTEM > BT MIDI's list, LAST, the search (ble_connect.c) */

/* every main loop pass, before ble_devices_poll (ui/opt_bt.h opt_bt_poll): the radio come up with the list open, its
 * scan starts (ble_devices.c asks only while the radio is up; Optimist's starts at once, fm1-base's may take a moment:
 * the host simulator's a second) */
static void opt_ble_dev_poll(void)
{
    static uint8_t was_up;
    uint8_t up = (uint8_t)(ble_up != 0);
    if (up && !was_up && ble_devs_open)
        ble_ll_scan(1);
    was_up = up;
}

/* a pairing with a device: its keys (app.c app_ble_device_keys, the main loop) to ble_connect.c, as the stack's
 * callback gives them on Optimist (there in the BLE interrupts' context: ble_connect.c copies them) */
void opt_ble_device_keys(const struct plat_ble_keys *k)
{
#if BLE_CEN_PAIR
    ble_app_central_keys((const struct ble_keys *)(const void *)k);
#else
    (void)k;
#endif
}
#elif OPT_BLE_PEERS
/* (the host without OPT_HOST_BT: the Optimist unit has no Bluetooth, fm1-base's central reports to nobody) */
void opt_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi) { (void)pdu, (void)n, (void)rssi; }
void opt_ble_device_keys(const struct plat_ble_keys *k) { (void)k; }
#endif
