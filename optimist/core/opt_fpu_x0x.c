/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The X0X 909 / 808 kits' float models (Optimist's drums/x0x/x0x_drums.c, from X0X) as a unit of its own on the FM-1,
 * as Optimist's tools/build.py builds it (-O2 with the FPU: fm1-base's o2 flags; its data headers in build/optgen). The
 * host has it inside core/opt_core.c already (drum_x0x.c includes it there without __PI32V2__): nothing here. The
 * build's switches: fm1.toml o2_include. */
#ifndef PLAT_HOST
#include "drums/x0x/x0x_drums.c"
#else
typedef int opt_fpu_x0x_host_empty;      /* (ISO C: a unit holds a declaration) */
#endif
