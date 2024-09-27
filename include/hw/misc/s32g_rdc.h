/*
 * S32 Reset Domain Controller
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_RDC_H_
#define S32G_RDC_H_

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

#define TYPE_S32_RDC "s32.rdc"

#define RDC_RD1_CTRL_REG_OFFSET            0x0004
#define RDC_RD2_CTRL_REG_OFFSET            0x0008
#define RDC_RD3_CTRL_REG_OFFSET            0x000C

#define RDC_RD1_STAT_REG_OFFSET            0x0084
#define RDC_RD2_STAT_REG_OFFSET            0x0088
#define RDC_RD3_STAT_REG_OFFSET            0x008C

#define RDC_CTRL_INTERFACE_DISABLE_MASK BIT(3)
#define RDC_CTRL_UNLOCK_MASK BIT(31)

#define RDC_STATUS_INTERFACE_DISABLE_REQ_ACK_MASK BIT(3)
#define RDC_STATUS_INTERFACE_DISABLE_ACK_MASK     BIT(4)

OBJECT_DECLARE_SIMPLE_TYPE(S32RDCState, S32_RDC)

struct S32RDCState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t rd1_ctrl;
    uint32_t rd2_ctrl;
    uint32_t rd3_ctrl;
    uint32_t rd1_stat;
    uint32_t rd2_stat;
    uint32_t rd3_stat;
};

#endif
