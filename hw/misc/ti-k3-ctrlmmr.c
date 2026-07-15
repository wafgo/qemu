/*
 * TI K3 CTRL_MMR stub
 *
 * Minimal model of the AM64x main-domain control MMRs: returns a
 * configurable MAIN_DEVSTAT (boot-mode pins) at offset 0x30, a configurable
 * MCU_RST_SRC at offset 0x18178 (mcu-ctrlmmr instance) and a configurable
 * K3_SEC_MGR_SYS_STATUS at offset 0x100 (sec-ctrlmmr instance), and accepts
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
#define CTRLMMR_MCU_RST_SRC  0x18178
/*
 * Offset of K3_SEC_MGR_SYS_STATUS within the sec-ctrlmmr instance mapped at
 * 0x44234000, i.e. absolute address 0x44234100. u-boot's get_device_type()
 * (arch/arm/mach-k3/common.c) reads this during FIT image post-processing
 * (board_fit_image_post_process(), called while SPL parses tispl.bin) to
 * decide GP vs HS-FS/HS-SE handling; on real silicon this lives in the
 * Security Manager MMR partition. QEMU has no security manager, so this
 * offset is only meaningful on the sec-ctrlmmr instance.
 */
#define CTRLMMR_SEC_MGR_SYS_STATUS 0x100
#define CTRLMMR_SIZE 0x20000 /* partitions 0-7 */

static uint64_t ti_k3_ctrlmmr_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3CtrlMmrState *s = TI_K3_CTRLMMR(opaque);

    if (addr == CTRLMMR_MAIN_DEVSTAT) {
        return s->devstat;
    }
    if (addr == CTRLMMR_MCU_RST_SRC) {
        return s->rst_src;
    }
    if (addr == CTRLMMR_SEC_MGR_SYS_STATUS) {
        return s->sec_mgr_sys_status;
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
    /*
     * MCU_RST_SRC reset-source register (offset 0x18178). u-boot's AM64x
     * SPL reads it to apply the CPSW errata i2331 cold-boot workaround: if
     * the value is COLD_BOOT (0) or has the SW_POR bits (24/25) set it forces
     * a full device reset. QEMU has no i2331 erratum, so report a plain warm
     * reset source (bit 0, no POR bits) to skip that reset loop.
     */
    DEFINE_PROP_UINT32("rst-src", TIK3CtrlMmrState, rst_src, 0x1),
    /*
     * K3_SEC_MGR_SYS_STATUS (sec-ctrlmmr instance only). Default decodes to
     * SYS_STATUS_DEV_TYPE_GP (0x3 in bits[3:0]) so u-boot's get_device_type()
     * takes the General Purpose (non-secure) path: no TIFS/certificate
     * authentication is attempted, matching a QEMU model with no security
     * manager.
     */
    DEFINE_PROP_UINT32("sec-mgr-sys-status", TIK3CtrlMmrState,
                       sec_mgr_sys_status, 0x3),
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
