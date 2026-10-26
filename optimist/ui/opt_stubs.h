/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The parts of Optimist the trial leaves out (later steps), as its UI sees them: stand-ins, each with its reason. */
#pragma once

/* io/usb/usb.c's state, as SYSTEM > BATTERY reads it ("USB" beside the level: a host has configured the FM-1).
 * fm1-base's USB is the device's and plat.h says only whether a USB host powers the FM-1 (plat_battery 4):
 * that, every frame (opt_ui.c) */
static struct {
    uint8_t config, suspended;
    uint32_t resets;                            /* (the editor's sessions: plat_usb_session, ui/opt_editor.h) */
} usb;
