/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's audio core (engines, voices, drums, effects, sequencer) as one unit of an fm1-base app (fm1.toml o2): its
 * own files compiled unchanged, from Optimist's tree (build/optimist: gen_optimist.py links it), on fm1_compat.h in
 * place of Optimist's HAL. A unit apart from the app's (host/plat_host.c's unity build): Optimist's names and
 * fm1-base's never meet; the app sees only opt_core.h. The include list is tests/hostsim.c's (Optimist's host render):
 * the same subset of firmware/src/felucca.c; its UI follows (ui/opt_ui.c, the end of this file). */
#define FELUCCA_ARRANGER 1               /* (felucca.c: always; SONG, the scenes and the chain are the UI's too) */
#ifndef FELUCCA_USB_AUDIO
#define FELUCCA_USB_AUDIO 1              /* (felucca.c's default: Optimist's USB audio; on fm1-base's here, below) */
#endif
/* Optimist's globals that fm1-base's unit has under the same names (device/main_fm1.c bootguard, device/panel.c panel,
 * both in .noinit): this unit's under its own (Optimist's boot guard is fm1-base's job here; its panel state is its UI's) */
#define bootguard opt_bootguard
#define panel opt_ui_panel
/* undo.c's ring (seq/undo.c, FELUCCA_UNDO_HISTORY) on the FM-1: Optimist's app.ld gives it the memory nothing is linked
 * into as _undo_* (from the pool's end to the stacks less 8 KiB); fm1-base's device/app.ld has the same two ends,
 * _pool_end and the stacks' first guard band _guard0, and its build keeps 8 KiB of the pool spare (tools/build.py
 * check): the ring from _pool_end to _guard0 less those 8 KiB (FELUCCA_UNDO_CAP; never 0, which is "all there is").
 * Main RAM's part: none (hi = lo), as Optimist's own build has it. The host: undo.c's own stand-in */
#include <stdint.h>
#ifndef PLAT_HOST
#define _undo_pool_lo _pool_end
#define _undo_pool_hi _guard0
#define _undo_ram_lo _pool_end
#define _undo_ram_hi _pool_end
extern uint8_t _pool_end[], _guard0[];     /* (device/app.ld) */
#define OPT_POOL_SPARE 8192u
static uint32_t opt_undo_cap(void)
{
    uint32_t room = (uint32_t)(_guard0 - _pool_end);
    return room > OPT_POOL_SPARE ? room - OPT_POOL_SPARE : 1u;
}
#define FELUCCA_UNDO_CAP opt_undo_cap()
#endif
#ifdef PLAT_HOST
#include <string.h>                      /* (the host's libc; the FM-1 has none: Optimist's own, below) */
#endif
#include "fm1_compat.h"
#include "opt_core.h"

/* Optimist's host convention (tests/hostsim.c): its section and alignment attributes (.noinit, .ram_hot ...) are the
 * FM-1 link's; on the host they go */
#ifdef PLAT_HOST
#define __attribute__(x)
#endif
/* Optimist's libc (memset & co. of its own, system/libc.c) under other names: the host's libc keeps its own, and on
 * the FM-1 felucca-bsp's chip/libc.c (the app's unit) has memset, memcpy and memcmp, for the compiler's own calls.
 * The host: the names back to its libc after; the FM-1 (no string.h): this unit keeps Optimist's, by those names */
#undef memset                            /* (the host's string.h may make them macros) */
#undef memcpy
#undef memcmp
#undef memmove
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#define memmove felucca_memmove
/* the smooth tables (felucca_tables.h TAB_CODED): on the FM-1 Optimist keeps them delta-coded and expands them into RAM
 * at boot, from a block of its boot stream (main.c tab_expand(s), TAB_STREAM: tools/build.py appends the blobs), which
 * fm1-base's image does not have. TAB_CODED_FORCE (the generator's own test switch, tests/tab_coded_test.c): the blobs
 * as arrays in flash, expanded by tab_expand() at power-on (ui/opt_ui.c opt_core_init, before anything reads them).
 * The host too, so the host runs the FM-1's path */
#define TAB_CODED_FORCE 1
#include "felucca_tables.h"
#include "system/libc.c"
#ifdef PLAT_HOST
#undef memset
#undef memcpy
#undef memcmp
#undef memmove
#endif

#include "core/core.h"
/* HOT / HOT2 (core.h): Optimist's audio path in RAM code (its app.ld .ram_hot / .ram_hot2, copied at boot by its
 * main.c), out of the XIP cache's way. Here both are fm1-base's app RAM code (plat.h PLAT_RAM_CODE: section .app_ram
 * in .ram_text, copied at boot, inside the guard's RAM code window) at the start of fm1-base's packed SRAM (device/
 * app.ld: RAM code, .data/.bss and the pool one budget; HOT and HOT2 42,500 B with the dev profile, more than the old
 * fixed 32 KiB RAMTEXT held). The same rules as Optimist's: HOT and HOT2 call each other directly, XIP code calls them
 * through a pointer (FAR, as core/audio.c: a direct call between RAM and XIP is out of the pi32v2's reach,
 * R_PI32V2_LONG_JUMP_23M2), and their calls into XIP go through a pointer too: tools/ramcode.py checks them, and
 * fm1.toml ram_code_allow lists those Optimist makes from its own RAM code (the reasons there). OPT_HOT_RAM 0: the
 * whole audio path from XIP instead (core.h's own ASM_CHECK way; the cache's misses, unmeasured on an FM-1) */
#ifndef OPT_HOT_RAM
#define OPT_HOT_RAM 1
#endif
#undef HOT
#undef HOT2
#if OPT_HOT_RAM
#define HOT PLAT_RAM_CODE
#define HOT2 PLAT_RAM_CODE
#else
#define HOT
#define HOT2
#endif
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

#if FELUCCA_USB_AUDIO
/* ---- USB audio (FELUCCA_USB_AUDIO, the profile's): Optimist's USB input track (fx.c ut_mix, ua_take) and its capture
 * routing (io/usb/ua_tap.h in core.h, io/usb/usb_audio_route.c here: SYSTEM > USB AUDIO, the settings record's "UAR3")
 * on fm1-base's USB audio in place of Optimist's io/usb driver (usb_audio_stream.c, usb_audio.c: from Melodee, not in
 * the trial). The computer's audio: plat_usb_audio_play once a render (APP_USB_PLAY: at unity, 24-bit, played as sent
 * with fm1.toml usb_audio_out_feedback), handed to ut_mix a mix block at a time in the mix bus's Q15 (>> 8: the 16 bits
 * Optimist's own decoder keeps of a 24-bit sample, ua_decode). The capture: each mix block's four channels as
 * Optimist's ua_route builds them (the stems, or the routing's sources), clipped to Q15 (its ua_clip) and taken to 24
 * bits (<< 8: the 16 bits its PCM24 sends, ua_encode, with felucca-bsp's 24 bits of headroom unused); app_usb_capture
 * hands felucca-bsp the render's (APP_USB_CAPTURE: resampled there to the computer's clock with usb_audio_in_host_clock,
 * as Optimist's own ua_rs does) */
#define UA_CAP_CHANNELS 4u                         /* (usb_audio_stream.c's: fm1.toml usb_audio_in_channels = 4) */
_Static_assert(NTRK == UA_CAP_CHANNELS, "the default routing hands the stems (NTRK a frame) over as the channels");
#include "io/usb/usb_audio_route.c"
/* usb_audio_stream.c's counters as SYSTEM > INFO and USB STATS read them (op_project.c), from fm1-base's (felucca-bsp's
 * uac / uo: plat_usb_audio_stats, APP_USB_STATS) each main loop pass (opt_ua_poll): the output's (playback) runs dry
 * and overflows, its packets in, the input's (capture) runs dry and overflows, its IN tokens that found nothing, its
 * packets out, the feedback. Not kept by fm1-base: a bad packet's count, the lowest fill, the polls' gaps (0 here) */
static struct {
    uint8_t play_alt, cap_alt;
    uint32_t play_underruns, play_overruns, cap_underruns, cap_overruns, bad_packets, rx_packets, tx_packets, missed_frames;
    uint32_t gap_max, gap_n, fill_lo1;
} ua;
static uint32_t ua_fb_now;
static uint32_t ua_feedback(void) { return ua_fb_now; }   /* (SYSTEM > INFO's feedback rate, 10.14 frames a ms) */
static void opt_ua_poll(void)
{
    plat_usb_audio_stats_t s;
    plat_usb_audio_stats(&s);
    ua.play_alt = s.out_alt, ua.cap_alt = s.in_alt;
    ua.play_underruns = s.out_underruns, ua.play_overruns = s.out_overruns, ua.rx_packets = s.out_packets;
    ua.cap_underruns = s.in_underruns, ua.cap_overruns = s.in_overruns, ua.missed_frames = s.in_missed;
    ua.tx_packets = s.in_packets;
    ua_fb_now = s.out_feedback;
}
static int32_t opt_play[2u * HALF_FRAMES];         /* this render's computer's audio (plat_usb_audio_play), 24-bit */
static int32_t opt_cap[UA_CAP_CHANNELS * HALF_FRAMES];   /* this render's capture, 24-bit, four channels a frame */
static uint32_t opt_play_got, opt_play_at;
static uint8_t opt_cap_on;                         /* the computer records this render (plat_usb_audio_rec) */
/* the USB input track's next n frames (fx.c ut_mix, from mix_block: RAM code, through FAR), Q15; 0 = none */
static HOT uint32_t ua_take(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, at = opt_play_at;
    if (!opt_play_got || at + n > HALF_FRAMES)
        return 0;
    for (i = 0; i < n; i++) {
        l[i] = opt_play[2u * (at + i)] >> 8;
        r[i] = opt_play[2u * (at + i) + 1u] >> 8;
    }
    opt_play_at = at + n;
    return 1;
}
/* a mix block's capture (o: its master, Q15, before the codec's shift), frames f .. f + n of the render */
static void opt_cap_block(const int32_t *o, uint32_t f, uint32_t n)
{
    const int32_t *c = ua_route(o, n);
    uint32_t i;
    for (i = 0; i < UA_CAP_CHANNELS * n && f * UA_CAP_CHANNELS + i < UA_CAP_CHANNELS * HALF_FRAMES; i++)
        opt_cap[f * UA_CAP_CHANNELS + i] = clamp(c[i], -32768, 32767) * 256;
}
const int32_t *opt_core_capture(uint32_t n) { return opt_cap_on && n <= HALF_FRAMES ? opt_cap : 0; }
/* the computer is streaming into the FM-1 (its audio in a render of the last 300 ms; now: fm1_ticks()): the flash
 * writes that can wait do (project.c audio_quiet), as usb_audio_stream.c's ua_streaming says */
static volatile uint32_t opt_play_t, opt_play_seen;
static int ua_streaming(uint32_t now) { return opt_play_seen && now - opt_play_t < 300u * 24000u; }
#else
const int32_t *opt_core_capture(uint32_t n) { (void)n; return 0; }
static void opt_ua_poll(void) {}
#endif

/* ------------------------------------------------------------- the app's side --- */
/* opt_core_init, the power-on: in ui/opt_ui.c (core/model.c's felucca_init path, after the UI's files) */

uint32_t opt_core_block(void) { return CTL; }

/* Optimist renders Q15 (fx.c mix_finish, MASTER inside: song.master_q12) and shifts it to 24 bit (core/audio.c
 * audio_block, OUT_SHIFT 7: -6 dBFS ceiling); fm1-base's render takes 24 bit with MASTER as master_q12 (APP_GAIN) */
#define OUT_SHIFT 7
/* the output as Optimist's main loop reads it (core/audio.c): the two I2S halves (abuf, 24 bit), the one rendered last
 * (fm1_audio_free_half) and a count of them (audio_halves): the mixer's meters (ui/meters.c). A copy here: fm1-base's
 * render writes its own buffer (APP_BLOCK = HALF_FRAMES frames a call) */
/* MASTER as the mirror's client sets it (editor cmd 108 kind 3, ed_mirror.c mir_master): its q12 + 1, 0 the pot's
 * (fm1-base's master_q12). Set by the main loop (ui/opt_editor.h opt_ed_master), read by the render */
static volatile uint32_t opt_master_q12;
#define HALF_WORDS (HALF_FRAMES * 2u)
static int32_t abuf[2u * HALF_WORDS];
static volatile uint32_t audio_halves;
static uint32_t fm1_audio_free_half(void) { return (audio_halves + 1u) & 1u; }   /* (the half written last) */
void opt_core_render(int32_t *lr, uint32_t n, uint32_t master_q12, uint32_t keys)
{
    uint32_t i, f;
    fm1_ms = plat_ms();
    fm1_in.notes = keys | fm1_rem_note;             /* (the mirror's client's keys too: fm1_compat.h) */
    song.master_q12 = master_q12 > 4096u ? 4096u : master_q12;
    if (opt_master_q12)                              /* (the mirror's client plays MASTER: ui/opt_editor.h) */
        song.master_q12 = opt_master_q12 - 1u;
#if FELUCCA_USB_AUDIO
    opt_cap_on = (uint8_t)(plat_usb_audio_rec() != 0);   /* (as audio.c's audio_block at its start: the stems this block) */
    track_capture_on = opt_cap_on;
    opt_play_got = (uint32_t)plat_usb_audio_play(opt_play, n <= HALF_FRAMES ? n : HALF_FRAMES);
    opt_play_at = 0;
    if (opt_play_got) {
        opt_play_t = fm1_ticks();
        opt_play_seen = 1;
    }
#endif
    for (f = 0; f + CTL <= n; f += CTL) {
        int32_t *o = lr + 2u * f;
        FAR(mix_block)(o, CTL);                     /* (HOT: RAM code with OPT_HOT_RAM; called as core/audio.c does) */
#if FELUCCA_USB_AUDIO
        if (opt_cap_on)
            opt_cap_block(o, f, CTL);               /* (audio.c ua_block: the channels, the master before the shift) */
#endif
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
