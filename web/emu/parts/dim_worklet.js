// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "dim" (fm1.toml emu = ["dim"]), the worklet's side: the lights the app shows dim (plat_lights' dim
// buttons and keys, plat_lights_mid's half-lit keys: web_dim_buttons, web_dim_keys) to the page, about 30 times a
// second when they change. The page's side is dim.js.

let last = -1, sent = "";
fm1Part({
  name: "dim",
  process(fm1) {
    if (currentTime - last < 1 / 30) return;
    last = currentTime;
    const buttons = fm1.ex.web_dim_buttons() >>> 0, keys = fm1.ex.web_dim_keys() >>> 0;
    if (`${buttons} ${keys}` === sent) return;
    sent = `${buttons} ${keys}`;
    fm1.part("dim", { buttons, keys });
  },
});
