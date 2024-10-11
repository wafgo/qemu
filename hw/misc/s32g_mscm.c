/*
 * S32 Miscellaneous System Control Module
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/s32g_mscm.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "target/arm/arm-powerctl.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "trace.h"

#define mscm_offset2idx(_off_) (_off_ / 4)

static mscm_region_t s32_mscm_region_type_from_offset(hwaddr offset)
{
    switch(offset) {
    case MSCM_CPXTYPE_OFFSET ... MSCM_CPXCFG3_OFFSET:
        return MSCM_REGION_CPX;
    case MSCM_CP0TYPE_OFFSET ... MSCM_CP6CFG3_OFFSET:
        return MSCM_REGION_CPN;
    case MSCM_IRCP0ISR0_OFFSET ... MSCM_IRNMIC_OFFSET:
        return MSCM_REGION_IRCP;
    case MSCM_IRSPRC_START_OFFSET ... MSCM_IRSPRC_END_OFFSET:
        return MSCM_REGION_IRSPRC;
    default:
        return MSCM_REGION_NO;
    }
}

static const char *s32_mscm_region_name(mscm_region_t region)
{
    static char unknown[20];
    switch (region) {
    case MSCM_REGION_CPX:
        return "MSCM_REGION_CPX";
    case MSCM_REGION_CPN:
        return "MSCM_REGION_CPN";
    case MSCM_REGION_IRCP:
        return "MSCM_REGION_IRCP";
    case MSCM_REGION_IRSPRC:
        return "MSCM_REGION_IRSPRC";
    default:
        snprintf(unknown, sizeof(unknown), "%u ?", region);
        return unknown;
    }
}

static const VMStateDescription vmstate_s32_mscm = {
    .name = TYPE_S32_MSCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, S32MSCMState, MSCM_REG_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static uint64_t s32_mscm_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    S32MSCMState *s = (S32MSCMState *)opaque;

    if (offset > MSCM_IRSPRC_END_OFFSET) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_MSCM, __func__, offset);
    } 

    switch (offset) {
    case MSCM_CPXNUM_OFFSET:
        value = current_cpu->cpu_index + s->num_app_cores;
        break;
    default:
        value = s->regs[mscm_offset2idx(offset)];
        break;
    }
    trace_s32_mscm_read(offset, s32_mscm_region_name(s32_mscm_region_type_from_offset(offset)), value);
    return value;
}


static void s32_mscm_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    S32MSCMState *s = (S32MSCMState *)opaque;
    unsigned long current_value = value;
    int core_id, irpc_offset, irq_no, bit;
    
    if (offset > MSCM_IRSPRC_END_OFFSET) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_MSCM, __func__, offset);
        return;
    }

    trace_s32_mscm_write(offset, s32_mscm_region_name(s32_mscm_region_type_from_offset(offset)), value);
    if (s32_mscm_region_type_from_offset(offset) == MSCM_REGION_IRCP) {
        core_id = (((offset - 0x200) & 0xff) / 0x20);
        irpc_offset = (offset - (0x200 + (core_id * 0x20)));
        irq_no = irpc_offset / 0x8;
        trace_s32_mscm_irq_access(core_id, irpc_offset, irq_no);
        if (irpc_offset == 0x0  ||
            irpc_offset == 0x8  ||
            irpc_offset == 0x10 ||
            irpc_offset == 0x18) {
            /* Status Register */
            /* We only consider the RT Cores for now */
            for (bit = s->num_app_cores; bit < s->num_app_cores + s->num_rt_cores; ++bit) {
                if (current_value & BIT(bit)) {
                    trace_s32_mscm_irq_clear(bit, value);
                    qemu_set_irq(s->msi[bit - s->num_app_cores].irq[irq_no + 1], 0);
                    current_value &= ~BIT(bit);
                }
            }
        } else {
            /* IRQ Generation Register */
            if (current_value & BIT(0)) {
                trace_s32_mscm_irq_raise(core_id, irq_no);
                qemu_set_irq(s->msi[core_id - s->num_app_cores].irq[irq_no + 1], 1);
                current_value &= ~BIT(0);
            }
        }
    }
    s->regs[mscm_offset2idx(offset)] = current_value;
}

static const struct MemoryRegionOps s32_mscm_ops = {
    .read = s32_mscm_read,
    .write = s32_mscm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void s32_mscm_reset(DeviceState *dev)
{
    S32MSCMState *s = S32_MSCM(dev);
    memset(s->regs, 0, sizeof(s->regs));
}

static void s32_mscm_realize(DeviceState *dev, Error **errp)
{
    S32MSCMState *s = S32_MSCM(dev);
    int core, irq;
    int num_cores = s->num_rt_cores;
    for (core = 0; core < num_cores; ++core) {
        for (irq = 0; irq < s->irq_per_core; ++irq) {
            sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->msi[core].irq[irq]);
        }
    }
    
    memory_region_init_io(&s->iomem, OBJECT(dev), &s32_mscm_ops, s,
                          TYPE_S32_MSCM, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static Property mscm_properties[] = {
    DEFINE_PROP_UINT32("num-application-cores", S32MSCMState, num_app_cores, 4),
    DEFINE_PROP_UINT32("num-rt-cores", S32MSCMState, num_rt_cores, 3),
    DEFINE_PROP_UINT32("irq-per-core", S32MSCMState, irq_per_core, 5),
    DEFINE_PROP_END_OF_LIST(),
};

static void s32_mscm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32_mscm_realize;
    dc->reset = s32_mscm_reset;
    dc->vmsd = &vmstate_s32_mscm;
    dc->desc = "S32 Miscellaneous System Control Module";
    device_class_set_props(dc, mscm_properties);
}

static const TypeInfo s32_mscm_info = {
    .name          = TYPE_S32_MSCM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32MSCMState),
    .class_init    = s32_mscm_class_init,
};

static void s32_mscm_register_types(void)
{
    type_register_static(&s32_mscm_info);
}

type_init(s32_mscm_register_types)

