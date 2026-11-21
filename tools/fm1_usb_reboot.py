#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""Restart an FM-1 over USB when its MIDI no longer answers: a firmware built with fm1.toml usb_reset_request = true
(felucca-bsp FELUCCA_USB_RESET_REQ) takes a USB control request on EP0, answered in its USB interrupt, so it still
works when MIDI and SysEx are stuck. No cable pull, nothing written to the flash. Needs pyusb (and libusb).

  tools/fm1_usb_reboot.py NAME [--uboot] [--wait S] [--remote PATH] [--python EXE]
  NAME: the FM-1's USB product string, or what follows "FM1-" in it (B71A for "FM1-B71A"); VID 0x1209 PID 0x0001
  --uboot     the chip's update mode (UBOOT) instead of the firmware again
  --wait S    how long to wait for it to come back and answer MIDI (default 20)
  --remote    tools/fm1_remote.py to check MIDI with (default: the one beside this file); --python: the Python that runs
              it (default this one; it needs mido with python-rtmidi)

What it does: finds the device, sends the request (bmRequestType 0x40, bRequest 0xF1, wValue 0xB007, wIndex 0x4150;
--uboot 0x5542), checks the device leaves the bus (the reset happened), waits for it to come back, asks it for its
screen over MIDI (`fm1_remote.py --port NAME screen`), then prints its `log`, whose "note USB_RESET" lines say what
the firmware was doing when it was restarted (device/hang_note.h; named below). Exit 0: back and answering MIDI;
1: not found or not back; 2: the request was refused (a firmware without it) or the device never left the bus.
A firmware without the request stalls it: nothing happens to the unit."""
import argparse
import subprocess
import sys
import time
from pathlib import Path

VID, PID = 0x1209, 0x0001
REQUEST_TYPE, REQUEST, MAGIC = 0x40, 0xF1, 0xB007         # felucca-bsp chip/usb.c USB_RESET_RQ, USB_RESET_MAGIC
KIND = {"app": 0x4150, "uboot": 0x5542}                    # wIndex: USB_RESET_APP, USB_RESET_UBOOT
REMOTE_DEFAULT = Path(__file__).resolve().with_name("fm1_remote.py")
FLAGS = ((1, "UBOOT asked"), (2, "SysEx ring held (sx_busy)"), (4, "SysEx stream on"), (8, "its frame cut"),
         (16, "host not reading MIDI (tx_deaf)"), (32, "update asked (ota_req)"), (64, "UBOOT key (uboot_req)"),
         (128, "SysEx coming in unfinished"), (256, "USB suspended"), (512, "in the audio render"),
         (1024, "SysEx frame being collected"))


def product_name(name: str) -> str:
    return name if name.upper().startswith("FM1-") or "-" in name else "FM1-" + name


def find(usb_core, usb_util, product: str) -> list:
    """the attached FM-1s whose product string is `product`"""
    found = []
    for d in usb_core.find(find_all=True, idVendor=VID, idProduct=PID):
        try:
            if usb_util.get_string(d, d.iProduct) == product:
                found.append(d)
        except (usb_core.USBError, ValueError):
            continue                                       # (a device that does not answer string requests)
    return found


def present(usb_core, where: "tuple[int, int]") -> bool:
    """the device at (bus, address) still enumerated (no string request: nothing sent to it)"""
    return any((d.bus, d.address) == where for d in usb_core.find(find_all=True, idVendor=VID, idProduct=PID))


def send(dev, usb_core, kind: str) -> str:
    """the request; -> "acked", "stalled" (a firmware without it) or "lost: ..." (the reset may have cut the transfer)"""
    try:
        dev.ctrl_transfer(REQUEST_TYPE, REQUEST, MAGIC, KIND[kind], None, timeout=1000)
        return "acked"
    except usb_core.USBError as e:
        if getattr(e, "errno", None) == 32 or "pipe" in str(e).lower():
            return "stalled"
        return f"lost: {e}"


def decode_note(lines: "list[str]") -> "list[str]":
    """the words of a "note USB_RESET" (device/hang_note.h) named; [] when the log has none"""
    words = []
    for ln in lines:
        parts = ln.split()
        if len(parts) == 5 and parts[0] == "note" and parts[1] in ("w0-2", "w3-5"):
            try:
                words += [int(p, 16) for p in parts[2:]]
            except ValueError:
                return []
    head = next((ln for ln in lines if ln.startswith("note ") and " ms " in ln), "")
    if len(words) != 6 or "USB_RESET" not in head:
        return []
    w = words
    flags = [t for b, t in FLAGS if w[0] & b] or ["none"]
    return [f"  {head}",
            f"  flags: {', '.join(flags)}",
            f"  SysEx out ring {w[1] >> 16} packets, MIDI out ring {w[1] & 0xFFFF}; SysEx streams let go {w[2]}",
            f"  real-time ring w {w[3] >> 16} r {w[3] & 0xFFFF}; MIDI in ring {w[4] >> 16}, SysEx frames not taken "
            f"{w[4] & 0xFFFF}",
            f"  update step {w[5] & 0xFF} (0 none), USB bus resets {w[5] >> 8}"]


def remote(py: str, script: Path, port: str, cmd: str, timeout: float) -> "subprocess.CompletedProcess | None":
    try:
        return subprocess.run([py, str(script), "--port", port, cmd], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


def main(argv: "list[str]") -> int:
    ap = argparse.ArgumentParser(description="Restart an FM-1 over USB (usb_reset_request firmware).")
    ap.add_argument("name")
    ap.add_argument("--uboot", action="store_true")
    ap.add_argument("--wait", type=float, default=20.0)
    ap.add_argument("--remote", type=Path, default=REMOTE_DEFAULT)
    ap.add_argument("--python", default=sys.executable)
    a = ap.parse_args(argv)
    try:
        import usb.core as usb_core
        import usb.util as usb_util
    except ImportError:
        print("fm1_usb_reboot: needs pyusb (pip install pyusb) and libusb")
        return 1
    product, kind = product_name(a.name), "uboot" if a.uboot else "app"
    devs = find(usb_core, usb_util, product)
    if len(devs) != 1:
        print(f"{product}: {'not found' if not devs else f'{len(devs)} devices with that name'} "
              f"(VID {VID:04x} PID {PID:04x})")
        return 1
    dev = devs[0]
    where = (dev.bus, dev.address)
    t0 = time.monotonic()
    got = send(dev, usb_core, kind)
    print(f"{product}: reset request ({'update mode' if a.uboot else 'the firmware again'}): {got}")
    if got == "stalled":
        print(f"{product}: its firmware has no reset request (fm1.toml usb_reset_request = true): nothing happened")
        return 2
    while present(usb_core, where) and time.monotonic() - t0 < 3.0:
        time.sleep(0.05)
    if present(usb_core, where):
        print(f"{product}: still on the bus 3 s later: no reset happened")
        return 2
    print(f"{product}: left the bus after {time.monotonic() - t0:.2f} s")
    if a.uboot:
        print(f"{product}: in the chip's update mode now (it shows up as the updater, not as {product})")
        return 0
    back = None
    while time.monotonic() - t0 < a.wait:
        if find(usb_core, usb_util, product):
            back = time.monotonic() - t0
            break
        time.sleep(0.25)
    if back is None:
        print(f"{product}: not back after {a.wait:.0f} s")
        return 1
    print(f"{product}: back on USB after {back:.1f} s")
    port = product[4:] if product.upper().startswith("FM1-") else product
    while True:
        r = remote(a.python, a.remote, port, "screen", 10.0)
        if r is not None and r.returncode == 0 and "screen:" in r.stdout:
            print(f"{product}: answers MIDI after {time.monotonic() - t0:.1f} s:")
            print("\n".join("  " + ln for ln in r.stdout.splitlines() if ln.startswith("screen:")))
            break
        if time.monotonic() - t0 >= a.wait:
            print(f"{product}: back on USB but no MIDI answer within {a.wait:.0f} s")
            if r is not None:
                print((r.stdout + r.stderr).strip())
            return 1
        time.sleep(1.0)
    r = remote(a.python, a.remote, port, "log", 15.0)
    lines = [ln[5:] for ln in (r.stdout.splitlines() if r is not None else []) if ln.startswith("log: ")]
    note = decode_note(lines)
    print(f"{product}: what it was doing when restarted (its log's note):" if note else
          f"{product}: no USB_RESET note in its log ({'no answer' if r is None else 'an older firmware?'})")
    print("\n".join(note))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
