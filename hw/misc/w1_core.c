/*
 * 1-Wire bus emulation
 *
 * Copyright (C) 2020 Jonatan Palsson <jonatan.p@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "chardev/char.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "hw/misc/w1_emu.h"
#include "qom/object_interfaces.h"

struct W1BusState {
    Object object;

    W1BusMaster *master;
    QTAILQ_HEAD(, W1BusClientState) clients;
    uint8_t contents;
};

static int w1_bus_write(W1BusState *bus, enum w1_message type, uint8_t data)
{
    if (!bus) {
        return -1;
    }

    ssize_t ret = 0;

    W1BusClientState *peer;
    if (bus == NULL) {
        return -1;
    }

    QTAILQ_FOREACH(peer, &bus->clients, next) {
        if (W1_BUS_CLIENT_GET_CLASS(peer)->send(peer, type, data)) {
            ret = 1;
        }
    }

    return ret;
}

int w1_bus_insert_client(W1BusState *bus, W1BusClientState *client)
{
    client->bus = bus;
    QTAILQ_INSERT_TAIL(&bus->clients, client, next);
    return 0;
}

int w1_bus_remove_client(W1BusClientState *client)
{
    W1BusState *bus = client->bus;
    if (bus == NULL) {
        return 0;
    }

    QTAILQ_REMOVE(&bus->clients, client, next);
    client->bus = NULL;
    return 1;
}

int w1_bus_send_to_master(W1BusState *bus, uint8_t data)
{
    W1BusMasterClass *master = W1_BUS_MASTER_GET_CLASS(W1_BUS(bus)->master);
    return master->send(W1_BUS_MASTER(bus->master), W1_DATA, data);
}

int w1_bus_send_to_clients(W1BusState *bus, uint8_t data)
{
    return w1_bus_write(bus, W1_DATA, data);
}

int w1_bus_reset(W1BusState *bus)
{
    return w1_bus_write (bus, W1_RESET, 0);
}

int w1_bus_set_master(W1BusState *bus, W1BusMaster *m)
{
    W1_BUS(bus)->master = m;

    return 0;
}

uint8_t w1_bus_read_byte(W1BusState *bus)
{
    W1BusClientState *peer = NULL;

    QTAILQ_FOREACH(peer, &bus->clients, next) {
        if (W1_BUS_CLIENT_GET_CLASS(peer)->has_data(peer)) {
            return W1_BUS_CLIENT_GET_CLASS(peer)->recv(peer);
        }
    }

    return 0;
}

W1BusMaster* w1_bus_get_master(W1BusState *bus)
{
    return W1_BUS(bus)->master;
}

static bool w1_core_can_be_deleted(UserCreatable *uc)
{
    return false;
}

static void w1_core_class_init(ObjectClass *klass,
                                void *class_data G_GNUC_UNUSED)
{
    UserCreatableClass *uc_klass = USER_CREATABLE_CLASS(klass);

    uc_klass->can_be_deleted = w1_core_can_be_deleted;
}

static void w1_bus_instance_init(Object *object)
{
    W1BusState *bus = (W1BusState *)object;

    QTAILQ_INIT(&bus->clients);
}

static const TypeInfo w1_bus_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_W1_BUS,
    .instance_size = sizeof(W1BusState),
    .instance_init = w1_bus_instance_init,
    .class_init = w1_core_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static const TypeInfo w1_bus_client_info = {
    .parent = TYPE_DEVICE,
    .name = TYPE_W1_BUS_CLIENT,
    .instance_size = sizeof(W1BusClientState),
    .abstract = true,
    .class_init = w1_core_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static const TypeInfo w1_bus_master_info = {
    .parent = TYPE_INTERFACE,
    .name = TYPE_W1_BUS_MASTER,
    .class_size = sizeof(W1BusMasterClass)
};

static void w1_core_register_types(void)
{

    type_register_static(&w1_bus_info);
    type_register_static(&w1_bus_client_info);
    type_register_static(&w1_bus_master_info);
}

type_init(w1_core_register_types);
