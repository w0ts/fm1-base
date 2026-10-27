#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The trial's boot stream (fm1.toml boot_pack, run by fm1-base's tools/build.py on a device build):

  boot_pack.py OUT DATA RAMTEXT

.data's initial values and the RAM code (.ram_text) as one stream coded by Optimist's own boot coder (tools/lzb.py of
the tree gen_optimist.py builds from: an LZ parse under an adaptive range coder, round-tripped as it is made), which
app.c app_boot_unpack decodes with Optimist's system/lzb.h, as Optimist's own firmware decodes its boot copies."""
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
sys.path.insert(0, str(APP))
import gen_optimist  # noqa: E402  (the Optimist tree: the env OPTIMIST, as the generator finds it)


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    out, *blobs = argv
    sys.path.insert(0, str(gen_optimist.optimist_tree() / "tools"))
    import lzb
    try:
        stream, _ = lzb.stream([Path(b).read_bytes() for b in blobs])
    except ValueError as e:
        raise SystemExit(f"boot_pack: {e}")
    Path(out).write_bytes(stream)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
