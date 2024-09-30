/*
 * S32 Clock Generation Module
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/s32g_cgm.h"
#include "hw/misc/s32g_dfs.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "target/arm/arm-powerctl.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#if defined(__linux__)
#include <elf.h>
#endif
#include <stdbool.h>
#include "exec/hwaddr.h"

#define DEBUG_S32G_CLOCK 1

#ifndef DEBUG_S32G_CLOCK
#define DEBUG_S32G_CLOCK 0
#endif

#define DPRINTF(tp, fmt, args...)                \
    do { \
        if (DEBUG_S32G_CLOCK) { \
            fprintf(stderr, "[%s]%s: " fmt , tp, \
                                             __func__, ##args); \
        } \
    } while (0)


#define STRINGIFY(x) #x
#define MCG_PROP_MUX(X, SEL, STAT, DIV0, DIV1)                        \
    DEFINE_PROP_UINT32("mux" STRINGIFY(X) "-select", S32CGMState, mux_def_clk[ X ], SEL), \
    DEFINE_PROP_UINT32("mux" STRINGIFY(X) "-status", S32CGMState, mux_sel[ X ].mux_status, STAT), \
    DEFINE_PROP_UINT32("mux" STRINGIFY(X) "-dc0", S32CGMState, mux_sel[ X ].div0_ctrl, DIV0), \
    DEFINE_PROP_UINT32("mux" STRINGIFY(X) "-dc1", S32CGMState, mux_sel[ X ].div1_ctrl, DIV1)

        

typedef enum {
    MUX_AUTOCLEAR_START = 0,
    RAMPUP = MUX_AUTOCLEAR_START,
    RAMPDOWN,
    CLK_SW,
    SAFE_SW,
    MUX_AUTOCLEAR_NO,
} mux_autoclear_bits;

typedef enum {
  PCFS_DIVC,
  PCFS_DIVE,
  PCFS_DIVS,
  PCFS_NO,
} pcfs_reg;

typedef enum {
  MUX_CTRL,
  MUX_STATUS,
  MUX_DIV0,
  MUX_DIV1,
  MUX_DIV_UPD,
  MUX_NO,
} mux_reg;

static pcfs_reg s32_cgm_get_pcfs_reg_type(hwaddr offset)
{
    switch(offset & 0xf) {
    case 0x8:
        return PCFS_DIVC;
    case 0xc:
        return PCFS_DIVE;
    case 0x0:
        return PCFS_DIVS;
    default:
        return PCFS_NO;
    }
}

static mux_reg s32_cgm_get_mux_reg_type(hwaddr offset)
{
    switch(offset & 0x3f) {
    case 0x0:
        return MUX_CTRL;
    case 0x4:
        return MUX_STATUS;
    case 0x8:
        return MUX_DIV0;
    case 0xc:
        return MUX_DIV1;
    case 0x3c:
        return MUX_DIV_UPD;
    default:
        return MUX_NO;
    }
}

static uint32_t* s32_cgm_get_pcfs_register(S32CGMState *s, hwaddr offset)
{
    int idx = 0;
    pcfs_reg reg_type = s32_cgm_get_pcfs_reg_type(offset);
    switch(reg_type) {
    case PCFS_DIVC:
        idx = MC_CGM_OFFSET_CONTROL_TO_DIVIDER_IDX(offset);
        return &s->pcfs[idx].divc;
    case PCFS_DIVE:
        idx = MC_CGM_OFFSET_END_TO_DIVIDER_IDX(offset);
        return &s->pcfs[idx].dive;
    case PCFS_DIVS:
        idx = MC_CGM_OFFSET_START_TO_DIVIDER_IDX(offset);
        return &s->pcfs[idx].divs;
    default:
        return NULL;
    }
}

static int32_t s32_cgm_get_pcfs_register_index(hwaddr offset)
{
    pcfs_reg reg_type = s32_cgm_get_pcfs_reg_type(offset);
    switch(reg_type) {
    case PCFS_DIVC:
        return MC_CGM_OFFSET_CONTROL_TO_DIVIDER_IDX(offset);
    case PCFS_DIVE:
        return MC_CGM_OFFSET_END_TO_DIVIDER_IDX(offset);
    case PCFS_DIVS:
        return MC_CGM_OFFSET_START_TO_DIVIDER_IDX(offset);
    default:
        return -1;
    }
}

static int32_t s32_cgm_get_mux_register_index(hwaddr offset)
{
    mux_reg reg_type = s32_cgm_get_mux_reg_type(offset);
    switch(reg_type) {
    case MUX_CTRL:
        return MC_CGM_OFFSET_CONTROL_TO_MUX_IDX(offset);
    case MUX_STATUS:
        return MC_CGM_OFFSET_STATUS_TO_MUX_IDX(offset);
    case MUX_DIV0:
        return MC_CGM_OFFSET_DIV0_TO_MUX_IDX(offset);
    case MUX_DIV1:
        return MC_CGM_OFFSET_DIV1_TO_MUX_IDX(offset);
    case MUX_DIV_UPD:
        return MC_CGM_OFFSET_UPD_STAT_TO_MUX_IDX(offset);
    default:
        return -1;
    }
}

static uint32_t *s32_cgm_get_mux_register(S32CGMState *s, hwaddr offset)
{
    int idx = 0;
    mux_reg reg_type = s32_cgm_get_mux_reg_type(offset);
    switch(reg_type) {
    case MUX_CTRL:
        idx = MC_CGM_OFFSET_CONTROL_TO_MUX_IDX(offset);
        return &s->mux_sel[idx].mux_control;
    case MUX_STATUS:
        idx = MC_CGM_OFFSET_STATUS_TO_MUX_IDX(offset);
        return &s->mux_sel[idx].mux_status;
    case MUX_DIV0:
        idx = MC_CGM_OFFSET_DIV0_TO_MUX_IDX(offset);
        return &s->mux_sel[idx].div0_ctrl;
    case MUX_DIV1:
        idx = MC_CGM_OFFSET_DIV1_TO_MUX_IDX(offset);
        return &s->mux_sel[idx].div1_ctrl;
    case MUX_DIV_UPD:
        idx = MC_CGM_OFFSET_UPD_STAT_TO_MUX_IDX(offset);
        return &s->mux_sel[idx].div_update;
    default:
        return NULL;
    }
}

static uint64_t s32_dfs_read(void *opaque, hwaddr offset, unsigned size)
{
    S32DFSState *s = (S32DFSState *)opaque;
    uint64_t value = 0;
    
    switch (offset) {
    case DFS_PORTSR:
        value = s->portsr;
        break;
    case DFS_PORTLOLSR:
        value = s->portlolsr;
        break;
    case DFS_PORTRESET:
        value = s->portreset;
        break;
    case DFS_CTL:
        value = s->ctl;
        break;
    case DFS_DVPORT0 ... DFS_DVPORT5:
        value = s->dvport[(offset - DFS_DVPORT0)/4];
        break;
    default:
        DPRINTF(TYPE_S32_DFS, "Invalid Register Access @ offset: 0x%" HWADDR_PRIx " Read\n", offset);
        break;
    }
    DPRINTF(TYPE_S32_DFS, "offset: 0x%" HWADDR_PRIx " Read: 0x%" PRIx64 "\n", offset, value);
    return value;
}

static uint64_t s32_cgm_read(void *opaque, hwaddr offset, unsigned size)
{
    S32CGMState *s = (S32CGMState *)opaque;
    uint64_t value = 0;
    uint32_t *pcfs_reg = NULL;
    uint32_t *mux_reg = NULL;
    
    switch(offset) {
    case MC_CGM_PCFS_SDUR:
        value = s->sdur;
        break;
    case MC_CGM_PCFS_DIVC4 ... MC_CGM_PCFS_DIVS63:
        pcfs_reg = s32_cgm_get_pcfs_register(s, offset);
        if (pcfs_reg)
            value = *pcfs_reg;
        break;
    case MC_CGM_MUX_0_CSC ... MC_CGM_MUX_16_CSS:
        mux_reg = s32_cgm_get_mux_register(s, offset);
        if (mux_reg) {
            value = *mux_reg;
        }
        break;
    default:
        DPRINTF(TYPE_S32_CGM, "Invalid Register Access @ offset: 0x%" HWADDR_PRIx " Read\n", offset);
        break;
    }
    DPRINTF(TYPE_S32_CGM, "offset: 0x%" HWADDR_PRIx " Read: 0x%" PRIx64 "\n", offset, value);
    return value;
}

static void s32_dfs_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    S32DFSState *s = (S32DFSState *)opaque;
    
    switch (offset) {
    case DFS_PORTSR:
        s->portsr = value;
        break;
    case DFS_PORTLOLSR:
        s->portlolsr = value;
        break;
    case DFS_PORTRESET:
        s->portreset = value;
        s->portsr = (~value) & 0x3f;
        break;
    case DFS_CTL:
        s->ctl = value;
        break;
    case DFS_DVPORT0 ... DFS_DVPORT5:
        s->dvport[(offset - DFS_DVPORT0)/4] = value;
        break;
    default:
        DPRINTF(TYPE_S32_DFS, "Invalid Register Access @ offset: 0x%" HWADDR_PRIx " Write\n", offset);
        break;
    }
    DPRINTF(TYPE_S32_DFS, "offset: 0x%" HWADDR_PRIx " Write: 0x%" PRIx64 "\n", offset, value);
}

static void s32_cgm_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    S32CGMState *s = (S32CGMState *)opaque;
    uint32_t *pcfs_reg = NULL;
    uint32_t *mux_reg = NULL;
    int idx = -1;
    uint32_t *status = NULL;
    
    switch(offset){
    case MC_CGM_PCFS_SDUR:
        s->sdur = value;
        break;
    case MC_CGM_PCFS_DIVC4 ... MC_CGM_PCFS_DIVS63:
        pcfs_reg = s32_cgm_get_pcfs_register(s, offset);
        if (pcfs_reg)
            *pcfs_reg = value;
        break;
    case MC_CGM_MUX_0_CSC ... MC_CGM_MUX_16_CSS:
        mux_reg = s32_cgm_get_mux_register(s, offset);
        if (mux_reg) {
            switch (s32_cgm_get_mux_reg_type(offset)) {
            case MUX_CTRL:
                idx = s32_cgm_get_mux_register_index(offset);
                status = &s->mux_sel[idx].mux_status;
                for (int i = MUX_AUTOCLEAR_START; i < MUX_AUTOCLEAR_NO; ++i) {
                    // if the bits are set in the control register, clear them and set the status bits
                    if (value & (1 << i)) {
                        value &= ~(1 << i);
                        *status |= (1 << i);
                    }   
                }
                // set switch after request succeeded
                *status &= ~(0x7 << 17);
                *status |= ((1 << 17));
                // mark switching is complete
                *status &= ~(1 << 16);
                *status &= ~(0x3f << 24);
                *status |= (value & (0x3f << 24));
                *status &= ~(3 | (0xfff << 4) | (0xf << 20) | (0x3 << 30));
                break;
            default:
                break;
            }
            *mux_reg = value;
        }
        break;
    default:
        DPRINTF(TYPE_S32_CGM, "Invalid Register Access @ offset: 0x%" HWADDR_PRIx " Write: 0x%" PRIx64 "\n", offset, value);
        break;
    }
    DPRINTF(TYPE_S32_CGM, "offset: 0x%" HWADDR_PRIx " Write: 0x%" PRIx64 "\n", offset, value);
}


static const struct MemoryRegionOps s32_cgm_ops = {
    .read = s32_cgm_read,
    .write = s32_cgm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static const struct MemoryRegionOps s32_dfs_ops = {
    .read = s32_dfs_read,
    .write = s32_dfs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void s32_cgm_realize(DeviceState *dev, Error **errp)
{
    S32CGMState *s = S32_CGM(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &s32_cgm_ops, s,
                          TYPE_S32_CGM, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void s32_dfs_realize(DeviceState *dev, Error **errp)
{
    S32DFSState *s = S32_DFS(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &s32_dfs_ops, s,
                          TYPE_S32_DFS, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void s32_cgm_reset(DeviceState *dev)
{
    S32CGMState *s = S32_CGM(dev);
    s->sdur = 0;
    for (int i = 0; i < MCG_MAX_PCFS; ++i) {
        s->pcfs[i].divc = 0;
        s->pcfs[i].dive = 0x3E7;
        s->pcfs[i].divs = 0x3E7;
    }

    for (int i = 0; i < MCG_MAX_MUX; ++i) {
        s->mux_sel[i].div_update = 0;
    }
}

static void s32_dfs_reset(DeviceState *dev)
{
    S32DFSState *s = S32_DFS(dev);
    
    s->portsr = 0;
    s->portlolsr = 0;
    s->portreset = 0x3f;
    s->ctl = 2;
    memset(s->dvport, 0, sizeof(s->dvport));
}

static const VMStateDescription vmstate_s32_cgm = {
    .name = TYPE_S32_CGM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(sdur, S32CGMState),
        /* VMSTATE_UINT32_ARRAY(pcfs, S32CGMState, MCG_MAX_PCFS), */
        /* VMSTATE_UINT32_ARRAY(mux_sel, S32CGMState, MCG_MAX_PCFS), */
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_s32_dfs = {
    .name = TYPE_S32_CGM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(portsr, S32DFSState),
        VMSTATE_UINT32(portlolsr, S32DFSState),
        VMSTATE_UINT32(portreset, S32DFSState),
        VMSTATE_UINT32(ctl, S32DFSState),
        VMSTATE_UINT32_ARRAY(dvport, S32DFSState, DFS_NUM_PORTS),
        VMSTATE_END_OF_LIST()
    },
};

static Property cgm_properties[] = {
    MCG_PROP_MUX(0, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0x80050000),
    MCG_PROP_MUX(1, MC_CGM_CLK_SRC_FXOSC, 0x02020000, 0, 0),
    MCG_PROP_MUX(2, MC_CGM_CLK_SRC_FXOSC, 0x02020000, 0, 0),
    MCG_PROP_MUX(3, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(4, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(5, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(6, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(7, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(8, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(9, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(10, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(11, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(12, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(13, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(14, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(15, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    MCG_PROP_MUX(16, MC_CGM_CLK_SRC_FIRC, 0x00080000, 0, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static Property dfs_properties[] = {
    DEFINE_PROP_UINT32("no-dividers", S32DFSState, no_divs , 6),
    DEFINE_PROP_END_OF_LIST(),
};

static void s32_cgm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32_cgm_realize;
    dc->reset = s32_cgm_reset;
    dc->vmsd = &vmstate_s32_cgm;
    dc->desc = "S32 Clock Generation Module";
    device_class_set_props(dc, cgm_properties);
}

static void s32_dfs_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32_dfs_realize;
    dc->reset = s32_dfs_reset;
    dc->vmsd = &vmstate_s32_dfs;
    dc->desc = "S32 Digital Frequency Synthesizer";
    device_class_set_props(dc, dfs_properties);
}

static const TypeInfo s32_cgm_info = {
    .name          = TYPE_S32_CGM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32CGMState),
    .class_init    = s32_cgm_class_init,
};

static const TypeInfo s32_dfs_info = {
    .name          = TYPE_S32_DFS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32DFSState),
    .class_init    = s32_dfs_class_init,
};

static void s32_cgm_register_types(void)
{
    type_register_static(&s32_cgm_info);
}

static void s32_dfs_register_types(void)
{
    type_register_static(&s32_dfs_info);
}

type_init(s32_cgm_register_types)
type_init(s32_dfs_register_types)
