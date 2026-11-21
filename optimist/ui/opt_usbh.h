/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's USB MIDI host in the trial (USB_HOST: SYSTEM > USB, MODE DEVICE / HOST and the DEVICE line; Optimist's
 * ui/optimist/op_project.c, which shows them with OPT_HAS_USBH) on fm1-base's (fm1.toml usb_host = true from
 * gen_optimist.py --conf; plat.h plat_usb_host, felucca-bsp's chip/usb_host.c: a USB-MIDI controller or synth, direct
 * or behind one powered hub; the FM-1 gives the port no power).
 *   the mode      usbh_set asks fm1-base (main loop); MODE shows what was asked, the DEVICE line what came of it.
 *                 Never kept: every boot is DEVICE (fm1-base's), so the FM-1 is never locked out of its computer
 *   the device    plat_usb_host_state and _name (its product string; CONNECTED without one); NO MIDI / LOW SPEED from
 *                 plat_usb_host_msg (the last said, until the next change)
 *   MIDI in       the port's packets while it is a host are MSRC_USBH (core/opt_core.c opt_core_midi): the track
 *                 channels as any input, the clock followed as USB's (clock_sync.c), stamped as USB's
 *   MIDI out      TO_USB as before: fm1-base sends it to the device while the port is a host (notes, CLOCK OUT)
 *   a lost device plat_usb_lost (the 1 kHz tick, before the packets): what the port's notes still hold is let go
 *                 (usbh_held: the notes and pedals that came in on PORT_USB, released through the input ring)
 * Included by ui/opt_ui.c before Optimist's UI; core/opt_core.c's tick calls usbh_tick_lost and usbh_note. */
#pragma once
#if FELUCCA_USB_HOST
#define OPT_HAS_USBH 1
static uint8_t usbh_want;                       /* MODE: 0 DEVICE, 1 HOST (asked; not kept) */
static uint8_t usbh_said;                       /* the last NO MIDI / LOW SPEED (USBH_MSG_*), 0: none */
static uint32_t usbh_mode(void) { return usbh_want; }
static void usbh_set(uint32_t on)
{
    usbh_want = (uint8_t)(on != 0u);
    usbh_said = 0;
    plat_usb_host((int)usbh_want);
}
/* the DEVICE line: its text and 1 when connected (green); main loop */
static const char *usbh_device(uint32_t *ok)
{
    int m = plat_usb_host_msg(), st = plat_usb_host_state();
    const char *nm;
    if (m == USBH_MSG_NOMIDI || m == USBH_MSG_LOW)
        usbh_said = (uint8_t)m;
    else if (m != USBH_MSG_NONE)
        usbh_said = 0;
    *ok = st == USBH_CONNECTED;
    if (st == USBH_CONNECTED) {
        nm = plat_usb_host_name();
        return nm[0] ? nm : "CONNECTED";
    }
    if (st == USBH_OFF)
        return usbh_want ? "STARTING" : "NONE";
    return usbh_said == USBH_MSG_NOMIDI ? "NO MIDI" : usbh_said == USBH_MSG_LOW ? "LOW SPEED" : "NONE";
}
/* the notes and pedals the port's input holds (channel x note; tick only), let go when its session ends */
static uint32_t usbh_held[16][4];
static uint16_t usbh_pedal;
static void usbh_note(uint32_t pkt)             /* a packet in from PORT_USB that Optimist takes */
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    if (st == 0x90u && d2)
        usbh_held[ch][d1 >> 5] |= 1u << (d1 & 31u);
    else if (st == 0x80u || st == 0x90u)
        usbh_held[ch][d1 >> 5] &= ~(1u << (d1 & 31u));
    else if (st == 0xB0u && d1 == 64u)
        usbh_pedal = (uint16_t)(d2 >= 64u ? usbh_pedal | 1u << ch : usbh_pedal & ~(1u << ch));
}
static void usbh_tick_lost(uint32_t t)          /* the 1 kHz tick: a USB session ended -> its notes off */
{
    uint32_t ch, w, b;
    if (!plat_usb_lost())
        return;
    for (ch = 0; ch < 16u; ch++) {
        if (usbh_pedal >> ch & 1u)
            (void)midi_in_enqueue(0x0Bu | (0xB0u | ch) << 8 | 64u << 16, MSRC_USBH, t);
        for (w = 0; w < 4u; w++)
            for (b = 0; b < 32u; b++)
                if (usbh_held[ch][w] >> b & 1u)
                    (void)midi_in_enqueue(0x08u | (0x80u | ch) << 8 | (w * 32u + b) << 16, MSRC_USBH, t);
        usbh_held[ch][0] = usbh_held[ch][1] = usbh_held[ch][2] = usbh_held[ch][3] = 0;
    }
    usbh_pedal = 0;
}
static uint32_t usbh_src(void) { return plat_usb_host_state() != USBH_OFF ? MSRC_USBH : MSRC_USB; }
#else
#define usbh_tick_lost(t) ((void)0)
#define usbh_note(pkt) ((void)0)
#define usbh_src() MSRC_USB
#endif
