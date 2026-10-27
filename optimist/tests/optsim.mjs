// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Henri David
// Optimist's own simulator (build/optsim/sim.wasm: optsim.py build) driven through its panel, as tests/storage.txt
// drives the trial: its flash written by Optimist's own code, for the trial to read (tests/optimist_image.txt), and
// the trial's flash read by Optimist's (the "touch": the trial's autosave resumed, the tempo moved, saved again).
//   node tests/optsim.mjs WASM write OUT.bin
//   node tests/optsim.mjs WASM touch IN.bin OUT.bin
//   node tests/optsim.mjs WASM idle IN.bin OUT.bin  (powered on with IN, hands off for 15 s: OUT)
// The checks are the trial's (it reads what this wrote); here only that the firmware ran and wrote its flash.
import { readFileSync, writeFileSync } from "node:fs";

const [wasm, mode, a, b] = process.argv.slice(2);
const x = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(wasm)), {}).exports;
if (x._initialize) x._initialize();
const names = (p) => { const u = new Uint8Array(x.memory.buffer, p); let t = ""; for (let i = 0; u[i]; i++) t += String.fromCharCode(u[i]); return t.split("\n"); };
const flash = () => new Uint8Array(x.memory.buffer, x.sim_flash(), x.sim_flash_size());
const run = (ms) => { const n = Math.round(ms * 44.1); for (let f = 0; f < n; f += 1024) { const st = x.sim_run(Math.min(1024, n - f)); x.sim_audio_take(); if (st) throw new Error(`the firmware stopped: status ${st}`); } };
const BTN = names(x.sim_button_names()), ENC = names(x.sim_enc_names());
const tap = (n) => { const i = BTN.indexOf(n); if (i < 0) throw new Error(`no button ${n} (${BTN})`); x.sim_button(i, 1); run(60); x.sim_button(i, 0); run(200); };
// a detent every 300 ms: no acceleration (ui/knob_accel.h), so N detents are N steps
const turn = (n, d) => { const i = ENC.indexOf(n); if (i < 0) throw new Error(`no knob ${n} (${ENC})`); for (let k = 0; k < Math.abs(d); k++) { x.sim_enc(i, Math.sign(d)); run(300); } run(100); };
const WHITE = [0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26];   // white key w -> key (0 = F3), as fm1-base's host
const key = (w) => { x.sim_key(WHITE[w], 1); run(50); x.sim_key(WHITE[w], 0); run(100); };
const save = (path) => { writeFileSync(path, flash()); console.log(`  ok optsim: flash ${path} (${x.sim_flash_size()} B, ${x.sim_flash_rev()} erases and writes)`); };

if (mode === "touch" || mode === "idle") {
  flash().set(readFileSync(a));                    // (the flash before the power-on: sim.c's sim_flash, as the page's)
} else if (mode !== "write") {
  console.error("optsim.mjs WASM write OUT.bin | touch IN.bin OUT.bin | idle IN.bin OUT.bin");
  process.exit(2);
}
x.sim_boot();
run(1500);
// the UI's layout differs with the build's switches (optsim.py: OPTSIM_UI): "dev" the trial's copy of Optimist's dev
// profile (PATTERNS: PROJECT's rows PROJECT, USER ...; SYSTEM's INFO then GENERAL, KNOB COLORS its first card), else
// the source defaults (PROJECT's slots: USER its third row; SYSTEM's GENERAL its 4th page here, KNOB COLORS its second
// card: a USB STATS row before it, USB audio on)
const DEV = process.env.OPTSIM_UI === "dev";
if (mode === "write") {
  tap("ENV"); turn("KNOB 2", 4);                       // a sound: track 1's DEC 112 -> 116
  tap("GLO"); turn("KNOB 1", 5);                       // the tempo: 111 -> 116
  tap("GLO");                                          // SYSTEM, GENERAL's KNOB COLORS on
  if (DEV) { turn("SELECT", 1); turn("KNOB 1", 1); } else { turn("SELECT", 3); turn("KNOB 2", 1); }
  if (DEV) { turn("SELECT", 1); turn("KNOB 1", 3); }   // and USB AUDIO's MASTER NONE -> 1+2 (the routing, "UAR3")
  tap("HOME");
  tap("SAVE"); turn("KNOB 2", 1); key(2); tap("SAVE"); tap("SAVE");   // PROJECT: SAVE to slot 3, YES, the name's SAVE
  turn("SELECT", DEV ? 1 : 2); turn("KNOB 2", 1); key(0); tap("SAVE"); tap("SAVE");   // USER: SAVE to U01, YES, the name's SAVE
  tap("HOME");
  run(12000);                                      // hands off: the autosave
  save(a);
} else if (mode === "idle") {
  run(15000);                                      // hands off past the autosave's 10 s: what a power-on writes by itself
  save(b);
} else {
  tap("GLO"); turn("KNOB 1", 4);                       // the tempo 4 up from what the trial's flash brought
  tap("HOME");
  run(12000);                                      // the autosave (its other copy)
  save(b);
}
