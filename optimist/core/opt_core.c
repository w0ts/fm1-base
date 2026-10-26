/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's audio core (engines, voices, drums, effects, sequencer) as one unit of an fm1-base app (fm1.toml o2): its
 * own files compiled unchanged, from Optimist's tree (build/optimist: gen_optimist.py links it), on fm1_compat.h in
 * place of Optimist's HAL. A unit apart from the app's (host/plat_host.c's unity build): Optimist's names and
 * fm1-base's never meet; the app sees only opt_core.h. The include list is tests/hostsim.c's (Optimist's host render):
 * the same subset of firmware/src/felucca.c. */
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
/* Optimist's USB device (io/usb/usb.c) for its MIDI rings (midi_in_q, midi_out_q, the clock's), as tests/hostsim.c
 * has it: its driver (usb_poll, the SIE) is never called here, fm1-base's USB is the device's. FELUCCA_USB_AUDIO,
 * FELUCCA_CDC and FELUCCA_OTA stay 0: USB audio and updates are fm1-base's */
#include "io/usb/usb.c"
#if FELUCCA_ARRANGER
#include "seq/arranger.c"
#endif
#include "seq/seq.c"

/* ------------------------------------------------------------- the app's side --- */
/* the power-on state as Optimist's host render sets it (tests/hostsim.c host_tracks_init, host_preset_req): the
 * factory defaults, empty patterns; firmware/src/system/main.c felucca_init does it through core/model.c and the
 * stored project (project.c), which are the UI's and storage's: not in this trial */
static void opt_preset(track_t *t, uint32_t e, uint32_t pi)        /* (hostsim.c host_preset_req, the factory tables) */
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    const preset_t *p = &ENGINES[e]->presets[pi % ENGINES[e]->npresets];
    uint32_t i;
    t->eng_req = (uint8_t)e;
    t->preset = (uint8_t)(pi % ENGINES[e]->npresets);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = p->e[i];
    t->p[P_ATK] = p->env[0];
    t->p[P_DEC] = p->env[1];
    t->p[P_SUS] = p->env[2];
    t->p[P_REL] = p->env[3];
    t->p[P_ED_FLT] = p->fenv;
    t->p[P_ED_FX] = preset_trim(eng_uid(e), pi % ENGINES[e]->npresets);
    t->p[P_VOICE] = p->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++) {
        t->p[P_DIST + i] = (int16_t)(p->fx[i] ? p->fx[i] - 1 : FX_DEF[i]);
        t->p[P_AMODE + i] = TP[P_AMODE + i].def;
    }
    preset_extras(t->p, p);
    analog2_extras(t->p, ENGINES[e], pi % ENGINES[e]->npresets);
    for (i = P_E1PIT; i <= P_E1Z; i++)
        t->p[i] = 0;
    (void)env_load(t->p);
    env2_dest_def(t->p, e);
    t->engine = (uint8_t)e;
}

/* the parts' power-on sounds (core/model.c TRK_DEF: engine UID, preset): ANALOG 808 BOOM, DIGITAL RHODES, SAMPLE
 * LOFI FLUTE (model.c is the UI's side: its table, not its code) */
static const uint8_t OPT_DEF[3][2] = {{0, 0}, {1, 0}, {4, 5}};

void opt_core_init(void)
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    memset(dsend, 0, sizeof dsend);
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_E0; i++)
            trk[k].p[i] = TP[i].def;
        for (i = P_ENG_END; i < P_COUNT; i++)
            trk[k].p[i] = TP[i].def;
        steps_clear(&trk[k]);
    }
    ins_defaults();
#if FELUCCA_MASTER_COMP
    for (i = 0; i < 3u * NTRK; i++)
        INS_CSET(i / 3u)[i % 3u] = GP[G_CRAT + i % 3u].def;
#endif
#if FELUCCA_DRUMS
    for (i = 0; i < 3u; i++)
        dsend_kit[i] = DKS_DEF;
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
#endif
    for (k = 0; k < NPART && k < 3u; k++)
        opt_preset(&trk[k], eng_built(OPT_DEF[k][0]) ? eng_slot(OPT_DEF[k][0]) : 0u, eng_built(OPT_DEF[k][0]) ? OPT_DEF[k][1] : 0u);
#if FELUCCA_ENG_WAVE
    for (k = 0; k < 64u && wt_wanted() >= 0 && wt_have != wt_wanted(); k++)
        wt_service();                                /* (WAVE's table: the main loop's work, done at once) */
#endif
    part_mem_boot();
    song.sel = 0;
    song.master_q12 = 2048;
    usb.up = usb.config = 1;                         /* (usb.c's out ring takes MIDI only while configured: fm1-base's
                                                      * USB is the one connected; the packets go to plat_midi_send) */
}

uint32_t opt_core_block(void) { return CTL; }

/* Optimist renders Q15 (fx.c mix_finish, MASTER inside: song.master_q12) and shifts it to 24 bit (core/audio.c
 * audio_block, OUT_SHIFT 7: -6 dBFS ceiling); fm1-base's render takes 24 bit with MASTER as master_q12 (APP_GAIN) */
#define OPT_OUT_SHIFT 7
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
            o[i] <<= OPT_OUT_SHIFT;
    }
    for (i = 2u * f; i < 2u * n; i++)                /* (n not a multiple of CTL: app.h's APP_BLOCK is) */
        lr[i] = 0;
    while (mo_r != mo_w) {                           /* the sequencer's and the keys' MIDI out (usb.c's ring) */
        plat_midi_send(midi_out_q[mo_r % MQ], TO_USB);
        mo_r++;
    }
}

void opt_core_midi(uint32_t pkt) { usb_midi_rx_packet(pkt, fm1_ticks()); }   /* (Optimist's own checks, its queue) */

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
