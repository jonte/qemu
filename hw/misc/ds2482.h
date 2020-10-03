/*
 * Maxim Integrated DS2482-800 1-Wire bus master
 *
 * Browse the data sheet:
 *
 *    https://datasheets.maximintegrated.com/en/ds/DS2482-100.pdf
 *
 * Copyright (C) 2020 Jonatan Palsson <jonatan.p@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#ifndef DS2482_H
#define DS2482_H

#include "hw/i2c/i2c.h"
#include "hw/misc/w1_emu.h"
#include "hw/misc/ds2482_regs.h"
#include "qom/object.h"

#define DS2482_NUM_BUSSES 1

#define TYPE_DS2482 "ds2482"
OBJECT_DECLARE_SIMPLE_TYPE(DS2482State, DS2482)

/**
 * DS2482State:
 * @config: Bits 5 and 6 (value 32 and 64) determine the precision of the
 * temperature. See Table 8 in the data sheet.
 *
 * @see_also: https://datasheets.maximintegrated.com/en/ds/DS2482-800.pdf
 */
struct DS2482State {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/

    uint8_t len;
    uint8_t buf;
    uint8_t buf_r_bit_idx;
    qemu_irq pin;

    uint8_t pointer;
    uint8_t config;
    uint8_t status;
    uint8_t pending_cmd;
    int16_t temperature;
    int16_t limit[2];
    int faults;
    uint8_t alarm;
    uint8_t channel;

    W1BusState *w1_busses[DS2482_NUM_BUSSES];

    uint8_t triplet_mode;
};

#endif
