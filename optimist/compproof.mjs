// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Henri David
// The trial in fm1-base's browser emulator, headless, with fm1-base's companion page's client (web/companion/
// companion.js) as a page would use it through the emulator's port: app.wasm (tools/build.py optimist --web) under
// node. What Optimist declares (app.c: three files, 22 parameters) is listed, read, written and set; the same values
// seen through Optimist's own state (the screen's BPM). Then the host simulator's --serve with tools/fm1_companion.py
// (optimist/run.sh does not run this; by hand):
//   node optimist/compproof.mjs optimist/build/emu
import { readFileSync } from "node:fs";
import { Companion } from "../web/companion/companion.js";

const dir = process.argv[2] || "optimist/build/emu";
let fails = 0;
const check = (ok, what, got) => { console.log(`  ${ok ? "ok" : "FAIL"} ${what}${ok || got === undefined ? "" : ": " + JSON.stringify(got)}`); fails += !ok; };

let mem;
const wasi = new Proxy({
  clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; },
}, { get: (t, k) => (k in t ? t[k] : () => 0) });
const { instance } = await WebAssembly.instantiate(readFileSync(`${dir}/app.wasm`),
  { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports;
mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_boot();
const run = (ms) => { for (let i = 0; i < Math.ceil(ms * 44.1 / 128); i++) ex.web_render(128); };
const listeners = new Set();
function drain() {
  const all = new Uint8Array(mem.buffer, ex.web_sx_out_buf(), ex.web_sx_out_len()).slice();
  ex.web_sx_out_clear();
  for (let at = 0; at < all.length;) {
    let k = at + 1;
    while (k < all.length && all[k] !== 0xf7 && all[k] !== 0xf0) k++;
    const end = k < all.length && all[k] === 0xf7 ? k + 1 : k;
    if (all[end - 1] === 0xf7) for (const f of listeners) f({ data: all.slice(at, end) });
    at = end;
  }
}
const input = { addEventListener: (t, f) => listeners.add(f), removeEventListener: (t, f) => listeners.delete(f) };
const output = { send: (b) => { new Uint8Array(mem.buffer, ex.web_sx_in_buf(), b.length).set(b); ex.web_sx_in(b.length); run(3); drain(); } };

run(1500);
drain();
const c = new Companion(input, output, { timeout: 300, tries: 1 });
const t0 = performance.now();
const h = await c.hello();
check(h.name === "OPTIMIST" && h.identity === "FM-1_7900" && h.extras.editor === "optimist", "the capability query", h);
const fs = await c.files();
check(fs.map((f) => `${f.name}:${f.size}`).join() === "notes.txt:0,settings.bin:4096,kits.bin:4096", "Optimist's files", fs);
const text = new TextEncoder().encode("Optimist on fm1-base: notes kept by plat_file\n".repeat(40));
await c.write(0, text);
const back = await c.read(0);
check(back.length === text.length && back.every((v, k) => v === text[k]), `notes.txt written and read back (${text.length} B)`);
const settings = await c.read(1);
check(settings.length === 4096, "settings.bin read whole, CRC-32 checked");
const ps = await c.params();
check(ps.length === 22 && ps[0].name === "BPM" && ps[4].labels.join() === "OFF,ON", "22 parameters from Optimist's tables",
  ps.slice(0, 5).map((p) => [p.id, p.name, p.min, p.max, p.labels]));
check((await c.set(0x100, 128)) === 128, "BPM set to 128");
check((await c.set(0x100, 999)) === 240, "BPM 999: clamped by Optimist's range");
await c.set(0x100, 128);
check((await c.get([0x100, 0x200])).get(0x100) === 128, "BPM read back");
const told = [];
c.onchange = (id, v) => told.push([id, v]);
await c.watch(true);
// a change on the FM-1: KNOB 1 on the mixer (the home screen) turns track 1's level; nothing else meanwhile
run(200);
drain();
check(told.length === 0, "watched: nothing comes while nothing changes", told);
ex.web_enc(3, -6);
run(200);
drain();
check(told.some(([id]) => id === 0x200), "KNOB 1 on the FM-1: track 1's LVL told", told);
console.log(`  (${(performance.now() - t0).toFixed(0)} ms for all of it, host CPU)`);
c.close();
console.log(fails ? `compproof: ${fails} FAILED` : "compproof: all ok");
process.exit(fails ? 1 : 0);
