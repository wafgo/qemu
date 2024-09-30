/*
 * S32 Clock Monitoring Unit - Frequency Counting/Frequency Metering
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_CMU_H_
#define S32G_CMU_H_

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

#define TYPE_S32_CMU_FC "s32.cmu.fc"

#define CMU_FC_GCR  0x0U
#define CMU_FC_RCCR 0x4U
#define CMU_FC_HTCR 0x8U
#define CMU_FC_LTCR 0xCU
#define CMU_FC_SR   0x10U
#define CMU_FC_IER  0x14U

OBJECT_DECLARE_SIMPLE_TYPE(S32CMUFCState, S32_CMU_FC)

struct S32CMUFCState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t ctrl;
    uint32_t rccr;
    uint32_t htcr;
    uint32_t ltcr;
    uint32_t sr;
    uint32_t ier;
};

#endif
