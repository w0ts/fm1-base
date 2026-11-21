/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* Host test of device/hang_note.h (the note of what was stuck, kept through a restart) and its way into the crash
 * history (device/crashlog.c cl_note), against a simulated NOR: no note after a power-on (garbage, no magic), a note
 * cut short before its magic reads as absent, the fields and the sequence, the three `log` lines exactly, and the
 * next boot's record (vec HN_VEC | type, the words where the readout's parser expects them) kept once only. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/plat.h"

#define HN_NOINIT
#include "../device/hang_note.h"

static uint8_t nor[0x100000];
static char sent[64][256];
static int nsent;
static int cl_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int cl_erase(uint32_t off) { memset(nor + off, 0xFF, 4096u); return 0; }
static int cl_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
static int cl_send(const uint8_t *f, uint32_t n)
{
    if (n < 5u || nsent == 64 || n - 5u > 255u)
        exit(1);
    memcpy(sent[nsent], f + 4, n - 5u);
    sent[nsent][n - 5u] = 0;
    nsent++;
    return 0;
}
#include "../device/crashlog.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    static const uint32_t W[HN_W] = {0x13u, 0x00050002u, 7u, 0x00120012u, 0x00400001u, 0x0302u};
    char b[HN_LINE];
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    memset(&hn, 0xA5, sizeof hn);                       /* a power-on: whatever the RAM holds */
    check("power-on garbage: no note, no line", !hn_line(b, 0));
    hn_put(HN_USB_RESET, 123456u, 4u, W);
    check("a note: magic, type, seq 1, ms, stage, not kept",
          hn.magic == HN_MAGIC && hn.type == HN_USB_RESET && hn.seq == 1u && hn.ms == 123456u && hn.stage == 4u &&
          !hn.kept);
    for (i = 0; i < HN_W; i++)
        if (hn.w[i] != W[i])
            break;
    check("its words", i == HN_W);
    check("line 0", hn_line(b, 0) && !strcmp(b, "note USB_RESET n 1 ms 123456 st 4"));
    check("line 1", hn_line(b, 1) && !strcmp(b, "note w0-2 00000013 00050002 00000007"));
    check("line 2", hn_line(b, 2) && !strcmp(b, "note w3-5 00120012 00400001 00000302"));
    check("no line 3", !hn_line(b, 3));
    {   /* the longest line fits HN_LINE */
        hn_t keep = hn;
        hn_put(HN_STALLED_WAIT, 0xFFFFFFFFu, 0xFFFFFFFFu, W);
        hn.seq = 0xFFFFFFFFu;
        check("the longest line 0 fits", hn_line(b, 0) && strlen(b) < HN_LINE &&
              !strcmp(b, "note STALLED_WAIT n 4294967295 ms 4294967295 st 4294967295"));
        hn = keep;
    }
    hn_put(HN_USB_RESET, 200000u, 6u, W);
    check("the next note: seq 2", hn.seq == 2u && hn.ms == 200000u);
    hn.magic = 0;                                       /* a note cut short before its magic */
    check("cut short: absent", !hn_line(b, 0) && cl_note(&hn, 1u) == 0);
    hn_put(HN_USB_RESET, 300000u, 5u, W);
    check("after a cut note: seq starts again at 1", hn.seq == 1u);

    /* the next boot: into the flash history once */
    check("the boot keeps it", cl_note(&hn, 0xABCD1234u) == 1 && hn.kept);
    check("a restart: not again", cl_note(&hn, 0xABCD1234u) == 0);
    nsent = 0;
    check("the history's readout: one record", cl_print(0x48) == 1u);
    {
        static const char E[] = "crash 1/1 n 83886081 vec 4E540002 pc 00000013 rets 00050002 sp 00000007 "
                                "ms 300000 emu 00120012 dbg 00400001 psr 00000302 build ABCD1234 seq ";
        check("its frame: n seq|stage<<24, vec HN_VEC|2, the words, ms",
              nsent == 1 && !strncmp(sent[0], E, sizeof E - 1u));
    }
    for (i = 0; i < (uint32_t)nsent; i++)
        printf("  sent: %s\n", sent[i]);
    hn_put(HN_STALLED_WAIT, 400000u, 3u, W);
    check("a new note: kept again", cl_note(&hn, 1u) == 1);
    printf(bad ? "hang note: FAILED\n" : "hang note: all passed\n");
    return bad ? 1 : 0;
}
