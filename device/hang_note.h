/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* A hang note: what the firmware was doing when it gave up on something stuck, kept through a restart (.noinit RAM:
 * a power-off loses it). Written where the giving up happens, in RAM only (no flash, nothing that waits), so it can be
 * written from an interrupt just before a reset; the magic last, so a note cut short reads as absent. One note, the
 * newest; its kind in `type`:
 *   HN_STALLED_WAIT   a bounded wait that ran out (its words: the waiter's)
 *   HN_USB_RESET      the USB reset request (felucca-bsp usb.c FELUCCA_USB_RESET_REQ; main_fm1.c fm1b_usb_reset_note):
 *                     w0 flags: 1 UBOOT asked (else the firmware again), 2 sx_busy (the SysEx ring held), 4 a SysEx
 *                        stream on (sxs.on, APP_SYSEX), 8 its frame cut (sxs.cut), 16 tx_deaf (the host not reading
 *                        MIDI), 32 usb.ota_req, 64 usb.uboot_req, 128 usb.sx_on (a SysEx coming in, unfinished),
 *                        256 usb.suspended, 512 in the audio render (fm1b_dbg.in_audio), 1024 sx_collect
 *                     w1 the SysEx out ring's fill (so_w - so_r) << 16 | the MIDI out ring's (mo_w - mo_r)
 *                     w2 sxs.held: SysEx streams let go (plat_sysex_hold, APP_SYSEX)
 *                     w3 the real-time ring (FELUCCA_MIDI_RT): rt_qw << 16 | rt_qr (16 bits each)
 *                     w4 the MIDI in ring's fill (mi_w - mi_r) << 16 | SysEx frames not taken by the main loop
 *                        (sx_made - sx_taken)
 *                     w5 the update's step on the screen (ota_show; 0 none since the boot) | the USB bus resets << 8
 * The readouts: `log` (main_fm1.c log_frame) prints it in three lines,
 *   note TYPE n SEQ ms MS st STAGE           (ms: fm1_ms at the note; st: fm1b_dbg.stage, the main loop's step)
 *   note w0-2 HEX HEX HEX
 *   note w3-5 HEX HEX HEX
 * and with crash_history the next boot keeps it in the flash history once (crashlog.c cl_note): a record whose vec is
 * HN_VEC | type, n seq | stage << 24, pc w0, rets w1, sp w2, ms the note's, emu w3, dbg w4, psr w5.
 * tools/fm1_remote.py names the words (log, crashes). */
#pragma once
#include <stdint.h>

#define HN_MAGIC 0x31544E48u                        /* "HNT1" */
#define HN_VEC 0x4E540000u                          /* "NT": the crash history's vec for a note, | its type */
#define HN_STALLED_WAIT 1u
#define HN_USB_RESET 2u
#define HN_W 6u

typedef struct {
    uint32_t magic;                                 /* HN_MAGIC, written last */
    uint32_t type, seq, ms, stage;
    uint32_t w[HN_W];
    uint32_t kept;                                  /* in the flash history already (crashlog.c cl_note) */
} hn_t;

#ifndef HN_NOINIT
#define HN_NOINIT __attribute__((section(".noinit")))
#endif
static hn_t hn HN_NOINIT;

static void hn_put(uint32_t type, uint32_t ms, uint32_t stage, const uint32_t w[HN_W])
{
    uint32_t i, seq = hn.magic == HN_MAGIC ? hn.seq + 1u : 1u;
    hn.magic = 0;
    __asm__ volatile("" ::: "memory");
    hn.type = type, hn.seq = seq, hn.ms = ms, hn.stage = stage, hn.kept = 0;
    for (i = 0; i < HN_W; i++)
        hn.w[i] = w[i];
    __asm__ volatile("" ::: "memory");
    hn.magic = HN_MAGIC;
}

static const char *hn_name(uint32_t type)
{
    return type == HN_STALLED_WAIT ? "STALLED_WAIT" : type == HN_USB_RESET ? "USB_RESET" : "?";
}

/* line k (0..2) of the note as text at b (HN_LINE bytes), 0-terminated; 0 = no note (or k past the last) */
#define HN_LINE 64u                                 /* (the longest: "note STALLED_WAIT" and three 10-digit numbers) */
static int hn_line(char *b, uint32_t k)
{
    static const char H[] = "0123456789ABCDEF";
    const char *t;
    char *q = b;
    uint32_t j, n;
    if (hn.magic != HN_MAGIC || k > 2u)
        return 0;
    for (t = "note "; *t; )
        *q++ = *t++;
    if (k == 0u) {
        const uint32_t v[3] = {hn.seq, hn.ms, hn.stage};
        static const char *const L[3] = {" n ", " ms ", " st "};
        for (t = hn_name(hn.type); *t; )
            *q++ = *t++;
        for (j = 0; j < 3u; j++) {
            char d[10];
            uint32_t x = v[j];
            for (t = L[j]; *t; )
                *q++ = *t++;
            n = 0;
            do
                d[n++] = (char)('0' + x % 10u);
            while (x /= 10u);
            while (n)
                *q++ = d[--n];
        }
    } else {
        for (t = k == 1u ? "w0-2" : "w3-5"; *t; )
            *q++ = *t++;
        for (j = 0; j < 3u; j++) {
            uint32_t x = hn.w[3u * (k - 1u) + j];
            *q++ = ' ';
            for (n = 0; n < 8u; n++)
                *q++ = H[(x >> (28u - 4u * n)) & 15u];
        }
    }
    *q = 0;
    return 1;
}
