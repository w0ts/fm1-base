/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* ACID's float DSP (Optimist's engines/acid/acid_dsp.c, from X0X) as a unit of its own on the FM-1, as Optimist's
 * tools/build.py builds it (-O2 with the FPU: fm1-base's o2 flags). The host has it inside core/opt_core.c already
 * (eng_acid.c includes it there without __PI32V2__): nothing here. The build's switches: fm1.toml o2_include. */
#ifndef PLAT_HOST
#define FAST_SECTION ".app_ram"                  /* ("on, code in RAM": fm1-base's RAM code, plat.h PLAT_RAM_CODE) */
#include "engines/acid/acid_dsp.c"
#else
typedef int opt_fpu_acid_host_empty;     /* (ISO C: a unit holds a declaration) */
#endif
