/*
 * TI AM64x UART emulation
 *
 * Copyright (c) 2025 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/char/ti-am64-uart.h"
#include "exec/cpu-common.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

static uint64_t am64_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    AM64Uart *au = AM64_UART(opaque);

    if (addr >= (8 << au->regshift)) {
        return 0;
    }

    return serial_io_ops.read(&au->serial, addr >> au->regshift, 1);
}

static void am64_uart_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    AM64Uart *au = AM64_UART(opaque);
    value &= 255;
    if (addr >= (8 << au->regshift)) {
        return;
    }

    serial_io_ops.write(&au->serial, addr >> au->regshift, value, 1);
}

static const MemoryRegionOps am64_uart_ops[3] = {
    [DEVICE_NATIVE_ENDIAN] = {
        .read = am64_uart_read,
        .write = am64_uart_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
        .valid.max_access_size = 8,
        .impl.max_access_size = 8,
    },
    [DEVICE_LITTLE_ENDIAN] = {
        .read = am64_uart_read,
        .write = am64_uart_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.max_access_size = 8,
        .impl.max_access_size = 8,
    },
    [DEVICE_BIG_ENDIAN] = {
        .read = am64_uart_read,
        .write = am64_uart_write,
        .endianness = DEVICE_BIG_ENDIAN,
        .valid.max_access_size = 8,
        .impl.max_access_size = 8,
    },
};

static void am64_uart_realize(DeviceState *dev, Error **errp)
{
    AM64Uart *au = AM64_UART(dev);
    SerialState *s = &au->serial;

    if (!qdev_realize(DEVICE(s), NULL, errp)) {
        return;
    }

    memory_region_init_io(&s->io, OBJECT(dev),
                          &am64_uart_ops[au->endianness], au, "serial",
                          64 << au->regshift);
    sysbus_init_mmio(SYS_BUS_DEVICE(au), &s->io);
    sysbus_init_irq(SYS_BUS_DEVICE(au), &au->serial.irq);
}

static const VMStateDescription vmstate_serial_mm = {
    .name = "serial",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(serial, AM64Uart, 0, vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

static void am64_uart_instance_init(Object *o)
{
    AM64Uart *au = AM64_UART(o);

    object_initialize_child(o, "serial", &au->serial, TYPE_SERIAL);

    
    qdev_alias_all_properties(DEVICE(&au->serial), o);
}

static const Property am64_uart_properties[] = {
    /*
     * Set the spacing between adjacent memory-mapped UART registers.
     * Each register will be at (1 << regshift) bytes after the previous one.
     */
    DEFINE_PROP_UINT8("regshift", AM64Uart, regshift, 2),
    DEFINE_PROP_UINT8("endianness", AM64Uart, endianness, DEVICE_NATIVE_ENDIAN),
};

static void am64_uart_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, am64_uart_properties);
    dc->realize = am64_uart_realize;
    dc->vmsd = &vmstate_serial_mm;
}

static const TypeInfo types[] = {
    {
        .name = TYPE_AM64_UART,
        .parent = TYPE_SYS_BUS_DEVICE,
        .class_init = am64_uart_class_init,
        .instance_init = am64_uart_instance_init,
        .instance_size = sizeof(AM64Uart),
    },
};

DEFINE_TYPES(types)
