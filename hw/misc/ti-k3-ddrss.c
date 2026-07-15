/*
 * TI K3 DDRSS register-file stub (AM64x, DDR4 flavour)
 *
 * RAM-backed model of the DDRSS "cfg" register file (DENALI CTL/PI/PHY
 * blocks). u-boot's k3-ddrss driver bulk-writes the DT-provided config
 * (CTL_0 word 0xA00 = DDR4), reads DRAM_CLASS back, kicks the start
 * sequence and polls three "init done" interrupt bits. We store all
 * writes and OR the three status bits into reads so the sequence
 * completes immediately. No PHY training exists in this driver path.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/ti-k3-ddrss.h"

/* read-side OR masks: {offset, bits} */
static const struct {
    hwaddr offset;
    uint32_t bits;
} ddrss_status_bits[] = {
    { 0x214C, 1u << 0 },   /* DENALI_PI_83:  PI init done         */
    { 0x0538, 1u << 13 },  /* DENALI_CTL_334: INT_STATUS_MASTER   */
    { 0x0558, 1u << 25 },  /* DENALI_CTL_342: INT_STATUS_INIT bit1 */
};

static uint64_t ti_k3_ddrss_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3DdrssState *s = TI_K3_DDRSS(opaque);
    uint32_t val = s->regs[addr >> 2];

    for (size_t i = 0; i < ARRAY_SIZE(ddrss_status_bits); i++) {
        if (addr == ddrss_status_bits[i].offset) {
            val |= ddrss_status_bits[i].bits;
        }
    }
    return val;
}

static void ti_k3_ddrss_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    TIK3DdrssState *s = TI_K3_DDRSS(opaque);

    s->regs[addr >> 2] = val;
}

static const MemoryRegionOps ti_k3_ddrss_ops = {
    .read = ti_k3_ddrss_read,
    .write = ti_k3_ddrss_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ti_k3_ddrss_reset(DeviceState *dev)
{
    TIK3DdrssState *s = TI_K3_DDRSS(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void ti_k3_ddrss_init(Object *obj)
{
    TIK3DdrssState *s = TI_K3_DDRSS(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_ddrss_ops, s,
                          TYPE_TI_K3_DDRSS, TI_K3_DDRSS_CFG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void ti_k3_ddrss_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ti_k3_ddrss_reset);
}

static const TypeInfo ti_k3_ddrss_info = {
    .name = TYPE_TI_K3_DDRSS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3DdrssState),
    .instance_init = ti_k3_ddrss_init,
    .class_init = ti_k3_ddrss_class_init,
};

static void ti_k3_ddrss_register_types(void)
{
    type_register_static(&ti_k3_ddrss_info);
}

type_init(ti_k3_ddrss_register_types)
