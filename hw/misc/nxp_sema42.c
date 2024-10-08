/*
 * NXP Sema42 Hardware Semaphores
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/cpu.h"
#include "hw/misc/nxp_sema42.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define DEBUG_NXP_SEMA 0

#ifndef DEBUG_NXP_SEMA
#define DEBUG_NXP_SEMA 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_NXP_SEMA) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_NXP_SEMA42, \
                                             __func__, ##args); \
        } \
    } while (0)

static void nxp_sema_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    NXPSEMA42State *s = opaque;

    DPRINTF("offset: 0x%" HWADDR_PRIx ": value: 0x%" HWADDR_PRIx "\n", offset, value);
    switch(offset) {
    case 0x0 ... 0xf:
        s->gate[offset] = value;
        break;
    case 0x42:
        s->rstgt_w = value;
        break;
    default:
        break;
    }
}

static uint64_t nxp_sema_read(void *opaque, hwaddr offset, unsigned size)
{
    NXPSEMA42State *s = opaque;
    uint64_t value = 0;

    switch(offset) {
    case 0x0 ... 0xf:
        value = s->gate[offset];
        break;
    case 0x42:
        value = s->rstgt_r;
        break;
    default:
        break;
    }
    DPRINTF("offset: 0x%" HWADDR_PRIx ": value: 0x%" HWADDR_PRIx "\n", offset, value);
    
    return value;
}

static const struct MemoryRegionOps nxp_sema_ops = {
    .read = nxp_sema_read,
    .write = nxp_sema_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void nxp_sema_realize(DeviceState *dev, Error **errp)
{
    NXPSEMA42State *s = NXP_SEMA42(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &nxp_sema_ops, s,
                          TYPE_NXP_SEMA42, 0x50);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void nxp_sema_reset(DeviceState *dev)
{
    NXPSEMA42State *s = NXP_SEMA42(dev);
    memset(&s->gate, 0, sizeof(s->gate));
    s->rstgt_r = s->rstgt_w = 0;
}

static const VMStateDescription vmstate_nxp_sema = {
    .name = TYPE_NXP_SEMA42,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(gate, NXPSEMA42State, NXP_SEMA_NUM_GATES_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void nxp_sema_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nxp_sema_realize;
    dc->reset = nxp_sema_reset;
    dc->vmsd = &vmstate_nxp_sema;
    dc->desc = "NXP Sema4 2";
    /* device_class_set_props(dc, mcme_properties); */
}

static const TypeInfo nxp_sema_info = {
    .name          = TYPE_NXP_SEMA42,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NXPSEMA42State),
    .class_init    = nxp_sema_class_init,
};

static void nxp_sema42_register_types(void)
{
    type_register_static(&nxp_sema_info);
}

type_init(nxp_sema42_register_types)
