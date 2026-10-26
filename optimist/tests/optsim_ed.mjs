// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Henri David
// Optimist's own simulator (build/optsim*/sim.wasm: optsim.py build) driven by the same SysEx script as the trial's host
// simulator (optimist/edproof.py: the editor's protocol, compared reply by reply), over its simulated USB-MIDI:
//   node tests/optsim_ed.mjs WASM SCRIPT [FLASH_IN]
// The script's lines, as the trial's host simulator reads them (host/plat_host.c): "wait MS", "sysex HEX..", "sysex_out"
// (print the SysEx frames the firmware sent since the last one, "  sysex< XX ..", and forget them), "flash_dump FILE"
// (its flash image), "fbdump FILE" (the screen, RGB565 high byte first); "#" comments; any other line is the trial's own (skipped here). A restart (a restore done, the mirror's test session) starts a fresh instance on the same flash,
// as the page does ("  restart").
import { readFileSync, writeFileSync } from "node:fs";

const [wasmPath, scriptPath, flashIn] = process.argv.slice(2);
if (!wasmPath || !scriptPath) {
  console.error("optsim_ed.mjs WASM SCRIPT [FLASH_IN]");
  process.exit(2);
}
const mod = new WebAssembly.Module(readFileSync(wasmPath));
let x, flashKeep = flashIn ? readFileSync(flashIn) : null;
const flash = () => new Uint8Array(x.memory.buffer, x.sim_flash(), x.sim_flash_size());

let frames = [], part = null;                       // the frames since the last sysex_out; one being received
function take() {                                    // the firmware's MIDI out: its SysEx frames
  const n = x.sim_midi_out();
  const b = new Uint8Array(x.memory.buffer, x.sim_midi_out_buf(), n);
  for (const v of b) {
    if (v === 0xF0) part = [v];
    else if (part && v >= 0xF8) continue;            // (realtime inside a SysEx: not part of it)
    else if (part) {
      part.push(v);
      if (v === 0xF7) {
        frames.push(part);
        part = null;
      } else if (v & 0x80) part = null;
    }
  }
}
function boot() {
  x = new WebAssembly.Instance(mod, {}).exports;
  if (x._initialize) x._initialize();
  if (flashKeep) flash().set(flashKeep);
  x.sim_boot();
}
function run(ms) {
  const n = Math.round(ms * 44.1);
  for (let f = 0; f < n; f += 441) {
    const st = x.sim_run(Math.min(441, n - f));
    x.sim_audio_take();
    take();
    if (st) {                                        // (a restart: a fresh instance on its flash)
      flashKeep = Uint8Array.from(flash());
      console.log("  restart");
      boot();
    }
  }
}
const hex = (a) => a.map((v) => v.toString(16).toUpperCase().padStart(2, "0")).join(" ");
boot();
for (const line of readFileSync(scriptPath, "utf8").split("\n")) {
  const w = line.trim().split(/\s+/);
  if (!w[0] || w[0].startsWith("#")) continue;
  if (w[0] === "wait") run(Number(w[1]));
  else if (w[0] === "sysex") {
    const b = w.slice(1).join("").match(/../g).map((h) => parseInt(h, 16));
    new Uint8Array(x.memory.buffer, x.sim_midi_buf(), b.length).set(b);
    x.sim_midi_in(b.length);
    run(1);
  } else if (w[0] === "sysex_out") {
    for (const f of frames) console.log(`  sysex< ${hex(f)}`);
    frames = [];
  } else if (w[0] === "fbdump") {                     // the screen's pixels, RGB565 high byte first (as the trial's)
    const px = new Uint16Array(x.memory.buffer, x.sim_pixels(), 240 * 240), o = new Uint8Array(2 * 240 * 240);
    px.forEach((c, i) => { o[2 * i] = c >> 8; o[2 * i + 1] = c & 255; });
    writeFileSync(w[1], o);
  } else if (w[0] === "flash_dump") {
    writeFileSync(w[1], flash());
    console.log(`  ok flash_dump ${w[1]}`);
  }
}
