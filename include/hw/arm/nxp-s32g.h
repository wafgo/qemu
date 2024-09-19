/*
 * NXP S32G SoC emulation
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef NXP_S32G_H
#define NXP_S32G_H

#include "hw/arm/armv7m.h"
#include "hw/sd/sdhci.h"
#include "hw/ssi/imx_spi.h"
#include "hw/usb/chipidea.h"
#include "hw/usb/imx-usb-phy.h"
#include "hw/pci-host/designware.h"
#include "exec/memory.h"
#include "cpu.h"
#include "qom/object.h"
#include "hw/arm/armv7m.h"
#include "exec/memory.h"
#include "qemu/units.h"

#define TYPE_NXP_S32G "nxp-s32g"
OBJECT_DECLARE_SIMPLE_TYPE(NxpS32GState, NXP_S32G)

#define NXP_S32G_NUM_M7_CPUS 3
#define NXP_S32G_NUM_A53_CPUS 4    
#define NXP_S32G_NUM_UARTS 5
#define NXP_S32G_NUM_EPITS 2
#define NXP_S32G_NUM_ESDHCS 4

struct NxpS32GState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    ARMv7MState        m7_cpu;
    ARMCPU             a53_cpu[NXP_S32G_NUM_A53_CPUS];
    DesignwarePCIEHost pcie;
    MemoryRegion       qspi_nor;
    MemoryRegion       standby_ram;
    MemoryRegion       sram;
    uint32_t           phy_num;
    Clock              *sysclk;
};

#define NXP_S32G_STANDBY_RAM_BASE 0x24000000
#define NXP_S32G_STANDBY_RAM_SIZE (32 * KiB)
#define NXP_S32G_SRAM_BASE        0x34000000
#define NXP_S32G_SRAM_SIZE        (8 * MiB)

#endif /* NXP_S32G_H */
