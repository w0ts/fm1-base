/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* the supply (plat_battery_charging): the chip's P33 power registers, read by the main loop (device/main_fm1.c, twice a second,
 * interrupts off: the P33 bus is shared). JieLi AC79 SDK include_lib/driver/cpu/wl82/asm/p33.h: LDO5V_DET_GET = P3_LDO5V_CON
 * (0xA1) bit 5, IS_CHARGE_EN = P3_CHG_CON0 (0x1A) bit 0, CHARGE_FULL_FLAG_GET = P3_CHG_READ (0x19) bit 0. Part of
 * plat_fm1.c (APP_BATTERY_RAW); on its own for tests/supply_test.c, which gives it fm1_p33_read over simulated registers */
static int8_t supply_st = -1;
static void plat_supply_read(void)
{
    uint8_t ldo = fm1_p33_read(0xA1u), con = fm1_p33_read(0x1Au), rd = fm1_p33_read(0x19u);
    supply_st = !(ldo & 0x20u) ? 0 : (con & 1u) && !(rd & 1u) ? 2 : 1;
}
int plat_battery_charging(void) { return supply_st; }
