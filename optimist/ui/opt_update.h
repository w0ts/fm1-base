/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* an update (the upgrade command) or the soft key's UBOOT from the host begins (app.h APP_UPDATE_BEGIN, polled: device/
 * main_fm1.c update_ready asks once per main-loop pass, the watchdog fed around it, until this returns 0 or
 * PLAT_UPDATE_BEGIN_MS (300 ms) have passed; before anything is staged or erased and before the restart): the transport
 * stops first, as on Optimist's own firmware (firmware/src/system/stop_first.h: transport_req 2, carried out by the audio
 * block's seq_stop: song stop, notes off, MIDI OUT all-notes-off, MIDI STOP when CLOCK OUT is on), but not waited for
 * here: the request is made once, and every pass says 1 while the song still plays or the request is not carried out
 * (the render and the audio interrupt run between the passes), 0 when it is done. Nothing playing: 0 at once. A mirror
 * frame left open (a mirror pass may open another between two asks) is ended on every pass: else fm1-base's ring stays
 * held and the update's own requests never leave. The card's open read: ended by device/main_fm1.c after the hook.
 * Main loop */
static uint8_t opt_stop_asked;                  /* the transport's stop is requested (cleared when it is carried out) */

int opt_update_begin(void)
{
    opt_sx_letgo();                             /* (a mirror frame left open: ended, so the update's requests go out) */
    if (!opt_stop_asked) {
        if (!song.playing && !transport_req)
            return 0;                           /* (nothing plays: back at once) */
        transport_req = 2;                      /* (as stop_first: asked once, the audio block carries it out) */
        opt_stop_asked = 1;
    }
    if (song.playing || transport_req)
        return 1;                               /* (still stopping: asked again next pass, 300 ms at most) */
    opt_stop_asked = 0;
    return 0;
}
