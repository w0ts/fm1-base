/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's audio core (engines, voices, drums, effects, sequencer) as one unit of an fm1-base app (fm1.toml o2): its
 * own files compiled unchanged, from Optimist's tree (build/optimist: gen_optimist.py links it), on fm1_compat.h in
 * place of Optimist's HAL. A unit apart from the app's (host/plat_host.c's unity build): Optimist's names and
 * fm1-base's never meet; the app sees only opt_core.h. The include list is tests/hostsim.c's (Optimist's host render):
 * the same subset of firmware/src/felucca.c; its UI follows (ui/opt_ui.c, the end of this file). */
#define FELUCCA_ARRANGER 1               /* (felucca.c: always; SONG, the scenes and the chain are the UI's too) */
/* the factory presets' tables kept in the image with the SD card mode too (core.h PRESETS_CARD: Optimist's SD card mode
 * reads them from the card's PRESETS/FACTORY files instead, to save its flash): without a card the presets are there
 * as on the trial without the mode; the card's files still listed and loaded from PROJECT's PRESETS row */
#define PRESETS_CARD 0
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
#if FELUCCA_LIVE_SAVE
/* saving while playing (LIVE_SAVE, felucca.c's): fm1-base's erases run in windows of about 1 ms, the sound on between
 * them (plat.h plat_data_erase: felucca-bsp's sliced erase, the same as Optimist's hal/fm1_flash_slice.h); one that held
 * the sound for the whole erase (a flash part with no erase suspend: plat_flash_held) makes the saves wait for the stop
 * again until the next start, as Optimist's FL_ERASE_HELD (ui/opt_storage.h st_erase) */
static uint8_t live_save_off;
#endif
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
#include "opt_lend.h"                           /* the switchable features' blocks the arena lends (FMEM_NLEND, before
                                                 * engines.c counts its clients) */
#include "engines/engines.c"
#include "drums/drums.c"
#include "core/params.c"
#include "core/voice.c"
#include "fx/slicer/slicer.c"
#if FELUCCA_MACROS
#include "core/macro.c"
#endif
#include "opt_feat_decl.h"                       /* the switchable features' table (core/feat_mem.c, in fx.c: ui/opt_feat.h) */
#include "fx/fx.c"
/* the MIDI rings (io/midi/midi_rings.c: the sequencer's in and out, the clock out), without Optimist's USB driver
 * (io/usb/usb.c): fm1-base's USB is the device's. The outputs are always open (midi_out_open): fm1-base routes what
 * the out ring holds (opt_core_render: plat_midi_send) */
static int midi_out_open(void) { return 1; }
#if FELUCCA_BLE
static void opt_ble_in(uint32_t pkt);          /* (ui/opt_bt.h: Bluetooth MIDI in, its time sent; the mirror's requests) */
static void opt_ble_tick(uint32_t now);        /* (ui/opt_bt.h: Bluetooth MIDI out, CLOCK OUT's F8s at their time) */
#endif
static void opt_link_tick(void);               /* (ui/opt_wifi.h: Link's MIDI clock ticks whose time has come, MSRC_LINK) */
#include "io/midi/midi_rings.c"
#if FELUCCA_EDITOR && FELUCCA_ED_LIVE && !defined(FELUCCA_OTA)
#define FELUCCA_OTA 1                            /* (felucca.c's, before the sequencer: the notes the live editor plays,
                                                  * seq/ednote.c EDN_ON; the editor's SysEx is fm1-base's, ui/opt_editor.h) */
#endif
#if FELUCCA_ARRANGER
#include "seq/arranger.c"
#endif
#include "seq/seq.c"
#if FELUCCA_DUAL >= 2
#include "opt_dual.c"                            /* the second core: Optimist's split on fm1-base's (mix_block) */
#endif
#if FELUCCA_CPU_GUARD
#include "system/cpuguard.c"                     /* the predictive CPU guard (felucca.c's place: after seq.c); the render
                                                  * below calls it every half, as core/audio.c does */
#endif

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
#define OPT_UA_PLAY_N (2u * HALF_FRAMES)            /* the computer's audio of a render (plat_usb_audio_play), 24-bit */
#define OPT_UA_CAP_N (UA_CAP_CHANNELS * HALF_FRAMES)  /* a render's capture, 24-bit, four channels a frame */
#if OPT_LEND_UAC
/* lent with USB audio's rings while the computer streams (core/opt_lend.h L_UAC, ui/opt_feat.h of_uac_lend: after the
 * rings in the block), 0 while not: no capture, nothing played. A render reads them once, at its start */
#define OPT_UA_OWN_BYTES ((OPT_UA_PLAY_N + OPT_UA_CAP_N) * 4u)
static int32_t *volatile opt_play_p, *volatile opt_cap_p;
#else
static int32_t opt_play_s[OPT_UA_PLAY_N], opt_cap_s[OPT_UA_CAP_N];
#define opt_play_p opt_play_s
#define opt_cap_p opt_cap_s
#endif
static int32_t *opt_play, *opt_cap;                 /* this render's (opt_core_render) */
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
/* the output as Optimist's main loop reads it (core/audio.c): the two I2S halves' largest |sample| (the master meter's
 * only use of them: opt_half_peak.h, AUDIO_HALF_PEAK), the one rendered last (fm1_audio_free_half) and a count of them
 * (audio_halves): the mixer's meters (ui/meters.c). fm1-base's render writes its own buffer (APP_BLOCK = HALF_FRAMES
 * frames a call) */
/* MASTER as the mirror's client sets it (editor cmd 108 kind 3, ed_mirror.c mir_master): its q12 + 1, 0 the pot's
 * (fm1-base's master_q12). Set by the main loop (ui/opt_editor.h opt_ed_master), read by the render */
static volatile uint32_t opt_master_q12;
#define HALF_WORDS (HALF_FRAMES * 2u)
#include "opt_half_peak.h"
static int32_t opt_half_pk[2];                   /* the two halves' largest |sample| (the meter's: was a 4 KB copy of them) */
#define AUDIO_HALF_PEAK(h) opt_half_pk[(h) & 1u]  /* (ui/meters.c meter_tap) */
static volatile uint32_t audio_halves;
static uint32_t fm1_audio_free_half(void) { return (audio_halves + 1u) & 1u; }   /* (the half written last) */

/* ---- each half's time, as core/audio.c's fm1_alnk0_irq keeps it around its render: fm1-base renders a half a call
 * (APP_BLOCK = HALF_FRAMES) straight into the I2S half the DMA has just freed, so a half's sample k leaves the DMA
 * HALF_FRAMES + k samples after the render starts, as on Optimist */
#include "../app.h"                              /* (APP_BLOCK) */
_Static_assert(APP_BLOCK == HALF_FRAMES, "a render is one I2S half (app.h APP_BLOCK)");
/* when the DMA switched halves (core/audio.c sync_anchor, the same): the render's start, as the lower envelope of the
 * starts one half apart (a start delayed by another interrupt or a lock counts 1/16; earlier than predicted: taken
 * as is; 1 ms off: taken as is). Each mix block's audio leaves from there (clock_sync.c sync_out_t: following an
 * external clock, the clock sent out) */
static uint32_t sync_anchor(uint32_t t0)
{
    static uint32_t a;
    int32_t e;
    a += (HALF_FRAMES * SY_TPS_Q8) >> 8;
    e = (int32_t)(t0 - a);
    if (e < 0 || e > (int32_t)(1000u * FM1_TICKS_PER_US))
        a = t0;
    else
        a += (uint32_t)e >> 4;
    return a;
}
/* the load: the last half's render time (us) into PEAK and DROPS (system/overload.h ovl_half: SYSTEM > INFO, the
 * header's mark) and the CPU guard (cpuguard.c cg_post; without it audio.c's own rule: two halves in a row over 85 %
 * shed a voice before the next). A half ran late when fm1-base counted an xrun after it (plat_xruns: the DMA moved on
 * while it rendered), known at the next render's start: so the last half's measure goes in there, before this half's
 * cg_pre, the order audio.c has them in. The time is the render's (Optimist's is its interrupt's, its wait for the
 * interrupt in PEAK too: fm1-base's entry is not the app's to see) */
#if !FELUCCA_CPU_GUARD
static volatile uint8_t shed_req;               /* (core/audio.c's: a voice shed before the next half) */
#endif
static uint32_t opt_half_us, opt_halves, opt_xr_seen;
static void opt_half_post(void)
{
    uint32_t xr = plat_xruns(), late = xr != opt_xr_seen;
    opt_xr_seen = xr;
    if (!opt_halves++)
        return;                                  /* (no half before this one) */
    ovl_half(opt_half_us, late, fm1_ms);
#if FELUCCA_CPU_GUARD
    cg.late = (uint8_t)late;
    FAR(cg_post)(opt_half_us);
#else
    {
        static uint8_t over;
        over = (uint8_t)(over << 1 | (opt_half_us * 100u > OVL_HALF_US * 85u));
        if ((over & 3u) == 3u)
            shed_req = 1;
    }
#endif
}
static void opt_half_pre(void)
{
#if FELUCCA_CPU_GUARD
    FAR(cg_pre)();                               /* the predicted load; a shed it asked for (cpuguard.c) */
#else
    if (shed_req) {
        shed_req = 0;
        shed_voice();
    }
#endif
}

/* ---- the CPU clock (core.h cpu_khz: SYSTEM > INFO's CPU MHz, the guard's instructions a sample, cpuguard.c cg_pre):
 * fm1-base's (plat_cpu_khz, APP_CPU_KHZ: the stock 360 MHz its start sets, 0 unknown and on the host, where the guard
 * never steps, as on Optimist's own host). Optimist's main.c measures it with a timed loop (hal/fm1_clock.h) */
static uint32_t opt_cpu_khz(void) { return plat_cpu_khz(); }

#if FELUCCA_SD_REC
static void sdr_feed(const int32_t *q15, uint32_t n);    /* storage/sd/sd_rec.c (ui/opt_sd.h) */
static uint32_t sdr_recording(void);
#endif
/* the frames Bluetooth audio takes from the render (app.c app_bt_frames): the output, or 0 (silence) when it was
 * corrected (fx/outcorr.c: a stream that started during it) */
const int32_t *opt_core_bt_frames(const int32_t *lr) { return oc_bt_frames(lr); }
void opt_core_render(int32_t *lr, uint32_t n, uint32_t master_q12, uint32_t keys)
{
    uint32_t i, f, t0 = fm1_ticks(), a, rec = 0;
    fm1_ms = plat_ms();
    a = sync_anchor(t0);
    opt_half_post();
    opt_half_pre();
    fm1_in.notes = keys | fm1_rem_note;             /* (the mirror's client's keys too: fm1_compat.h) */
    song.master_q12 = master_q12 > 4096u ? 4096u : master_q12;
    if (opt_master_q12)                              /* (the mirror's client plays MASTER: ui/opt_editor.h) */
        song.master_q12 = opt_master_q12 - 1u;
#if FELUCCA_USB_AUDIO
    opt_play = opt_play_p, opt_cap = opt_cap_p;      /* (lent or not, for the whole render) */
    opt_cap_on = (uint8_t)(opt_cap && plat_usb_audio_rec() != 0);   /* (as audio.c's audio_block at its start: the stems this block) */
    track_capture_on = opt_cap_on;
    opt_play_got = opt_play ? (uint32_t)plat_usb_audio_play(opt_play, n <= HALF_FRAMES ? n : HALF_FRAMES) : 0u;
    opt_play_at = 0;
    if (opt_play_got) {
        opt_play_t = fm1_ticks();
        opt_play_seen = 1;
    }
#endif
#if FELUCCA_SD_REC
    rec = sdr_recording();                          /* (this render's: the recorder takes the plain output) */
#endif
    oc_begin((uint32_t)plat_bta_streams(), rec, n);    /* SPK LOWCUT / HP BASS, the codec's output only (fx/outcorr.c; the
                                                     * frames Bluetooth takes: app.c app_bt_frames) */
    for (f = 0; f + CTL <= n; f += CTL) {
        int32_t *o = lr + 2u * f;
        sync_out_t = a + (((HALF_FRAMES + f) * SY_TPS_Q8) >> 8);   /* (this block's audio leaves then: audio.c's) */
        FAR(mix_block)(o, CTL);                     /* (HOT: RAM code with OPT_HOT_RAM; called as core/audio.c does) */
#if FELUCCA_USB_AUDIO
        if (opt_cap_on)
            opt_cap_block(o, f, CTL);               /* (audio.c ua_block: the channels, the master before the shift) */
#endif
#if FELUCCA_SD_REC
        if (rec)
            sdr_feed(oc_rec_frames(o), CTL);        /* the master into the card's ring (audio.c audio_block's place): the
                                                     * plain output, or its own stage's while the codec's is corrected */
#endif
        for (i = 0; i < 2u * CTL; i++)
            o[i] <<= OUT_SHIFT;
    }
    for (i = 2u * f; i < 2u * n; i++)                /* (n not a multiple of CTL: app.h's APP_BLOCK is) */
        lr[i] = 0;
    for (f = 0; f < n; f += HALF_FRAMES) {           /* (the halves the meters scan) */
        opt_half_pk[audio_halves & 1u] = opt_half_peak(lr, f, n, HALF_WORDS);
        audio_halves++;
#if FELUCCA_DUAL >= 2
        dual_load_half();                            /* CPU1's load, once a half (core/opt_dual.c, dual_load.h) */
#endif
    }
    while (mo_r != mo_w) {                           /* the sequencer's and the keys' MIDI out (the out ring) */
        plat_midi_send(midi_out_q[mo_r % MQ], TO_USB);
        mo_r++;
    }
    opt_half_us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;   /* (into PEAK and the guard at the next render) */
}

#include "../ui/opt_usbh.h"                        /* the USB MIDI host (USB_HOST): MSRC_USBH, a lost session's notes let go */

/* a USB-MIDI packet from fm1-base (plat_midi_in, from port PORT_*): Optimist's checks (usb.c usb_midi_rx_packet's, the
 * rings' midi_rx_bad / midi_rx_wanted) into its queue, with its source (SYNC USB / TRS / BLE) and arrival time */
static uint32_t opt_rx_bad;
#ifdef PLAT_HOST
#define OPT_MIDI_T24() fm1_ticks()                   /* (the host's ticks follow its simulated ms: fm1_compat.h) */
#else
#define OPT_MIDI_T24() plat_midi_in_t24()            /* when it came in (plat.h) */
#endif
void opt_core_midi(uint32_t pkt, int port)
{
    uint32_t src = port == PORT_TRS ? MSRC_TRS : port == PORT_BLE ? MSRC_BLE : usbh_src();   /* (USB: the computer, or
                                                     * the host's device: ui/opt_usbh.h) */
#if FELUCCA_BLE
    if (port == PORT_BLE) {
        opt_ble_in(pkt);                             /* (Optimist's midi_ble.c checks, the sender's time: ui/opt_bt.h) */
        return;
    }
#endif
    if (midi_rx_bad(pkt)) {
        opt_rx_bad++;
        return;
    }
    if (midi_rx_wanted(pkt)) {
        if (port == PORT_USB)
            usbh_note(pkt);                          /* (what the port holds, let go when its session ends) */
        (void)midi_in_enqueue(pkt, src, OPT_MIDI_T24());   /* (when it came in: plat_midi_in_t24) */
    }
}

/* fm1-base's 1 kHz tick (app.c app_tick, APP_TICK: the timer interrupt), Optimist's TIMER5 work (system/main.c
 * fm1_timer5_irq) that the trial has: the MIDI in from fm1-base's queue into Optimist's, stamped where it
 * arrived: the platform's stamp (plat_midi_in_t24: a 10 kHz look at the endpoint and the jack's ring, within 0.05 ms, as
 * Optimist's own usb_rx_peek / uart_midi_peek; the stamp taken here instead was late by up to the render's 5.8 ms half
 * before this, which threw the clock follower's tempo filter off); and CLOCK OUT's packets (io/midi/clock_out.c, rt_out_*: each due when its audio
 * leaves) sent at the tick nearest their time, ahead of the notes as usb.c ep1_tx sends them (within half a
 * millisecond: a USB frame is one) */
#define OPT_TICK_HALF (500u * FM1_TICKS_PER_US)
void opt_core_tick(void)
{
    uint32_t pkt;
    int port;
    usbh_tick_lost(fm1_ticks());                    /* a USB session ended: its notes off first (ui/opt_usbh.h) */
    while (plat_midi_in(&pkt, &port))
        opt_core_midi(pkt, port);
#if USB_RT_OUT
    {
        uint32_t now = fm1_ticks() + OPT_TICK_HALF - SYNC_PEEK_HALF;   /* (rt_out_ready: due within SYNC_PEEK_HALF) */
        while (rt_out_ready(now)) {
            pkt = rt_out_pkt[rt_r % RTQ];
            RING_PUBLISH();
            rt_r++;
            plat_midi_send(pkt, TO_USB);
        }
    }
#endif
#if FELUCCA_BLE
    opt_ble_tick(fm1_ticks() + OPT_TICK_HALF);      /* Bluetooth: the events and CLOCK OUT to the computer's link */
#endif
    opt_link_tick();                                 /* Link's session as MIDI clock (ui/opt_wifi.h: nothing without WIFI_LINK) */
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

#if FELUCCA_DUAL >= 2
void opt_core_dual_parts(uint32_t jobs) { dual_fixed = jobs & DUAL_ALLOWED; }
int32_t opt_core_dual_stat(uint32_t k)
{
    if (k == 0u)
        return dual.up;
    if (k == 1u)
        return dual.why;
    if (k == 2u)
        return (int32_t)dual.jobs;
    if (k == 3u)
        return (int32_t)dual.posts;
    return -1;
}
#else
void opt_core_dual_parts(uint32_t jobs) { (void)jobs; }
int32_t opt_core_dual_stat(uint32_t k) { (void)k; return -1; }
#endif

/* ------------------------------------------------------------- the UI --- */
#include "../ui/opt_ui.c"                         /* Optimist's UI on fm1-base's canvas and panel (after the core: one unit) */
