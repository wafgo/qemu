/*
 * S32 Phase Locked Loop
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_PLL_H_
#define S32G_PLL_H_

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

#define TYPE_S32_PLL "s32.plldig"

// Register Offsets
#define PLLCR_OFFSET       0x0000U   // PLL Control
#define PLLSR_OFFSET       0x0004U   // PLL Status
#define PLLDV_OFFSET       0x0008U   // PLL Divider
#define PLLFM_OFFSET       0x000CU   // PLL Frequency Modulation
#define PLLFD_OFFSET       0x0010U   // PLL Fractional Divider
#define PLLCLKMUX_OFFSET   0x0020U   // PLL Clock Multiplexer
#define PLLODIV_0_OFFSET   0x0080U   // PLL Output Divider 0
#define PLLODIV_1_OFFSET   0x0084U     // PLL Output Divider 1
#define PLLODIV_2_OFFSET   0x0088U     // PLL Output Divider 2
#define PLLODIV_3_OFFSET   0x008CU     // PLL Output Divider 3
#define PLLODIV_4_OFFSET   0x0090U     // PLL Output Divider 4
#define PLLODIV_5_OFFSET   0x0094U     // PLL Output Divider 5
#define PLLODIV_6_OFFSET   0x0098U     // PLL Output Divider 6
#define PLLODIV_7_OFFSET   0x009CU     // PLL Output Divider 7

#define PLLDIV_MAX 8

OBJECT_DECLARE_SIMPLE_TYPE(S32PLLState, S32_PLL)

struct S32PLLState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t ctrl;
    uint32_t sts;
    uint32_t div;
    uint32_t fm;
    uint32_t fd;
    uint32_t mux;
    uint32_t odiv[PLLDIV_MAX];
    uint32_t no_divs;
};

#endif
