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
#include "hw/misc/s32g_mscm.h"
#include "hw/misc/s32g_mcme.h"
#include "hw/misc/s32g_rdc.h"
#include "hw/misc/s32g_cgm.h"
#include "hw/misc/s32g_dfs.h"
#include "hw/timer/s32_stm.h"

#define TYPE_NXP_S32G "nxp-s32g"
OBJECT_DECLARE_SIMPLE_TYPE(NxpS32GState, NXP_S32G)

#define NXP_S32G_NUM_M7_CPUS 3
#define NXP_S32G_NUM_A53_CPUS 4    
#define NXP_S32G_NUM_UARTS 5
#define NXP_S32G_NUM_EPITS 2
#define NXP_S32G_NUM_ESDHCS 4
#define NXP_S32G_NUM_STM    8
#define NXP_S32G_NUM_CGM    4
    
struct NxpS32GState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    ARMv7MState        m7_cpu;
    ARMCPU             a53_cpu[NXP_S32G_NUM_A53_CPUS];
    DesignwarePCIEHost pcie;
    S32MSCMState       mscm;
    MemoryRegion       qspi_nor;
    MemoryRegion       standby_ram;
    MemoryRegion       sram;
    uint32_t           phy_num;
    Clock              *sysclk;
    S32STMTimerState   stm[NXP_S32G_NUM_STM];
    S32MCMEState       mod_entry;
    S32RDCState        rdc;
    S32CGMState        cgm[NXP_S32G_NUM_CGM];
    S32DFSState        core_dfs;
    S32DFSState        periph_dfs;
};


#define NXP_S32G_STANDBY_RAM_BASE 0x24000000
#define NXP_S32G_STANDBY_RAM_SIZE (32 * KiB)

#define NXP_S32G_SRAM_BASE        0x34000000
#define NXP_S32G_SRAM_SIZE        (8 * MiB)

#define NXP_S32G_QSPI_AHB_BASE    0x0
#define NXP_S32G_QSPI_AHB_SIZE    (128 * MiB)

#define NXP_S32G_MSCM_BASE_ADDR   0x40198000

#define NXP_S32G_STM0_BASE_ADDR 0x4011C000
#define NXP_S32G_STM0_IRQ       24

#define NXP_S32G_STM1_BASE_ADDR 0x40120000
#define NXP_S32G_STM1_IRQ       25

#define NXP_S32G_STM2_BASE_ADDR 0x40124000
#define NXP_S32G_STM2_IRQ       26

#define NXP_S32G_STM3_BASE_ADDR 0x40128000
#define NXP_S32G_STM3_IRQ       27

#define NXP_S32G_STM4_BASE_ADDR 0x4021C000
#define NXP_S32G_STM4_IRQ       28

#define NXP_S32G_STM5_BASE_ADDR 0x40220000
#define NXP_S32G_STM5_IRQ       29

#define NXP_S32G_STM6_BASE_ADDR 0x40224000
#define NXP_S32G_STM6_IRQ       30

#define NXP_S32G_STM7_BASE_ADDR 0x40228000
#define NXP_S32G_STM7_IRQ       31

#define NXP_S32G_MCME_BASE_ADDR 0x40088000
#define NXP_S32G_RDC_BASE_ADDR 0x40080000

#define NXP_S32G_CGM0_BASE_ADDR 0x40030000
#define NXP_S32G_CGM1_BASE_ADDR 0x40034000
#define NXP_S32G_CGM2_BASE_ADDR 0x44018000
#define NXP_S32G_CGM5_BASE_ADDR 0x40068000

#define NXP_S32G_CORE_DFS_BASE_ADDR 0x40054000
#define NXP_S32G_PERIPH_DFS_BASE_ADDR 0x40058000

#endif /* NXP_S32G_H */