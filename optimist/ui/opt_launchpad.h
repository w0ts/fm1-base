/* SPDX-License-Identifier: GPL-3.0-only */
/* A Launchpad on Optimist's USB host (LAUNCHPAD, under USB_HOST: Optimist's io/launchpad/, jank's app in third_party/jank). While the
 * device on the port is a Launchpad (its product name: lp_name_ok), the port's packets come raw (plat_usb_host_raw: every
 * cable, SysEx too) to the app instead of the tracks' MIDI input, and the app's packets (its mode's SysEx, the lights) go
 * out on TO_USB; the sequencer's own MIDI out to the port (opt_core.c's drain: notes, CLOCK OUT) is held back meanwhile,
 * so the Launchpad hears only the app. Main loop (app_loop), every pass: attach / detach, the packets in, the app's
 * step, the packets out while fm1-base's ring has room. Included by ui/opt_ui.c after Optimist's UI (the app selects
 * tracks and says things through it). */
#pragma once
#if FELUCCA_LAUNCHPAD && FELUCCA_USB_HOST
#include "io/launchpad/launchpad.c"             /* Optimist's: the FM-1 as the app's unit, jank's app, the packets */
#define OLP_OUT_KEEP 8u                          /* slots of fm1-base's out ring left for everything else */
void opt_lp_poll(uint32_t now)
{
    uint32_t pkt;
    int up = plat_usb_host_state() == USBH_CONNECTED && lp_name_ok(plat_usb_host_name());
    if (up != (int)opt_lp_on) {
        opt_lp_on = (uint8_t)up;
        plat_usb_host_raw(up);
        if (up)
            lp_attach(plat_usb_host_name());
        else
            lp_detach();
    }
    if (!opt_lp_on)
        return;
    while (plat_usb_host_rx(&pkt))
        lp_in(pkt);
    lp_step(now);
    while (plat_midi_out_room(TO_USB) > OLP_OUT_KEEP && lp_out(&pkt))
        plat_midi_send(pkt, TO_USB);
}
#else
void opt_lp_poll(uint32_t now) { (void)now; }
#endif
