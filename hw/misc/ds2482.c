/*
 * Maxim Integrated DS2482-800 1-Wire bus master
 *
 * Copyright (C) 2020 Jonatan Palsson <jonatan.p@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "ds2482.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"

/* Bitmasks for the config register. See table on page 6 in the data sheet */
#define DS2482_CONFIG_APU   0x1
#define DS2482_CONFIG_BIT1  0x2
#define DS2482_CONFIG_SPU   0x4
#define DS2482_CONFIG_1WS   0x8
#define DS2482_CONFIG_APU_N 0x10
#define DS2482_CONFIG_BIT5  0x20
#define DS2482_CONFIG_SPU_N 0x40
#define DS2482_CONFIG_1WS_N 0x80

/* Bitmasks for the status register. See table on page 8 in the data sheet */
#define DS2482_STATUS_1WB   0x1
#define DS2482_STATUS_PPD   0x2
#define DS2482_STATUS_SD    0x4
#define DS2482_STATUS_LL    0x8
#define DS2482_STATUS_RST   0x10
#define DS2482_STATUS_SBR   0x20
#define DS2482_STATUS_TSB   0x40
#define DS2482_STATUS_DIR   0x80

/* Function commands. See page 9 and onwards in data sheet */
#define DS2482_CMD_1WIRE_TRIPLET    0x78
#define DS2482_CMD_1WIRE_SINGLE_BIT 0x87
#define DS2482_CMD_1WIRE_READ_BYTE  0x96
#define DS2482_CMD_1WIRE_WRITE_BYTE 0xA5
#define DS2482_CMD_1WIRE_RESET      0xB4
#define DS2482_CMD_CHANNEL_SELECT   0xC3
#define DS2482_CMD_WRITE_CONFIG     0xD2
#define DS2482_CMD_SET_READ_PTR     0xE1
#define DS2482_CMD_RESET            0xF0

static uint8_t ds2482_rx_status(DS2482State *s)
{
    if (s->triplet_mode) {
        if (!s->buf_r_bit_idx) {
            s->buf = w1_bus_read_byte(s->w1_busses[s->channel]);
        }
        uint8_t b = test_bit(s->buf_r_bit_idx++, (unsigned long *)&s->buf);

        s->buf_r_bit_idx %= 8;

        if (b) {
            clear_bit(6, (unsigned long *)&s->status);
            set_bit(7, (unsigned long *)&s->status);
        } else {
            set_bit(6, (unsigned long *)&s->status);
            clear_bit(7, (unsigned long *)&s->status);
        }

        s->triplet_mode = 0;
    }

    return s->status;
}

static uint8_t ds2482_rx(I2CSlave *i2c)
{
    DS2482State *s = DS2482(i2c);

    switch (s->pointer) {
        case DS2482_REG_STATUS:
            return ds2482_rx_status(s);
        case DS2482_REG_READ_DATA:
            return s->buf;
        case DS2482_REG_CONFIG:
            return s->config;
        case DS2482_REG_CHANNEL_SELECT:

            switch (s->channel) {
                case 0: return 0xB8;
                case 1: return 0xB1;
                case 2: return 0xAA;
                case 3: return 0xA3;
                case 4: return 0x9C;
                case 5: return 0x95;
                case 6: return 0x8E;
                case 7: return 0x87;
                default:
                    printf("%s: Unhandled 1W channel: %d\n", __FUNCTION__,
                           s->channel);
                    return 0x00;
            }
        default:
            printf("%s: Unhandled read pointer: %x\n", __FUNCTION__,
                   s->pointer);
            break;
    }

    return 0;
}

static int ds2482_dev_reset(DS2482State *s)
{
    s->status = DS2482_STATUS_RST | DS2482_STATUS_LL;
    s->config = 0xF0;
    s->pointer = DS2482_REG_STATUS;
    s->channel = 0;

    return 0;
}

static int ds2482_1w_reset(DS2482State *s)
{
    if (s->status & DS2482_STATUS_1WB) {
        s->pending_cmd = 0;
        return -1;
    }

    w1_bus_reset(s->w1_busses[s->channel]);
    s->pointer = DS2482_REG_STATUS;

    return 0;
}

static int ds2482_write_config(DS2482State *s, uint8_t data)
{
    if (s->status & DS2482_STATUS_1WB) {
        s->pending_cmd = 0;
        return -1;
    }

    if (!s->pending_cmd) {
        s->pending_cmd = DS2482_CMD_WRITE_CONFIG;
    } else {
        s->config = data;
        s->pending_cmd = 0;
        s->pointer = DS2482_REG_CONFIG;
    }

    return 0;
}

static int ds2482_write_bit(DS2482State *s, uint8_t data)
{
    if (s->status & DS2482_STATUS_1WB) {
        s->pending_cmd = 0;
        return -1;
    }

    if (!s->pending_cmd) {
        s->pending_cmd = DS2482_CMD_1WIRE_SINGLE_BIT;
    } else {
        w1_bus_send_to_clients(s->w1_busses[s->channel], data);

        s->pending_cmd = 0;
        s->pointer = DS2482_REG_STATUS;
    }

    return 0;
}

static int ds2482_write_byte(DS2482State *s, uint8_t data)
{
    if (s->status & DS2482_STATUS_1WB) {
        s->pending_cmd = 0;
        return -1;
    }

    if (!s->pending_cmd) {
        s->pending_cmd = DS2482_CMD_1WIRE_WRITE_BYTE;
    } else {
        w1_bus_send_to_clients(s->w1_busses[s->channel], data);



        s->pending_cmd = 0;
        s->pointer = DS2482_REG_STATUS;
    }

    return 0;
}

static int ds2482_channel_select(DS2482State *s, uint8_t data)
{
    if (s->status & DS2482_STATUS_1WB) {
        s->pending_cmd = 0;
        return -1;
    }

    if (DS2482_NUM_BUSSES == 1) {
        return 1;
    }

    if (!s->pending_cmd) {
        s->pending_cmd = DS2482_CMD_CHANNEL_SELECT;
    } else {
        /* See table on page 11 of data sheet for details */
        if ((data & 0xF) >= 0 && (data & 0xF) < 8) {
            s->channel = data & 0x0F;
        } else {
            printf("%s: Unknown channel: %x\n", __FUNCTION__, data);
            return -1;
        }

        s->pointer = DS2482_REG_CHANNEL_SELECT;
        s->pending_cmd = 0;
    }

    return 0;
}

static int ds2482_write_triplet(DS2482State *s, uint8_t data)
{
    if (s->status & DS2482_STATUS_1WB) {
        s->pending_cmd = 0;
        return -1;
    }

    if (!s->pending_cmd) {
        s->pending_cmd = DS2482_CMD_1WIRE_TRIPLET;
    } else {
        w1_bus_send_to_clients(s->w1_busses[s->channel], data);
        s->pointer = DS2482_REG_STATUS;
        s->pending_cmd = 0;
        s->triplet_mode = 1;
    }

    return 0;
}

static int ds2482_set_read_ptr(DS2482State *s, uint8_t data)
{
    if (!s->pending_cmd) {
        s->pending_cmd = DS2482_CMD_SET_READ_PTR;
    } else {
        s->pointer = data;
        s->pending_cmd = 0;
    }

    return 0;
}

static int ds2482_1w_read_byte(DS2482State *s)
{
    s->buf = w1_bus_read_byte(s->w1_busses[s->channel]);
    return 0;
}

static int ds2482_tx(I2CSlave *i2c, uint8_t data)
{
    DS2482State *s = DS2482(i2c);
    int ret = 0;

    switch (s->pending_cmd ? s->pending_cmd : data) {
        case DS2482_CMD_RESET:
            ret = ds2482_dev_reset(s);
            break;
        case DS2482_CMD_1WIRE_RESET:
            ret = ds2482_1w_reset(s);
            break;
        case DS2482_CMD_1WIRE_READ_BYTE:
            ret = ds2482_1w_read_byte(s);
            break;
        case DS2482_CMD_WRITE_CONFIG:
            ret = ds2482_write_config(s, data);
            break;
        case DS2482_CMD_1WIRE_SINGLE_BIT:
            ret = ds2482_write_bit(s, data);
            break;
        case DS2482_CMD_1WIRE_WRITE_BYTE:
            ret = ds2482_write_byte(s, data);
            break;
        case DS2482_CMD_CHANNEL_SELECT:
            ret = ds2482_channel_select(s, data);
            break;
        case DS2482_CMD_1WIRE_TRIPLET:
            ret = ds2482_write_triplet(s, data);
            break;
        case DS2482_CMD_SET_READ_PTR:
            ret = ds2482_set_read_ptr(s, data);
            break;
        default:
            printf("%s: Unhandled command: %x\n", __FUNCTION__, data);
            break;
    }

    return ret;
}

static int ds2482_event(I2CSlave *i2c, enum i2c_event event)
{
    DS2482State *s = DS2482(i2c);

    if (event == I2C_START_RECV) {
        s->pending_cmd = 0;
    }

    return 0;
}

static int ds2482_post_load(void *opaque, int version_id)
{
    /*DS2482State *s = opaque;*/
    return 0;
}

static const VMStateDescription vmstate_ds2482 = {
    .name = "DS2482",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = ds2482_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, DS2482State),
        VMSTATE_UINT8(buf, DS2482State),
        VMSTATE_UINT8(pointer, DS2482State),
        VMSTATE_UINT8(config, DS2482State),
        VMSTATE_UINT8(status, DS2482State),
        VMSTATE_INT16(temperature, DS2482State),
        VMSTATE_INT16_ARRAY(limit, DS2482State, 2),
        VMSTATE_UINT8(alarm, DS2482State),
        VMSTATE_I2C_SLAVE(i2c, DS2482State),
        VMSTATE_END_OF_LIST()
    }
};

static void ds2482_reset(I2CSlave *i2c)
{
    DS2482State *s = DS2482(i2c);

    s->temperature = 0;
    s->pointer = 0;
    s->config = 0;
    s->faults = 0;
    s->alarm = 0;
    s->pending_cmd = 0;
    s->channel = 0;
}

static void ds2482_realize(DeviceState *dev, Error **errp)
{
    I2CSlave *i2c = I2C_SLAVE(dev);
    DS2482State *s = DS2482(i2c);

    qdev_init_gpio_out(&i2c->qdev, &s->pin, 1);

    ds2482_reset(&s->i2c);


    for (int i = 0; i < ARRAY_SIZE(s->w1_busses); i++) {
        if (s->w1_busses[i]) {
            w1_bus_set_master(s->w1_busses[i], W1_BUS_MASTER(dev));
        }
    }
}

static void ds2482_initfn(Object *obj)
{
    DS2482State *s = DS2482(obj);

    for (int i = 0; i < ARRAY_SIZE(s->w1_busses); i++) {
        s->w1_busses[i] = 0;
    }

    for (int i = 0; i < ARRAY_SIZE(s->w1_busses); i++) {
        char bus_name[sizeof("w1-bus-") + 3 /* No more than 999 busses */ + 1];
        snprintf(bus_name, sizeof(bus_name), "w1-bus-%d", i);

        object_property_add_link(obj, bus_name, TYPE_W1_BUS,
                                 (Object **)&s->w1_busses[i],
                                 object_property_allow_set_link,
                                 OBJ_PROP_LINK_STRONG);
    }
}

static int ds2482_w1_bus_master_tx(W1BusMaster *m, enum w1_message type,
                                   uint8_t data)
{
    DS2482State *s = DS2482(m);

    switch (type) {
        case W1_RESET:
            s->status |= DS2482_STATUS_PPD;
            break;
        case W1_DATA:
            w1_bus_send_to_clients(s->w1_busses[s->channel], data);
            break;
        default:
            printf("%s: Unknown message type: %x\n", __FUNCTION__, type);
    }

    return 0;
}

static uint8_t ds2482_w1_bus_master_rx(W1BusMaster *m)
{
    return 0;
}

static void ds2482_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    W1BusMasterClass *w1_bus_master_class = W1_BUS_MASTER_CLASS(klass);

    dc->realize = ds2482_realize;
    k->event = ds2482_event;
    k->recv = ds2482_rx;
    k->send = ds2482_tx;
    w1_bus_master_class->send = ds2482_w1_bus_master_tx;
    w1_bus_master_class->recv = ds2482_w1_bus_master_rx;
    dc->vmsd = &vmstate_ds2482;
}

static const TypeInfo ds2482_info = {
    .name          = TYPE_DS2482,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DS2482State),
    .instance_init = ds2482_initfn,
    .class_init    = ds2482_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_W1_BUS_MASTER },
        { }
    }
};

static void ds2482_register_types(void)
{
    type_register_static(&ds2482_info);
}

type_init(ds2482_register_types)
