# Optimist

Optimist is a modular firmware for the M-VAVE FM-1: three synth tracks and a drum track, up to 28 synth engines, 39 drum kits, two insert slots on every track and on the master, reverb, delay and chorus sends, a sequencer with parameter locks, motion recording, chance, conditions and note generators, and one interface for all of it. Bluetooth MIDI, Bluetooth audio, USB audio and the web editor are builds of it. It is free software, built on [SLOOP](https://github.com/isod89/sloop-fm1), Felucca, Melodee, X0X and other FM-1 projects.

This is the release built on fm1-base: the same Optimist, running on fm1-base's platform and installed with its installer.

> **Early access: no warranty, use at your own risk.** Most features are tested in the emulator and on the host, and formats can change between versions. An install could fail or erase your data: back up first.

Version {{VERSION}}.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB. The installer can put M-VAVE's own firmware back, and offers m-bootloader after the firmware.
- [Manual](manual/): the controls, the sound, the sequencer, the drums, the effects, MIDI and USB.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/w0ts/optimist): GPL-3.0-only, with the parts under other licences named in the repository's LICENSING.md.

Optimist's own site, with its builder, its web editor (backup, librarian, the screen mirrored) and its simulator, is at [w0ts.github.io/optimist](https://w0ts.github.io/optimist/).

M-VAVE and FM-1 are trademarks of their owners. Optimist is not affiliated with M-VAVE.
