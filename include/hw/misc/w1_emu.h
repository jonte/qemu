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

#ifndef W1_EMU_H
#define W1_EMU_H

#include "hw/qdev-core.h"
#include "qemu/queue.h"
#include "qom/object.h"

#define TYPE_W1_BUS "w1-bus"
#define TYPE_W1_BUS_CLIENT "w1-bus-client"
#define TYPE_W1_BUS_MASTER "w1-bus-master"
OBJECT_DECLARE_SIMPLE_TYPE(W1BusState, W1_BUS)
OBJECT_DECLARE_TYPE(W1BusClientState, W1BusClientClass, W1_BUS_CLIENT)

typedef struct W1BusMasterClass W1BusMasterClass;
DECLARE_CLASS_CHECKERS(W1BusMasterClass, W1_BUS_MASTER, TYPE_W1_BUS_MASTER)
#define W1_BUS_MASTER(obj)\
    INTERFACE_CHECK(W1BusMaster, (obj), TYPE_W1_BUS_MASTER)

typedef struct W1BusMaster W1BusMaster;
typedef struct W1BusClientState W1BusClientState;
typedef struct W1BusState W1BusState;

enum w1_message {
    W1_RESET,
    W1_DATA
};

struct W1BusClientClass {
    DeviceClass parent_class;

    int (*send)(W1BusClientState *c, enum w1_message, uint8_t data);
    uint8_t (*recv)(W1BusClientState *s);
    bool (*has_data)(W1BusClientState *c);
};

struct W1BusMasterClass {
    InterfaceClass parent_class;

    int (*send)(W1BusMaster *m, enum w1_message, uint8_t data);
    uint8_t (*recv)(W1BusMaster *m);
};

struct W1BusMaster {
    DeviceClass device;

    W1BusState *bus;
};

struct W1BusClientState {
    DeviceClass device;

    W1BusState *bus;
    QTAILQ_ENTRY(W1BusClientState) next;
};

int w1_bus_insert_client(W1BusState *bus, W1BusClientState *client);

int w1_bus_remove_client(W1BusClientState *client);

int w1_bus_send_to_master(W1BusState *bus, uint8_t data);
int w1_bus_send_to_clients(W1BusState *bus, uint8_t data);
uint8_t w1_bus_read_byte(W1BusState *bus);

/**
 * Generate reset/presence detect on the selected bus
 */
int w1_bus_reset(W1BusState *bus);

int w1_bus_set_master(W1BusState *bus, W1BusMaster *master);
W1BusMaster* w1_bus_get_master(W1BusState *bus);

#endif /* W1_EMU_H */
