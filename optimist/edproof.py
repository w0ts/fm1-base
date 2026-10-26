#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The editor's protocol in the trial against Optimist's own firmware, byte for byte (optimist/run.sh, the dev profile):
the same SysEx requests, as web/editor.html and the iPhone app send them, to the trial's host simulator
(build/host/optimist_host: Optimist's editor on fm1-base's SysEx, ui/opt_editor.h) and to Optimist's own simulator built
with the same switches (optsim.py build-trial: its usb.c, its main loop), every reply compared.

  edproof.py FLASH.bin OUTDIR     FLASH: the image both power on with (optsim.py write's: a project, a preset ...)

  1 proto     INFO, BUILD, PING; GET and SET (not in this build: no reply on both); the librarian's lists and reads
              (sample slots, user presets, user kits, patterns, a project slot)
  2 backup    BK_LIST (every page), then BK_READ of every object that holds data, chunk by chunk (web/editor.html bkRead)
  3 restore   on a blank flash, the backup put back as the editor does (bkCheckAll: every storage object checked with
              COMMIT i 1 first, then BEGIN / DATA / COMMIT each, BK_END 1): the restart, then BK_LIST again (the CRCs the
              backup's), and the two flashes compared (optsim.py same)
  6 session   (the trial alone) the mirror's test session: a restart as a blank FM-1, the flash untouched, and back
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


def proto(out, flash):
    s = Script()
    s.ask(fr(1))                                # INFO
    s.ask(fr(49))                               # BUILD
    s.ask(fr(25))                               # PING
    s.ask(fr(2, 1, *b7(8, 2)))                  # GET global 8 (ED_LIVE off: no reply)
    s.ask(fr(3, 0, *b7(3, 2), *b7(8192 + 5, 2)))   # SET (no reply)
    s.ask(fr(15))                               # SMP_INFO
    s.ask(fr(16, 0, 16))                        # UP_LIST 0..15
    s.ask(fr(16, 16, 16))
    s.ask(fr(17, 0))                            # UP_GET U01
    s.ask(fr(17, 5))
    s.ask(fr(38))                               # UKIT_LIST
    s.ask(fr(39, 0))                            # UKIT_GET 0
    s.ask(fr(79, 0))                            # PAT_LIST
    s.ask(fr(9, 2, 0))                          # PROJECT query A
    s.ask(fr(9, 2, 2))
    s.ask(fr(42, 1, *b7(0, 3), *b7(64, 2)))     # SMP_READ USR2
    s.ask(fr(77))                               # an unknown command: no reply
    t, o = run(out, "proto", s, flash)
    return compare("proto (INFO, BUILD, PING, GET, SET, the librarian)", t, o)


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


def backup(out, flash):
    s = list_script(Script())
    t, o = run(out, "list", s, flash)
    bad = compare("backup: BK_LIST", t, o)
    objs, n = bk_list(t)
    s = Script()
    for i, tag, kind, flags, ln, crc in objs:
        if flags & 2 and kind != 2:
            for off in range(0, ln, 256):
                s.ask(fr(44, i, *b7(off, 3)), wait=30)
    t, o = run(out, "read", s, flash)
    bad += compare(f"backup: BK_READ of {sum(1 for x in objs if x[3] & 2)} objects", t, o)
    data = {}
    for f in t:
        if f == "restart" or f[4] != 44:
            continue
        a = f[5:-1]
        i, off, crc = a[0], r7(a[1:], 3), r7(a[4:], 5)
        chunk = unpack7(a[9:])
        if zlib.crc32(bytes(chunk)) != crc:
            print(f"FAIL edproof backup: object {i} at {off}: the chunk's CRC")
            bad += 1
        data.setdefault(i, bytearray()).extend(chunk)
    for i, tag, kind, flags, ln, crc in objs:
        if flags & 2 and kind != 2 and (i not in data or len(data[i]) != ln or zlib.crc32(bytes(data[i])) != crc):
            print(f"FAIL edproof backup: {tag}: {len(data.get(i, b''))} of {ln} bytes, CRC {'ok' if i in data and zlib.crc32(bytes(data[i])) == crc else 'bad'}")
            bad += 1
    print(f"  {'ok  ' if not bad else 'FAIL'} edproof backup: {len(data)} objects read whole, "
          f"{sum(len(d) for d in data.values())} bytes: {' '.join(x[1] for x in objs if x[0] in data)}")
    return bad, objs, data


def restore(out, objs, data, blank):
    s = Script()
    s.ask(fr(43, 4, 0))
    order = [x for x in objs if x[0] in data and x[2] != 1]   # (the sample slots: 11..13, not a backup object write)
    def send(i, d, check):
        crc = zlib.crc32(bytes(d))
        s.ask(fr(45, i, *b7(len(d), 3), *b7(crc, 5)), wait=40)
        for off in range(0, len(d), 256):
            c = d[off:off + 256]
            s.ask(fr(46, i, *b7(off, 3), *b7(zlib.crc32(bytes(c)), 5), *pack7(c)), wait=30)
        s.ask(fr(47, i, *([1] if check else [])), wait=400)
    for i, tag, kind, flags, ln, crc in order:
        if kind == 0:
            send(i, data[i], True)              # (bkCheckAll: every storage object checked first)
    for i, tag, kind, flags, ln, crc in order:
        send(i, data[i], False)
    s.ask(fr(48, 1), wait=130)                  # done: the restart 150 ms later
    s.raw(f"flash_dump {out / 'edproof-end-@SIDE@.bin'}")   # (the flash as the restart finds it)
    s.raw("wait 300")
    s.raw("sysex_out")
    t, o = run(out, "restore", s, blank)
    bad = compare(f"restore: {len(order)} objects ({sum(1 for x in order if x[2] == 0)} checked first), BK_END, restart", t, o)
    # the start after it: a power-on with the flash the restart left (the trial's host keeps the app's RAM across its
    # restart, which an FM-1 clears: a new process instead, as a power cycle; Optimist's simulator starts afresh too)
    s2 = list_script(Script())
    s2.raw("wait 2000")
    s2.raw(f"flash_dump {out / 'edproof-restored-@SIDE@.bin'}")
    for side in ("trial", "optimist"):          # (each side on its own flash)
        (out / f"edproof-end.bin").write_bytes((out / f"edproof-end-{side}.bin").read_bytes())
        ts, os_ = run(out, f"restart-{side}", s2, out / "edproof-end.bin")
        if side == "trial":
            t2 = ts
        else:
            o2 = os_
    bad += compare("restore: the start after it, BK_LIST", t2, o2)
    objs2, _ = bk_list([f for f in t2 if f != "restart"])
    want = {x[1]: (x[4], x[5]) for x in objs if x[0] in data and x[2] != 1}
    got = {x[1]: (x[4], x[5]) for x in objs2}
    miss = [tag for tag in want if got.get(tag) != want[tag]]
    print(f"  {'ok  ' if not miss else 'FAIL'} edproof restore: after the restart BK_LIST holds every object restored "
          f"with the backup's length and CRC{': not ' + ' '.join(miss) if miss else ''} ({'restart' in t and 'restart' in o and 'both restarted' or 'NO RESTART'})")
    r = subprocess.run([sys.executable, str(APP / "optsim.py"), "same", str(out / "edproof-restored-optimist.bin"),
                        str(out / "edproof-restored-trial.bin")], capture_output=True, text=True)
    same = (out / "edproof-restored-optimist.bin").read_bytes() == (out / "edproof-restored-trial.bin").read_bytes()
    print(f"  {'ok  ' if not r.returncode and same else 'FAIL'} edproof restore: the flashes after the restore and the "
          f"start: {'the same 1 MiB byte for byte' if same else 'they differ'} (optsim.py same: {r.stdout.strip().splitlines()[-1]})")
    return bad + bool(miss) + ("restart" not in t or "restart" not in o) + (r.returncode != 0) + (not same)


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
    list_script(s)
    s.raw(f"flash_dump {out / 'edproof-session.bin'}")
    s.ask(fr(108, 5, 0, 0, 2), wait=1500)       # the end: the user's own again
    list_script(s)
    p = out / "edproof-session.trial.script.txt"
    p.write_text(s.text())
    t = subprocess.run([str(HOST), str(p), str(out)], capture_output=True, text=True, env={**os.environ, "OPT_FLASH": str(flash)})
    (out / "edproof-session.trial.txt").write_text(t.stdout + t.stderr)
    fr_ = blocks(t.stdout)
    states = [f for f in fr_ if f != "restart" and is_push(f) and f[5] == 4]
    second = [i for i, f in enumerate(fr_) if f == "restart"][-1:] or [len(fr_)]
    lists = [[f for f in fr_[:second[0]] if f != "restart" and f[4] == 43],
             [f for f in fr_[second[0]:] if f != "restart" and f[4] == 43]]
    in_blank = [f for f in states if f[-2] & 2]
    had = lambda fs: sum(1 for o in bk_list(fs)[0] if o[3] & 2)
    untouched = (out / "edproof-session.bin").read_bytes() == flash.read_bytes()
    ok = (t.returncode == 0 and fr_.count("restart") == 2 and in_blank and len(lists) == 2 and had(lists[0]) == 0
          and had(lists[1]) > 0 and untouched)
    print(f"  {'ok  ' if ok else 'FAIL'} edproof session: the test session restarted blank (STATE bit 1, BK_LIST "
          f"{had(lists[0]) if lists else '?'} objects with data), the flash {'untouched' if untouched else 'WRITTEN'}, "
          f"back to the user's own ({had(lists[1]) if len(lists) > 1 else '?'} objects); {fr_.count('restart')} restarts")
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
    print(f"{'FAIL' if bad else '  ok'} edproof: {'differences' if bad else 'the trial answers as Optimist does'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
