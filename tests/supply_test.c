/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* plat_battery_charging (device/plat_supply.c) over simulated P33 registers: -1 before the first reading; 0 with no
 * 5 V on the supply (LDO5V_DET, P3_LDO5V_CON 0xA1 bit 5, clear), whatever the charger's bits say; 1 with 5 V and the
 * charger off (P3_CHG_CON0 0x1A bit 0 clear) or full (P3_CHG_READ 0x19 bit 0 set); 2 with 5 V, the charger on and not
 * full. The other bits of each register change nothing; only the three registers are read. Not the chip: the bits are
 * the JieLi AC79 SDK's (p33.h), not yet measured on an FM-1. */
#include <stdio.h>
#include <stdint.h>

static uint8_t sim_p33[256];
static uint32_t sim_reads, sim_strays;
static uint8_t fm1_p33_read(uint8_t addr)
{
    sim_reads++;
    sim_strays += addr != 0xA1u && addr != 0x1Au && addr != 0x19u;
    return sim_p33[addr];
}
#include "plat_supply.c"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static int read_with(uint8_t ldo, uint8_t con, uint8_t rd)
{
    sim_p33[0xA1] = ldo, sim_p33[0x1A] = con, sim_p33[0x19] = rd;
    plat_supply_read();
    return plat_battery_charging();
}

int main(void)
{
    uint32_t noise;
    want("before the first reading: -1", plat_battery_charging() == -1);
    want("no 5 V: 0 (on the cell)", read_with(0x00, 0x00, 0x00) == 0);
    want("no 5 V, the charger's bits set: still 0", read_with(0x00, 0x01, 0x00) == 0 && read_with(0xDF, 0xFF, 0xFE) == 0);
    want("5 V, the charger off: 1", read_with(0x20, 0x00, 0x00) == 1);
    want("5 V, the charger on and full: 1", read_with(0x20, 0x01, 0x01) == 1);
    want("5 V, the charger on, not full: 2 (charging)", read_with(0x20, 0x01, 0x00) == 2);
    for (noise = 0; noise < 256u; noise++) {      /* every other bit of the three registers set or not */
        uint8_t n = (uint8_t)noise;
        if (read_with((uint8_t)(n & ~0x20u), (uint8_t)(n & ~1u), (uint8_t)(n & ~1u)) != 0 ||
            read_with((uint8_t)(n | 0x20u), (uint8_t)(n & ~1u), (uint8_t)(n & ~1u)) != 1 ||
            read_with((uint8_t)(n | 0x20u), (uint8_t)(n | 1u), (uint8_t)(n | 1u)) != 1 ||
            read_with((uint8_t)(n | 0x20u), (uint8_t)(n | 1u), (uint8_t)(n & ~1u)) != 2)
            break;
    }
    want("the registers' other bits change nothing", noise == 256u);
    want("only P3_LDO5V_CON, P3_CHG_CON0 and P3_CHG_READ read", sim_reads > 0u && sim_strays == 0u);
    printf(fails ? "supply: %d FAILED\n" : "supply: all passed\n", fails);
    return fails != 0;
}
