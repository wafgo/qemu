/*
 * S32 Clock Generation Module
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_CGM_H_
#define S32G_CGM_H_

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

#define MC_CGM_PCFS_DIVC(X) ((12 * X) - 8)
#define MC_CGM_PCFS_DIVE(X) ((12 * X) - 7)
#define MC_CGM_PCFS_DIVS(X) ((12 * X) - 6)

//G_STATIC_ASSERT(x > 0x27);
#define MC_CGM_OFFSET_CONTROL_TO_DIVIDER_IDX(X) ((X + 8) / 12)
#define MC_CGM_OFFSET_END_TO_DIVIDER_IDX(X)     ((X + 7) / 12)
#define MC_CGM_OFFSET_START_TO_DIVIDER_IDX(X)  ((X + 6) / 12)

#define MC_CGM_OFFSET_CONTROL_TO_MUX_IDX(X) ((X - 0x300) / 0x40)
#define MC_CGM_OFFSET_STATUS_TO_MUX_IDX(X) ((X - 0x304) / 0x40)
#define MC_CGM_OFFSET_DIV0_TO_MUX_IDX(X) ((X - 0x308) / 0x40)
#define MC_CGM_OFFSET_DIV1_TO_MUX_IDX(X) ((X - 0x30C) / 0x40)
#define MC_CGM_OFFSET_UPD_STAT_TO_MUX_IDX(X) ((X - 0x33C) / 0x40)

// Register offsets
#define MC_CGM_PCFS_SDUR            0x000 // PCFS Step Duration

#define MC_CGM_PCFS_DIVC4           0x028   // PCFS Divider Change 4 Register
#define MC_CGM_PCFS_DIVE4           0x02C   // PCFS Divider End 12 Register
#define MC_CGM_PCFS_DIVS4           0x030   // PCFS Divider Change 12 Register
#define MC_CGM_PCFS_DIVC12          0x088   // PCFS Divider End 12 Register        
#define MC_CGM_PCFS_DIVE12          0x08C   // PCFS Divider End 12 Register
#define MC_CGM_PCFS_DIVS12          0x090 // PCFS Divider Start 12 Register
#define MC_CGM_PCFS_DIVC23          0x10C  // PCFS Divider Change 23 Register
#define MC_CGM_PCFS_DIVE23          0x110  // PCFS Divider End 23 Register
#define MC_CGM_PCFS_DIVS23          0x114  // PCFS Divider Start 23 Register
#define MC_CGM_PCFS_DIVC33          0x184  // PCFS Divider Change 33 Register
#define MC_CGM_PCFS_DIVE33          0x188  // PCFS Divider End 33 Register
#define MC_CGM_PCFS_DIVS33          0x18C  // PCFS Divider Start 33 Register
#define MC_CGM_PCFS_DIVC46          0x220  // PCFS Divider Change 46 Register
#define MC_CGM_PCFS_DIVE46          0x224  // PCFS Divider End 46 Register
#define MC_CGM_PCFS_DIVS46          0x228  // PCFS Divider Start 46 Register
#define MC_CGM_PCFS_DIVC47          0x22C  // PCFS Divider Change 47 Register
#define MC_CGM_PCFS_DIVE47          0x230  // PCFS Divider End 47 Register
#define MC_CGM_PCFS_DIVS47          0x234  // PCFS Divider Start 47 Register
#define MC_CGM_PCFS_DIVC48          0x238  // PCFS Divider Change 48 Register
#define MC_CGM_PCFS_DIVE48          0x23C  // PCFS Divider End 48 Register
#define MC_CGM_PCFS_DIVS48          0x240  // PCFS Divider Start 48 Register
#define MC_CGM_PCFS_DIVC49          0x244  // PCFS Divider Change 49 Register
#define MC_CGM_PCFS_DIVE49          0x248  // PCFS Divider End 49 Register
#define MC_CGM_PCFS_DIVS49          0x24C  // PCFS Divider Start 49 Register
#define MC_CGM_PCFS_DIVC50          0x250  // PCFS Divider Change 50 Register
#define MC_CGM_PCFS_DIVE50          0x254  // PCFS Divider End 50 Register
#define MC_CGM_PCFS_DIVS50          0x258  // PCFS Divider Start 50 Register
#define MC_CGM_PCFS_DIVC51          0x25C  // PCFS Divider Change 51 Register
#define MC_CGM_PCFS_DIVE51          0x260  // PCFS Divider End 51 Register
#define MC_CGM_PCFS_DIVS51          0x264  // PCFS Divider Start 51 Register
#define MC_CGM_PCFS_DIVC52          0x268  // PCFS Divider Change 52 Register
#define MC_CGM_PCFS_DIVE52          0x26C  // PCFS Divider End 52 Register
#define MC_CGM_PCFS_DIVS52          0x270  // PCFS Divider Start 52 Register
#define MC_CGM_PCFS_DIVC53          0x274  // PCFS Divider Change 53 Register
#define MC_CGM_PCFS_DIVE53          0x278  // PCFS Divider End 53 Register
#define MC_CGM_PCFS_DIVS53          0x27C  // PCFS Divider Start 53 Register
#define MC_CGM_PCFS_DIVC54          0x280  // PCFS Divider Change 54 Register
#define MC_CGM_PCFS_DIVE54          0x284  // PCFS Divider End 54 Register
#define MC_CGM_PCFS_DIVS54          0x288  // PCFS Divider Start 54 Register
#define MC_CGM_PCFS_DIVC55          0x28C  // PCFS Divider Change 55 Register
#define MC_CGM_PCFS_DIVE55          0x290  // PCFS Divider End 55 Register
#define MC_CGM_PCFS_DIVS55          0x294  // PCFS Divider Start 55 Register
#define MC_CGM_PCFS_DIVC56          0x298  // PCFS Divider Change 56 Register
#define MC_CGM_PCFS_DIVE56          0x29C  // PCFS Divider End 56 Register
#define MC_CGM_PCFS_DIVS56          0x2A0  // PCFS Divider Start 56 Register
#define MC_CGM_PCFS_DIVC57          0x2A4  // PCFS Divider Change 57 Register
#define MC_CGM_PCFS_DIVE57          0x2A8  // PCFS Divider End 57 Register
#define MC_CGM_PCFS_DIVS57          0x2AC  // PCFS Divider Start 57 Register
#define MC_CGM_PCFS_DIVC58          0x2B0  // PCFS Divider Change 58 Register
#define MC_CGM_PCFS_DIVE58          0x2B4  // PCFS Divider End 58 Register
#define MC_CGM_PCFS_DIVS58          0x2B8  // PCFS Divider Start 58 Register
#define MC_CGM_PCFS_DIVC59          0x2BC  // PCFS Divider Change 59 Register
#define MC_CGM_PCFS_DIVE59          0x2C0  // PCFS Divider End 59 Register
#define MC_CGM_PCFS_DIVS59          0x2C4  // PCFS Divider Start 59 Register
#define MC_CGM_PCFS_DIVC60          0x2C8  // PCFS Divider Change 60 Register
#define MC_CGM_PCFS_DIVE60          0x2CC  // PCFS Divider End 60 Register
#define MC_CGM_PCFS_DIVS60          0x2D0  // PCFS Divider Start 60 Register
#define MC_CGM_PCFS_DIVC61          0x2D4  // PCFS Divider Change 61 Register
#define MC_CGM_PCFS_DIVE61          0x2D8  // PCFS Divider End 61 Register
#define MC_CGM_PCFS_DIVS61          0x2DC  // PCFS Divider Start 61 Register
#define MC_CGM_PCFS_DIVC62          0x2E0  // PCFS Divider Change 62 Register
#define MC_CGM_PCFS_DIVE62          0x2E4  // PCFS Divider End 62 Register
#define MC_CGM_PCFS_DIVS62          0x2E8  // PCFS Divider Start 62 Register
#define MC_CGM_PCFS_DIVC63          0x2EC  // PCFS Divider Change 63 Register
#define MC_CGM_PCFS_DIVE63          0x2F0  // PCFS Divider End 63 Register
#define MC_CGM_PCFS_DIVS63          0x2F4  // PCFS Divider Start 63 Register

#define MC_CGM_MUX_0_CSC            0x300   // Clock Mux 0 Select Control Register
#define MC_CGM_MUX_0_CSS            0x304   // Clock Mux 0 Select Status Register
#define MC_CGM_MUX_0_DC_0           0x308   // Clock Mux 0 Divider 0 Control Register
#define MC_CGM_MUX_0_DC_1           0x30C   // Clock Mux 0 Divider 1 Control Register
#define MC_CGM_MUX_0_DIV_UPD_STAT   0x33C   // Clock Mux 0 Divider Update Status Register
#define MC_CGM_MUX_1_CSC            0x340   // Clock Mux 1 Select Control Register
#define MC_CGM_MUX_1_CSS            0x344   // Clock Mux 1 Select Status Register
#define MC_CGM_MUX_1_DC_0           0x348   // Clock Mux 1 Divider 0 Control Register
#define MC_CGM_MUX_1_DIV_UPD_STAT   0x37C   // Clock Mux 1 Divider Update Status Register
#define MC_CGM_MUX_2_CSC            0x380   // Clock Mux 2 Select Control Register
#define MC_CGM_MUX_2_CSS            0x384   // Clock Mux 2 Select Status Register
#define MC_CGM_MUX_2_DC_0           0x388   // Clock Mux 2 Divider 0 Control Register
#define MC_CGM_MUX_2_DIV_UPD_STAT   0x3BC   // Clock Mux 2 Divider Update Status Register
#define MC_CGM_MUX_3_CSC            0x3C0   // Clock Mux 3 Select Control Register
#define MC_CGM_MUX_3_CSS            0x3C4   // Clock Mux 3 Select Status Register
#define MC_CGM_MUX_3_DC_0           0x3C8   // Clock Mux 3 Divider 0 Control Register
#define MC_CGM_MUX_3_DIV_UPD_STAT   0x3FC   // Clock Mux 3 Divider Update Status Register
#define MC_CGM_MUX_4_CSC            0x400   // Clock Mux 4 Select Control Register
#define MC_CGM_MUX_4_CSS            0x404   // Clock Mux 4 Select Status Register
#define MC_CGM_MUX_4_DC_0           0x408   // Clock Mux 4 Divider 0 Control Register
#define MC_CGM_MUX_4_DIV_UPD_STAT   0x43C   // Clock Mux 4 Divider Update Status Register
#define MC_CGM_MUX_5_CSC            0x440   // Clock Mux 5 Select Control Register
#define MC_CGM_MUX_5_CSS            0x444   // Clock Mux 5 Select Status Register
#define MC_CGM_MUX_5_DC_0           0x448   // Clock Mux 5 Divider 0 Control Register
#define MC_CGM_MUX_5_DIV_UPD_STAT   0x47C   // Clock Mux 5 Divider Update Status Register
#define MC_CGM_MUX_6_CSC            0x480   // Clock Mux 6 Select Control Register
#define MC_CGM_MUX_6_CSS            0x484   // Clock Mux 6 Select Status Register
#define MC_CGM_MUX_6_DC_0           0x488   // Clock Mux 6 Divider 0 Control Register
#define MC_CGM_MUX_6_DIV_UPD_STAT   0x4BC   // Clock Mux 6 Divider Update Status Register
#define MC_CGM_MUX_7_CSC            0x4C0   // Clock Mux 7 Select Control Register
#define MC_CGM_MUX_7_CSS            0x4C4   // Clock Mux 7 Select Status Register
#define MC_CGM_MUX_7_DC_0           0x4C8   // Clock Mux 7 Divider 0 Control Register
#define MC_CGM_MUX_7_DIV_UPD_STAT   0x4FC   // Clock Mux 7 Divider Update Status Register
#define MC_CGM_MUX_8_CSC            0x500   // Clock Mux 8 Select Control Register
#define MC_CGM_MUX_8_CSS            0x504   // Clock Mux 8 Select Status Register
#define MC_CGM_MUX_9_CSC            0x540   // Clock Mux 9 Select Control Register
#define MC_CGM_MUX_9_CSS            0x544   // Clock Mux 9 Select Status Register
#define MC_CGM_MUX_9_DC_0           0x548   // Clock Mux 9 Divider 0 Control Register
#define MC_CGM_MUX_9_DIV_UPD_STAT   0x57C   // Clock Mux 9 Divider Update Status Register
#define MC_CGM_MUX_10_CSC           0x580   // Clock Mux 10 Select Control Register
#define MC_CGM_MUX_10_CSS           0x584   // Clock Mux 10 Select Status Register
#define MC_CGM_MUX_10_DC_0          0x588   // Clock Mux 10 Divider 0 Control Register
#define MC_CGM_MUX_10_DIV_UPD_STAT  0x5BC   // Clock Mux 10 Divider Update Status Register
#define MC_CGM_MUX_11_CSC           0x5C0   // Clock Mux 11 Select Control Register
#define MC_CGM_MUX_11_CSS           0x5C4   // Clock Mux 11 Select Status Register
#define MC_CGM_MUX_12_CSC           0x600   // Clock Mux 12 Select Control Register
#define MC_CGM_MUX_12_CSS           0x604   // Clock Mux 12 Select Status Register
#define MC_CGM_MUX_12_DC_0          0x608   // Clock Mux 12 Divider 0 Control Register
#define MC_CGM_MUX_12_DIV_UPD_STAT  0x63C   // Clock Mux 12 Divider Update Status Register
#define MC_CGM_MUX_14_CSC           0x680   // Clock Mux 14 Select Control Register
#define MC_CGM_MUX_14_CSS           0x684   // Clock Mux 14 Select Status Register
#define MC_CGM_MUX_14_DC_0          0x688   // Clock Mux 14 Divider 0 Control Register
#define MC_CGM_MUX_14_DIV_UPD_STAT  0x6BC   // Clock Mux 14 Divider Update Status Register
#define MC_CGM_MUX_15_CSC           0x6C0   // Clock Mux 15 Select Control Register
#define MC_CGM_MUX_15_CSS           0x6C4   // Clock Mux 15 Select Status Register
#define MC_CGM_MUX_15_DC_0          0x6C8   // Clock Mux 15 Divider 0 Control Register
#define MC_CGM_MUX_15_DIV_UPD_STAT  0x6FC   // Clock Mux 15 Divider Update Status Register
#define MC_CGM_MUX_16_CSC           0x700   // Clock Mux 16 Select Control Register
#define MC_CGM_MUX_16_CSS           0x704   // Clock Mux 16 Select Status Register

// Enum for Clock Sources
typedef enum {
  MC_CGM_CLK_SRC_FIRC = 0,                 // FIRC_CLK
  MC_CGM_CLK_SRC_SIRC = 1,                 // SIRC_CLK
  MC_CGM_CLK_SRC_FXOSC = 2,                // FXOSC_CLK
  MC_CGM_CLK_SRC_CORE_PLL_PHI0 = 4,        // CORE_PLL_PHI0_CLK
  MC_CGM_CLK_SRC_CORE_PLL_PHI1 = 5,        // CORE_PLL_PHI1_CLK
  MC_CGM_CLK_SRC_CORE_DFS1 = 12,           // CORE_DFS1_CLK
  MC_CGM_CLK_SRC_CORE_DFS2 = 13,           // CORE_DFS2_CLK
  MC_CGM_CLK_SRC_CORE_DFS3 = 14,           // CORE_DFS3_CLK
  MC_CGM_CLK_SRC_CORE_DFS4 = 15,           // CORE_DFS4_CLK
  MC_CGM_CLK_SRC_CORE_DFS5 = 16,           // CORE_DFS5_CLK
  MC_CGM_CLK_SRC_CORE_DFS6 = 17,           // CORE_DFS6_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI0 = 18,     // PERIPH_PLL_PHI0_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI1 = 19,     // PERIPH_PLL_PHI1_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI2 = 20,     // PERIPH_PLL_PHI2_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI3 = 21,     // PERIPH_PLL_PHI3_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI4 = 22,     // PERIPH_PLL_PHI4_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI5 = 23,     // PERIPH_PLL_PHI5_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI6 = 24,     // PERIPH_PLL_PHI6_CLK
  MC_CGM_CLK_SRC_PERIPH_PLL_PHI7 = 25,     // PERIPH_PLL_PHI7_CLK
  MC_CGM_CLK_SRC_PERIPH_DFS1 = 26,         // PERIPH_DFS1_CLK
  MC_CGM_CLK_SRC_PERIPH_DFS2 = 27,         // PERIPH_DFS2_CLK
  MC_CGM_CLK_SRC_PERIPH_DFS3 = 28,         // PERIPH_DFS3_CLK
  MC_CGM_CLK_SRC_PERIPH_DFS4 = 29,         // PERIPH_DFS4_CLK
  MC_CGM_CLK_SRC_PERIPH_DFS5 = 30,         // PERIPH_DFS5_CLK
  MC_CGM_CLK_SRC_PERIPH_DFS6 = 31,         // PERIPH_DFS6_CLK
  MC_CGM_CLK_SRC_ACCEL_PLL_PHI0 = 32,      // ACCEL_PLL_PHI0_CLK
  MC_CGM_CLK_SRC_ACCEL_PLL_PHI1 = 33,      // ACCEL_PLL_PHI1_CLK
  MC_CGM_CLK_SRC_FTM0_EXT_CLK = 34,        // FTM0_EXT_CLK
  MC_CGM_CLK_SRC_FTM1_EXT_CLK = 35,        // FTM1_EXT_CLK
  MC_CGM_CLK_SRC_DDR_PLL_PHI0 = 36,        // DDR_PLL_PHI0
  MC_CGM_CLK_SRC_GMAC0_TX = 37,            // GMAC0_TX_CLK
  MC_CGM_CLK_SRC_GMAC0_RX = 38,            // GMAC0_RX_CLK
  MC_CGM_CLK_SRC_GMAC0_RMII_REF = 39,      // GMAC0_RMII_REF_CLK
  MC_CGM_CLK_SRC_SERDES_0_XPCS_0_CDR = 41, // SERDES_0_XPCS_0_CDR_CLK
  MC_CGM_CLK_SRC_GMAC0_TS = 44,            // GMAC0_TS_CLK
  MC_CGM_CLK_SRC_GMAC0_REF_DIV = 45,       // GMAC_0_REF_DIV_CLK
  MC_CGM_CLK_SRC_SERDES_0_XPCS_1_CDR = 47, // SERDES_0_XPCS_1_CDR_CLK (MC_CGM_2 only)
  MC_CGM_CLK_SRC_PFE_MAC0_TX = 48, // PFE_MAC0_TX_CLK
  MC_CGM_CLK_SRC_PFE_MAC0_RX = 49, // PFE_MAC0_RX_CLK
  MC_CGM_CLK_SRC_PFE_MAC0_RMII_REF = 50,   // PFE_MAC0_RMII_REF_CLK
  MC_CGM_CLK_SRC_PFE_MAC1_TX = 51,         // PFE_MAC1_TX_CLK
  MC_CGM_CLK_SRC_PFE_MAC1_RX = 52,         // PFE_MAC1_RX_CLK
  MC_CGM_CLK_SRC_PFE_MAC1_RMII_REF = 53,   // PFE_MAC1_RMII_REF_CLK
  MC_CGM_CLK_SRC_PFE_MAC2_TX = 54,         // PFE_MAC2_TX_CLK
  MC_CGM_CLK_SRC_PFE_MAC2_RX = 55,         // PFE_MAC2_RX_CLK
  MC_CGM_CLK_SRC_PFE_MAC2_RMII_REF = 56,   // PFE_MAC2_RMII_REF_CLK
  MC_CGM_CLK_SRC_SERDES_1_XPCS_0_CDR = 58, // SERDES_1_XPCS_0_CDR_CLK
  MC_CGM_CLK_SRC_PFE_MAC_0_REF_DIV = 59,   // PFE_MAC_0_REF_DIV_CLK
  MC_CGM_CLK_SRC_PFE_MAC_1_REF_DIV = 60,   // PFE_MAC_1_REF_DIV_CLK
  MC_CGM_CLK_SRC_PFE_MAC_2_REF_DIV = 61,   // PFE_MAC_2_REF_DIV_CLK
  MC_CGM_CLK_SRC_SERDES_1_XPCS_1_CDR = 63  // SERDES_1_XPCS_1_CDR_CLK
} MC_CGM_ClockSource;

struct pcfs_div {
    uint32_t divc;
    uint32_t dive;
    uint32_t divs;
};

struct mux_select {
    uint32_t mux_control;
    uint32_t mux_status;
    uint32_t div0_ctrl;
    uint32_t div1_ctrl;
    uint32_t div_update;
};

#define MCG_MAX_PCFS 64
#define MCG_MAX_MUX  17

#define TYPE_S32_CGM "s32.cgm"

OBJECT_DECLARE_SIMPLE_TYPE(S32CGMState, S32_CGM)

struct S32CGMState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t sdur;
    struct pcfs_div pcfs[MCG_MAX_PCFS];
    struct mux_select mux_sel[MCG_MAX_MUX];
    uint32_t mux_def_clk[MCG_MAX_MUX];
};

#endif
