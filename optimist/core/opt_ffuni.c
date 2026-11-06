/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* FatFs' code page tables (storage/sd/fatfs/ffunicode.c, BSD-1-Clause), a unit of its own as Optimist builds it */
#if defined(FELUCCA_SDCARD) && FELUCCA_SDCARD && FELUCCA_LAYOUT == 1   /* (registry.h FELUCCA_SD_FATFS) */
#include "storage/sd/fatfs/ffunicode.c"
#endif
