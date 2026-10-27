/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the Felucca update loader (bsp/loader/ldr_core.c): a fake
 * host serves a package over the SysEx protocol, the "device" flash starts as
 * another package's flash.bin (as if that firmware were installed).
 *   ldr_test OLD.fwsc NEW.fwsc */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, erases, data_erases, bad_range, record_cleared, f0_asked;
static int rec_prog_fails;

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../bsp/chip/ota.c"

static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u || off >= 0xFC000u || (off & 0xFFFu)) bad_range++;
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    erases++;
    data_erases += off >= 0x93000u;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    int rec = (off & 0xFFFu) == 0xF00u && n <= 80u && off >= 0x93000u && off < 0xFC000u &&
              !(off >= 0x97000u && off < 0xE0000u);   /* an update record's slot, outside the store: made invalid */
    if (!rec && (off < 0x4000u || off + n > 0x93000u || ((off & 0xFFu) + n) > 256u)) bad_range++;
    if (rec && rec_prog_fails) return -1;      /* (the driver refusing: the loader falls back to the erase) */
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../bsp/loader/ldr_core.c"

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}

static uint32_t flash_off(const uint8_t *lg)        /* flash.bin offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

static int check(const char *what, int ok) { printf("%-56s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

/* an update record (UPDATA_PARM, update_mode_api_v2: ota.c's layout) at a 4 KiB boundary - 256 */
static void put_record(uint32_t at, const char *ident)
{
    uint8_t r[112] = {0};
    r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
    memcpy(r + 8, ident, strlen(ident) < 12 ? strlen(ident) : 12);
    ota_wr32(r + 72, 0xE0000u);
    ota_wr16(r, ota_crc16(r + 2, 78, 0));
    memcpy(nor + at, r, sizeof r);
}
/* a record the SPL could still take, anywhere from the app area's end to the globals (4K boundary - 256): its CRC
 * matching over the 78 bytes from 2 (a stored 0 too, should those bytes sum to 0) or "AT" still at 6 */
static uint32_t record_left(void)
{
    uint32_t s;
    for (s = 0x93F00u; s < 0xFC000u; s += 0x1000u)
        if ((s < 0x97000u || s >= 0xE0000u) &&
            (ota_rd16(nor + s) == ota_crc16(nor + s + 2, 78, 0) || ota_rd16(nor + s + 6) == 0x5441u))
            return s;
    return 0;
}
/* a sector's bytes as they were, but for the first n of its record slot (0xF00): 1 = kept */
static int sector_kept(uint32_t sec, const uint8_t *was, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < 0x1000u; i++)
        if (nor[sec + i] != was[i] && !(i >= 0xF00u && i < 0xF00u + n))
            return 0;
    return 1;
}
/* a record whose bytes 2..79 have a CRC of 0 once its first 8 are 0 (its last two chosen so): zeroing only those
 * would leave a stored CRC 0 that matches, so the loader must zero more of it */
static void put_record_crc0(uint32_t at)
{
    uint8_t z[78];
    uint32_t c;
    put_record(at, "crc0-case");
    memcpy(z, nor + at + 2, sizeof z);
    memset(z, 0, 6);
    c = ota_crc16(z, 76, 0);
    nor[at + 78] = (uint8_t)(c >> 8);
    nor[at + 79] = (uint8_t)c;
    ota_wr16(nor + at, ota_crc16(nor + at + 2, 78, 0));
}
static void fill(uint32_t sec, uint8_t seed)      /* an app's data (or the SDK's VM): not erased bytes */
{
    uint32_t i;
    for (i = 0; i < 0x1000u; i++)
        nor[sec + i] = (uint8_t)(seed + i * 7u + (i >> 8));
}

/* after an install the records go, nothing else: the loader's own (the staging area's 0xE4F00, the sector erased as
 * ota.c's next staging does anyway) and any other found where a firmware keeps data (the stock firmware's 0xE8F00
 * in the SDK's VM, an app's sector in XDATA0 / XDATA1 whose bytes read as one): made invalid where they are */
static int records_test(const uint8_t *old, uint32_t ofo)
{
    static const uint32_t SEC[] = {0x95000u, 0xE5000u, 0xE6000u, 0xE8000u, 0xFB000u};
    static uint8_t was[sizeof SEC / sizeof SEC[0]][0x1000], vm[0x1000], untouched[0x1000];
    uint32_t i, k;
    int bad = 0, rc;
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, old + ofo, 0x93000);
    for (i = 0; i < sizeof SEC / sizeof SEC[0]; i++) {
        fill(SEC[i], (uint8_t)(0x31u * i + 5u));
        if (i == 2)
            put_record_crc0(SEC[i] + 0xF00u);
        else
            put_record(SEC[i] + 0xF00u, i == 3 ? "ota-stock" : "app-data");
        memcpy(was[i], nor + SEC[i], 0x1000);
    }
    fill(0xE7000u, 0x99u);                            /* a sector of data with no record: never touched */
    memcpy(untouched, nor + 0xE7000u, 0x1000);
    fill(0xA0000u, 0x42u);                            /* the store: never even looked at */
    put_record(0xA0F00u, "in-the-store");
    memcpy(vm, nor + 0xA0000u, 0x1000);
    put_record(0xE4F00u, "ota-FM-1_015");             /* the loader's own, as ota.c stages it */
    nor[0xE0000] = 0x12;                              /* (the staged loader's first byte: its staging is not erased) */
    erases = 0; data_erases = 0; requests = 0; bad_range = 0;
    rc = ldr_session();
    bad += check("records: install completes", rc == 0);
    bad += check("  the loader's own record: its staging sector erased", nor[0xE4F00] == 0xFF && nor[0xE4F06] == 0xFF &&
                 !memcmp(nor + 0xE4000u, nor + 0xE4001u, 0xFFF));
    bad += check("  the staged loader itself left (the app's boot clears it)", nor[0xE0000] == 0x12);
    for (i = 0, k = 1; i < sizeof SEC / sizeof SEC[0]; i++)
        k &= sector_kept(SEC[i], was[i], 80);
    bad += check("  every other record's sector kept, but for its record", k);
    bad += check("  the stock record in the SDK's VM (0xE8F00) kept its sector", sector_kept(0xE8000u, was[3], 80));
    bad += check("  no record the SPL could take is left", record_left() == 0);
    bad += check("  a sector without a record untouched", !memcmp(nor + 0xE7000u, untouched, 0x1000));
    bad += check("  the store untouched, a record-like block in it too", !memcmp(nor + 0xA0000u, vm, 0x1000));
    bad += check("  above the app area one erase only (the staging sector)", data_erases == 1 && bad_range == 0);
    for (i = 0, k = 0; i < sizeof SEC / sizeof SEC[0]; i++)
        k |= ota_rd16(nor + SEC[i] + 0xF00u) | ota_rd16(nor + SEC[i] + 0xF06u);
    bad += check("  each record's CRC and \"AT\" now 0", k == 0);
    {
        uint32_t e = erases;
        erases = 0;
        rc = ldr_session();
        bad += check("records: a second install changes nothing more", rc == 0 && erases == 0 && record_left() == 0);
        erases = e;
    }
    {   /* the crafted one needed more than its first 8 bytes: still only its 80 at most */
        uint8_t z[2] = {0, 0};
        bad += check("  a record whose zeroed CRC would match: more of it zeroed",
                     record_left() == 0 && !memcmp(nor + 0xE6F00u, z, 2) && nor[0xE6F08] == 0);
    }
    /* a record the driver will not program over: the loader erases its sector, as it always did (no record left
     * to bring the SPL back into the loader) */
    fill(0xE5000u, 0x77u);
    put_record(0xE5F00u, "app-data");
    rec_prog_fails = 1;
    data_erases = 0;
    rc = ldr_session();
    rec_prog_fails = 0;
    bad += check("records: one not made invalid in place: its sector erased", rc == 0 && data_erases == 1 &&
                 nor[0xE5000] == 0xFF && nor[0xE5F06] == 0xFF && record_left() == 0);
    return bad;
}

int main(int argc, char **argv)
{
    size_t olen;
    uint8_t *old, head[0x4000];
    uint32_t ofo, nfo;
    int bad = 0, rc;
    if (argc < 3) { printf("usage: ldr_test OLD.fwsc NEW.fwsc\n"); return 2; }
    old = load_logical(argv[1], &olen);
    logical = load_logical(argv[2], &logical_len);
    ofo = flash_off(old);
    nfo = flash_off(logical);
    /* device: OLD installed, plus a valid update record at 0xE4F00 */
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, old + ofo, 0x93000);
    memcpy(head, nor, sizeof head);
    {
        uint8_t r[112] = {0};
        r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
        ota_wr16(r, ota_crc16(r + 2, 78, 0));
        memcpy(nor + 0xE4F00, r, sizeof r);
    }
    rc = ldr_session();
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("install completes", rc == 0);
    bad += check("app area == the new package's flash.bin", !memcmp(nor + 0x4000, logical + nfo + 0x4000, 0x93000 - 0x4000));
    bad += check("flash head [0, 0x4000) untouched", !memcmp(nor, head, sizeof head));
    bad += check("finish asked (0xF0000000)", f0_asked >= 1);
    bad += check("update record cleared (RAM + flash)", record_cleared && nor[0xE4F06] == 0xFF);
    bad += check("no write outside the allowed windows", bad_range == 0);
    /* same package again: nothing to erase */
    erases = 0; requests = 0;
    rc = ldr_session();
    bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0);
    /* a package for another chip key is refused before any erase */
    {
        uint8_t save = logical[nfo + 0x4000 + 5];
        logical[nfo + 0x4000 + 5] ^= 0x5A;           /* app area head no longer decrypts */
        memcpy(nor, old + ofo, 0x93000);
        erases = 0;
        rc = ldr_session();
        bad += check("foreign key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
        logical[nfo + 0x4000 + 5] = save;
    }
    bad += records_test(old, ofo);
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}
