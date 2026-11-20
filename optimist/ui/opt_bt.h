/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's Bluetooth in the trial (BLE: SYSTEM > BT MIDI; BT_AUDIO: SYSTEM > BT AUDIO; the screen mirrored over
 * Bluetooth): its screens (ui/optimist/op_bt.c, op_project.c), its devices list (io/midi/ble_devices.c; with the
 * central, BLE_CENTRAL: ui/opt_bt_dev.h), its settings record's Bluetooth fields and its mirror (io/editor/ed_mirror.c's
 * Bluetooth ring) compiled unchanged, on fm1-base's Bluetooth: its own stack (fm1.toml ble_stack = "own"), which is
 * Optimist's (bt/), through plat.h's calls, in place of Optimist's io/midi/midi_ble.c and bt/bt_audio.c:
 *   radio     ble_midi_set: plat_ble_enable; the radio's start and calibration are fm1-base's (at every start: no
 *             stored trims needed, ble_radio_ok always 1); BT MIDI OFF with BT AUDIO on: the radio up, no advertising
 *   MIDI in   opt_ble_in (core/opt_core.c opt_core_midi, the tick): channel messages and clock with the time they were
 *             sent (plat_midi_in_t24: fm1.toml ble_midi_time, the sender's BLE-MIDI timestamps, Optimist's
 *             ble_in_time), the mirror's SysEx to ed_mirror.c (mir_ble_rx); a link gone ends the notes it left on
 *   MIDI out  midi_rings.c's events (ble_midi_out) and CLOCK OUT's F8s at their time (ble_rt_*) to the computer's link
 *             (TO_BLE_HOST) from the tick; the mirror's frames (mir_ble_peek / pop) read by the radio's interrupts
 *             straight from the mirror's ring (plat_ble_stream, the own stack), whole: while one is under way the
 *             channel messages and the clock wait, as Optimist's own radio never splits a frame
 *   audio     bta and bta_* (bt/bt_audio.h, as op_bt.c reads them) on plat_bta_*: the remembered devices as fm1-base
 *             lists them (plat_bta_known: the last one first), the found ones (plat_bta_found) less those; AUTO's round
 *             pages the oldest remembered one (plat_bta_reconnect makes it the last: each in turn)
 *   kept      fm1-base's record of what Bluetooth keeps (plat.h struct plat_ble_rec, APP_BLE_STORE: app.c) is Optimist's
 *             settings record's fields as Optimist keeps them (the BLE address, the bonded central's key, the BR/EDR
 *             address and the remembered audio devices with their keys: storage/persist_rec.c ble_addr, ble_bond,
 *             bt_rec, bt_key, bt_kept), so an FM-1 keeps its pairings from Optimist's firmware to the trial and back;
 *             ble_rf (Optimist's copy of the stored trims) and ble_dev (its devices list) as read
 * Main loop, but opt_ble_in, opt_ble_tick and ble_midi_out / ble_rt_push (the tick and the render). */
#pragma once
#define MIR_BLE_BUILT (FELUCCA_EDITOR && FELUCCA_ED_MIRROR && FELUCCA_BLE)   /* the mirror over Bluetooth (opt_editor.h MIR_BLE) */

#if FELUCCA_BLE || FELUCCA_BT_AUDIO
#ifndef BLE_API
#define BLE_API static                          /* (Optimist's unity build's: these files' names stay this unit's) */
#endif
#if !FELUCCA_BLE
/* BT AUDIO without BT MIDI (Optimist's felucca.c: io/midi/ble_none.c in place of midi_ble.c, before anything of ble/:
 * its BLE_CENTRAL 0 first): the page's MIDI side as Optimist's own build has it, never on, no device, no list.
 * fm1-base's Bluetooth audio rides on its BLE stack (the radio's start and calibration: fm1.toml ble follows BT_AUDIO
 * too, gen_optimist.py --conf), which never advertises here: Bluetooth starts with an AUDIO mode only (opt_ble_at_boot
 * 0), and bta_start switches its BLE side off */
#include "io/midi/ble_none.c"
#endif
#include "ble/ble_vm.h"                         /* (BLE_RF_COPY_SIZE: the settings record's field) */
#define BLE_BOND_SIZE 28u                       /* (midi_ble.c's: the settings record's field) */
#define BLE_BOND_KEPT 0xB5u
#define BLE_ADDR_KEPT 0xA5u
static uint8_t ble_addr_kept[8], ble_rf_kept[BLE_RF_COPY_SIZE], ble_bond_kept[BLE_BOND_SIZE];
static uint8_t settings_later;                  /* (ui/panel.c's: saved with the settings once quiet, project.c) */
#endif
#if FELUCCA_BLE
#include "ble/ble_store.c"                      /* (Optimist's: the devices list's store, the record's ble_dev: plain data) */
static uint8_t ble_on;                          /* SYSTEM > BT MIDI on (the settings word's bit 24: settings_word.c) */
#define ble_up (plat_ble_state() == BLE_READY)  /* (the radio started) */
#if BLE_CENTRAL
/* a link is up, either role (midi_ble.c's): the computer's, or the device's once ready (fm1-base's one link) */
static int ble_connected(void) { return plat_ble_connected() || plat_ble_device() == BLE_DEV_READY; }
#else
static int ble_connected(void) { return plat_ble_connected(); }
#endif
static int ble_radio_ok(void) { return 1; }     /* (fm1-base calibrates the radio at every start) */
#define ble_out_on (ble_on && plat_ble_connected())   /* (a computer listens: ed_mirror.c mir_bt_up) */
/* the MIDI's link: the computer's, or with BLE_CENTRAL a device the FM-1 connected to (fm1-base's one link at a time:
 * ui/opt_bt_dev.h), the channel messages and CLOCK OUT to whichever (the mirror's frames: to the computer alone) */
#if BLE_CENTRAL
#define ble_midi_linked ble_connected()
#define OPT_BLE_MIDI_TO TO_BLE
#else
#define ble_midi_linked plat_ble_connected()
#define OPT_BLE_MIDI_TO TO_BLE_HOST
#endif
#define ble_midi_on (ble_on && ble_midi_linked)
/* the MIDI counters SYSTEM > BT MIDI's ACTIVITY compares (midi_ble.c's rings' indexes there) */
#define bmi_w plat_midi_count(PORT_BLE, 0)
#define bmo_w plat_midi_count(PORT_BLE, 1)
#endif

#if FELUCCA_BT_AUDIO
#include "bt/bt_audio.h"                        /* (Optimist's: bta as the screens read it, the bta_* calls below) */
#endif
#if FELUCCA_BLE
static void ble_midi_set(uint8_t on);
#if !BLE_CENTRAL
#include "io/midi/ble_devices.c"                /* SYSTEM > BT MIDI's list (NONE / LAST: no central in the dev profile) */
#endif
#endif
#include "opt_bt_dev.h"                         /* with BLE_CENTRAL: the list, LAST and the search on fm1-base's central */

#if FELUCCA_BLE || FELUCCA_BT_AUDIO
/* ---- what fm1-base keeps, in Optimist's settings record (app.c app_ble_load / app_ble_save, APP_BLE_STORE). The
 * audio devices are Optimist's struct bta_mem as the record keeps them (bt_rec: the last one's address and name,
 * bt_key: our BR/EDR address and the last one's key, bt_kept: the other three whole), plat.h's struct plat_bta_dev */
static struct plat_ble_rec opt_btr;             /* as the record read it, or fm1-base last saved it */
#if FELUCCA_BT_AUDIO
_Static_assert(sizeof(struct plat_bta_dev) == sizeof(struct bta_mem) && sizeof opt_btr.bta == sizeof bta.mem,
               "plat.h plat_bta_dev: Optimist's bta_mem (bt/bt_audio.h)");
#endif
static void opt_btr_from_kept(void)             /* the record's BLE address and bond into opt_btr (after the record) */
{
    memcpy(opt_btr.addr, ble_addr_kept, 6);
    opt_btr.bonded = (uint8_t)(ble_bond_kept[0] == BLE_BOND_KEPT);
    opt_btr.bond_ediv = (uint16_t)(ble_bond_kept[2] | ble_bond_kept[3] << 8);
    memcpy(opt_btr.bond_rand, ble_bond_kept + 4, 8);
    memcpy(opt_btr.bond_ltk, ble_bond_kept + 12, 16);
}
int opt_ble_load(struct plat_ble_rec *r)
{
    if (ble_addr_kept[6] != BLE_ADDR_KEPT || (ble_addr_kept[5] >> 6) != 3u)
        return 0;                               /* (no address made yet: fm1-base makes one, opt_ble_save keeps it) */
    opt_btr_from_kept();
    *r = opt_btr;
    return 1;
}
int opt_ble_save(const struct plat_ble_rec *r)
{
#if !FELUCCA_BLE
    /* (no BLE fields in the settings record without BT MIDI: storage/persist_rec.c; the BLE address fm1-base makes at
     * each start is not kept, so only a change of the audio devices' fields is a reason to save the settings) */
    int same = opt_btr.bta_addr_set == r->bta_addr_set && opt_btr.bta_n == r->bta_n &&
               !memcmp(opt_btr.bta_addr, r->bta_addr, sizeof r->bta_addr) && !memcmp(opt_btr.bta, r->bta, sizeof r->bta);
#endif
    opt_btr = *r;
    memcpy(ble_addr_kept, r->addr, 6);
    ble_addr_kept[6] = BLE_ADDR_KEPT;
    memset(ble_bond_kept, 0, sizeof ble_bond_kept);
    if (r->bonded) {
        ble_bond_kept[0] = BLE_BOND_KEPT;
        ble_bond_kept[2] = (uint8_t)r->bond_ediv;
        ble_bond_kept[3] = (uint8_t)(r->bond_ediv >> 8);
        memcpy(ble_bond_kept + 4, r->bond_rand, 8);
        memcpy(ble_bond_kept + 12, r->bond_ltk, 16);
    }
#if !FELUCCA_BLE
    if (same)
        return 0;
#endif
#if FELUCCA_BT_AUDIO
    bta.key_new = 1;                            /* (op_bt.c bt_poll: the settings saved once quiet) */
#endif
    settings_later = 1;
    return 0;
}
#else
int opt_ble_load(struct plat_ble_rec *r) { (void)r; return 0; }
int opt_ble_save(const struct plat_ble_rec *r) { (void)r; return 0; }
#endif

/* this FM-1's four hex digits, for the USB name (app.c app_usb_name): the low two octets of the BLE address kept in the
 * record (an FM-1 that named itself "Optimist XXXX" keeps its XXXX), else plat_dev_id() = what fm1-base's own stack
 * makes the address's low two octets of when none is kept; -1: no flash ID either */
int opt_dev_id(void)
{
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
    if (ble_addr_kept[6] == BLE_ADDR_KEPT && (ble_addr_kept[5] >> 6) == 3u)
        return (int)((uint32_t)ble_addr_kept[1] << 8 | ble_addr_kept[0]);
#endif
    return plat_dev_id();
}

#if FELUCCA_BLE
/* ---- the radio: SYSTEM > BT MIDI's ON / OFF (op_bt.c bt_enter, op_project.c) */
static void ble_midi_set(uint8_t on)
{
    on = on ? 1u : 0u;
    if (on == ble_on)
        return;
    ble_on = on;
    plat_ble_enable(on);                        /* (on: started if not yet, advertising; off: its link ended, quiet) */
}
int opt_ble_at_boot(void) { return ble_on; }   /* (app.h APP_BLE_AT_BOOT: as the settings word saved it) */

/* ---- MIDI in (core/opt_core.c opt_core_midi, the tick) */
static uint8_t ble_held[16][16];                /* the notes the computer has on (bit per note, per channel) */
static uint8_t ble_was_conn, ble_release;
static void ble_note_seen(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, n = (pkt >> 16) & 0x7Fu;
    if (st == 0x90u && ((pkt >> 24) & 0x7Fu))
        ble_held[ch][n >> 3] |= (uint8_t)(1u << (n & 7u));
    else if (st == 0x80u || st == 0x90u)
        ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
}
static int ble_clock_msg(uint32_t pkt)          /* F8 FA FB FC (CIN F), F2 (CIN 3): for clock_sync.c (midi_ble.c's) */
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    return (cin == 0xFu && (st == 0xF8u || (st >= 0xFAu && st <= 0xFCu))) || (cin == 3u && st == 0xF2u);
}
#if MIR_BLE_BUILT
static void mir_ble_rx(uint32_t pkt);
/* the link's figures the mirror reports to its client (ed_mirror.c mir_poll, op 3 LINK: the app logs them), as fm1-base's
 * stack agreed them (plat_ble_link_info: 0 each with another stack) */
static uint32_t opt_ble_link(uint32_t i)
{
    uint32_t v[7];
    plat_ble_link_info(v);
    return v[i];
}
static uint8_t ble_ll_tx_octets(void) { return (uint8_t)opt_ble_link(0); }   /* (the types: ble/ble_ll.h's, ble_host.h's) */
static uint16_t ble_att_mtu(void) { return (uint16_t)opt_ble_link(1); }
static uint16_t ble_ll_interval(void) { return (uint16_t)opt_ble_link(2); }
static uint16_t ble_ll_peer_len(uint32_t i) { return (uint16_t)opt_ble_link(3u + (i & 3u)); }
#endif
static void opt_ble_in(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, age;
#if MIR_BLE_BUILT
    if (ble_on && cin >= 4u && cin <= 7u) {
        mir_ble_rx(pkt);                        /* (SysEx: only the mirror's requests are taken) */
        return;
    }
#endif
    if (!ble_on || ((cin < 8u || cin > 0xEu) && !ble_clock_msg(pkt)))
        return;
    age = plat_midi_in_t24();                   /* (how long ago it was sent: plat.h, ble_midi_time; never ahead) */
    age = plat_ticks24() - age;
    if ((int32_t)age < 0)
        age = 0;
    if (midi_in_enqueue(pkt, MSRC_BLE, fm1_ticks() - age))    /* (on Optimist's clock: TIMER4 on the FM-1, the
                                                 * simulated one on the host) */
        ble_note_seen(pkt);
}
static void ble_notes_end(void)                 /* a note-off for each note the link left on (the next tick if full) */
{
    uint32_t ch, n, left = 0;
    for (ch = 0; ch < 16u; ch++)
        for (n = 0; n < 128u; n++) {
            if (!(ble_held[ch][n >> 3] & (1u << (n & 7u))))
                continue;
            if (midi_in_enqueue(8u | (0x80u | ch) << 8 | n << 16, MSRC_BLE, fm1_ticks()))
                ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
            else
                left = 1;
        }
    ble_release = (uint8_t)left;
}

/* ---- MIDI out to the computer's link: the events (midi_rings.c midi_out_event, the render) and CLOCK OUT's F8s at
 * their time (clock_out.c, the render), sent from the tick; held while a mirror frame is under way */
#define BOQ 64u
static uint32_t ble_out_q[BOQ];
static volatile uint32_t boq_w, boq_r;
static volatile uint8_t opt_mir_mid;            /* a mirror frame is going out (the main loop's: opt_bt_poll) */
static void ble_midi_out(uint32_t pkt)
{
    if (!ble_midi_on || boq_w - boq_r >= BOQ)
        return;
    ble_out_q[boq_w % BOQ] = pkt;
    RING_PUBLISH();
    boq_w++;
}
#if FELUCCA_MIDI_CLKOUT
#define BRTQ 8u
static uint32_t ble_rt_q[BRTQ], ble_rt_due[BRTQ];   /* (0: taken back) */
static volatile uint32_t brt_w, brt_r;
static int ble_rt_on(void) { return ble_midi_on; }
static void ble_rt_push(uint32_t due, uint32_t pkt)
{
    if (!ble_midi_on || brt_w - brt_r >= BRTQ)
        return;
    ble_rt_q[brt_w % BRTQ] = pkt;
    ble_rt_due[brt_w % BRTQ] = due;
    RING_PUBLISH();
    brt_w++;
}
static void ble_rt_drop_after(uint32_t t, uint32_t pkt)   /* (a start's F8s taken back: midi_rings.c rt_out_drop_after) */
{
    uint32_t i;
    for (i = brt_r; i != brt_w; i++)
        if (ble_rt_q[i % BRTQ] == pkt && (int32_t)(ble_rt_due[i % BRTQ] - t) > 0)
            ble_rt_q[i % BRTQ] = 0;
}
#endif
/* the tick (core/opt_core.c opt_core_tick, after the MIDI in): what waits, out; a link gone, its notes ended */
static void opt_ble_tick(uint32_t now)
{
    int conn = ble_midi_linked;
    if (ble_was_conn && !conn)
        ble_release = 1;                        /* (its notes are not ended by a note-off that will never come) */
    ble_was_conn = (uint8_t)conn;
    if (ble_release)
        ble_notes_end();
    if (!ble_midi_on) {
        boq_r = boq_w;                          /* (nothing waits for a computer that does not listen) */
#if FELUCCA_MIDI_CLKOUT
        brt_r = brt_w;
#endif
        return;
    }
    if (opt_mir_mid)
        return;                                 /* (a mirror frame under way: after it) */
#if FELUCCA_MIDI_CLKOUT
    while (brt_r != brt_w && (!ble_rt_q[brt_r % BRTQ] || (int32_t)(now - ble_rt_due[brt_r % BRTQ]) >= 0)) {
        if (ble_rt_q[brt_r % BRTQ])
            plat_midi_send(ble_rt_q[brt_r % BRTQ], OPT_BLE_MIDI_TO);
        RING_PUBLISH();
        brt_r++;
    }
#else
    (void)now;
#endif
    while (boq_r != boq_w && plat_midi_out_room(OPT_BLE_MIDI_TO)) {
        RING_PUBLISH();
        plat_midi_send(ble_out_q[boq_r % BOQ], OPT_BLE_MIDI_TO);
        boq_r++;
    }
}
#else
int opt_ble_at_boot(void) { return 0; }        /* (no BT MIDI: Bluetooth starts with an AUDIO mode only) */
#endif

#if FELUCCA_BT_AUDIO
/* ---- Bluetooth audio: bta as op_bt.c reads it, from plat_bta_* (opt_bt_poll, every main loop pass). mem[k]'s
 * address is k (bta_page finds the device by it); the keys and real addresses are fm1-base's (opt_btr) */
static int bta_known_name(const char *nm)       /* the found device of this name is a remembered one */
{
    uint32_t k;
    for (k = 0; k < bta.nmem; k++)
        if (str_eq(bta.mem[k].name, nm))
            return 1;
    return 0;
}
static void opt_bta_view(void)
{
    uint32_t k, st = (uint32_t)plat_bta_state();
    char nm[17];
    for (k = 0; k < BTA_MEMS && plat_bta_known((int)k, nm); k++) {
        memset(&bta.mem[k], 0, sizeof bta.mem[k]);
        bta.mem[k].addr[0] = (uint8_t)(1u + k);
        str_cpy(bta.mem[k].name, nm, sizeof bta.mem[k].name);
    }
    bta.nmem = (uint8_t)k;
    for (k = 0; k < BTA_DEVS && plat_bta_found((int)k, nm); k++)
        str_cpy(bta.d[k].name, nm, sizeof bta.d[k].name);
    bta.n = (uint8_t)k;
    bta.tried = (uint8_t)(st == BTA_S_FAILED);
    if (st == BTA_S_PAGING || (st >= BTA_S_LINKED && st <= BTA_S_STREAMING))
        bta.page_i = 0;                         /* (the one connecting or connected is the last one: fm1-base's) */
}
#if FM1B_BTA_MEM && !FMEM_ANY
#error "opt_bt.h: fm1.toml bt_audio_mem = app without Optimist's arena (ARENA): nothing lends the buffers (gen_optimist.py --conf)"
#endif
#if FELUCCA_BTA_LEND
/* the buffers lent (fm1.toml bt_audio_mem = "app"): Optimist's arena holds them while an AUDIO mode is on (core/
 * arena_bufs.c ARB_BTA, core/feat_mem.c): bta_start sets the mode, the arena places the block at its next pass
 * (bta_lend), and the radio starts once it is lent (opt_bta_go, opt_bt_poll); no room: the question (feat_mem.c), the
 * radio not started. Off: the radio let go (its run ends a moment later: bta_lend_want holds the block until fm1-base
 * gives it back, plat_bta_mem), then the arena has it again */
#define OPT_BTA_ON() (bta.mode != BTA_OFF)
#include "opt_bta_lend.h"
#endif
static void opt_bta_go(void)                    /* the radio up for the mode set (bta_start) */
{
    plat_bta_on(1);                             /* (the radio up: fm1-base starts it, and BLE with it) */
    if (!ble_on)
        plat_ble_enable(0);                     /* (BT MIDI OFF, or not built: no advertising, the radio for the audio
                                                 * alone) */
    opt_bta_view();
}
static void bta_start(uint32_t m)
{
    bta.mode = (uint8_t)m;
#if FELUCCA_BTA_LEND
    if (!opt_bta_lent) {
        opt_bta_pend = 1;                       /* (started once the arena lends the block: opt_bt_poll) */
        return;
    }
#endif
    opt_bta_go();
}
static void bta_stop(void)
{
    bta.mode = BTA_OFF;
#if FELUCCA_BTA_LEND
    opt_bta_pend = 0;
#endif
    plat_bta_on(0);
}
static void bta_hangup(void) { plat_bta_hangup(); }
static void bta_pick(uint32_t i) { plat_bta_pick((int)i); }
static void bta_forget(uint32_t i)
{
    plat_bta_forget((int)i);
    opt_bta_view();
}
static int bta_next(void)                       /* AUTO's round: the oldest remembered one next (it becomes the last) */
{
    if (bta.nmem < 2u)
        return 0;
    bta.page_i = (uint8_t)(bta.nmem - 1u);
    return 1;
}
static void bta_settle(int up) { (void)up; bta.page_i = 0; }   /* (fm1-base keeps the one that answered first) */
static uint32_t bta_rows(void)
{
    uint32_t k, n = bta.nmem;
    for (k = 0; k < bta.n; k++)
        n += !bta_known_name(bta.d[k].name);
    return n;
}
static int bta_row(uint32_t i)                  /* row i: remembered k (>= 0), or found r (-1 - r) */
{
    uint32_t k;
    if (i < bta.nmem)
        return (int)i;
    i -= bta.nmem;
    for (k = 0; k < bta.n; k++)
        if (!bta_known_name(bta.d[k].name) && !i--)
            return -1 - (int)k;
    return -1 - (int)BTA_DEVS;
}
static void bta_pick_row(uint32_t i)
{
    int r = bta_row(i);
    if (r >= 0)
        (void)plat_bta_reconnect(r);
    else if (r > -1 - (int)BTA_DEVS)
        plat_bta_pick(-1 - r);
    opt_bta_view();
}
static int bta_page(const uint8_t a[6])         /* (a: mem[k].addr, k + 1 in its first byte) */
{
    int ok = a[0] >= 1u && a[0] <= bta.nmem && plat_bta_reconnect(a[0] - 1) == 0;
    opt_bta_view();
    return ok;
}
static void bta_scan(int on) { plat_bta_scan(on); }   /* (2: again, the list kept) */
static uint32_t bta_state(void) { return (uint32_t)plat_bta_state(); }
static const char *bta_dev_name(uint32_t i) { return i < bta.n ? bta.d[i].name : ""; }
static const char *bta_lost(void)
{
    static const char *const W[] = {0, "LOST AFTER CONNECTING", "LOST WHILE PAIRING", "LOST AT ENCRYPTION",
                                    "LOST IN AUDIO SETUP", "LOST WHILE STREAMING", "REFUSED"};   /* (bt_audio.c's) */
    int w = plat_bta_lost();
    return w > 0 && w < 7 ? W[w] : 0;
}
static int bta_out_bt(void) { return plat_bta_out_device(); }
static int bta_gave_up(void) { return plat_bta_timed_out(); }
static int bta_rejoin(void) { return 0; }       /* (after a pairing fm1-base connects again by itself: bt_own_audio.c) */
static void bta_output(int bt) { plat_bta_output(bt); }
#if FELUCCA_BLE
static void ble_sig_audio(int on) { (void)on; }   /* (the MIDI link slowed while the audio streams: fm1-base's, bt_own.c) */
#endif
static int32_t bta_lat_t(void)                  /* (clock_sync.c: the stream's own delay, TIMER4 ticks; -1 none) */
{
    int32_t us = plat_bta_latency_us();
    return us < 0 ? -1 : us * (int32_t)FM1_TICKS_PER_US;
}
_Static_assert(BTA_S_STREAMING == PLAT_BTA_STREAMING && BTA_S_READY == PLAT_BTA_READY, "bt_audio.h BTA_S_*: plat.h's");
#endif

#if FELUCCA_BLE || FELUCCA_BT_AUDIO
/* op_bt.c's settings record functions under other names (opt_bt_rec.h after the UI: the record's audio fields from
 * opt_btr, fm1-base's real addresses and keys, rather than the screens' view of them) */
#define bt_rec_out op_bt_rec_out
#define bt_rec_in op_bt_rec_in
#define bt_key_out op_bt_key_out
#define bt_key_in op_bt_key_in
#define bt_kept_out op_bt_kept_out
#define bt_kept_in op_bt_kept_in
#endif

/* ---- every main loop pass (ui/opt_ui.c, before the input): the view, the mirror's frames out */
#if MIR_BLE_BUILT
static int mir_ble_peek(uint32_t *pkt);
static void mir_ble_pop(void);
static int mir_ble_mid(void);
static void mir_ble_flush(void);
/* the mirror's ring read by the radio itself (plat_ble_stream: fm1-base's own stack), as Optimist's own radio does
 * (io/midi/midi_ble.c ble_app_midi_peek): full notifications, the link's rate. mir_ble_peek / pop / mid run in the
 * radio's interrupts then, as there; 0 (another stack): packet by packet below */
static const struct plat_ble_stream OPT_MIR_STREAM = {mir_ble_peek, mir_ble_pop, mir_ble_mid};
static uint8_t opt_mir_stream;
#endif
static void of_poll(void);                      /* (ui/opt_feat.h: a feature's UNLOAD once stopped) */
static void opt_bt_poll(void)
{
#if FELUCCA_BT_AUDIO
#if FELUCCA_BTA_LEND
    if (opt_bta_pend && opt_bta_lent && bta.mode != BTA_OFF) {
        opt_bta_pend = 0;
        opt_bta_go();
    }
#endif
    opt_bta_view();
#endif
    of_poll();
#if FELUCCA_BLE
#if BLE_CENTRAL
    opt_ble_dev_poll();                         /* (the list's scan once the radio is up: ui/opt_bt_dev.h) */
#endif
    ble_devices_poll();
#if MIR_BLE_BUILT
    if (!ble_out_on) {
        mir_ble_flush();                        /* (nothing waits for a computer that does not listen: the radio
                                                 * reads the ring only while one does) */
        opt_mir_mid = 0;
        return;
    }
    if (opt_mir_stream)
        return;                                 /* (the radio takes the frames, and holds the channel messages while
                                                 * one is under way) */
    for (;;) {                                  /* the mirror's frames, packet by packet, while the link has room */
        uint32_t pkt;
        if (!mir_ble_mid() && boq_r != boq_w)
            break;                              /* (between frames the channel messages go first) */
        if (plat_midi_out_room(TO_BLE_HOST) < 4u || !mir_ble_peek(&pkt))
            break;
        opt_mir_mid = 1;
        RING_PUBLISH();
        plat_midi_send(pkt, TO_BLE_HOST);
        mir_ble_pop();
        opt_mir_mid = (uint8_t)mir_ble_mid();
    }
    if (!mir_ble_mid())
        opt_mir_mid = 0;
#endif
#endif
}
/* at power-on, after the settings record (ui/opt_ui.c opt_core_init: main.c's ble_midi_init order) */
static void opt_bt_boot(void)
{
#if FELUCCA_BLE
    ble_devices_boot();                         /* (the devices list's store: the record's ble_dev) */
#endif
#if MIR_BLE_BUILT
    opt_mir_stream = (uint8_t)plat_ble_stream(&OPT_MIR_STREAM);
#endif
#if FELUCCA_BLE || FELUCCA_BT_AUDIO
    opt_btr_from_kept();
#endif
}
