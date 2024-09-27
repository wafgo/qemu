/*
 * S32 Mode Entry Module
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_MCME_H
#define S32G_MCME_H

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

typedef enum {
    MCME_REGION_CONTROL = 0,
    MCME_REGION_PARTITION0,
    MCME_REGION_PARTITION1,
    MCME_REGION_PARTITION2,
    MCME_REGION_PARTITION3,
    MCME_REGION_NO,
} mcme_region_t;

// Register offsets
#define MC_ME_CTL_KEY_OFFSET               0x0000  // Control Key Register (CTL_KEY)
#define MC_ME_MODE_CONF_OFFSET             0x0004  // Mode Configuration Register (MODE_CONF)
#define MC_ME_MODE_UPD_OFFSET              0x0008  // Mode Update Register (MODE_UPD)
#define MC_ME_MODE_STAT_OFFSET             0x000C  // Mode Status Register (MODE_STAT)
#define MC_ME_MAIN_COREID_OFFSET           0x0010  // Main Core ID Register (MAIN_COREID)

// Partition 0
#define MC_ME_PRTN0_PCONF_OFFSET           0x0100  // Partition 0 Process Configuration Register (PRTN0_PCONF)
#define MC_ME_PRTN0_PUPD_OFFSET            0x0104  // Partition 0 Process Update Register (PRTN0_PUPD)
#define MC_ME_PRTN0_STAT_OFFSET            0x0108  // Partition 0 Status Register (PRTN0_STAT)
#define MC_ME_PRTN0_COFB0_STAT_OFFSET      0x0110  // Partition 0 COFB Set 0 Clock Status Register (PRTN0_COFB0_STAT)
#define MC_ME_PRTN0_COFB0_CLKEN_OFFSET     0x0130  // Partition 0 COFB Set 0 Clock Enable Register (PRTN0_COFB0_CLKEN)
#define MC_ME_PRTN0_CORE0_PCONF_OFFSET     0x0140  // Partition 0 Core 0 Process Configuration Register (PRTN0_CORE0_PCONF)
#define MC_ME_PRTN0_CORE0_PUPD_OFFSET      0x0144  // Partition 0 Core 0 Process Update Register (PRTN0_CORE0_PUPD)
#define MC_ME_PRTN0_CORE0_STAT_OFFSET      0x0148  // Partition 0 Core 0 Status Register (PRTN0_CORE0_STAT)
#define MC_ME_PRTN0_CORE0_ADDR_OFFSET      0x014C  // Partition 0 Core 0 Address Register (PRTN0_CORE0_ADDR)
#define MC_ME_PRTN0_CORE1_PCONF_OFFSET     0x0160  // Partition 0 Core 1 Process Configuration Register (PRTN0_CORE1_PCONF)
#define MC_ME_PRTN0_CORE1_PUPD_OFFSET      0x0164  // Partition 0 Core 1 Process Update Register (PRTN0_CORE1_PUPD)
#define MC_ME_PRTN0_CORE1_STAT_OFFSET      0x0168  // Partition 0 Core 1 Status Register (PRTN0_CORE1_STAT)
#define MC_ME_PRTN0_CORE1_ADDR_OFFSET      0x016C  // Partition 0 Core 1 Address Register (PRTN0_CORE1_ADDR)
#define MC_ME_PRTN0_CORE2_PCONF_OFFSET     0x0180  // Partition 0 Core 2 Process Configuration Register (PRTN0_CORE2_PCONF)
#define MC_ME_PRTN0_CORE2_PUPD_OFFSET      0x0184  // Partition 0 Core 2 Process Update Register (PRTN0_CORE2_PUPD)
#define MC_ME_PRTN0_CORE2_STAT_OFFSET      0x0188  // Partition 0 Core 2 Status Register (PRTN0_CORE2_STAT)
#define MC_ME_PRTN0_CORE2_ADDR_OFFSET      0x018C  // Partition 0 Core 2 Address Register (PRTN0_CORE2_ADDR)
#define MC_ME_PRTN0_CORE3_PCONF_OFFSET     0x01A0  // Partition 0 Core 3 Process Configuration Register (PRTN0_CORE3_PCONF)
#define MC_ME_PRTN0_CORE3_PUPD_OFFSET      0x01A4  // Partition 0 Core 3 Process Update Register (PRTN0_CORE3_PUPD)
#define MC_ME_PRTN0_CORE3_STAT_OFFSET      0x01A8  // Partition 0 Core 3 Status Register (PRTN0_CORE3_STAT)
#define MC_ME_PRTN0_CORE3_ADDR_OFFSET      0x01AC  // Partition 0 Core 3 Address Register (PRTN0_CORE3_ADDR)

// Partition 1
#define MC_ME_PRTN1_PCONF_OFFSET           0x0300  // Partition 1 Process Configuration Register (PRTN1_PCONF)
#define MC_ME_PRTN1_PUPD_OFFSET            0x0304  // Partition 1 Process Update Register (PRTN1_PUPD)
#define MC_ME_PRTN1_STAT_OFFSET            0x0308  // Partition 1 Status Register (PRTN1_STAT)
#define MC_ME_PRTN1_CORE0_PCONF_OFFSET     0x0340  // Partition 1 Core 0 Process Configuration Register (PRTN1_CORE0_PCONF)
#define MC_ME_PRTN1_CORE0_PUPD_OFFSET      0x0344  // Partition 1 Core 0 Process Update Register (PRTN1_CORE0_PUPD)
#define MC_ME_PRTN1_CORE0_STAT_OFFSET      0x0348  // Partition 1 Core 0 Status Register (PRTN1_CORE0_STAT)
#define MC_ME_PRTN1_CORE0_ADDR_OFFSET      0x034C  // Partition 1 Core 0 Address Register (PRTN1_CORE0_ADDR)
#define MC_ME_PRTN1_CORE1_PCONF_OFFSET     0x0360  // Partition 1 Core 1 Process Configuration Register (PRTN1_CORE1_PCONF)
#define MC_ME_PRTN1_CORE1_PUPD_OFFSET      0x0364  // Partition 1 Core 1 Process Update Register (PRTN1_CORE1_PUPD)
#define MC_ME_PRTN1_CORE1_STAT_OFFSET      0x0368  // Partition 1 Core 1 Status Register (PRTN1_CORE1_STAT)
#define MC_ME_PRTN1_CORE1_ADDR_OFFSET      0x036C  // Partition 1 Core 1 Address Register (PRTN1_CORE1_ADDR)
#define MC_ME_PRTN1_CORE2_PCONF_OFFSET     0x0380  // Partition 1 Core 2 Process Configuration Register (PRTN1_CORE2_PCONF)
#define MC_ME_PRTN1_CORE2_PUPD_OFFSET      0x0384  // Partition 1 Core 2 Process Update Register (PRTN1_CORE2_PUPD)
#define MC_ME_PRTN1_CORE2_STAT_OFFSET      0x0388  // Partition 1 Core 2 Status Register (PRTN1_CORE2_STAT)
#define MC_ME_PRTN1_CORE2_ADDR_OFFSET      0x038C  // Partition 1 Core 2 Address Register (PRTN1_CORE2_ADDR)
#define MC_ME_PRTN1_CORE3_PCONF_OFFSET     0x03A0  // Partition 1 Core 3 Process Configuration Register (PRTN1_CORE3_PCONF)
#define MC_ME_PRTN1_CORE3_PUPD_OFFSET      0x03A4  // Partition 1 Core 3 Process Update Register (PRTN1_CORE3_PUPD)
#define MC_ME_PRTN1_CORE3_STAT_OFFSET      0x03A8  // Partition 1 Core 3 Status Register (PRTN1_CORE3_STAT)
#define MC_ME_PRTN1_CORE3_ADDR_OFFSET      0x03AC  // Partition 1 Core 3 Address Register (PRTN1_CORE3_ADDR)

// Partition 2
#define MC_ME_PRTN2_PCONF_OFFSET           0x0500  // Partition 2 Process Configuration Register (PRTN2_PCONF)
#define MC_ME_PRTN2_PUPD_OFFSET            0x0504  // Partition 2 Process Update Register (PRTN2_PUPD)
#define MC_ME_PRTN2_STAT_OFFSET            0x0508  // Partition 2 Status Register (PRTN2_STAT)
#define MC_ME_PRTN2_COFB0_STAT_OFFSET      0x0510  // Partition 2 COFB Set 0 Clock Status Register (PRTN2_COFB0_STAT)
#define MC_ME_PRTN2_COFB0_CLKEN_OFFSET     0x0530  // Partition 2 COFB Set 0 Clock Enable Register (PRTN2_COFB0_CLKEN)

// Partition 3
#define MC_ME_PRTN3_PCONF_OFFSET           0x0700  // Partition 3 Process Configuration Register (PRTN3_PCONF)
#define MC_ME_PRTN3_PUPD_OFFSET            0x0704  // Partition 3 Process Update Register (PRTN3_PUPD)
#define MC_ME_PRTN3_STAT_OFFSET            0x0708  // Partition 3 Status Register (PRTN3_STAT)
#define MC_ME_PRTN3_COFB0_STAT_OFFSET      0x0710  // Partition 3 COFB Set 0 Clock Status Register (PRTN3_COFB0_STAT)
#define MC_ME_PRTN3_COFB0_CLKEN_OFFSET     0x0730  // Partition 3 COFB Set 0 Clock Enable Register (PRTN3_COFB0_CLKEN)

#define TYPE_S32_MCME "s32.mcme"

#define MC_ME_CTRL_REGS ((MC_ME_MAIN_COREID_OFFSET / 4) + 1)
#define MC_ME_PART0_REGS                                                       \
  (((MC_ME_PRTN0_CORE3_ADDR_OFFSET - MC_ME_PRTN0_PCONF_OFFSET) / 4) + 1)
#define MC_ME_PART1_REGS                                                       \
  (((MC_ME_PRTN1_CORE3_ADDR_OFFSET - MC_ME_PRTN1_PCONF_OFFSET) / 4) + 1)
#define MC_ME_PART2_REGS                                                       \
  (((MC_ME_PRTN2_COFB0_CLKEN_OFFSET - MC_ME_PRTN2_PCONF_OFFSET) / 4) + 1)
#define MC_ME_PART3_REGS                                                       \
  (((MC_ME_PRTN3_COFB0_CLKEN_OFFSET - MC_ME_PRTN3_PCONF_OFFSET) / 4) + 1)

#define MCME_PART_UPD_OFFSET_INDEX     0x1
#define MCME_PART_STATUS_OFFSET_INDEX 0x2

OBJECT_DECLARE_SIMPLE_TYPE(S32MCMEState, S32_MCME)

struct S32MCMEState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

    bool unlocked;
    uint32_t num_app_cores;
    uint32_t ctrl_regs[MC_ME_CTRL_REGS];
    uint32_t part0_regs[MC_ME_PART0_REGS];
    uint32_t part1_regs[MC_ME_PART1_REGS];
    uint32_t part2_regs[MC_ME_PART2_REGS];
    uint32_t part3_regs[MC_ME_PART3_REGS];
};

#endif /* S32G_MCME_H */
