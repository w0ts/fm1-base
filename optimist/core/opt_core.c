/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's audio core (engines, voices, drums, effects, sequencer) as one unit of an fm1-base app (fm1.toml o2): its
 * own files compiled unchanged, from Optimist's tree (build/optimist: gen_optimist.py links it), on fm1_compat.h in
 * place of Optimist's HAL. A unit apart from the app's (host/plat_host.c's unity build): Optimist's names and
 * fm1-base's never meet; the app sees only opt_core.h. The include list is tests/hostsim.c's (Optimist's host render):
 * the same subset of firmware/src/felucca.c; its UI follows (ui/opt_ui.c, the end of this file). */
#define FELUCCA_ARRANGER 1               /* (felucca.c: always; SONG, the scenes and the chain are the UI's too) */
#include <stdint.h>
#include <string.h>
#include "fm1_compat.h"
#include "opt_core.h"

/* Optimist's host convention (tests/hostsim.c): its section and alignment attributes (.noinit, .ram_hot ...) are the
 * FM-1 link's; on the host they go */
#ifdef PLAT_HOST
#define __attribute__(x)
#endif
/* Optimist's libc (memset & co. of its own, system/libc.c) under other names: the host's libc keeps its own */
#undef memset                            /* (the host's string.h may make them macros) */
#undef memcpy
#undef memcmp
#undef memmove
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#define memmove felucca_memmove
#include "felucca_tables.h"
#include "system/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#undef memmove

#include "core/core.h"
#include "engines/engines.c"
#include "drums/drums.c"
#include "core/params.c"
#include "core/voice.c"
#include "fx/slicer/slicer.c"
#if FELUCCA_MACROS
#include "core/macro.c"
#endif
#include "fx/fx.c"
/* the MIDI rings (io/midi/midi_rings.c: the sequencer's in and out, the clock out), without Optimist's USB driver
 * (io/usb/usb.c): fm1-base's USB is the device's. The outputs are always open (midi_out_open): fm1-base routes what
 * the out ring holds (opt_core_render: plat_midi_send) */
static int midi_out_open(void) { return 1; }
#include "io/midi/midi_rings.c"
#if FELUCCA_ARRANGER
#include "seq/arranger.c"
#endif
#include "seq/seq.c"

/* ------------------------------------------------------------- the app's side --- */
/* opt_core_init, the power-on: in ui/opt_ui.c (core/model.c's felucca_init path, after the UI's files) */

uint32_t opt_core_block(void) { return CTL; }

/* Optimist renders Q15 (fx.c mix_finish, MASTER inside: song.master_q12) and shifts it to 24 bit (core/audio.c
 * audio_block, OUT_SHIFT 7: -6 dBFS ceiling); fm1-base's render takes 24 bit with MASTER as master_q12 (APP_GAIN) */
#define OUT_SHIFT 7
/* the output as Optimist's main loop reads it (core/audio.c): the two I2S halves (abuf, 24 bit), the one rendered last
 * (fm1_audio_free_half) and a count of them (audio_halves): the mixer's meters (ui/meters.c). A copy here: fm1-base's
 * render writes its own buffer (APP_BLOCK = HALF_FRAMES frames a call) */
#define HALF_WORDS (HALF_FRAMES * 2u)
static int32_t abuf[2u * HALF_WORDS];
static volatile uint32_t audio_halves;
static uint32_t fm1_audio_free_half(void) { return (audio_halves + 1u) & 1u; }   /* (the half written last) */
void opt_core_render(int32_t *lr, uint32_t n, uint32_t master_q12, uint32_t keys)
{
    uint32_t i, f;
    fm1_ms = plat_ms();
    fm1_in.notes = keys;
    song.master_q12 = master_q12 > 4096u ? 4096u : master_q12;
    for (f = 0; f + CTL <= n; f += CTL) {
        int32_t *o = lr + 2u * f;
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            o[i] <<= OUT_SHIFT;
    }
    for (i = 2u * f; i < 2u * n; i++)                /* (n not a multiple of CTL: app.h's APP_BLOCK is) */
        lr[i] = 0;
    for (f = 0; f < n; f += HALF_FRAMES) {           /* (the halves the meters scan) */
        int32_t *h = &abuf[(audio_halves & 1u) * HALF_WORDS];
        for (i = 0; i < HALF_WORDS; i++)
            h[i] = f * 2u + i < 2u * n ? lr[f * 2u + i] : 0;
        audio_halves++;
    }
    while (mo_r != mo_w) {                           /* the sequencer's and the keys' MIDI out (the out ring) */
        plat_midi_send(midi_out_q[mo_r % MQ], TO_USB);
        mo_r++;
    }
}

/* a USB-MIDI packet from fm1-base (plat_midi_in, from port PORT_*): Optimist's checks (usb.c usb_midi_rx_packet's, the
 * rings' midi_rx_bad / midi_rx_wanted) into its queue, with its source (SYNC USB / TRS / BLE) and arrival time */
static uint32_t opt_rx_bad;
void opt_core_midi(uint32_t pkt, int port)
{
    uint32_t src = port == PORT_TRS ? MSRC_TRS : port == PORT_BLE ? MSRC_BLE : MSRC_USB;
    if (midi_rx_bad(pkt)) {
        opt_rx_bad++;
        return;
    }
    if (midi_rx_wanted(pkt))
        (void)midi_in_enqueue(pkt, src, fm1_ticks());
}

void opt_core_panic(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        trk_all_off(&trk[k]);
}

uint32_t opt_core_voices(void)
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}

const char *opt_core_engine(uint32_t part) { return part < NPART ? ENGINES[trk[part].engine]->name : ""; }

/* ------------------------------------------------------------- the UI --- */
#include "../ui/opt_ui.c"                         /* Optimist's UI on fm1-base's canvas and panel (after the core: one unit) */
