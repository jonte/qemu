/*
 * Maxim Integrated DS18B20 Temperature Sensor
 *
 * Browse the data sheet:
 *
 *    https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf
 *
 * Copyright (C) 2020 Jonatan Palsson <jonatan.p@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#ifndef DS18B20_H
#define DS18B20_H

#include "hw/misc/w1_emu.h"
#include "qom/object.h"

#define TYPE_DS18B20 "ds18b20"
OBJECT_DECLARE_SIMPLE_TYPE(DS18B20State, DS18B20)

typedef enum DS18B20Reg {
    DS18B20_REG_STATUS           = 0xF0,
    DS18B20_REG_READ_DATA        = 0xE1,
    DS18B20_REG_CHANNEL_SELECT   = 0xD2,
    DS18B20_REG_CONFIG           = 0xC3,
} DS18B20Reg;

/**
 * DS18B20State:
 * @config: Bits 5 and 6 (value 32 and 64) determine the precision of the
 * temperature. See Table 8 in the data sheet.
 *
 * @see_also: https://datasheets.maximintegrated.com/en/ds/DS18B20-800.pdf
 */
struct DS18B20State {
    /*< private >*/
    W1BusClientState w1;
    /*< public >*/

    W1BusState *w1_bus;
    uint8_t serial[8];

    uint8_t scratchpad[9];

    uint8_t *read_p;
    uint8_t read_len;

    uint16_t temperature;
};

#endif
