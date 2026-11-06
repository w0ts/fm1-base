#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Optimist's side of APP_UPDATE_BEGIN (fm1-base's tests/update_begin_test.c has the platform's): app.h defines it,
app.c calls opt_update_begin, ui/opt_ui.c includes opt_update.h, which runs stop_first. Run from fm1-base's root."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
h = (ROOT / "optimist/app.h").read_text()
ac = (ROOT / "optimist/app.c").read_text()
ui = (ROOT / "optimist/ui/opt_ui.c").read_text()
u = (ROOT / "optimist/ui/opt_update.h").read_text()
assert "#define APP_UPDATE_BEGIN\nvoid app_update_begin(void);" in h
assert "void app_update_begin(void) { opt_update_begin(); }" in ac
assert ui.index('#include "opt_in.h"') < ui.index('#include "opt_update.h"')
assert u.index('#include "system/stop_first.h"') < u.index("void opt_update_begin(void) { stop_first(); }")
print("Optimist: APP_UPDATE_BEGIN -> stop_first()     ok")
