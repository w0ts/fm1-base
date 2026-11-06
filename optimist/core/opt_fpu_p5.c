/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* PROPHET's voice (Optimist's engines/prophet/p5_dsp.c, Melodee's: float) as a unit of its own on the FM-1, as Optimist's
 * tools/build.py builds it (-O2 with the FPU: fm1-base's o2 flags), only with ENG_PROPHET. The host has it inside
 * core/opt_core.c already (eng_prophet.c includes it there without __PI32V2__): nothing here. The build's switches:
 * fm1.toml o2_include. */
#if !defined(PLAT_HOST) && FELUCCA_ENG_PROPHET
#define FAST_SECTION ".app_ram"                  /* ("on, code in RAM": fm1-base's RAM code, plat.h PLAT_RAM_CODE) */
#include "engines/prophet/p5_dsp.c"
#else
typedef int opt_fpu_p5_empty;            /* (ISO C: a unit holds a declaration) */
#endif
