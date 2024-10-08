/*
 * NXP Sema42 Hardware Semaphores
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef NXP_SEMA42_H
#define NXP_SEMA42_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define NXP_SEMA_NUM_GATES_MAX 16

#define TYPE_NXP_SEMA42 "nxp.sema42"

OBJECT_DECLARE_SIMPLE_TYPE(NXPSEMA42State, NXP_SEMA42)

struct NXPSEMA42State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion iomem;
    uint8_t gate[NXP_SEMA_NUM_GATES_MAX];
    uint16_t rstgt_r;
    uint16_t rstgt_w;
};
    
#endif
