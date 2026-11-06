/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SD test app: see app.h */
static uint32_t sd_ok, sd_bad;
static uint8_t blk[4 * 512] __attribute__((aligned(4)));

void app_init(void) {}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}
void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }
void app_frame(void) {}
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a, (void)b; }

#ifdef PLAT_HOST
static void check(int cond, const char *what)
{
    printf("  %s sd: %s\n", cond ? "ok  " : "FAIL", what);
    if (cond)
        sd_ok++;
    else
        sd_bad++;
}

static uint8_t pat(uint32_t b, uint32_t i) { return (uint8_t)(b * 5u + i * 3u + 1u); }

/* the card's blocks 1000..1003 written with a pattern and read back, by every call; block 0 read (the image's
 * first bytes: a boot sector ends 55 AA) */
static void sdcheck(void)
{
    uint32_t b, i, okp;
    int r = plat_sd_init();
    check(r == 0 && plat_sd_present() && plat_sd_blocks() > 1004u, "the card started, its size known");
    if (r)
        return;
    check(plat_sd_read(0, blk, 1) == 0 && blk[510] == 0x55u && blk[511] == 0xAAu, "block 0 read: a boot sector");
    for (b = 0; b < 4u; b++)
        for (i = 0; i < 512u; i++)
            blk[b * 512u + i] = pat(1000u + b, i);
    check(plat_sd_write(1000, blk, 1) == 0 && plat_sd_write(1001, blk + 512, 3) == 0, "one block written, then three");
    for (i = 0; i < sizeof blk; i++)
        blk[i] = 0;
    okp = plat_sd_read(1000, blk, 4) == 0;
    for (b = 0; okp && b < 4u; b++)
        for (i = 0; i < 512u; i++)
            okp = okp && blk[b * 512u + i] == pat(1000u + b, i);
    check(okp, "the four read back as one run");
    okp = plat_sd_read_open(1001) == 0 && plat_sd_read(0, blk, 1) == PLAT_SD_BUSY;
    for (b = 0; okp && b < 2u; b++) {
        okp = plat_sd_read_next(blk, 1000) == 0;
        for (i = 0; okp && i < 512u; i++)
            okp = blk[i] == pat(1001u + b, i);
    }
    check(okp && plat_sd_close() == 0, "a read run: two blocks, the other calls busy meanwhile");
    for (i = 0; i < 512u; i++)
        blk[i] = pat(2000u, i);
    check(plat_sd_write_open(1002) == 0 && plat_sd_write_next(blk) == 0 && plat_sd_write_next(blk) == 0 &&
          plat_sd_close() == 0 && plat_sd_read(1003, blk + 512, 1) == 0 && blk[512 + 7] == pat(2000u, 7),
          "a write run: two blocks");
    check(plat_sd_read(plat_sd_blocks(), blk, 1) == PLAT_SD_ERR, "past the card's end: refused");
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a, (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "sdcheck"))
        sdcheck();
    else if (!strcmp(cmd, "sdgone"))        /* the card out: no start-up, every call refused */
        check(plat_sd_init() == PLAT_SD_NOCARD && !plat_sd_present() && plat_sd_read(0, blk, 1) == PLAT_SD_NOCARD,
              "no card: PLAT_SD_NOCARD, a read refused");
    else
        return 0;
    return 1;
}

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "sd_ok"))
        *got = (int)sd_ok;
    else if (!strcmp(what, "sd_bad"))
        *got = (int)sd_bad;
    else
        return 0;
    return 1;
}
#endif
