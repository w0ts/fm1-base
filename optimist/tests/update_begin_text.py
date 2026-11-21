#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Optimist's side of APP_UPDATE_BEGIN, the polled contract (fm1-base's tests/update_begin_test.c has the platform's):
app.h defines it as int app_update_begin(void), app.c returns opt_update_begin(), ui/opt_ui.c includes opt_update.h, whose
hook lets go of the mirror's SysEx frame, asks the transport to stop once (what stop_first did, without waiting) and
returns 1 while it is still stopping, 0 when done. Run from fm1-base's root."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
h = (ROOT / "optimist/app.h").read_text()
ac = (ROOT / "optimist/app.c").read_text()
core = (ROOT / "optimist/core/opt_core.h").read_text()
ui = (ROOT / "optimist/ui/opt_ui.c").read_text()
u = (ROOT / "optimist/ui/opt_update.h").read_text()
assert "#define APP_UPDATE_BEGIN\nint app_update_begin(void);" in h
assert "int app_update_begin(void) { return opt_update_begin(); }" in ac
assert "int opt_update_begin(void);" in core
assert ui.index('#include "opt_in.h"') < ui.index('#include "opt_update.h"')
assert ui.index('#include "opt_editor.h"') < ui.index('#include "opt_update.h"')   # (opt_sx_letgo)
body = u[u.index("int opt_update_begin(void)\n{"):]
body = body[:body.index("\n}")]
assert "stop_first();" not in body and "fm1_delay_ms" not in body     # (never waits inside: the platform polls)
assert "#include \"system/stop_first.h\"" not in u                   # (its blocking loop is not part of the hook)
assert body.index("opt_sx_letgo();") < body.index("transport_req = 2;")   # (a mirror frame left open ended first)
assert body.count("return 1;") == 1 and body.count("return 0;") == 2     # (1 = still stopping; 0 = nothing plays / done)
assert body.index("transport_req = 2;") < body.index("return 1;") < body.rindex("return 0;")
assert "opt_stop_asked = 0;" in body                                 # (the next update asks afresh)
print("Optimist: APP_UPDATE_BEGIN polled -> opt_sx_letgo(), transport_req 2, 1 while stopping, 0 done     ok")
