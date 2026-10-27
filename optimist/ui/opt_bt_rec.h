/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The settings record's Bluetooth fields (storage/project.c: bt_rec_in / out, bt_key_in / out, bt_kept_in / out), after
 * the UI (ui/opt_bt.h renamed op_bt.c's own): SYSTEM > Bluetooth's modes as op_bt.c writes them, the audio devices
 * (the last one's address and name, our BR/EDR address, the keys, the other three) from opt_btr, fm1-base's record of
 * them (plat.h struct plat_ble_rec), in the same bytes as Optimist's own: its firmware reads them, and the trial reads
 * Optimist's. Main loop only. */
#pragma once

#if FELUCCA_BLE || FELUCCA_BT_AUDIO
#undef bt_rec_out
#undef bt_rec_in
#undef bt_key_out
#undef bt_key_in
#undef bt_kept_out
#undef bt_kept_in
static void bt_rec_out(uint8_t r[BT_REC_SIZE])
{
    op_bt_rec_out(r);                           /* (the mark, the modes; the last device below) */
#if FELUCCA_BT_AUDIO
    memset(r + 5, 0, 1u + 6u + 17u);
    r[5] = (uint8_t)(opt_btr.bta_n != 0u);
    if (opt_btr.bta_n) {
        memcpy(r + 6, opt_btr.bta[0].addr, 6);
        str_cpy((char *)r + 12, opt_btr.bta[0].name, 17);
    }
#endif
}
static void bt_rec_in(const uint8_t r[BT_REC_SIZE])
{
    uint32_t mk;
    op_bt_rec_in(r);                            /* (the modes) */
#if FELUCCA_BT_AUDIO
    memcpy(&mk, r, 4);
    if (mk != BT_REC_MARK)
        return;
    opt_btr.bta_n = (uint8_t)(r[5] == 1u);
    memset(opt_btr.bta, 0, sizeof opt_btr.bta);
    memcpy(opt_btr.bta[0].addr, r + 6, 6);
    str_cpy(opt_btr.bta[0].name, (const char *)r + 12, sizeof opt_btr.bta[0].name);
#else
    (void)mk;
#endif
}
#if FELUCCA_BT_AUDIO
static void bt_key_out(uint8_t r[32])
{
    uint32_t mk = BT_KEY_MARK;
    memset(r, 0, 32);
    memcpy(r, &mk, 4);
    r[4] = opt_btr.bta_addr_set, r[5] = opt_btr.bta[0].bonded;
    memcpy(r + 6, opt_btr.bta_addr, 6);
    memcpy(r + 12, opt_btr.bta[0].link_key, 16);
}
static void bt_key_in(const uint8_t r[32])
{
    uint32_t mk;
    memcpy(&mk, r, 4);
    if (mk != BT_KEY_MARK)
        return;
    opt_btr.bta_addr_set = (uint8_t)(r[4] == 1u), opt_btr.bta[0].bonded = (uint8_t)(r[5] == 1u);
    memcpy(opt_btr.bta_addr, r + 6, 6);
    memcpy(opt_btr.bta[0].link_key, r + 12, 16);
}
static void bt_kept_out(uint8_t r[BT_KEPT_SIZE])
{
    memset(r, 0, BT_KEPT_SIZE);
    if (opt_btr.bta_n > 1u)
        memcpy(r, &opt_btr.bta[1], 40u * (opt_btr.bta_n - 1u));
}
static void bt_kept_in(const uint8_t r[BT_KEPT_SIZE])   /* (after bt_rec_in and bt_key_in: none without a last device) */
{
    for (; opt_btr.bta_n && opt_btr.bta_n < 4u && r[23]; r += 40)   /* (r[23]: the name's first byte) */
        memcpy(&opt_btr.bta[opt_btr.bta_n++], r, 40);
}
#endif

/* the host simulator's restart keeps the app's RAM, which an FM-1's clears: SYSTEM > Bluetooth's state forgotten there at
 * the start (opt_core_init: the record gives it again), as opt_ed_forget does the editor's */
static void opt_bt_forget(void)
{
#ifdef PLAT_HOST
    memset(&bt, 0, sizeof bt);
#if FELUCCA_BT_AUDIO
    memset(&bta, 0, sizeof bta);
#endif
    memset(&opt_btr, 0, sizeof opt_btr);
#if FELUCCA_BLE
    ble_on = 0;
#endif
#endif
}
#else
static void opt_bt_forget(void) {}
#endif
