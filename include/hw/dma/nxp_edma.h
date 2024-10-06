/*
 * NXP Enhanced Direct Memory Access (eDMA)
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef NXP_EDMA_H
#define NXP_EDMA_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/register.h"
#include "stdint.h"

#define NXP_EDMA_MAX_NCHANS 32
#define NXP_EDMA_NUM_MG_REGS 36
#define NXP_EDMA_NUM_TCG_REGS 20

#define TYPE_NXP_EDMA "nxp.edma"
#define TYPE_NXP_EDMA_TCD "nxp.edma.tcd"

OBJECT_DECLARE_SIMPLE_TYPE(NXPEDMAState, NXP_EDMA)

OBJECT_DECLARE_SIMPLE_TYPE(NXPEDMATCDState, NXP_EDMA_TCD)

typedef struct {
    int chan_no;
    struct NXPEDMATCDState *s;
    RegisterInfoArray *reg_array;
    uint32_t regs[NXP_EDMA_NUM_TCG_REGS];
    RegisterInfo regs_info[NXP_EDMA_NUM_TCG_REGS];
    uint16_t tcd_csr;
    uint16_t tcd_doff;
    uint16_t tcd_attr;
    uint16_t tcd_soff;
    uint16_t tcd_biter_elink;
    uint16_t tcd_citer_elink;
} EdmaTCGRegisterInfo;

struct NXPEDMAState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion *dma_mr;
    AddressSpace dma_as;

    RegisterInfoArray *reg_array;
    uint32_t regs[NXP_EDMA_NUM_MG_REGS];
    RegisterInfo regs_info[NXP_EDMA_NUM_MG_REGS];
    uint32_t number_channels;
};

struct NXPEDMATCDState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    NXPEDMAState *dma_mg;
    uint32_t number_channels;
    uint32_t sbr_reset;
    EdmaTCGRegisterInfo regs[NXP_EDMA_MAX_NCHANS];
};

#endif
