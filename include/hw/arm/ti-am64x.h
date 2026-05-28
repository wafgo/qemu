/*
 * TI K3 AM64x SoC
 *
 * Copyright (c) 2026 CMBlu
 * Written by Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_TI_AM64X_H
#define HW_ARM_TI_AM64X_H

#include "hw/sysbus.h"
#include "hw/intc/arm_gic.h"
#include "hw/char/serial-mm.h"
#include "qom/object.h"
#include "target/arm/cpu.h"

#define TYPE_TI_AM64X "ti-am64x"
OBJECT_DECLARE_SIMPLE_TYPE(TIAM64xState, TI_AM64X)

#define TI_AM64X_NUM_A53_CPUS 2
#define TI_AM64X_NUM_MAIN_UARTS 2
#define TI_AM64X_NUM_SPIS 256  /* AM64x GICSS0 supports up to 992 SPIs */

struct TIAM64xState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMCPU a53[TI_AM64X_NUM_A53_CPUS];
    GICState gic;
    SerialMM main_uart[TI_AM64X_NUM_MAIN_UARTS];
};

#endif /* HW_ARM_TI_AM64X_H */
