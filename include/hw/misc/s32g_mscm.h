/*
 * S32 Miscellaneous System Control Module
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_MSCM_H
#define S32G_MSCM_H

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

typedef enum {
    MSCM_REGION_CPX = 0,
    MSCM_REGION_CPN,
    MSCM_REGION_IRCP,
    MSCM_REGION_IRSPRC,
    MSCM_REGION_NO,
} mscm_region_t;

// Processor X registers
#define MSCM_CPXTYPE_OFFSET      0x0000  // Processor X Type
#define MSCM_CPXNUM_OFFSET       0x0004  // Processor X Number
#define MSCM_CPXREV_OFFSET       0x0008  // Processor X Revision
#define MSCM_CPXCFG0_OFFSET      0x000C  // Processor X Configuration 0
#define MSCM_CPXCFG1_OFFSET      0x0010  // Processor X Configuration 1
#define MSCM_CPXCFG2_OFFSET      0x0014  // Processor X Configuration 2
#define MSCM_CPXCFG3_OFFSET      0x0018  // Processor X Configuration 3

// Processor 0 registers
#define MSCM_CP0TYPE_OFFSET      0x0020  // Processor 0 Type
#define MSCM_CP0NUM_OFFSET       0x0024  // Processor 0 Number
#define MSCM_CP0REV_OFFSET       0x0028  // Processor 0 Count
#define MSCM_CP0CFG0_OFFSET      0x002C  // Processor 0 Configuration 0
#define MSCM_CP0CFG1_OFFSET      0x0030  // Processor 0 Configuration 1
#define MSCM_CP0CFG2_OFFSET      0x0034  // Processor 0 Configuration 2
#define MSCM_CP0CFG3_OFFSET      0x0038  // Processor 0 Configuration 3

// Processor 1 registers
#define MSCM_CP1TYPE_OFFSET      0x0040  // Processor 1 Type
#define MSCM_CP1NUM_OFFSET       0x0044  // Processor 1 Number
#define MSCM_CP1REV_OFFSET       0x0048  // Processor 1 Count
#define MSCM_CP1CFG0_OFFSET      0x004C  // Processor 1 Configuration 0
#define MSCM_CP1CFG1_OFFSET      0x0050  // Processor 1 Configuration 1
#define MSCM_CP1CFG2_OFFSET      0x0054  // Processor 1 Configuration 2
#define MSCM_CP1CFG3_OFFSET      0x0058  // Processor 1 Configuration 3

// Processor 2 registers
#define MSCM_CP2TYPE_OFFSET      0x0060  // Processor 2 Type
#define MSCM_CP2NUM_OFFSET       0x0064  // Processor 2 Number
#define MSCM_CP2REV_OFFSET       0x0068  // Processor 2 Count
#define MSCM_CP2CFG0_OFFSET      0x006C  // Processor 2 Configuration 0
#define MSCM_CP2CFG1_OFFSET      0x0070  // Processor 2 Configuration 1
#define MSCM_CP2CFG2_OFFSET      0x0074  // Processor 2 Configuration 2
#define MSCM_CP2CFG3_OFFSET      0x0078  // Processor 2 Configuration 3

// Processor 3 registers
#define MSCM_CP3TYPE_OFFSET      0x0080  // Processor 3 Type
#define MSCM_CP3NUM_OFFSET       0x0084  // Processor 3 Number
#define MSCM_CP3REV_OFFSET       0x0088  // Processor 3 Count
#define MSCM_CP3CFG0_OFFSET      0x008C  // Processor 3 Configuration 0
#define MSCM_CP3CFG1_OFFSET      0x0090  // Processor 3 Configuration 1
#define MSCM_CP3CFG2_OFFSET      0x0094  // Processor 3 Configuration 2
#define MSCM_CP3CFG3_OFFSET      0x0098  // Processor 3 Configuration 3

// Processor 4 registers
#define MSCM_CP4TYPE_OFFSET      0x00A0  // Processor 4 Type
#define MSCM_CP4NUM_OFFSET       0x00A4  // Processor 4 Number
#define MSCM_CP4REV_OFFSET       0x00A8  // Processor 4 Count
#define MSCM_CP4CFG0_OFFSET      0x00AC  // Processor 4 Configuration 0
#define MSCM_CP4CFG1_OFFSET      0x00B0  // Processor 4 Configuration 1
#define MSCM_CP4CFG2_OFFSET      0x00B4  // Processor 4 Configuration 2
#define MSCM_CP4CFG3_OFFSET      0x00B8  // Processor 4 Configuration 3

// Processor 5 registers
#define MSCM_CP5TYPE_OFFSET      0x00C0  // Processor 5 Type
#define MSCM_CP5NUM_OFFSET       0x00C4  // Processor 5 Number
#define MSCM_CP5REV_OFFSET       0x00C8  // Processor 5 Count
#define MSCM_CP5CFG0_OFFSET      0x00CC  // Processor 5 Configuration 0
#define MSCM_CP5CFG1_OFFSET      0x00D0  // Processor 5 Configuration 1
#define MSCM_CP5CFG2_OFFSET      0x00D4  // Processor 5 Configuration 2
#define MSCM_CP5CFG3_OFFSET      0x00D8  // Processor 5 Configuration 3

// Processor 6 registers
#define MSCM_CP6TYPE_OFFSET      0x00E0  // Processor 6 Type
#define MSCM_CP6NUM_OFFSET       0x00E4  // Processor 6 Number
#define MSCM_CP6REV_OFFSET       0x00E8  // Processor 6 Count
#define MSCM_CP6CFG0_OFFSET      0x00EC  // Processor 6 Configuration 0
#define MSCM_CP6CFG1_OFFSET      0x00F0  // Processor 6 Configuration 1
#define MSCM_CP6CFG2_OFFSET      0x00F4  // Processor 6 Configuration 2
#define MSCM_CP6CFG3_OFFSET      0x00F8  // Processor 6 Configuration 3

// Interrupt Router registers
#define MSCM_IRCP0ISR0_OFFSET    0x0200  // CP0 Interrupt 0 Status
#define MSCM_IRCP0IGR0_OFFSET    0x0204  // CP0 Interrupt 0 Generation
#define MSCM_IRCP0ISR1_OFFSET    0x0208  // CP0 Interrupt 1 Status
#define MSCM_IRCP0IGR1_OFFSET    0x020C  // CP0 Interrupt 1 Generation
#define MSCM_IRCP0ISR2_OFFSET    0x0210  // CP0 Interrupt 2 Status
#define MSCM_IRCP0IGR2_OFFSET    0x0214  // CP0 Interrupt 2 Generation
#define MSCM_IRCP0ISR3_OFFSET    0x0218  // CP0 Interrupt 3 Status
#define MSCM_IRCP0IGR3_OFFSET    0x021C  // CP0 Interrupt 3 Generation

#define MSCM_IRCP1ISR0_OFFSET    0x0220  // CP1 Interrupt 0 Status
#define MSCM_IRCP1IGR0_OFFSET    0x0224  // CP1 Interrupt 0 Generation
#define MSCM_IRCP1ISR1_OFFSET    0x0228  // CP1 Interrupt 1 Status
#define MSCM_IRCP1IGR1_OFFSET    0x022C  // CP1 Interrupt 1 Generation
#define MSCM_IRCP1ISR2_OFFSET    0x0230  // CP1 Interrupt 2 Status
#define MSCM_IRCP1IGR2_OFFSET    0x0234  // CP1 Interrupt 2 Generation
#define MSCM_IRCP1ISR3_OFFSET    0x0238  // CP1 Interrupt 3 Status
#define MSCM_IRCP1IGR3_OFFSET    0x023C  // CP1 Interrupt 3 Generation

#define MSCM_IRCP2ISR0_OFFSET    0x0240  // CP2 Interrupt 0 Status
#define MSCM_IRCP2IGR0_OFFSET    0x0244  // CP2 Interrupt 0 Generation
#define MSCM_IRCP2ISR1_OFFSET    0x0248  // CP2 Interrupt 1 Status
#define MSCM_IRCP2IGR1_OFFSET    0x024C  // CP2 Interrupt 1 Generation
#define MSCM_IRCP2ISR2_OFFSET    0x0250  // CP2 Interrupt 2 Status
#define MSCM_IRCP2IGR2_OFFSET    0x0254  // CP2 Interrupt 2 Generation
#define MSCM_IRCP2ISR3_OFFSET    0x0258  // CP2 Interrupt 3 Status
#define MSCM_IRCP2IGR3_OFFSET    0x025C  // CP2 Interrupt 3 Generation

#define MSCM_IRCP3ISR0_OFFSET    0x0260  // CP3 Interrupt 0 Status
#define MSCM_IRCP3IGR0_OFFSET    0x0264  // CP3 Interrupt 0 Generation
#define MSCM_IRCP3ISR1_OFFSET    0x0268  // CP3 Interrupt 1 Status
#define MSCM_IRCP3IGR1_OFFSET    0x026C  // CP3 Interrupt 1 Generation
#define MSCM_IRCP3ISR2_OFFSET    0x0270  // CP3 Interrupt 2 Status
#define MSCM_IRCP3IGR2_OFFSET    0x0274  // CP3 Interrupt 2 Generation
#define MSCM_IRCP3ISR3_OFFSET    0x0278  // CP3 Interrupt 3 Status
#define MSCM_IRCP3IGR3_OFFSET    0x027C  // CP3 Interrupt 3 Generation

#define MSCM_IRCP4ISR0_OFFSET    0x0280  // CP4 Interrupt 0 Status
#define MSCM_IRCP4IGR0_OFFSET    0x0284  // CP4 Interrupt 0 Generation
#define MSCM_IRCP4ISR1_OFFSET    0x0288  // CP4 Interrupt 1 Status
#define MSCM_IRCP4IGR1_OFFSET    0x028C  // CP4 Interrupt 1 Generation
#define MSCM_IRCP4ISR2_OFFSET    0x0290  // CP4 Interrupt 2 Status
#define MSCM_IRCP4IGR2_OFFSET    0x0294  // CP4 Interrupt 2 Generation
#define MSCM_IRCP4ISR3_OFFSET    0x0298  // CP4 Interrupt 3 Status
#define MSCM_IRCP4IGR3_OFFSET    0x029C  // CP4 Interrupt 3 Generation

#define MSCM_IRCP5ISR0_OFFSET    0x02A0  // CP5 Interrupt 0 Status
#define MSCM_IRCP5IGR0_OFFSET    0x02A4  // CP5 Interrupt 0 Generation
#define MSCM_IRCP5ISR1_OFFSET    0x02A8  // CP5 Interrupt 1 Status
#define MSCM_IRCP5IGR1_OFFSET    0x02AC  // CP5 Interrupt 1 Generation
#define MSCM_IRCP5ISR2_OFFSET    0x02B0  // CP5 Interrupt 2 Status
#define MSCM_IRCP5IGR2_OFFSET    0x02B4  // CP5 Interrupt 2 Generation
#define MSCM_IRCP5ISR3_OFFSET    0x02B8  // CP5 Interrupt 3 Status
#define MSCM_IRCP5IGR3_OFFSET    0x02BC  // CP5 Interrupt 3 Generation

#define MSCM_IRCP6ISR0_OFFSET    0x02C0  // CP6 Interrupt 0 Status
#define MSCM_IRCP6IGR0_OFFSET    0x02C4  // CP6 Interrupt 0 Generation
#define MSCM_IRCP6ISR1_OFFSET    0x02C8  // CP6 Interrupt 1 Status
#define MSCM_IRCP6IGR1_OFFSET    0x02CC  // CP6 Interrupt 1 Generation
#define MSCM_IRCP6ISR2_OFFSET    0x02D0  // CP6 Interrupt 2 Status
#define MSCM_IRCP6IGR2_OFFSET    0x02D4  // CP6 Interrupt 2 Generation
#define MSCM_IRCP6ISR3_OFFSET    0x02D8  // CP6 Interrupt 3 Status
#define MSCM_IRCP6IGR3_OFFSET    0x02DC  // CP6 Interrupt 3 Generation

#define MSCM_IRCPCFG_OFFSET      0x400   // Interrupt Router Configuration Register (IRCPCFG)
#define MSCM_IRNMIC_OFFSET       0x800   // Interrupt Router Non-Maskable Interrupt Control Register (IRNMIC)

// 16-bit per entry
#define MSCM_IRSPRC_START_OFFSET 0x880   // 880h Interrupt Router Shared Peripheral Routing Control Register (IRSPRC0)
#define MSCM_IRSPRC_END_OFFSET   0xA5E   // A5Eh Interrupt Router Shared Peripheral Routing Control Register (IRSPRC239)

#define MSCM_REG_MAX    ((MSCM_IRSPRC_END_OFFSET / 4) + 1)

#define TYPE_S32_MSCM "s32.mscm"
OBJECT_DECLARE_SIMPLE_TYPE(S32MSCMState, S32_MSCM)

#define MSCM_NUM_CORES     3
#define MSCM_NUM_IRQ_PER_CORE 5

struct MSCM_IRQs {
    qemu_irq irq[MSCM_NUM_IRQ_PER_CORE];
};

struct S32MSCMState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

    uint32_t num_app_cores;
    uint32_t num_rt_cores;
    uint32_t irq_per_core;
    uint32_t regs[MSCM_REG_MAX];
    struct MSCM_IRQs msi[MSCM_NUM_CORES];
};

#endif /* S32G_MSCM_H */
