/*
 * S32 Clock Monitoring Unit - Frequency Counting/Frequency Metering
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/s32g_cmu.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "target/arm/arm-powerctl.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#include <stdbool.h>
#include "exec/hwaddr.h"

#define DEBUG_S32G_CMU 0

#ifndef DEBUG_S32G_CMU
#define DEBUG_S32G_CMU 0
#endif

#define DPRINTF(tp, fmt, args...)                \
    do { \
        if (DEBUG_S32G_CMU) { \
            fprintf(stderr, "[%s]%s: " fmt , tp, \
                                             __func__, ##args); \
        } \
    } while (0)




static uint64_t s32_cmu_fc_read(void *opaque, hwaddr offset, unsigned size)
{
    S32CMUFCState *s = (S32CMUFCState *)opaque;
    uint64_t value = 0;
        
    switch(offset) {
    case CMU_FC_GCR:
        value = s->ctrl;
        break;
    case CMU_FC_RCCR:
        value = s->rccr;
        break;
    case CMU_FC_HTCR:
        value = s->htcr;
        break;
    case CMU_FC_LTCR:
        value = s->ltcr;
        break;
    case CMU_FC_SR:
        value = s->sr;
        break;
    case CMU_FC_IER:
        value = s->ier;
        break;
    default:
        DPRINTF(TYPE_S32_CMU_FC, "Invalid Register Access @ offset: 0x%" HWADDR_PRIx " Read\n", offset);
        break;
    }
    DPRINTF(TYPE_S32_CMU_FC, "offset: 0x%" HWADDR_PRIx " Read: 0x%lx\n", offset, value);
    return value;
}

static void s32_cmu_fc_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    S32CMUFCState *s = (S32CMUFCState *)opaque;
    switch(offset) {
    case CMU_FC_GCR:
        s->ctrl = value & BIT(0);
        if (s->ctrl)
            s->sr |= BIT(4);
        else
            s->sr &= ~BIT(4);
        break;
    case CMU_FC_RCCR:
        s->rccr = value & 0xffff;
        break;
    case CMU_FC_HTCR:
        s->htcr = value & 0xffffff;
        break;
    case CMU_FC_LTCR:
        s->ltcr = value & 0xffffff;
        break;
    case CMU_FC_SR:
        if (value & BIT(0))
            s->sr &= ~(BIT(0));
        if (value & BIT(1))
            s->sr &= ~(BIT(1));
        break;
    case CMU_FC_IER:
        s->ier = value & 0xf;
        break;
    default:
        DPRINTF(TYPE_S32_CMU_FC, "Invalid Register Access @ offset: 0x%" HWADDR_PRIx " Read\n", offset);
        break;
    }
    DPRINTF(TYPE_S32_CMU_FC, "offset: 0x%" HWADDR_PRIx " Write: 0x%lx\n", offset, value);
}

static const struct MemoryRegionOps s32_cmu_fc_ops = {
    .read = s32_cmu_fc_read,
    .write = s32_cmu_fc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static const VMStateDescription vmstate_s32_cmu_fc = {
    .name = TYPE_S32_CMU_FC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, S32CMUFCState),
        VMSTATE_UINT32(rccr, S32CMUFCState),
        VMSTATE_UINT32(htcr, S32CMUFCState),
        VMSTATE_UINT32(ltcr, S32CMUFCState),
        VMSTATE_UINT32(sr, S32CMUFCState),
        VMSTATE_UINT32(ier, S32CMUFCState),
        VMSTATE_END_OF_LIST()
    },
};

static void s32_cmu_fc_reset(DeviceState *dev)
{
    S32CMUFCState *s = S32_CMU_FC(dev);
    s->ctrl = 0;
    s->rccr = 0;
    s->htcr = 0xffffff;
    s->ltcr = 0;
    s->sr = 0;
    s->ier = 0;
}

static void s32_cmu_fc_realize(DeviceState *dev, Error **errp)
{
    S32CMUFCState *s = S32_CMU_FC(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &s32_cmu_fc_ops, s,
                          TYPE_S32_CMU_FC, 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void s32_cmu_fc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32_cmu_fc_realize;
    dc->reset = s32_cmu_fc_reset;
    dc->vmsd = &vmstate_s32_cmu_fc;
    dc->desc = "S32 Clock Monitoring Unit - Frequency Counting";
    //device_class_set_props(dc, cmu_fc_properties);
}

static const TypeInfo s32_cmu_fc_info = {
    .name          = TYPE_S32_CMU_FC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32CMUFCState),
    .class_init    = s32_cmu_fc_class_init,
};

static void s32_cmu_fc_register_types(void)
{
    type_register_static(&s32_cmu_fc_info);
}

type_init(s32_cmu_fc_register_types)
