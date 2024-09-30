/*
 * S32 Fast Crystal Oscillator Digital Controller
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_FXOSC_H_
#define S32G_FXOSC_H_

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

#define FXOSC_CTRL 0x0
#define FXOSC_STAT 0x4

#define TYPE_S32_FXOSC "s32.fxosc"

OBJECT_DECLARE_SIMPLE_TYPE(S32FXOSCState, S32_FXOSC)

struct S32FXOSCState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t ctrl;
};

#endif
