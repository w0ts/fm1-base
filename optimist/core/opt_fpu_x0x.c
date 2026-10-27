/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The X0X 909 / 808 kits' float models (Optimist's drums/x0x/x0x_drums.c, from X0X) as a unit of its own on the FM-1,
 * as Optimist's tools/build.py builds it (-O2 with the FPU: fm1-base's o2 flags; its data headers in build/optgen). The
 * host has it inside core/opt_core.c already (drum_x0x.c includes it there without __PI32V2__): nothing here. The
 * build's switches: fm1.toml o2_include. */
#ifndef PLAT_HOST
/* the arena's extras (registry.h FELUCCA_FX_ARENA: ARENA in the SD card mode's LAYOUT 1), as the Optimist unit has them:
 * the machines' state in the arena (x0x_mem, core/arena_bufs.c), as Optimist's tools/build.py passes it to its x0x.o */
#if !defined(FELUCCA_FX_ARENA) && defined(FELUCCA_ARENA_KB) && FELUCCA_ARENA_KB && defined(FELUCCA_LAYOUT) && \
    FELUCCA_LAYOUT == 1
#define FELUCCA_FX_ARENA 1
#endif
#include "drums/x0x/x0x_drums.c"
#else
typedef int opt_fpu_x0x_host_empty;      /* (ISO C: a unit holds a declaration) */
#endif
