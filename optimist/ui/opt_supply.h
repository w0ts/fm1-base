/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* SYSTEM > INTERNALS' SUPPLY line (ui/optimist/op_project.c, SYS_DEBUG builds with STOCK_SUPPLY) as Optimist's
 * system/supply.c keeps it: the rails after the stock start-up. That start-up is fm1-base's here (device/main_fm1.c, at
 * every boot: M-VAVE's VDC14 7 = 1.60 V and SYSVDD 15 = 1.38 V, read back and tried again, then the 1.3 V supply on its
 * DC-DC), so this only reads them back at power-on (app.c opt_supply_read), as supply.c's supply_boot does after its
 * own. err: the levels are not the raised ones (Optimist's FM1_SUP_* says why; here only that). Before the UI's files. */
#pragma once
#if FELUCCA_STOCK_SUPPLY && FELUCCA_SYS_DEBUG          /* (only the debug pages read it) */
static struct {
    uint8_t was, now;                              /* VDC14 << 4 | SYSVDD: as the boot loader left them (unknown: 0), now */
    uint8_t err;                                   /* 1: not raised */
    uint8_t done;
    uint32_t regs;                                 /* P3_ANA_CON0 | CON5 << 8 | CON6 << 16 | CON9 << 24 */
} supply;
/* the rails in plain words (supply.c's): "1.38 V  1.60 V", and the 1.4 V rail's regulator */
static inline uint32_t supply_sysvdd_mv(uint32_t code) { return 930u + 30u * (code & 15u); }
static inline uint32_t supply_vdc14_mv(uint32_t code) { return 1250u + 50u * (code & 7u); }
static inline int supply_on_dcdc(uint32_t regs) { return (regs & 0x0Cu) == 0x08u; }
static void opt_supply_boot(void)
{
    supply.now = (uint8_t)opt_supply_read(&supply.regs);
    supply.err = (uint8_t)(supply.now != 0x7Fu);
    supply.done = 1;
}
#else
static void opt_supply_boot(void) {}
#endif
