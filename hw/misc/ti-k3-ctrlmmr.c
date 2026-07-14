/*
 * TI K3 CTRL_MMR stub
 *
 * Minimal model of the AM64x main-domain control MMRs: returns a
 * configurable MAIN_DEVSTAT (boot-mode pins) at offset 0x30 and accepts
 * (ignores) all writes, so u-boot's mmr_unlock() kick sequences succeed.
 * Everything else reads as zero.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/misc/ti-k3-ctrlmmr.h"

#define CTRLMMR_MAIN_DEVSTAT 0x30
#define CTRLMMR_SIZE 0x20000 /* partitions 0-7 */

static uint64_t ti_k3_ctrlmmr_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3CtrlMmrState *s = TI_K3_CTRLMMR(opaque);

    if (addr == CTRLMMR_MAIN_DEVSTAT) {
        return s->devstat;
    }
    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented read @0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void ti_k3_ctrlmmr_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    /* lock-kick and pinmux writes: accept and ignore */
}

static const MemoryRegionOps ti_k3_ctrlmmr_ops = {
    .read = ti_k3_ctrlmmr_read,
    .write = ti_k3_ctrlmmr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ti_k3_ctrlmmr_init(Object *obj)
{
    TIK3CtrlMmrState *s = TI_K3_CTRLMMR(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_ctrlmmr_ops, s,
                          TYPE_TI_K3_CTRLMMR, CTRLMMR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const Property ti_k3_ctrlmmr_properties[] = {
    /* default: primary bootmode = eMMC (0x9 << 3) */
    DEFINE_PROP_UINT32("devstat", TIK3CtrlMmrState, devstat, 0x48),
};

static void ti_k3_ctrlmmr_class_init(ObjectClass *klass, const void *data)
{
    device_class_set_props(DEVICE_CLASS(klass), ti_k3_ctrlmmr_properties);
}

static const TypeInfo ti_k3_ctrlmmr_info = {
    .name = TYPE_TI_K3_CTRLMMR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3CtrlMmrState),
    .instance_init = ti_k3_ctrlmmr_init,
    .class_init = ti_k3_ctrlmmr_class_init,
};

static void ti_k3_ctrlmmr_register_types(void)
{
    type_register_static(&ti_k3_ctrlmmr_info);
}

type_init(ti_k3_ctrlmmr_register_types)
