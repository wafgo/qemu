/*
 * S32 SRAM Controller
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/s32g_sramc.h"
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

REG32(PRAMCR, 0x0)
    FIELD(PRAMCR, INITREQ, 0, 1) 
    FIELD(PRAMCR, IWS, 1, 2)

REG32(PRAMIAS, 0x4)
    FIELD(PRAMIAS, IAS, 0, 16)

REG32(PRAMIAE, 0x8)
    FIELD(PRAMIAE, IAE, 0, 16)    

REG32(PRAMSR, 0xc)
    FIELD(PRAMSR, IDONE, 0, 1)
    FIELD(PRAMSR, IERR, 1, 1) 
    FIELD(PRAMSR, IPEND, 2, 1) 
    FIELD(PRAMSR, AEXT, 4, 1)
    FIELD(PRAMSR, AERR, 5, 1)
    FIELD(PRAMSR, MLTERR, 6, 1)
    FIELD(PRAMSR, SGLERR, 7, 1)
    FIELD(PRAMSR, SYND, 8, 8)

REG32(PRAMECCA, 0x10)
    FIELD(PRAMECCA, EADR, 0, 16)
    FIELD(PRAMECCA, EBNK, 20, 1)
    FIELD(PRAMECCA, CTRLID, 21, 4)

    
#define DEBUG_S32G_SRAMC 0

#ifndef DEBUG_S32G_SRAMC
#define DEBUG_S32G_SRAMC 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_S32G_SRAMC) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_S32_SRAMC, \
                                             __func__, ##args); \
        } \
    } while (0)


static void pramcr_post_write(RegisterInfo *reg, uint64_t val)
{
    S32SRAMCState *s = S32_SRAMC(reg->opaque);
    if (val & R_PRAMCR_INITREQ_MASK)
        s->regs[R_PRAMSR] |= R_PRAMSR_IDONE_MASK;
}


static const RegisterAccessInfo s32_sramc_regs_info[] = {
    { .name = "PRAMCR",  .addr = A_PRAMCR, .rsvd = 0xfffffff8, .post_write = pramcr_post_write},
    { .name = "PRAMIAS",  .addr = A_PRAMIAS, .rsvd = 0xfffe0000 },
    { .name = "PRAMIAE",  .addr = A_PRAMIAE,.rsvd = 0xfffe0000, .reset = 0x1ffff, .ro =  0xfffe0000 },
    { .name = "PRAMSR",  .addr = A_PRAMSR, .ro = 0xffffff1c, .w1c = 0xe3},
    { .name = "PRAMECCA",  .addr = A_PRAMECCA, .ro = 0xffffffff },
};

static const MemoryRegionOps s32_sramc_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void s32_sramc_init(Object *obj)
{
    S32SRAMCState *s = S32_SRAMC(obj);
    
    s->reg_array = register_init_block32(DEVICE(obj), s32_sramc_regs_info,
                              ARRAY_SIZE(s32_sramc_regs_info),
                              s->regs_info, s->regs,
                              &s32_sramc_ops,
                              DEBUG_S32G_SRAMC,
                              CRL_R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->reg_array->mem);
}

static void s32_sramc_reset_enter(Object *obj, ResetType type)
{
    S32SRAMCState *s = S32_SRAMC(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void s32_sramc_finalize(Object *obj)
{
    S32SRAMCState *s = S32_SRAMC(obj);
    register_finalize_block(s->reg_array);
}

static const VMStateDescription vmstate_sramc = {
    .name = TYPE_S32_SRAMC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, S32SRAMCState, CRL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void s32_sramc_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_sramc;
    rc->phases.enter = s32_sramc_reset_enter;
    dc->desc = "S32 SRAM Controller";
}

static const TypeInfo s32_sramc_info = {
    .name          = TYPE_S32_SRAMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32SRAMCState),
    .instance_init = s32_sramc_init,
    .instance_finalize = s32_sramc_finalize,
    .class_init    = s32_sramc_class_init,
};

static void s32_sramc_register_types(void)
{
    type_register_static(&s32_sramc_info);
}

type_init(s32_sramc_register_types)
