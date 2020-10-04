/*
 * Maxim Integrated DS18B20 temperature sensor.
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
#include "hw/misc/w1_emu.h"
#include "migration/vmstate.h"
#include "ds18b20.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"

#define DS18B20_ROM_CMD_SEARCH_ROM      0xF0
#define DS18B20_ROM_CMD_READ_ROM        0x33
#define DS18B20_ROM_CMD_MATCH_ROM       0x55
#define DS18B20_ROM_CMD_SKIP_ROM        0xCC
#define DS18B20_ROM_CMD_ALARM_SEARCH    0xEC

#define DS18B20_FUN_CMD_CONVERT         0x44
#define DS18B20_FUN_CMD_WRITE_SCRATCH   0x4E
#define DS18B20_FUN_CMD_READ_SCRATCH    0xBE
#define DS18B20_FUN_CMD_COPY_SCRATCH    0x48
#define DS18B20_FUN_CMD_RECALL_E2       0xB8
#define DS18B20_FUN_CMD_READ_POWER      0xB4


static uint8_t ds18b20_rx(W1BusClientState *w1)
{
    DS18B20State *c = DS18B20(w1);
    if (c->read_p && c->read_len-- > 0) {
        return *(c->read_p++);
    }

    printf("%s: Read requested, but there's no data!\n", __FUNCTION__);
    return 0;
}

static bool ds18b20_has_valid_data(W1BusClientState *c)
{
    return DS18B20(c)->read_len > 0;
}

static uint8_t ds18b20_crc8( uint8_t *addr, uint8_t len)
{
    uint8_t crc = 0;

    while (len--) {
        uint8_t inbyte = *addr++;
        for (uint8_t i = 8; i; i--) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

static int ds18b20_tx(W1BusClientState *c, enum w1_message type,
                      uint8_t data)
{
    DS18B20State *s = DS18B20(c);
    W1BusMaster *m = w1_bus_get_master(s->w1_bus);
    int ret = 0;

    switch (type) {
        case W1_RESET:
            W1_BUS_MASTER_GET_CLASS(m)->send(m, W1_RESET, 0);
            break;
        case W1_DATA:
            switch (data) {
                case 0: /* What is this ..? */
                    break;
                case DS18B20_ROM_CMD_SEARCH_ROM:
                    s->read_p = s->serial;
                    s->read_len = 8;
                    break;
                case DS18B20_ROM_CMD_SKIP_ROM:
                    break;
                case DS18B20_FUN_CMD_READ_POWER:
                    s->scratchpad[0] = 0xFF;
                    s->read_p = s->scratchpad;
                    s->read_len = 1;
                    break;
                case DS18B20_FUN_CMD_CONVERT:
                    uint16_t temp = (s->temperature / 100) << 4;
                    s->scratchpad[0] = temp & 0x00FF;
                    s->scratchpad[1] = (temp & 0xFF00) >> 8;
                    s->scratchpad[8] = ds18b20_crc8(s->scratchpad, 8);
                    break;
                case DS18B20_FUN_CMD_READ_SCRATCH:
                    s->read_p = s->scratchpad;
                    s->read_len = 9;
                    break;
                default:
                    printf("%s: Unhandled command: %x\n", __FUNCTION__, data);
                    break;
            }
            break;
        default:
            printf("%s: Unhandled message type: %x\n", __FUNCTION__, type);
            break;
    }

    return ret;
}

static int ds18b20_post_load(void *opaque, int version_id)
{
    /*DS18B20State *s = opaque;*/
    return 0;
}

static const VMStateDescription vmstate_ds18b20 = {
    .name = "DS18B20",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = ds18b20_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void ds18b20_reset(W1BusClientState *w1)
{
    //DS18B20State *s = DS18B20(i2c);
}

static void ds18b20_realize(DeviceState *dev, Error **errp)
{
    W1BusClientState *w1 = W1_BUS_CLIENT(dev);
    DS18B20State *s = DS18B20(w1);

    w1_bus_insert_client(s->w1_bus, w1);

    ds18b20_reset(&s->w1);
}

static void set_serial(Object *obj, const char *serial, Error **e)
{
    DS18B20State *s = DS18B20(obj);

    if (strlen(serial) != 16) {
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(s->serial); i++) {
        sscanf(serial, "%2hhx", &s->serial[i]);
        serial += 2;
    }
}

static char *get_serial(Object *obj, Error **e)
{
    DS18B20State *s = DS18B20(obj);

    return g_strdup_printf("%x%x%x%x%x%x%x%x",
                           s->serial[0],
                           s->serial[1],
                           s->serial[2],
                           s->serial[3],
                           s->serial[4],
                           s->serial[5],
                           s->serial[6],
                           s->serial[7]);
}

static void ds18b20_initfn(Object *obj)
{
    DS18B20State *s = DS18B20(obj);

    object_property_add_link(obj, "w1-bus", TYPE_W1_BUS,
                             (Object **)&s->w1_bus,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);

    object_property_add_str(obj, "serial", get_serial, set_serial);
    object_property_add_uint16_ptr(obj, "temperature", &s->temperature,
                                   OBJ_PROP_FLAG_READWRITE);
}

static void ds18b20_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    W1BusClientClass *k = W1_BUS_CLIENT_CLASS(klass);

    dc->realize = ds18b20_realize;
    k->recv = ds18b20_rx;
    k->send = ds18b20_tx;
    k->has_data = ds18b20_has_valid_data;
    dc->vmsd = &vmstate_ds18b20;
}

static const TypeInfo ds18b20_info = {
    .name          = TYPE_DS18B20,
    .parent        = TYPE_W1_BUS_CLIENT,
    .instance_size = sizeof(DS18B20State),
    .instance_init = ds18b20_initfn,
    .class_size    = sizeof(W1BusClientClass),
    .class_init    = ds18b20_class_init,
};

static void ds18b20_register_types(void)
{
    type_register_static(&ds18b20_info);
}

type_init(ds18b20_register_types)
