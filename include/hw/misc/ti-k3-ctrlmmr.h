/*
 * TI K3 CTRL_MMR stub (DEVSTAT + lock-kick sink)
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_CTRLMMR_H
#define HW_MISC_TI_K3_CTRLMMR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_CTRLMMR "ti.k3-ctrlmmr"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3CtrlMmrState, TI_K3_CTRLMMR)

struct TIK3CtrlMmrState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t devstat;
};

#endif
