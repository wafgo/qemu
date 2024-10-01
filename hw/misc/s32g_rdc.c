/*
 * S32 Reset Domain Controller
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/s32g_rdc.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "target/arm/arm-powerctl.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#if defined(__linux__)
#include <elf.h>
#endif
#include <stdbool.h>
#include "exec/hwaddr.h"

#define DEBUG_S32G_RDC 1

#ifndef DEBUG_S32G_RDC
#define DEBUG_S32G_RDC 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_S32G_RDC) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_S32_RDC, \
                                             __func__, ##args); \
        } \
    } while (0)


static uint64_t s32_rdc_read(void *opaque, hwaddr offset, unsigned size)
{
    S32RDCState *s = (S32RDCState *)opaque;
    uint64_t value = 0;
    switch(offset) {
    case RDC_RD1_CTRL_REG_OFFSET:
        value = s->rd1_ctrl;
        break;
    case RDC_RD2_CTRL_REG_OFFSET:
        value = s->rd2_ctrl;
        break;
    case RDC_RD3_CTRL_REG_OFFSET:
        value = s->rd3_ctrl;
        break;
    case RDC_RD1_STAT_REG_OFFSET:
        if (s->rd1_unlocked == true) {
            s->rd1_stat &= ~RDC_STATUS_INTERFACE_DISABLE_REQ_ACK_MASK;
        }
        value = s->rd1_stat;
        break;
    case RDC_RD2_STAT_REG_OFFSET:
        if (!(s->rd2_ctrl & RDC_CTRL_INTERFACE_DISABLE_MASK))
            s->rd2_stat &= ~RDC_STATUS_INTERFACE_DISABLE_REQ_ACK_MASK;
        value = s->rd2_stat;
        break;
    case RDC_RD3_STAT_REG_OFFSET:
        if (!(s->rd3_ctrl & RDC_CTRL_INTERFACE_DISABLE_MASK))
            s->rd3_stat &= ~RDC_STATUS_INTERFACE_DISABLE_REQ_ACK_MASK;
        value = s->rd3_stat;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_RDC, __func__, offset);
        break;
                    
    }
    DPRINTF("offset: 0x%" HWADDR_PRIx ", value : 0x%" PRIx64 "\n", offset, value);
    return value;
}

static void s32_rdc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    S32RDCState *s = (S32RDCState *)opaque;
    
    switch(offset) {
    case RDC_RD1_CTRL_REG_OFFSET:
        s->rd1_unlocked = (value & BIT(31)) ? true : false;
        s->rd1_ctrl = value;
        if (s->rd1_unlocked) {
            s->rd1_stat &= ~(BIT(3) | BIT(4));
            if (s->rd1_ctrl & BIT(3)) {
                s->rd1_stat |= BIT(3);
            }
            s->rd1_stat |= (s->rd1_ctrl & BIT(3));
        }
        break;
    case RDC_RD2_CTRL_REG_OFFSET:
        s->rd2_unlocked = (value & BIT(31)) ? true : false;
        s->rd2_ctrl = value;
        if (s->rd2_unlocked) {
            s->rd2_stat &= ~(BIT(3) | BIT(4));
            if (s->rd2_ctrl & BIT(3)) {
                s->rd2_stat |= BIT(3);
            }
            s->rd2_stat |= (s->rd2_ctrl & BIT(3));
        }
        break;
    case RDC_RD3_CTRL_REG_OFFSET:
        s->rd3_unlocked = (value & BIT(31)) ? true : false;
        s->rd3_ctrl = value;
        if (s->rd3_unlocked) {
            s->rd3_stat &= ~(BIT(3) | BIT(4));
            if (s->rd3_ctrl & BIT(3)) {
                s->rd3_stat |= BIT(3);
            }
            s->rd3_stat |= (s->rd3_ctrl & BIT(3));
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_RDC, __func__, offset);
        break;
    }
    DPRINTF("offset: 0x%" HWADDR_PRIx " Write: 0x%" PRIx64 "\n", offset, value);
}


static const struct MemoryRegionOps s32_rdc_ops = {
    .read = s32_rdc_read,
    .write = s32_rdc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void s32_rdc_realize(DeviceState *dev, Error **errp)
{
    S32RDCState *s = S32_RDC(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &s32_rdc_ops, s,
                          TYPE_S32_RDC, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void s32_rdc_reset(DeviceState *dev)
{
    S32RDCState *s = S32_RDC(dev);
    
    s->rd1_ctrl = 0x0000000F;
    s->rd2_ctrl = 0x0000000F;
    s->rd3_ctrl = 0x0000000F;

    s->rd1_stat = 0x00000018;
    s->rd2_stat = 0x00000018;
    s->rd3_stat = 0x00000018;

    s->rd1_unlocked = false;
    s->rd2_unlocked = false;
    s->rd3_unlocked = false;
}

static const VMStateDescription vmstate_s32_rdc = {
    .name = TYPE_S32_RDC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(rd1_ctrl, S32RDCState),
        VMSTATE_UINT32(rd2_ctrl, S32RDCState),
        VMSTATE_UINT32(rd3_ctrl, S32RDCState),
        VMSTATE_UINT32(rd1_stat, S32RDCState),
        VMSTATE_UINT32(rd2_stat, S32RDCState),
        VMSTATE_UINT32(rd3_stat, S32RDCState),
        VMSTATE_END_OF_LIST()
    },
};

static void s32_rdc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32_rdc_realize;
    dc->reset = s32_rdc_reset;
    dc->vmsd = &vmstate_s32_rdc;
    dc->desc = "S32 Reset Domain Controller";
}

static const TypeInfo s32_rdc_info = {
    .name          = TYPE_S32_RDC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32RDCState),
    .class_init    = s32_rdc_class_init,
};

static void s32_rdc_register_types(void)
{
    type_register_static(&s32_rdc_info);
}

type_init(s32_rdc_register_types)
