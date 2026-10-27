/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* Optimist's storage in the trial (milestone 3): its own files (storage/storage.c's A/B objects, project.c's projects,
 * the autosave and the settings record, sections/'s section log, upreset.c's user presets, drum_kits.c's kit bank,
 * snapshots/, drum_store.c, motion_flash.c) compiled unchanged after its UI, as Optimist's felucca.c includes them,
 * on fm1-base's flash access instead of Optimist's flash driver (hal/fm1_flash.h): the same addresses, the same
 * bytes, so an FM-1's data stays readable.
 *   st_read / st_erase / st_prog   Optimist's three hooks: plat_data / plat_data_erase / plat_data_write for what is
 *                                  inside plat.h's windows (the main store 0x97000..0xDFFFF, XDATA0's 0x95000 / 0x96000,
 *                                  XDATA1's drum records 0xE5000 / 0xE6000) and its globals 0xFC000..0xFEFFF (the settings
 *                                  A/B, the autosave's copy B: plat.h's fourth window, PLAT_XDATA2, through app.c opt_glob_*)
 *   FL_STORE_OK                    Optimist's allow-list on top (hal/fm1_flash_map.h): never the SDK's VM, BTIF or USR
 *                                  area (0xE7000..0xFBFFF, which plat.h's XDATA1 includes), never key_mac
 *   st_xip                         the banks read in place: plat_data, or the globals' own window (opt_glob_ptr)
 * Main loop only (plat.h: an erase or a write stops everything for a moment). */
#pragma once

#include "../hal/fm1_flash_map.h"              /* (Optimist's hal/, beside its src/ on the include path: FL_STORE_OK, FL_NEVER) */
#define FELUCCA_FLASH 1                         /* (felucca.c: the flash driver and storage.c) */
#if FELUCCA_EDITOR && FELUCCA_ED_MIRROR
static uint8_t st_blank;                        /* a test session (ed_mirror.c, 108 kind 5; opt_editor.h): Optimist's own
                                                 * data reads erased (a blank FM-1), nothing is written (felucca.c's) */
#else
#define st_blank 0                              /* (felucca.c without the mirror's test sessions) */
#endif
static uint8_t flash_ok;                        /* (persist_boot: fl_jedec_ram) */

/* the globals' sectors 0xFC000..0xFEFFF: the app's unit (app.c opt_glob_*, core/opt_core.h) */

/* where [off, off + n) is read in place: one of plat.h's windows, or the globals; 0 = neither (or across two) */
static const uint8_t *opt_flash_at(uint32_t off, uint32_t n)
{
    if (PLAT_DATA_OK(off, n))
        return plat_data(off);
    if (plat_in(off, n, OPT_GLOB_LO, OPT_GLOB_HI))
        return opt_glob_ptr(off);
    return 0;
}

/* Optimist's flash driver calls in storage/ (hal/fm1_flash.h), on fm1-base: */
#define FL_FAR(fn) (fn)
static uint32_t fl_jedec_ram(void)              /* the part Optimist expects: plat_data answers (fm1-base's flash_ok) */
{
    return plat_data(PLAT_DATA_LO) ? 0x856014u : 0u;
}
static void fl_plain_window_init(void) {}       /* (fm1-base's start-up maps the data area as plaintext: main_fm1.c) */
#define fl_inval(off, len) ((void)(off), (void)(len))   /* (nbank.c's CZ store: felucca-bsp's driver drops the
                                                 * cache lines itself, after each erase and program) */
static uint32_t irq_save(void) { fm1_irq_off(); return 1u; }
static void irq_restore(uint32_t f) { if (f) fm1_irq_on(); }
#define ST_XIP_PTR(off) opt_flash_at((off), 1u)  /* (storage.c st_xip: the device's way, on the host too) */

static int st_read(uint32_t off, void *dst, uint32_t n)
{
    const uint8_t *p;
    if (st_blank && FL_STORE_OK(off, n)) {
        memset(dst, 0xFF, n);                   /* (a test session: erased) */
        return 0;
    }
    p = opt_flash_at(off, n);
    if (!p)
        return -8;
    memcpy(dst, p, n);
    return 0;
}
/* an erase that held the sound for the whole of it (fm1-base's plat_flash_held grew): Optimist's FL_ERASE_HELD (felucca.c
 * with LIVE_SAVE: the saves wait for the stop from then on) */
static int opt_erase_held(int rc)
{
#if FELUCCA_LIVE_SAVE
    static uint32_t opt_held_seen;
    uint32_t h = plat_flash_held();
    if (h != opt_held_seen)
        live_save_off = 1;
    opt_held_seen = h;
#endif
    return rc;
}
static int st_erase(uint32_t off)
{
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    if (st_blank)
        return 0;                               /* (a test session: done, nothing erased) */
    if (PLAT_DATA_OK(off, 0x1000u))
        return opt_erase_held(plat_data_erase(off));
    if (plat_in(off, 0x1000u, OPT_GLOB_LO, OPT_GLOB_HI))
        return opt_erase_held(opt_glob_erase(off));
    return -8;
}
/* Optimist's section log clears bits in place (sec_log.c: a record's state, dead): plat.h promises that plat_data_write
 * may (each byte the old AND the new, on the device and the host) */
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    if (st_blank)
        return 0;                               /* (a test session: done, nothing written) */
    if (PLAT_DATA_OK(off, n))
        return plat_data_write(off, src, n);
    if (plat_in(off, n, OPT_GLOB_LO, OPT_GLOB_HI))
        return opt_glob_write(off, src, n);
    return -8;
}

/* felucca.c's order from here */
#include "storage/storage.c"
#include "storage/upreset.c"                    /* user presets */
#include "storage/project.c"
#if FELUCCA_DRUM_KITS
#include "drums/drum_kits.c"                    /* user drum kits: the bank of 16 */
#endif
#include "engines/fm6/fm6_sysex.c"              /* DX7 SysEx into FM6 (nbank.c, upreset.c's FM6 voices) */
#if FELUCCA_NB_LIST
#include "storage/nbank.c"                      /* the FM6 / CZ native collections in PRESETS */
#endif
/* the platform's own store object OBJ_PANEL sits at 0xD8000 / 0xD9000 (app.h APP_STORE_MAP; the SD card mode: 0xCE000 /
 * 0xCF000): nothing of Optimist's there.
 * The areas that end at it: the user sample slots (USR3 ends at SMP_USR3_END), the snapshots and the CZ collection
 * below SMP_BANKS; the kit bank starts above it. The objects' own sectors: opt_storage_hits_panel (the host's check) */
#if FELUCCA_LAYOUT == 1
/* the SD card mode (LAYOUT 1): OBJ_PANEL above fm1-base's bigger slot (app.h), below Optimist's squeezed store */
#define OPT_PANEL_LO FM1B_APP_END
#define OPT_PANEL_HI (FM1B_APP_END + 0x2000u)
_Static_assert(FM1B_APP_END == 0xCE000u && OPT_PANEL_HI <= FL_DATA_LO && PLAT_DATA_LO == FM1B_APP_END,
               "the SD card mode on fm1-base's slot to 0xCE000: OBJ_PANEL at 0xCE000 / 0xCF000, Optimist's store above");
#else
#define OPT_PANEL_LO 0xD8000u
#define OPT_PANEL_HI 0xDA000u
_Static_assert(FM1B_APP_END == 0x93000u, "the normal layout on fm1-base's normal slot");
_Static_assert(SMP_USR3_END <= OPT_PANEL_LO && SMP_BANKS == OPT_PANEL_LO && ST_UKIT_SECTOR >= OPT_PANEL_HI,
               "Optimist's data off the platform's OBJ_PANEL (0xD8000 / 0xD9000)");
#endif
#if FELUCCA_SNAPSHOTS
_Static_assert(SN_TOP <= OPT_PANEL_LO, "the snapshots below OBJ_PANEL");
#endif
#if SEC_LOGGED && FELUCCA_LAYOUT == 1
_Static_assert(SEC_LOG_BASE >= OPT_PANEL_HI, "the section log above OBJ_PANEL (the SD card mode)");
#elif SEC_LOGGED
_Static_assert(SEC_LOG_BASE + SEC_LOG_SECTORS * ST_SECTOR <= OPT_PANEL_LO, "the section log below OBJ_PANEL");
#endif
static uint32_t opt_storage_hits_panel(void)    /* storage.c's objects whose copy A or B is on OBJ_PANEL's two */
{
    uint32_t o, c, n = 0;
    for (o = 0; o < OBJ_COUNT; o++)
        for (c = 0; c < 2u; c++)
            n += FL_HITS(st_sector(o, c), ST_SECTOR, OPT_PANEL_LO, OPT_PANEL_HI);
    return n;
}
