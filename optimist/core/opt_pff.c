/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* ChaN's Petit FatFs R0.03a (BSD-1-Clause, Optimist's storage/sd/pff.c: its pffconf.h), a unit of its own as Optimist
 * builds it in every SDCARD build without the SD card mode's big layout (tools/build.py: pff.c at -Os; ir_pass.py here):
 * FAT16 and FAT32, 8.3 names, no files made. Its disk layer is the Optimist unit's (core/opt_sd_mmc.c disk_initialize,
 * disk_readp, disk_writep, on fm1-base's card, as opt_pff_disk_*) */
#if defined(FELUCCA_SDCARD) && FELUCCA_SDCARD && !(FELUCCA_LAYOUT == 1)   /* (registry.h: no FELUCCA_SD_FATFS) */
#define disk_initialize opt_pff_disk_initialize   /* (core/opt_sd_mmc.c's, renamed the same: fm1-base's FatFs, linked */
#define disk_readp opt_pff_disk_readp             /* with sd = true, has disk_initialize) */
#define disk_writep opt_pff_disk_writep
#include "storage/sd/pff.c"
#else
typedef int opt_pff_empty;               /* (ISO C: a unit holds a declaration) */
#endif
