/*
 * Maxim Integrated DS2482 Temperature Sensor I2C messages
 *
 * Browse the data sheet:
 *
 *   https://datasheets.maximintegrated.com/en/ds/DS2482-800.pdf
 *
 * Copyright (C) 2020 Jonatan Palsson <jonatan.p@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#ifndef DS2482_REGS_H
#define DS2482_REGS_H

/**
 * DS2482Reg:
 * @DS2482_REG_STATUS: Status register (0xF0)
 * @DS2482_REG_READ_DATA: Read Data register (0xE1)
 * @DS2482_REG_CONFIG: Configuration register (0xC3)
 *
 **/
typedef enum DS2482Reg {
    DS2482_REG_STATUS           = 0xF0,
    DS2482_REG_READ_DATA        = 0xE1,
    DS2482_REG_CHANNEL_SELECT   = 0xD2,
    DS2482_REG_CONFIG           = 0xC3,
} DS2482Reg;

#endif
