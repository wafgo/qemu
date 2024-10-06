/*
 * S32 SRAM Controller
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_SRAMC_H
#define S32G_SRAMC_H

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"
#include "hw/register.h"

#define TYPE_S32_SRAMC "s32.sramc"

#define CRL_R_MAX 5

OBJECT_DECLARE_SIMPLE_TYPE(S32SRAMCState, S32_SRAMC)

struct S32SRAMCState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    RegisterInfoArray *reg_array;
    uint32_t regs[CRL_R_MAX];
    RegisterInfo regs_info[CRL_R_MAX];
};

#endif /* S32G_SRAMC_H */
