/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 w0ts */
/* Optimist's USB network in the trial (USB_NET: SYSTEM > USB, NETWORK and its STATE; Optimist's
 * ui/optimist/op_project.c, which shows them with OPT_HAS_UNET) on fm1-base's (fm1.toml usb_net = true from
 * gen_optimist.py --conf; plat.h plat_usb_net_*, felucca-bsp's CDC-NCM function): the computer sees a network adapter,
 * Link runs over the cable with the Wi-Fi OFF, and in MODE JANK the computer is one more unit of the group.
 *   NETWORK  only where the adapter takes another function's place (plat_usb_net_shared: the USB audio output, whose
 *            feedback has its endpoint, or the serial console): OFF that function, ON the adapter. Switching drops the
 *            FM-1 off USB for about a second. Kept (the user, 2026-11-25: "remember the setting"): the settings word's
 *            bit 31, applied at boot before USB starts (unet_boot); a record without it reads OFF. Without a function
 *            to give way the adapter is always there
 *   STATE    OFF, WAITING (no computer's adapter up yet), the FM-1's address on the cable (169.254.x.y), or WI-FI (the
 *            Wi-Fi is in a mode whose network is not the cable's: the cable carries nothing)
 * Included by ui/opt_ui.c before Optimist's UI. */
#pragma once
#if FM1B_USB_NET
#define OPT_HAS_UNET 1
static uint8_t unet_keep;                       /* NETWORK as kept: the settings word's bit 31 (storage/settings_word.c) */
static uint32_t unet_shared(void) { return plat_usb_net_shared() != 0; }
static uint32_t unet_on(void) { return plat_usb_net() >= USB_NET_WAIT; }
static void unet_set(uint32_t on)
{
    unet_keep = (uint8_t)(on != 0u);
    plat_usb_net_present((int)unet_keep);
}
static void unet_boot(void)                     /* after settings_init, before fm1-base starts USB (ui/opt_ui.c) */
{
    if (unet_shared())
        plat_usb_net_present((int)unet_keep);   /* (without a function to give way it is always presented) */
}
/* the STATE line: its text, 1 when the cable carries the FM-1's address (green); main loop */
static const char *unet_state(uint32_t *ok)
{
    static char s[16];
    uint32_t a = plat_usb_net_addr(), i, n = 0, b;
    int st = plat_usb_net();
    *ok = 0;
    if (st <= USB_NET_OFF)
        return "OFF";
    if (st == USB_NET_WAIT)
        return "WAITING";
    if (!a)
        return "WI-FI";
    for (i = 0; i < 4u; i++) {                  /* a.b.c.d */
        b = a >> (24u - 8u * i) & 0xFFu;
        if (b >= 100u)
            s[n++] = (char)('0' + b / 100u);
        if (b >= 10u)
            s[n++] = (char)('0' + b / 10u % 10u);
        s[n++] = (char)('0' + b % 10u);
        if (i < 3u)
            s[n++] = '.';
    }
    s[n] = 0;
    *ok = 1;
    return s;
}
#endif
