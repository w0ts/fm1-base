/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* ANALOG 2's filter models (Optimist's engines/analog/a2_models.c: float) as a unit of its own on the FM-1, as Optimist's
 * tools/build.py builds it (-O2 with the FPU: fm1-base's o2 flags). The host has it inside core/opt_core.c already
 * (eng_analog2.c includes it there without __PI32V2__): nothing here. The build's switches: fm1.toml o2_include. */
#ifndef PLAT_HOST
#include "engines/analog/a2_models.c"
#else
typedef int opt_fpu_a2m_host_empty;      /* (ISO C: a unit holds a declaration) */
#endif
