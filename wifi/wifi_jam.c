/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Copyright (C) 2026 Henri David (hdavid), Optimist (github.com/w0ts/optimist) */
/* MODE JAM's air (wifi_jam.h). */
#include "wifi_jam.h"

#ifndef __PI32V2__
#include <string.h>                      /* (the firmware's: felucca-bsp's libc.c, in the unity) */
#endif

void ja_init(struct ja_net *n, const uint8_t mac[6], uint32_t group)
{
    memset(n, 0, sizeof *n);
    memcpy(n->mac, mac, 6);
    memcpy(n->name, "fm1-jam-", 8);
    n->name[8] = (uint8_t)('0' + group);
    n->name_len = group < 2u ? 7u : 9u;
    n->ch = (uint8_t)(1u + 5u * ((group + 2u) % 3u));     /* 1 -> 1, 2 -> 6, 3 -> 11, 4 -> 1 .. */
    memcpy(n->bssid, "\x02" "fm1j", 5);
    n->bssid[5] = (uint8_t)group;
}
uint32_t ja_addr(const uint8_t b[2])
{
    return 0xA9FE0000u | (1u + (uint32_t)b[0] % 254u) << 8 | (1u + (uint32_t)b[1] % 254u);
}

static void ja_note(struct ja_net *n, const uint8_t *f, int8_t rssi, uint32_t now_ms, uint32_t beacon)
{
    struct ja_peer *p = 0, *q;
    for (q = n->peer; q < n->peer + JA_PEERS; q++) {
        if (q->used && !memcmp(q->mac, f + 10, 6)) {
            p = q;
            break;
        }
        if (!p && (!q->used || now_ms - q->t_heard >= JA_HEARD_MS))
            p = q;                            /* (a free place, or one gone) */
    }
    if (!p)
        return;                               /* (full: it still plays, it is only not counted) */
    if (memcmp(p->mac, f + 10, 6) || !p->used) {
        memset(p, 0, sizeof *p);
        memcpy(p->mac, f + 10, 6);
        p->used = 1;
    }
    p->rssi = rssi;
    p->t_heard = now_ms;
    (beacon ? &p->n_beacon : &p->n_data)[0]++;
}

uint32_t ja_heard(struct ja_net *n, const uint8_t *f, uint32_t len, int8_t rssi, uint32_t now_ms)
{
    uint32_t type;
    if (len < 24u || (f[1] & 0x03u) || (f[10] & 1u) || memcmp(f + 16, n->bssid, 6) || !memcmp(f + 10, n->mac, 6))
        return JA_NOT_OURS;                   /* (a frame of another network, or an access point's, or our own) */
    type = f[0] & 0x0Cu;
    if (type == 0u && f[0] == 0x80u) {        /* a beacon: the name says the same as the BSSID, a stranger's may not */
        if (len < 38u + n->name_len || f[36] || f[37] != n->name_len || memcmp(f + 38, n->name, n->name_len))
            return JA_NOT_OURS;
        ja_note(n, f, rssi, now_ms, 1u);
        return JA_BEACON;
    }
    if (type != 0x08u || (f[1] & 0x40u))
        return JA_NOT_OURS;                   /* (the group's frames are open: a protected one is not ours) */
    ja_note(n, f, rssi, now_ms, 0u);
    return (f[4] & 1u) || !memcmp(f + 4, n->mac, 6) ? JA_DATA : JA_NOT_OURS;
}

uint32_t ja_sender_ip(const uint8_t *f, uint32_t len)
{
    const uint8_t *e = f + 24u + 8u;         /* (our frames carry no QoS field; a computer's QoS data: not read as an address) */
    if (f[0] != 0x08u || len < 24u + 8u + 20u || e[-2] != 0x08u || e[-1] != 0x00u)
        return 0;
    return (uint32_t)e[12] << 24 | (uint32_t)e[13] << 16 | (uint32_t)e[14] << 8 | e[15];
}

uint32_t ja_beacon(struct ja_net *n, uint64_t tsf, uint8_t *t)
{
    static const uint8_t RATES[3] = {1, 1, 0x82};   /* 1 Mb/s, what every frame of the group goes at */
    uint32_t i, o;
    memset(t, 0, 36);
    t[0] = 0x80u;
    memset(t + 4, 0xFF, 6);
    memcpy(t + 10, n->mac, 6);
    memcpy(t + 16, n->bssid, 6);
    t[22] = (uint8_t)(n->seq << 4);
    t[23] = (uint8_t)(n->seq >> 4);
    n->seq++;
    for (i = 0; i < 8u; i++)
        t[24u + i] = (uint8_t)(tsf >> (8u * i));
    t[32] = 0xE8u, t[33] = 0x03u;             /* the interval: 1000 TU */
    t[34] = 0x02u;                            /* the capability: IBSS */
    t[36] = 0;
    t[37] = n->name_len;
    memcpy(t + 38, n->name, n->name_len);
    o = 38u + n->name_len;
    memcpy(t + o, RATES, 3);
    t[o + 3u] = 3, t[o + 4u] = 1, t[o + 5u] = n->ch;
    t[o + 6u] = 6, t[o + 7u] = 2, t[o + 8u] = 0, t[o + 9u] = 0;   /* the IBSS parameter set: no ATIM window */
    return o + 10u;
}

uint32_t ja_count(const struct ja_net *n, uint32_t now_ms)
{
    uint32_t i, c = 0;
    for (i = 0; i < JA_PEERS; i++)
        c += n->peer[i].used && now_ms - n->peer[i].t_heard < JA_HEARD_MS;
    return c;
}
