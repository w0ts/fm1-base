/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's SD card in the trial (builder item SDCARD, the SD card mode: LAYOUT 1, FAR_OK, ARENA, SD_RING, SD_REC;
 * Optimist's docs/SD-CARD-DESIGN.md), after its storage, in felucca.c's order: its card code compiled unchanged
 * (storage/sd/sdcard.c with full FatFs, its disk layer, the card's own folder, the index, the browser, the saves, the
 * recorder, the streams and their plan; storage/sd/sd_mode.c: the projects, presets, kits and samples by name), on
 * fm1-base's SD card (core/opt_sd_mmc.c in sd_mmc.c's place: plat_sd_*). FatFs itself (storage/sd/fatfs/ff.c,
 * ffunicode.c) is its own two units (core/opt_ff.c, core/opt_ffuni.c: fm1.toml o2). The main loop's card work
 * (system/main.c: the streams' plan and runs, the recorder's blocks, the SD card mode's poll) is opt_sd_poll and
 * opt_sd_wait (ui/opt_ui.c); the master into the recorder's ring: core/opt_core.c opt_core_render (sdr_feed). The
 * card's start at power-on: fm1-base's lcd_init puts it into SPI mode before the screen's first byte (as Optimist's
 * main.c sd_mount_boot before lcd_init); opt_sd_boot mounts it. */
#pragma once

#if FELUCCA_SDCARD
#define SD_MMC_C "opt_sd_mmc.c"                 /* (core/: the unit's own directory, on the include path) */
#include "storage/sd/sdcard.c"
#endif
#if FELUCCA_SD_UI
static void fm1_reboot(void);                   /* (ui/opt_editor.h: plat_restart; a card restore's restart, sd_mode.c) */
#include "storage/sd/sd_mode.c"
#endif

static void opt_sd_boot(void)                   /* power-on: the card's filesystem (main.c sd_mount_boot) */
{
#if FELUCCA_SDCARD
    (void)sd_mount_boot();
#endif
}

static void opt_sd_poll(void)                   /* a main loop pass (main.c's order: after the screen) */
{
#if FELUCCA_SD_STREAM
    sst_plan_poll();                            /* the card samples' heads and rings in the arena (sd_stream_plan.c) */
    (void)sst_service();                        /* a run for the stream with the least left (sd_stream.c) */
#endif
#if FELUCCA_SD_REC
    (void)sdr_service();                        /* a recording's next block to the card (sd_rec.c) */
#endif
}

static void opt_sd_mode_poll(void)              /* after the autosave (main.c's order) */
{
#if FELUCCA_SD_UI
    sdm_poll();                                 /* SD card mode: the card after a mount, a save left pending (sd_mode.c) */
#endif
}

static void opt_sd_wait(void)                   /* between passes, while the screen idles (main.c MAIN_WAIT_BEGIN) */
{
#if FELUCCA_SD_STREAM
    (void)sst_service();
#endif
#if FELUCCA_SD_REC
    (void)sdr_service();
#endif
}
