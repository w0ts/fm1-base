# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""Which USB-MIDI ports are an FM-1's, for the tools that talk to it (fm1_remote.py, fm1_debug.py)."""
import re

_DEV_NAME = re.compile(r"FM1-[0-9A-Fa-f]{4}")


def fm1_ports(names, extra=()):
    """the ports among names that are an FM-1's USB-MIDI ports, in their order: "FM1-XXXX" (four hex digits: the
    device name plat_dev_name gives, an app's USB product through FELUCCA_USB_NAME_FN), "Optimist" (an FM-1 on an
    older Optimist firmware) or "FM-1" in it (the stock names), or one of extra; never a Bluetooth port, never other
    gear"""
    return [n for n in names if "bluetooth" not in n.lower() and
            (_DEV_NAME.search(n) or "FM-1" in n or "optimist" in n.lower() or any(e in n for e in extra))]
