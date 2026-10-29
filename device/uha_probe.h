/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 henri (hdavid) */
/* The USB host audio bench probe (FM1B_UHA_PROBE=1 in the environment of a build with usb_host_audio; main_fm1.c includes
 * it): FM-1 A, the host, sends a 1 kHz sine (half scale, both channels) instead of the master while it plays into
 * FM-1 B's USB audio output; the first stream that starts in 16-bit (alt 1) is measured for UHA_PROBE_S seconds,
 * then the probe asks for 24-bit (alt 2) and measures that as long, then goes back to 16-bit (what the user asked).
 * Each phase's record (in .noinit: a reset keeps it) is read over the LG log readout (F0 7D 4C 47 F7) once A is a
 * device again (SYSTEM > USB > MODE DEVICE, the cable to a computer): uha_probe_log, before Bluetooth's lines.
 *   pk/s fr/s   packets and frames a second sent (1000, ~44.1k: the device's rate)
 *   err und ovr cu nf late wm   TX errors, A's ring dry / full, packets that made up a missed frame, streams whose
 *               frame number stood still (nf: then sent by TxPktRdy alone), polls the last packet was still queued
 *               in a new frame, the longest run of them
 *   fb n e ppm  feedback reads, failed reads, its ppm against A's I2S (min..max, mean): B's crystal against A's
 *   sof/t4      A's SOF clock against its TIMER4 (the 24 MHz crystal the I2S runs from): ~0 = the same crystal
 *   rf ld       us from a packet loaded to the poll that found it gone; from one load to the next (both min..max)
 *   fill        A's ring at a render's start (min..max) */
#pragma once
#if FM1B_UHA_PROBE && FELUCCA_USB_HOST_AUDIO
#define UHA_PROBE_S 30u
#define UHA_PROBE_MAGIC 0x55484150u            /* "UHAP" */
typedef struct {
    uint32_t secs, pkts, frames, tx_errors, underruns, overruns, catchups, late, wait_max;
    uint32_t fb_reads, fb_errors, fill_min, fill_max;
    int32_t ppm_min, ppm_max, ppm_mean, sof_ppm10;
    uint32_t rf_min, rf_max, ld_min, ld_max;   /* us */
    uint32_t bits, frame_stuck;
} uha_probe_rec_t;
static struct {
    uint32_t magic, done;                       /* done: bit 0 alt 1 measured, bit 1 alt 2 */
    uha_probe_rec_t r[2];
} uha_probe_log_rec __attribute__((section(".noinit")));
static struct {
    uint8_t phase;                              /* 0 waiting for 16-bit, 1 measuring it, 2 waiting for 24-bit, 3 measuring, 4 done */
    uint32_t t0, fb_reads_seen;
    uha_stats_t s0;
    int32_t ppm_min, ppm_max;
    int32_t ppm_sum;                            /* (a few thousand ppm x ~2000 reads: in 32 bits) */
    uint32_t ppm_n, fill_min, fill_max;
    float c, s, dc, ds;                         /* the sine: a rotation a frame */
    int32_t buf[2u * HALF_FRAMES];
} uhp;

static const int32_t *uha_probe_src(const int32_t *o, uint32_t n)   /* audio ISR: what the tap takes */
{
    uint32_t i;
    float c = uhp.c, s = uhp.s, k;
    if (uhp.dc == 0.0f) {                       /* 2 pi 1000 / 44117.647 a frame */
        uhp.dc = 0.989875f, uhp.ds = 0.141941f; /* (cos, sin of 0.1424212 rad) */
        c = 1.0f, s = 0.0f;
    }
    if (n > HALF_FRAMES)
        return o;
    for (i = 0; i < n; i++) {
        float t = c * uhp.dc - s * uhp.ds;
        s = s * uhp.dc + c * uhp.ds;
        c = t;
        uhp.buf[2u * i] = uhp.buf[2u * i + 1u] = (int32_t)(s * 4194304.0f);
    }
    k = 1.5f - 0.5f * (c * c + s * s);          /* (kept on the circle) */
    uhp.c = c * k, uhp.s = s * k;
    return uhp.buf;
}

static void uha_probe_rec(uha_probe_rec_t *r, uint32_t now)
{
    uha_stats_t s;
    uint32_t sec;
    usb_host_audio_stats(&s);
    sec = (now - uhp.t0) / 1000u;
    sec = sec ? sec : 1u;
    *r = (uha_probe_rec_t){0};
    r->secs = sec, r->bits = s.bits, r->frame_stuck = s.frame_stuck;
    r->pkts = (s.pkts - uhp.s0.pkts) / sec, r->frames = (s.frames - uhp.s0.frames) / sec;
    r->tx_errors = s.tx_errors - uhp.s0.tx_errors, r->underruns = s.underruns - uhp.s0.underruns;
    r->overruns = s.overruns - uhp.s0.overruns, r->catchups = s.catchups - uhp.s0.catchups;
    r->late = s.late - uhp.s0.late, r->wait_max = s.wait_max;
    r->fb_reads = s.fb_reads - uhp.s0.fb_reads, r->fb_errors = s.fb_errors - uhp.s0.fb_errors;
    r->fill_min = uhp.fill_min, r->fill_max = uhp.fill_max;
    r->ppm_min = uhp.ppm_min, r->ppm_max = uhp.ppm_max;
    r->ppm_mean = uhp.ppm_n ? uhp.ppm_sum / (int32_t)uhp.ppm_n : 0;
    if (s.tk_frames)                            /* (no 64-bit division, no double: the ticks off 24,000 a frame) */
        r->sof_ppm10 = (int32_t)((float)(int32_t)(int64_t)(s.tk_sum - (uint64_t)s.tk_frames * 24000u) /
                                 (float)s.tk_frames * (1e7f / 24000.0f));
    r->rf_min = s.rf_min / 24u, r->rf_max = s.rf_max / 24u, r->ld_min = s.ld_min / 24u, r->ld_max = s.ld_max / 24u;
}

static void uha_probe_start(uint32_t now)
{
    usb_host_audio_marks();
    usb_host_audio_stats(&uhp.s0);
    uhp.t0 = now;
    uhp.ppm_min = 0x7FFFFFFF, uhp.ppm_max = -0x7FFFFFFF;
    uhp.ppm_sum = 0, uhp.ppm_n = 0;
    uhp.fill_min = 0xFFFFFFFFu, uhp.fill_max = 0;
    uhp.fb_reads_seen = uhp.s0.fb_reads;
}

/* main loop: the phases (after usb_host_task) */
static void uha_probe_step(uint32_t now)
{
    uint32_t bits = usb_host_audio_bits();
    uha_stats_t s;
    if (uha_probe_log_rec.magic != UHA_PROBE_MAGIC) {
        uha_probe_log_rec.magic = UHA_PROBE_MAGIC;
        uha_probe_log_rec.done = 0;
    }
    if (uhp.phase == 0 && bits == 16u) {
        uha_probe_start(now);
        uhp.phase = 1;
    } else if (uhp.phase == 2 && bits == 24u) {
        uha_probe_start(now);
        uhp.phase = 3;
    } else if (uhp.phase == 2 && !bits && now - uhp.t0 > 5000u) {
        uhp.phase = 4;                          /* (no 24-bit: the device has none, or the stream stopped) */
    }
    if (uhp.phase != 1 && uhp.phase != 3)
        return;
    if (bits != (uhp.phase == 1 ? 16u : 24u)) {   /* stopped (OFF, unplugged): what it had, then wait again */
        uha_probe_rec(&uha_probe_log_rec.r[uhp.phase / 2u], now);
        uhp.phase = (uint8_t)(uhp.phase - 1u);
        return;
    }
    usb_host_audio_stats(&s);
    if (s.fb_reads != uhp.fb_reads_seen && now - uhp.t0 > 2000u) {   /* (after the stream's first seconds) */
        uhp.fb_reads_seen = s.fb_reads;
        uhp.ppm_min = s.ppm < uhp.ppm_min ? s.ppm : uhp.ppm_min;
        uhp.ppm_max = s.ppm > uhp.ppm_max ? s.ppm : uhp.ppm_max;
        uhp.ppm_sum += s.ppm;
        uhp.ppm_n++;
    }
    if (now - uhp.t0 > 2000u) {
        uhp.fill_min = s.fill < uhp.fill_min ? s.fill : uhp.fill_min;
        uhp.fill_max = s.fill > uhp.fill_max ? s.fill : uhp.fill_max;
    }
    if (now - uhp.t0 < UHA_PROBE_S * 1000u)
        return;
    uha_probe_rec(&uha_probe_log_rec.r[uhp.phase / 2u], now);
    uha_probe_log_rec.done |= 1u << (uhp.phase / 2u);
    if (uhp.phase == 1) {
        usb_host_audio(1, 1);                   /* alt 2 next */
        uhp.t0 = now;
        uhp.phase = 2;
    } else {
        usb_host_audio(1, 0);                   /* back to what was asked */
        uhp.phase = 4;
    }
}

static char *uhp_dec(char *b, int32_t v)
{
    char t[11];
    int n = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        *b++ = t[--n];
    return b;
}
static char *uhp_str(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    return b;
}
/* the LG readout's lines (log_line_send) */
static void uha_probe_log(void (*send)(const char *))
{
    char b[96], *q;                             /* (a frame takes the first 43) */
    uint32_t k;
    if (uha_probe_log_rec.magic != UHA_PROBE_MAGIC || !uha_probe_log_rec.r[0].secs) {
        send("uha probe: no stream measured");
        return;
    }
    for (k = 0; k < 2u; k++) {
        const uha_probe_rec_t *r = &uha_probe_log_rec.r[k];
        const char *a = k ? "uha alt2 " : "uha alt1 ";
        if (!r->secs)
            continue;
        q = uhp_str(b, a), q = uhp_dec(q, (int32_t)r->secs), q = uhp_str(q, uha_probe_log_rec.done >> k & 1u ? "s" : "s cut");
        q = uhp_str(q, " pk/s "), q = uhp_dec(q, (int32_t)r->pkts), q = uhp_str(q, " fr/s ");
        q = uhp_dec(q, (int32_t)r->frames), *q = 0;
        send(b);
        q = uhp_str(b, a), q = uhp_str(q, "err "), q = uhp_dec(q, (int32_t)r->tx_errors), q = uhp_str(q, " und ");
        q = uhp_dec(q, (int32_t)r->underruns), q = uhp_str(q, " ovr "), q = uhp_dec(q, (int32_t)r->overruns);
        q = uhp_str(q, " cu "), q = uhp_dec(q, (int32_t)r->catchups), q = uhp_str(q, " nf ");
        q = uhp_dec(q, (int32_t)r->frame_stuck), *q = 0;
        send(b);
        q = uhp_str(b, a), q = uhp_str(q, "late "), q = uhp_dec(q, (int32_t)r->late), q = uhp_str(q, " wm ");
        q = uhp_dec(q, (int32_t)r->wait_max), q = uhp_str(q, " fill "), q = uhp_dec(q, (int32_t)r->fill_min);
        q = uhp_str(q, ".."), q = uhp_dec(q, (int32_t)r->fill_max), *q = 0;
        send(b);
        q = uhp_str(b, a), q = uhp_str(q, "fb "), q = uhp_dec(q, (int32_t)r->fb_reads), q = uhp_str(q, " e ");
        q = uhp_dec(q, (int32_t)r->fb_errors), q = uhp_str(q, " ppm "), q = uhp_dec(q, r->ppm_min), q = uhp_str(q, "..");
        q = uhp_dec(q, r->ppm_max), *q = 0;
        send(b);
        q = uhp_str(b, a), q = uhp_str(q, "ppm mean "), q = uhp_dec(q, r->ppm_mean), q = uhp_str(q, " sof/t4 ");
        q = uhp_dec(q, r->sof_ppm10), q = uhp_str(q, "/10ppm"), *q = 0;
        send(b);
        q = uhp_str(b, a), q = uhp_str(q, "rf "), q = uhp_dec(q, (int32_t)r->rf_min), q = uhp_str(q, "..");
        q = uhp_dec(q, (int32_t)r->rf_max), q = uhp_str(q, " ld "), q = uhp_dec(q, (int32_t)r->ld_min);
        q = uhp_str(q, ".."), q = uhp_dec(q, (int32_t)r->ld_max), q = uhp_str(q, "us"), *q = 0;
        send(b);
    }
}
#define UHA_PROBE_SRC(o, n) uha_probe_src(o, n)
#else
#define UHA_PROBE_SRC(o, n) (o)
#endif
