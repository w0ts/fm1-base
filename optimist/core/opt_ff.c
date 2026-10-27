/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* ChaN's FatFs R0.16 (BSD-1-Clause, Optimist's storage/sd/fatfs/: its ffconf.h), a unit of its own as Optimist builds it
 * (tools/build.py: ff.c at -Os; ir_pass.py here); its disk layer is the Optimist unit's (ui/opt_sd.h: sd_diskio.c) */
#if defined(FELUCCA_SDCARD) && FELUCCA_SDCARD && FELUCCA_LAYOUT == 1 && FELUCCA_FAR_OK   /* (registry.h FELUCCA_SD_FATFS) */
#include "storage/sd/fatfs/ff.c"
#endif
