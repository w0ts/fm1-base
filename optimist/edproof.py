#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The editor's protocol in the trial against Optimist's own firmware, byte for byte (optimist/run.sh, the dev profile):
the same SysEx requests, as web/editor.html and the iPhone app send them, to the trial's host simulator
(build/host/optimist_host: Optimist's editor on fm1-base's SysEx, ui/opt_editor.h) and to Optimist's own simulator built
with the same switches (optsim.py build-trial: its usb.c, its main loop), every reply compared.

  edproof.py FLASH.bin OUTDIR     FLASH: the image both power on with (optsim.py write's: a project, a preset ...)

  1 proto     INFO, BUILD, PING; GET and SET (not in this build: no reply on both); the librarian's lists and reads
              (user presets, user kits, patterns, a project slot); the commands the trial leaves to fm1-base's companion
              services (11..15, 42 the sample slots, 43..48 the backup: ui/opt_editor.h FELUCCA_ED_COMPANION) unanswered
              there, and its capability query (0x60) answered
  2 backup    Optimist's: BK_LIST (every page), then BK_READ of every object that holds data, chunk by chunk (web/
              editor.html bkRead); the trial's: the companion's files (0x61: the list, each one read, its CRC-32 asked),
              each object's name, size, kind and flags (BKINFO) as BK_LIST's, its bytes the same byte for byte, BKINFO's
              caps BK_LIST's
  3 restore   on a blank flash, the backup put back as the editor does (bkCheckAll: every storage object checked first,
              BK_COMMIT i 1 / the trial's BKCTL 2, then each written, BK_END 1 / BKCTL 1): the restart, then each side's
              list again (the sizes and CRCs the backup's), and the two flashes compared (optsim.py same)
  6 session   (the trial alone) the mirror's test session: a restart as a blank FM-1, the flash untouched, and back
  7 slots     (the trial's companion files) USR1 and USR2's head where the section log is: refused, the flash
              untouched; a sample slot written whole (its header last), read back, deleted
  5 dx7       (the trial alone) a DX7 bank to FM6 over APP_SYSEX_BYTE, one with a bad checksum
  4 mirror    106 on (with STATE), a press over 108 PANEL, 109 NAV, MASTER from the client, 106 off: the replies compared;
              the pushed frames (the panel's pixels: each side's own drawing) written to OUTDIR/{trial,optimist}-mirror.syx
              for tools/fm1_mirror.py decode (run.sh: each decoded screen against that side's own framebuffer)
Writes OUTDIR/edproof-*.txt (each phase's script and both sides' output). Exit 1 on any difference."""
import os
import subprocess
import sys
import zlib
from pathlib import Path

APP = Path(__file__).resolve().parent
HOST = APP / "build" / "host" / "optimist_host"
HDR = [0x7D, 0x46, 0x4C]


def fr(cmd, *args):
    return [0xF0, *HDR, cmd, *args, 0xF7]


def b7(v, k):
    return [(v >> (7 * i)) & 0x7F for i in range(k)]


def r7(a, k):
    return sum(a[i] << (7 * i) for i in range(k))


def pack7(data):
    out = []
    for i in range(0, len(data), 7):
        g = data[i:i + 7]
        out.append(sum(((b >> 7) & 1) << j for j, b in enumerate(g)))
        out += [b & 0x7F for b in g]
    return out


def unpack7(a, n_max=1 << 30):
    out, i = [], 0
    while i < len(a) and len(out) < n_max:
        m = a[i]
        i += 1
        for j in range(7):
            if i >= len(a) or len(out) >= n_max:
                break
            out.append(a[i] | ((m >> j) & 1) << 7)
            i += 1
    return out


def cpack7(data):                               # fm1-base's pack7 (docs/COMPANION.md): 8-bit bytes as 7-bit, LSB first
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return out


def cunpack7(a):
    out, acc, nb = [], 0, 0
    for b in a:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return out


def cfr(cmd, body):                              # a companion request: F0 pack7(00 59 cmd len24 body ~sum8) F7
    n = len(body)
    return [0xF0, *cpack7([0, 0x59, cmd, n & 255, n >> 8 & 255, n >> 16 & 255, *body, ~sum(body) & 255]), 0xF7]


def cdec(f):                                     # a companion answer -> (cmd, body), or None (another frame)
    if f == "restart" or len(f) < 3 or f[1:4] == HDR:
        return None
    u = cunpack7(f[1:-1])
    if len(u) < 7 or u[0] != 0 or u[1] != 0x59:
        return None
    n = u[3] | u[4] << 8 | u[5] << 16
    return (u[2], u[6:6 + n]) if len(u) >= n + 7 else None


def le(v, k):
    return [(v >> (8 * i)) & 255 for i in range(k)]


def rle(a, o, k):
    return sum(a[o + i] << (8 * i) for i in range(k))


def cfiles(frames):
    """the trial's companion answers (0x61) in order: (op, status, index, rest)"""
    out = []
    for f in frames:
        d = cdec(f)
        if d and d[0] == 0x61 and len(d[1]) >= 4:
            b = d[1]
            out.append((b[0], b[1], b[2] | b[3] << 8, b[4:]))
    return out


def cf_list(frames):
    """the list's answers: [(index, name, kind, flags, size, room)], the count"""
    files, n = {}, None
    for op, st, i, b in cfiles(frames):
        if op != 0 or st != 0:
            continue
        n = b[0] | b[1] << 8
        name = bytes(b[12:b.index(0, 12)]).decode()
        files[i] = (i, name, b[2], b[3], rle(b, 4, 4), rle(b, 8, 4))
    if n is None or sorted(files) != list(range(n)):
        raise SystemExit(f"FAIL edproof: the companion's list: {len(files)} of {n} files")
    return [files[i] for i in range(n)], n


def cf_list_script(s, n_guess=80):
    for i in range(n_guess):
        s.ask(cfr(0x61, [0, *le(i, 2)]))
    return s


def cf_write(s, i, d):
    """file i written: begin (size, CRC-32), the data 512 bytes a frame, commit"""
    s.ask(cfr(0x61, [3, *le(i, 2), *le(len(d), 4), *le(zlib.crc32(bytes(d)), 4)]), wait=60)
    for off in range(0, len(d), 512):
        s.ask(cfr(0x61, [4, *le(i, 2), *le(off, 4), *d[off:off + 512]]), wait=40)
    s.ask(cfr(0x61, [5, *le(i, 2)]), wait=400)


def run2(out, name, ts, os_, flash):
    """the trial with its own script (the companion's), Optimist's simulator with its own (its editor's)"""
    p, q = out / f"edproof-{name}.trial.script.txt", out / f"edproof-{name}.optimist.script.txt"
    p.write_text(ts.text().replace("@SIDE@", "trial").replace("@OUT@", ""))
    q.write_text(os_.text().replace("@SIDE@", "optimist").replace("@OUT@", f"{out}/"))
    env = {**os.environ, **({"OPT_FLASH": str(flash)} if flash else {})}
    t = subprocess.run([str(HOST), str(p), str(out)], capture_output=True, text=True, env=env)
    o = subprocess.run([sys.executable, str(APP / "optsim.py"), "ed", str(q), *([str(flash)] if flash else [])],
                       capture_output=True, text=True)
    (out / f"edproof-{name}.trial.txt").write_text(t.stdout + t.stderr)
    (out / f"edproof-{name}.optimist.txt").write_text(o.stdout + o.stderr)
    if t.returncode or o.returncode:
        raise SystemExit(f"FAIL edproof {name}: a simulator failed (see {out}/edproof-{name}.*.txt)")
    return blocks(t.stdout), blocks(o.stdout)


def hx(f):
    return " ".join(f"{b:02X}" for b in f)


class Script:
    def __init__(self):
        self.lines = ["wait 1500", "sysex_out"]

    def ask(self, f, wait=20):
        self.lines += [f"sysex {hx(f)}", f"wait {wait}", "sysex_out"]

    def raw(self, line):
        self.lines.append(line)

    def text(self):
        return "\n".join(self.lines) + "\n"


def blocks(out):
    """the output's frames in order, and its restarts ("restart")"""
    res = []
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("sysex< ("):
            raise SystemExit(f"FAIL edproof: the host kept no more of what was sent ({line})")
        if line.startswith("sysex<"):
            res.append([int(h, 16) for h in line[6:].split()])
        elif line == "restart" or line.startswith("restart (plat_restart)"):
            res.append("restart")
    return res


def is_push(f):                                 # a mirror push (106, not the reply to a request: on, version 2, w, h)
    return f != "restart" and len(f) > 5 and f[1:4] == HDR and f[4] == 106 and not (len(f) == 12 and f[6] == 2)


def run(out, name, script, flash):
    p, q = out / f"edproof-{name}.trial.script.txt", out / f"edproof-{name}.optimist.script.txt"
    p.write_text(script.text().replace("@SIDE@", "trial").replace("@OUT@", ""))   # (the dumps: one file a side)
    q.write_text(script.text().replace("@SIDE@", "optimist").replace("@OUT@", f"{out}/"))
    env = {**os.environ}
    if flash:
        env["OPT_FLASH"] = str(flash)
    t = subprocess.run([str(HOST), str(p), str(out)], capture_output=True, text=True, env=env)
    o = subprocess.run([sys.executable, str(APP / "optsim.py"), "ed", str(q), *([str(flash)] if flash else [])],
                       capture_output=True, text=True)
    (out / f"edproof-{name}.trial.txt").write_text(t.stdout + t.stderr)
    (out / f"edproof-{name}.optimist.txt").write_text(o.stdout + o.stderr)
    if t.returncode or o.returncode:
        raise SystemExit(f"FAIL edproof {name}: a simulator failed (see {out}/edproof-{name}.*.txt)")
    return blocks(t.stdout), blocks(o.stdout)


def compare(name, tf, of, pushes_apart=False):
    if pushes_apart:
        tf, of = [f for f in tf if not is_push(f)], [f for f in of if not is_push(f)]
    same = tf == of
    n = sum(1 for f in tf if f != "restart")
    nb = sum(len(f) for f in tf if f != "restart")
    if same:
        print(f"  ok   edproof {name}: {n} replies, {nb} bytes, the same byte for byte")
        return 0
    for i, (a, b) in enumerate(zip(tf, of)):
        if a != b:
            print(f"FAIL edproof {name}: reply {i} differs\n   trial    {hx(a) if a != 'restart' else a}\n"
                  f"   optimist {hx(b) if b != 'restart' else b}")
            break
    else:
        print(f"FAIL edproof {name}: {len(tf)} replies in the trial, {len(of)} in Optimist's")
    return 1


def norm_info(f):
    """the trial's INFO with its tag 0x66 naming opti_cpfiles_1 (the backup as the companion's files: ed_opti.h) as Optimist's
    names opti_backup_4 there, for the comparison: everything else byte for byte"""
    new, old = list(b"opti_cpfiles_1 "), list(b"opti_backup_4 ")
    if f == "restart" or f[4] != 1:
        return f
    for k in range(5, len(f) - 2):
        if f[k] == 0x66 and f[k + 2:k + 2 + len(new)] == new:
            return f[:k + 1] + [f[k + 1] - 1] + old + f[k + 2 + len(new):]
    return f


def proto(out, flash):
    s = Script()
    s.ask(fr(1))                                # INFO
    s.ask(fr(49))                               # BUILD
    s.ask(fr(25))                               # PING
    s.ask(fr(2, 1, *b7(8, 2)))                  # GET global 8 (ED_LIVE off: no reply)
    s.ask(fr(3, 0, *b7(3, 2), *b7(8192 + 5, 2)))   # SET (no reply)
    s.ask(fr(16, 0, 16))                        # UP_LIST 0..15
    s.ask(fr(16, 16, 16))
    s.ask(fr(17, 0))                            # UP_GET U01
    s.ask(fr(17, 5))
    s.ask(fr(38))                               # UKIT_LIST
    s.ask(fr(39, 0))                            # UKIT_GET 0
    s.ask(fr(79, 0))                            # PAT_LIST
    s.ask(fr(9, 2, 0))                          # PROJECT query A
    s.ask(fr(9, 2, 2))
    s.ask(fr(77))                               # an unknown command: no reply
    t, o = run(out, "proto", s, flash)
    t = [norm_info(f) for f in t]
    bad = compare("proto (INFO, BUILD, PING, GET, SET, the librarian; INFO's opti_cpfiles_1 read as opti_backup_4)", t, o)
    s = Script()                                # (the trial's: what the companion's files took over, then 0x60)
    for f in (fr(15), fr(42, 1, *b7(0, 3), *b7(64, 2)), fr(43, 4, 0), fr(44, 0, *b7(0, 3)), fr(48, 0)):
        s.ask(f)
    s.ask(cfr(0x60, []))
    p = out / "edproof-proto2.trial.script.txt"
    p.write_text(s.text())
    t = subprocess.run([str(HOST), str(p), str(out)], capture_output=True, text=True, env={**os.environ, "OPT_FLASH": str(flash)})
    (out / "edproof-proto2.trial.txt").write_text(t.stdout + t.stderr)
    fs = blocks(t.stdout)
    ours = [f for f in fs if f != "restart" and f[1:4] == HDR]
    hello = [d for d in map(cdec, fs) if d and d[0] == 0x60]
    ok = t.returncode == 0 and not ours and len(hello) == 1 and rle(hello[0][1], 1, 4) & 2
    print(f"  {'ok  ' if ok else 'FAIL'} edproof proto: the trial leaves 15, 42, 43, 44, 48 unanswered ({len(ours)} "
          f"replies) and answers the capability query (files {'on' if hello and rle(hello[0][1], 1, 4) & 2 else 'OFF'})")
    return bad + (not ok)


def bk_list(frames):
    """the objects of BK_LIST's pages: (tag, kind, flags, len, crc)"""
    objs, n = [], None
    for f in frames:
        if f == "restart" or f[4] != 43:
            continue
        a = f[5:-1]
        n = a[1]
        first, count, i = a[10], a[11], 12
        for k in range(count):
            tag = bytes(a[i:i + 4]).decode()
            kind, flags, ln, crc = a[i + 4], a[i + 5], r7(a[i + 6:], 3), r7(a[i + 9:], 5)
            objs.append((first + k, tag, kind, flags, ln, crc))
            i += 14
    objs = sorted({o[0]: o for o in objs}.values())   # (a page asked past the end: the last one again)
    if n is not None and [o[0] for o in objs] != list(range(n)):
        raise SystemExit(f"FAIL edproof: BK_LIST: {len(objs)} of {n} objects listed")
    return objs, n


def list_script(s, n_guess=80):
    first = 0
    while first < n_guess:
        s.ask(fr(43, 4, first))
        first += 8
    return s


def bkinfo(b):
    """BKINFO's bytes (Optimist's io/editor/ed_comp.c): BK_LIST's head, each object's kind and flags, bk_caps"""
    n = b[1]
    return b[:10], [(b[12 + 2 * k], b[13 + 2 * k]) for k in range(n)], b[12 + 2 * n:]


def bk_caps_of(frames):
    """BK_LIST's head and the caps its last page ends with"""
    last = [f for f in frames if f != "restart" and f[4] == 43][-1][5:-1]
    return last[:10], last[12 + 14 * last[11]:]


def backup(out, flash):
    os_ = list_script(Script())
    ts = cf_list_script(Script())
    t, o = run2(out, "list", ts, os_, flash)
    olist = o
    objs, n = bk_list(o)
    files, nf = cf_list(t)
    bad = 0
    want = [(x[1].rstrip(), x[4]) for x in objs] + [("BKINFO", None), ("BKCTL", 0)]
    got = [(f[1], f[4] if f[1] != "BKINFO" else None) for f in files]
    if got != want:
        print(f"FAIL edproof backup: the companion's files are not BK_LIST's objects: "
              f"{[g for g, w in zip(got, want) if g != w][:4]} ({nf} files, {n} objects)")
        bad += 1
    else:
        print(f"  ok   edproof backup: the companion lists BK_LIST's {n} objects, each by its tag with its size, "
              f"then BKINFO and BKCTL")
    data = {}
    os2, ts2 = Script(), Script()
    for i, tag, kind, flags, ln, crc in objs:
        if flags & 2 and kind != 2:
            for off in range(0, ln, 256):
                os2.ask(fr(44, i, *b7(off, 3)), wait=30)
                ts2.ask(cfr(0x61, [1, *le(i, 2), *le(off, 4), *le(256, 2)]), wait=30)
            ts2.ask(cfr(0x61, [2, *le(i, 2)]), wait=60)
    info = [f for f in files if f[1] == "BKINFO"][0]
    ts2.ask(cfr(0x61, [1, *le(info[0], 2), *le(0, 4), *le(256, 2)]))
    t, o = run2(out, "read", ts2, os2, flash)
    for f in o:
        if f == "restart" or f[4] != 44:
            continue
        a = f[5:-1]
        i, off, crc = a[0], r7(a[1:], 3), r7(a[4:], 5)
        chunk = unpack7(a[9:])
        if zlib.crc32(bytes(chunk)) != crc:
            print(f"FAIL edproof backup: object {i} at {off}: the chunk's CRC")
            bad += 1
        data.setdefault(i, bytearray()).extend(chunk)
    tdata, sums, binfo = {}, {}, None
    for op, st, i, b in cfiles(t):
        if op == 1 and st == 0 and i == info[0]:
            binfo = b[4:]
        elif op == 1 and st == 0:
            tdata.setdefault(i, bytearray()).extend(b[4:])
        elif op == 2 and st == 0:
            sums[i] = (rle(b, 0, 4), rle(b, 4, 4))
        elif st != 0:
            print(f"FAIL edproof backup: the companion's op {op} on file {i}: status {st}")
            bad += 1
    for i, tag, kind, flags, ln, crc in objs:
        if flags & 2 and kind != 2 and (i not in data or len(data[i]) != ln or zlib.crc32(bytes(data[i])) != crc):
            print(f"FAIL edproof backup: {tag}: {len(data.get(i, b''))} of {ln} bytes, CRC {'ok' if i in data and zlib.crc32(bytes(data[i])) == crc else 'bad'}")
            bad += 1
        if flags & 2 and kind != 2 and (bytes(tdata.get(i, b"")) != bytes(data.get(i, b"")) or sums.get(i) != (ln, crc)):
            print(f"FAIL edproof backup: {tag}: the companion's file is not BK_READ's bytes ({len(tdata.get(i, b''))} "
                  f"bytes, its sum {sums.get(i)}, BK_LIST's {(ln, crc)})")
            bad += 1
    print(f"  {'ok  ' if not bad else 'FAIL'} edproof backup: {len(data)} objects read whole, "
          f"{sum(len(d) for d in data.values())} bytes, the companion's files the same byte for byte, their CRC-32 "
          f"BK_LIST's: {' '.join(x[1] for x in objs if x[0] in data)}")
    head, kf, caps = bkinfo(binfo or [0, 0])
    ohead, ocaps = bk_caps_of(olist)
    kf_want = [(x[2], x[3] & ~2) for x in objs]
    ok = head == ohead and caps == ocaps and kf == kf_want
    print(f"  {'ok  ' if ok else 'FAIL'} edproof backup: BKINFO ({len(binfo or [])} bytes): BK_LIST's head, kinds, flags "
          f"and caps")
    return bad + (not ok), objs, data


def restore(out, objs, data, blank):
    s = Script()                                 # (Optimist's: its editor's commands)
    s.ask(fr(43, 4, 0))
    order = [x for x in objs if x[0] in data and x[2] != 1]   # (the sample slots: 11..13, not a backup object write)
    def send(i, d, check):
        crc = zlib.crc32(bytes(d))
        s.ask(fr(45, i, *b7(len(d), 3), *b7(crc, 5)), wait=40)
        for off in range(0, len(d), 256):
            c = d[off:off + 256]
            s.ask(fr(46, i, *b7(off, 3), *b7(zlib.crc32(bytes(c)), 5), *pack7(c)), wait=30)
        s.ask(fr(47, i, *([1] if check else [])), wait=400)
    ctl = len(objs) + 1                          # (the trial's: the companion's files, BKCTL after BKINFO)
    ts = Script()
    def tctl(v):
        cf_write(ts, ctl, [v])
    tctl(2)                                      # (BKCTL 2: the commits only check)
    for i, tag, kind, flags, ln, crc in order:
        if kind == 0:
            send(i, data[i], True)              # (bkCheckAll: every storage object checked first)
            cf_write(ts, i, data[i])
    tctl(3)
    for i, tag, kind, flags, ln, crc in order:
        send(i, data[i], False)
        cf_write(ts, i, data[i])
    s.ask(fr(48, 1), wait=130)                  # done: the restart 150 ms later
    tctl(1)
    ts.lines[-1] = "wait 130"                   # (BKCTL 1's commit: its answer, then the restart 150 ms later)
    ts.raw("sysex_out")
    for x in (s, ts):
        x.raw(f"flash_dump {out / 'edproof-end-@SIDE@.bin'}")   # (the flash as the restart finds it)
        x.raw("wait 300")
        x.raw("sysex_out")
    t, o = run2(out, "restore", ts, s, blank)
    st = [(op, sv) for op, sv, i, b in cfiles(t) if op in (3, 4, 5)]
    tbad = [x for x in st if x[1]]
    obad = [f for f in o if f != "restart" and f[4] in (45, 46, 47) and f[-2]]
    print(f"  {'ok  ' if not tbad and not obad else 'FAIL'} edproof restore: {len(order)} objects "
          f"({sum(1 for x in order if x[2] == 0)} checked first): Optimist's BEGIN / DATA / COMMIT {len(obad)} refused, "
          f"the trial's companion begin / data / commit {len(tbad)} refused of {len(st)}, both restarted: "
          f"{'restart' in t and 'restart' in o}")
    bad = bool(tbad or obad) + ("restart" not in t or "restart" not in o)
    # the start after it: a power-on with the flash the restart left (the trial's host keeps the app's RAM across its
    # restart, which an FM-1 clears: a new process instead, as a power cycle; Optimist's simulator starts afresh too)
    for side in ("trial", "optimist"):          # (each side on its own flash)
        (out / "edproof-end.bin").write_bytes((out / f"edproof-end-{side}.bin").read_bytes())
        s2 = cf_list_script(Script()) if side == "trial" else list_script(Script())
        s2.raw("wait 2000")
        s2.raw(f"flash_dump {out / 'edproof-restored-@SIDE@.bin'}")
        ts_, os_ = run2(out, f"restart-{side}", s2, s2, out / "edproof-end.bin")
        if side == "trial":
            t2 = ts_
        else:
            o2 = os_
    objs2, _ = bk_list([f for f in o2 if f != "restart"])
    files2, _ = cf_list(t2)
    want = {x[1].rstrip(): x[4] for x in objs if x[0] in data and x[2] != 1}
    got_o = {x[1].rstrip(): x[4] for x in objs2}
    got_t = {f[1]: f[4] for f in files2}
    miss = [tag for tag in want if got_o.get(tag) != want[tag] or got_t.get(tag) != want[tag]]
    crc_o = {x[1].rstrip(): x[5] for x in objs2}
    crc_w = {x[1].rstrip(): x[5] for x in objs if x[0] in data and x[2] != 1}
    miss += [tag for tag in crc_w if crc_o.get(tag) != crc_w[tag]]
    print(f"  {'ok  ' if not miss else 'FAIL'} edproof restore: after the restart both list every object restored "
          f"with the backup's length (Optimist's BK_LIST its CRC too){': not ' + ' '.join(miss) if miss else ''}")
    r = subprocess.run([sys.executable, str(APP / "optsim.py"), "same", str(out / "edproof-restored-optimist.bin"),
                        str(out / "edproof-restored-trial.bin")], capture_output=True, text=True)
    same = (out / "edproof-restored-optimist.bin").read_bytes() == (out / "edproof-restored-trial.bin").read_bytes()
    print(f"  {'ok  ' if not r.returncode and same else 'FAIL'} edproof restore: the flashes after the restore and the "
          f"start: {'the same 1 MiB byte for byte' if same else 'they differ'} (optsim.py same: {r.stdout.strip().splitlines()[-1]})")
    return bad + bool(miss) + (r.returncode != 0) + (not same)


def mirror(out, flash):
    s = Script()
    s.ask(fr(106, 1 | 4), wait=300)             # on, STATE too
    s.ask(fr(25), wait=300)                     # (keep-alive)
    s.ask(fr(108, 0, 5, 1, 1), wait=60)         # GLO down (seq 1)
    s.ask(fr(108, 0, 5, 1, 1), wait=60)         # the same seq again: answered again, not done twice
    s.ask(fr(108, 0, 5, 0, 2), wait=300)        # GLO up
    s.ask(fr(109, 0), wait=100)                 # NAV state
    s.ask(fr(108, 3, 0, 4, 3), wait=100)        # MASTER 512 from the client
    s.ask(fr(108, 4, 0, 0, 4), wait=100)        # let go
    s.ask(fr(108, 2, 3, 65, 5), wait=300)       # KNOB 1 one detent clockwise
    s.raw("fbdump @OUT@edproof-screen-@SIDE@.raw")   # (the trial's host: in its OUTDIR)   # (the screen as the mirror last drew it)
    s.ask(fr(106, 0), wait=100)                 # off
    s.raw("wait 200")
    s.raw("sysex_out")
    t, o = run(out, "mirror", s, flash)
    bad = compare("mirror: 106, 108 PANEL, 109 NAV replies", t, o, pushes_apart=True)
    for side, frames in (("trial", t), ("optimist", o)):
        p = [f for f in frames if is_push(f)]
        syx, png = out / f"{side}-mirror.syx", out / f"{side}-mirror.png"
        syx.write_bytes(bytes(b for f in p for b in f))
        r = subprocess.run([sys.executable, str(APP / "build" / "optimist" / "tools" / "fm1_mirror.py"), "decode",
                            str(syx), str(png)], capture_output=True, text=True)
        sys.path.insert(0, str(APP / "build" / "optimist" / "tools"))
        import fm1_mirror                       # (Optimist's decoder, as the iPhone app's and the editor's)
        dec = fm1_mirror.Decoder()
        undecoded = sum(not dec.frame(f) for f in p)
        raw = (out / f"edproof-screen-{side}.raw").read_bytes()
        scr = [raw[2 * i] << 8 | raw[2 * i + 1] for i in range(240 * 240)]
        diff = sum(1 for a, b in zip(dec.fb, scr) if a != b)
        ok = r.returncode == 0 and not undecoded and not diff
        print(f"  {'ok  ' if ok else 'FAIL'} edproof mirror: {side}: {len(p)} frames pushed, {sum(len(f) for f in p)} "
              f"bytes; tools/fm1_mirror.py decode: {png.name}, {undecoded} frames not decoded, the decoded screen "
              f"{'is' if not diff else 'is NOT'} the {side}'s own ({diff} pixels differ)")
        bad += not ok
    return bad


def dx7(out, flash):
    """DX7 SysEx to FM6 (Optimist's fm6_sx_byte on fm1-base's APP_SYSEX_BYTE): a bank of 32 voices (4104 bytes, past
    the frame buffer) taken whole, its checksum checked; one with a bad checksum refused (the trial's header message)"""
    data = [(i * 7 + 3) % 100 for i in range(4096)]
    good = [0xF0, 0x43, 0x00, 0x09, 0x20, 0x00, *data, -sum(data) & 0x7F, 0xF7]
    bad_ = good[:-2] + [(good[-2] + 1) & 0x7F, 0xF7]
    s = Script()
    s.raw(f"sysex {hx(good)}")
    s.raw("wait 100")
    s.raw("msg FM6: 32 VOICES IN PRESETS")
    s.raw("wait 2000")
    s.raw(f"sysex {hx(bad_)}")
    s.raw("wait 100")
    s.raw("msg FM6: BAD CHECKSUM")
    p = out / "edproof-dx7.trial.script.txt"
    p.write_text(s.text())
    t = subprocess.run([str(HOST), str(p), str(out)], capture_output=True, text=True,
                       env={**os.environ, "OPT_FLASH": str(flash)})
    (out / "edproof-dx7.trial.txt").write_text(t.stdout + t.stderr)
    ok = t.returncode == 0 and t.stdout.count("  ok msg") == 2
    print(f"  {'ok  ' if ok else 'FAIL'} edproof dx7: a bank of 32 voices (4104 bytes) taken, a bad checksum refused "
          f"(the trial's messages; Optimist's simulator shows them on its screen only)")
    return not ok


def session(out, flash):
    """(the trial alone: Optimist's simulator starts a fresh instance at a restart, its .noinit lost) the mirror's test
    session: 108 kind 5 restarts as a blank FM-1 (STATE's last byte bit 1, BK_LIST without data), the flash never
    written; kind 5 0 back to the user's own"""
    s = Script()
    s.ask(fr(106, 1 | 4), wait=100)
    s.ask(fr(108, 5, 1, 0, 1), wait=1500)       # the test session: its reply, the restart, the start
    s.ask(fr(106, 1 | 4), wait=600)
    cf_list_script(s)
    s.raw(f"flash_dump {out / 'edproof-session.bin'}")
    s.ask(fr(108, 5, 0, 0, 2), wait=1500)       # the end: the user's own again
    cf_list_script(s)
    p = out / "edproof-session.trial.script.txt"
    p.write_text(s.text())
    t = subprocess.run([str(HOST), str(p), str(out)], capture_output=True, text=True, env={**os.environ, "OPT_FLASH": str(flash)})
    (out / "edproof-session.trial.txt").write_text(t.stdout + t.stderr)
    fr_ = blocks(t.stdout)
    states = [f for f in fr_ if f != "restart" and is_push(f) and f[5] == 4]
    second = [i for i, f in enumerate(fr_) if f == "restart"][-1:] or [len(fr_)]
    lists = [fr_[:second[0]], fr_[second[0]:]]
    in_blank = [f for f in states if f[-2] & 2]
    had = lambda fs: sum(1 for f in cf_list(fs)[0] if f[4] and f[1] != "BKINFO")
    untouched = (out / "edproof-session.bin").read_bytes() == flash.read_bytes()
    ok = (t.returncode == 0 and fr_.count("restart") == 2 and in_blank and len(lists) == 2 and had(lists[0]) == 0
          and had(lists[1]) > 0 and untouched)
    print(f"  {'ok  ' if ok else 'FAIL'} edproof session: the test session restarted blank (STATE bit 1, the files: "
          f"{had(lists[0]) if lists else '?'} objects with data), the flash {'untouched' if untouched else 'WRITTEN'}, "
          f"back to the user's own ({had(lists[1]) if len(lists) > 1 else '?'} objects); {fr_.count('restart')} restarts")
    return not ok


def slot_file(name, data):
    """a sample slot (Optimist's header, 480 bytes, then IMA ADPCM from 512): one zone over data, as the editor builds it"""
    import struct
    h = bytearray(512)
    struct.pack_into("<4sHBB8sII", h, 0, b"FSMP", 1, 1, 0, name.encode().ljust(8, b"\0"), len(data), zlib.crc32(bytes(data)))
    struct.pack_into("<IIIIIhhBBBB", h, 32, 0, 2 * len(data), 0, 2 * len(data) - 1, 32768, 60 * 16, 0, 0, 0, 127, 0)
    return bytes(h) + bytes(data)


def slots(out, flash):
    """(the trial's companion files) USR1 (room 0: the section log's first sector) and USR2 (its header sector 0xB4000
    in the log, with PATTERNS) refused: begin and delete, the flash untouched; then a slot whose room is its own (USR3)
    written whole, read back the same, deleted"""
    s = cf_list_script(Script())
    t, _ = run2(out, "slots-list", s, Script(), flash)
    files = {f[1]: f for f in cf_list(t)[0]}
    s = Script()
    smp = slot_file("EDPROOF", [(k * 37 + 11) & 255 for k in range(6000)])
    for name in ("USR1", "USR2"):
        s.ask(cfr(0x61, [3, *le(files[name][0], 2), *le(len(smp), 4), *le(zlib.crc32(smp), 4)]), wait=200)
        s.ask(cfr(0x61, [6, *le(files[name][0], 2)]), wait=200)
    s.raw(f"flash_dump {out / 'edproof-slots-trial.bin'}")
    k = files["USR3"][0]
    ok3 = files["USR3"][5] >= len(smp)
    if ok3:
        cf_write(s, k, list(smp))
        cf_write(s, len(files) - 1, [0])        # (BKCTL 0: the session ends)
        s.ask(cfr(0x61, [0, *le(k, 2)]))
        for off in range(0, len(smp), 256):
            s.ask(cfr(0x61, [1, *le(k, 2), *le(off, 4), *le(256, 2)]), wait=30)
        s.ask(cfr(0x61, [6, *le(k, 2)]), wait=2000)
        s.ask(cfr(0x61, [0, *le(k, 2)]))
    t, _ = run2(out, "slots", s, Script(), flash)
    a = flash.read_bytes()
    b = (out / "edproof-slots-trial.bin").read_bytes()
    hit = [hex(x) for x in range(0, len(a), 0x1000) if a[x:x + 0x1000] != b[x:x + 0x1000]]
    ans = cfiles(t)
    refused = [st for op, st, i, rest in ans[:4]]
    back = bytes(x for op, st, i, rest in ans if op == 1 and i == k and st == 0 for x in rest[4:])
    sizes = [rle(rest, 4, 4) for op, st, i, rest in ans if op == 0 and i == k]
    wst = [st for op, st, i, rest in ans[4:] if op in (3, 4, 5)]
    ok = not hit and len(refused) == 4 and all(refused) and ok3 and back == smp and sizes == [len(smp), 0] and not any(wst)
    print(f"  {'ok  ' if ok else 'FAIL'} edproof slots: USR1, USR2 begin and delete refused by the trial (status "
          f"{refused}), its flash untouched{' (' + ' '.join(hit[:4]) + ')' if hit else ''}; USR3 written ({len(smp)} "
          f"bytes, header last), read back {'the same' if back == smp else 'DIFFERENT'}, deleted (sizes {sizes})")
    return not ok


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    flash, out = Path(argv[0]).resolve(), Path(argv[1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    blank = out / "edproof-blank.bin"
    blank.write_bytes(b"\xff" * 0x100000)
    bad = proto(out, flash)
    b, objs, data = backup(out, flash)
    bad += b
    bad += restore(out, objs, data, blank)
    bad += mirror(out, flash)
    bad += dx7(out, flash)
    bad += session(out, flash)
    bad += slots(out, flash)
    print(f"{'FAIL' if bad else '  ok'} edproof: {'differences' if bad else 'the trial answers as Optimist does'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
