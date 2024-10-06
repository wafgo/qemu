/*
 * NXP Enhanced Direct Memory Access (eDMA)
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */


#include "qemu/osdep.h"
#include "hw/dma/nxp_edma.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/register.h"
#include "hw/registerfields.h"

#define DEBUG_NXP_EDMA_MG 0

#ifndef DEBUG_NXP_EDMA_MG
#define DEBUG_NXP_EDMA_MG 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_NXP_EDMA_MG) { \
            fprintf(stderr, "[%s] %s: " fmt , TYPE_NXP_EDMA_TCD, \
                                             __func__, ##args); \
        } \
    } while (0)

REG32(CSR, 0x0)
    FIELD(CSR, EDBG, 1, 1)
    FIELD(CSR, ERCA, 2, 1)
    FIELD(CSR, HAE, 4, 1)
    FIELD(CSR, HALT, 5, 1)
    FIELD(CSR, GCLC, 6, 1)
    FIELD(CSR, GMRC, 7, 1)
    FIELD(CSR, ECX, 8, 1)
    FIELD(CSR, CX, 9, 1)
    FIELD(CSR, ACTIVE_ID, 24, 5)
    FIELD(CSR, ACTIVE, 31, 1)
    
REG32(ES, 0x4)
    FIELD(ES, DBE, 0, 1)
    FIELD(ES, SBE, 1, 1)
    FIELD(ES, SGE, 2, 1)
    FIELD(ES, NCE, 3, 1)
    FIELD(ES, DOE, 4, 1)
    FIELD(ES, DAE, 5, 1)
    FIELD(ES, SOE, 6, 1)
    FIELD(ES, SAE, 7, 1)
    FIELD(ES, ECX, 8, 1)
    FIELD(ES, UCE, 9, 1)
    FIELD(ES, ERRCHN, 24, 5)
    FIELD(ES, VLD, 31, 1)

REG32(INT, 0x8)

REG32(HRS, 0x8)

REG32(CHX_GRPRI, 0x100)
    FIELD(CHX_GRPRI, GRPRI, 0, 5)

REG32(CH_CSR, 0x0)
    FIELD(CH_CSR, ERQ, 0, 1)
    FIELD(CH_CSR, EARQ, 1, 1)
    FIELD(CH_CSR, EEI, 2, 1)
    FIELD(CH_CSR, EBW, 3, 1)
    FIELD(CH_CSR, DONE, 30, 1)
    FIELD(CH_CSR, ACTIVE, 31, 1)


REG32(CH_ES, 0x4)
   FIELD(CH_ES, DBE, 0, 1)
   FIELD(CH_ES, SBE, 1, 1)
   FIELD(CH_ES, SGE, 2, 1)
   FIELD(CH_ES, NCE, 3, 1)
   FIELD(CH_ES, DOE, 4, 1)
   FIELD(CH_ES, DAE, 5, 1)
   FIELD(CH_ES, SOE, 6, 1)
   FIELD(CH_ES, SAE, 7, 1)
   FIELD(CH_ES, ERR, 31, 1)  


REG32(CH_INT, 0x8)
    FIELD(CH_INT, INT, 0, 1)
    
REG32(CH_SBR, 0xC)
    FIELD(CH_SBR, MID, 0, 6)
    FIELD(CH_SBR, PAL, 15, 1)
    FIELD(CH_SBR, EMI, 16, 1)
    FIELD(CH_SBR, ATTR, 17, 3)
    
REG32(CH_PRI, 0x10)
    FIELD(CH_PRI, APL, 0, 3)
    FIELD(CH_PRI, DPA, 30, 1)
    FIELD(CH_PRI, ECP, 31, 1)

REG32(TCD_SADDR, 0x20)
REG16(TCD_SOFF, 0x24)
REG16(TCD_ATTR, 0x26)
    FIELD(TCD_ATTR, DSIZE, 0, 3)
    FIELD(TCD_ATTR, DMOD, 3, 5)
    FIELD(TCD_ATTR, SSIZE, 8, 3)
    FIELD(TCD_ATTR, SMOD, 11, 5)    

REG32(TCD_NBYTES_MLOFFNO, 0x28)
    FIELD(TCD_NBYTES_MLOFFNO, NBYTES, 0, 30)
    FIELD(TCD_NBYTES_MLOFFNO, DMLOE, 30, 1)
    FIELD(TCD_NBYTES_MLOFFNO, SMLOE, 31, 1)

REG32(TCD_NBYTES_MLOFFYES, 0x28)
    FIELD(TCD_NBYTES_MLOFFYES, NBYTES, 0, 10)
    FIELD(TCD_NBYTES_MLOFFYES, MLOFF, 10, 20)
    FIELD(TCD_NBYTES_MLOFFYES, DMLOE, 30, 1)
    FIELD(TCD_NBYTES_MLOFFYES, SMLOE, 31, 1)
        
REG32(TCD_SLAST_SDA, 0x2C)
   
REG32(TCD_DADDR, 0x30)
REG16(TCD_DOFF, 0x34)
REG16(TCD_CITER_ELINKNO, 0x36)
   FIELD(TCD_CITER_ELINKNO, CITER, 0, 15)
   FIELD(TCD_CITER_ELINKNO, ELINK, 15, 1)
    
REG16(TCD_CITER_ELINKYES, 0x36)
   FIELD(TCD_CITER_ELINKYES, CITER, 0, 9)
   FIELD(TCD_CITER_ELINKYES, LINKCH, 9, 5)
   FIELD(TCD_CITER_ELINKYES, ELINK, 15, 1)
   
REG32(TCD_DLAST_SDA, 0x38)

REG16(TCD_CSR, 0x3C)
   FIELD(TCD_CSR, START, 0, 1)
   FIELD(TCD_CSR, INTMAJOR, 1, 1)
   FIELD(TCD_CSR, INTHALF, 2, 1)
   FIELD(TCD_CSR, DREQ, 3, 1)
   FIELD(TCD_CSR, ESG, 4, 1)
   FIELD(TCD_CSR, MAJORRELINK, 5, 1)
   FIELD(TCD_CSR, ESDA, 7, 1)
   FIELD(TCD_CSR, MAJORLINKCH, 8, 5)
   FIELD(TCD_CSR, BWC, 14, 2)   
    
REG16(TCD_BITER_ELINKNO, 0x3E)
   FIELD(TCD_BITER_ELINKNO, BITER, 0, 15)
   FIELD(TCD_BITER_ELINKNO, ELINK, 15, 1)
    
REG16(TCD_BITER_ELINKYES, 0x3E)
   FIELD(TCD_BITER_ELINKYES, BITER, 0, 9)
   FIELD(TCD_BITER_ELINKYES, LINKCH, 9, 5)
   FIELD(TCD_BITER_ELINKYES, ELINK, 15, 1)

#define MK_CHX_GRPRI(N)                                                 \
    {.name = "CH" #N "_GRPRI", .addr = A_CHX_GRPRI + (N * 4), .rsvd = 0xffffffe0}

#define MK_ALL_GPRI \
        MK_CHX_GRPRI(0),                            \
        MK_CHX_GRPRI(1),                        \
        MK_CHX_GRPRI(2),                        \
        MK_CHX_GRPRI(3),                        \
        MK_CHX_GRPRI(4),                        \
        MK_CHX_GRPRI(5),                        \
        MK_CHX_GRPRI(6),                        \
        MK_CHX_GRPRI(7),                        \
        MK_CHX_GRPRI(8),                        \
        MK_CHX_GRPRI(9),                        \
        MK_CHX_GRPRI(10),                        \
        MK_CHX_GRPRI(11),                        \
        MK_CHX_GRPRI(12),                        \
        MK_CHX_GRPRI(13),                        \
        MK_CHX_GRPRI(14),                        \
        MK_CHX_GRPRI(15),                        \
        MK_CHX_GRPRI(16),                        \
        MK_CHX_GRPRI(17),                        \
        MK_CHX_GRPRI(18),                        \
        MK_CHX_GRPRI(19),                        \
        MK_CHX_GRPRI(20),                        \
        MK_CHX_GRPRI(21),                        \
        MK_CHX_GRPRI(22),                        \
        MK_CHX_GRPRI(23),                        \
        MK_CHX_GRPRI(24),                        \
        MK_CHX_GRPRI(25),                        \
        MK_CHX_GRPRI(26),                        \
        MK_CHX_GRPRI(27),                        \
        MK_CHX_GRPRI(28),                        \
        MK_CHX_GRPRI(29),                        \
        MK_CHX_GRPRI(30),                        \
        MK_CHX_GRPRI(31)

#define MK_TCD_DESCRIPTOR(N) \
    { .name = "CH_CSR",  .addr = A_CH_CSR + (N * 0x1000),  .ro = 0xbffffff0, .w1c = R_CH_CSR_DONE_MASK},\
    { .name = "CH_ES",  .addr = A_CH_ES + (N * 0x1000),  .ro = 0x7fffffff, .w1c = R_CH_ES_ERR_MASK},\
    { .name = "CH_INT",  .addr = A_CH_INT + (N * 0x1000), .ro = 0xfffffffe, .w1c = R_CH_INT_INT_MASK},\
    { .name = "CH_SBR",  .addr = A_CH_SBR + (N * 0x1000),  .ro = 0xfff0ffff, .reset = 0x00008006}, \
    { .name = "CH_PRI",  .addr = A_CH_PRI + (N * 0x1000),  .rsvd = 0x3ffffff8},\
    { .name = "TCD_SADDR",  .addr = A_TCD_SADDR + (N * 0x1000)},\
    { .name = "TCD_SOFF",  .addr = A_TCD_SOFF + (N * 0x1000)},\
    { .name = "TCD_ATTR",  .addr = A_TCD_ATTR + (N * 0x1000)},\
    { .name = "TCD_NBYTES_MLOFFNO",  .addr = A_TCD_NBYTES_MLOFFNO + (N * 0x1000)},\
    { .name = "TCD_NBYTES_MLOFFYE",  .addr = A_TCD_NBYTES_MLOFFYES + (N * 0x1000)},\
    { .name = "TCD_SLAST_SDA",  .addr = A_TCD_SLAST_SDA + (N * 0x1000)},\
    { .name = "TCD_DADDR",  .addr = A_TCD_DADDR + (N * 0x1000)},\
    { .name = "TCD_DOFF",  .addr = A_TCD_DOFF + (N * 0x1000)},\
    { .name = "TCD_CITER_ELINKNO",  .addr = A_TCD_CITER_ELINKNO + (N * 0x1000)},\
    { .name = "TCD_CITER_ELINKYES",  .addr = A_TCD_CITER_ELINKYES + (N * 0x1000)},\
    { .name = "TCD_DLAST_SDA",  .addr = A_TCD_DLAST_SDA + (N * 0x1000)},\
    { .name = "TCD_CSR",  .addr = A_TCD_CSR + (N * 0x1000), .ro = 0x2000, .rsvd = 0x0040},\
    { .name = "TCD_BITER_ELINKNO",  .addr = A_TCD_BITER_ELINKNO + (N * 0x1000)},\
    { .name = "TCD_BITER_ELINKYES",  .addr = A_TCD_BITER_ELINKYES + (N * 0x1000)}

   
static const RegisterAccessInfo edma_mg_regs_info[] = {
    { .name = "CSR",  .addr = A_CSR, .reset = 0x300000, .rsvd = 0x60fffc08},
    { .name = "ES",  .addr = A_ES, .ro = 0xffffffff, .rsvd = 0x60000000},
    { .name = "INT",  .addr = A_INT, .ro =  0xffffffff },
    { .name = "HRS",  .addr = A_HRS, .ro = 0xffffffff },
    MK_ALL_GPRI
};

static const RegisterAccessInfo edma_tcd_regs_info[] = {
    MK_TCD_DESCRIPTOR(0),
    /* MK_TCD_DESCRIPTOR(1), */
    /* MK_TCD_DESCRIPTOR(2), */
    /* MK_TCD_DESCRIPTOR(3), */
    /* MK_TCD_DESCRIPTOR(4), */
    /* MK_TCD_DESCRIPTOR(5), */
    /* MK_TCD_DESCRIPTOR(6), */
    /* MK_TCD_DESCRIPTOR(7), */
    /* MK_TCD_DESCRIPTOR(8), */
    /* MK_TCD_DESCRIPTOR(9), */
    /* MK_TCD_DESCRIPTOR(10), */
    /* MK_TCD_DESCRIPTOR(11), */
    /* MK_TCD_DESCRIPTOR(12), */
    /* MK_TCD_DESCRIPTOR(13), */
    /* MK_TCD_DESCRIPTOR(14), */
    /* MK_TCD_DESCRIPTOR(15), */
    /* MK_TCD_DESCRIPTOR(16), */
    /* MK_TCD_DESCRIPTOR(17), */
    /* MK_TCD_DESCRIPTOR(18), */
    /* MK_TCD_DESCRIPTOR(19), */
    /* MK_TCD_DESCRIPTOR(20), */
    /* MK_TCD_DESCRIPTOR(21), */
    /* MK_TCD_DESCRIPTOR(22), */
    /* MK_TCD_DESCRIPTOR(23), */
    /* MK_TCD_DESCRIPTOR(24), */
    /* MK_TCD_DESCRIPTOR(25), */
    /* MK_TCD_DESCRIPTOR(26), */
    /* MK_TCD_DESCRIPTOR(27), */
    /* MK_TCD_DESCRIPTOR(28), */
    /* MK_TCD_DESCRIPTOR(29), */
    /* MK_TCD_DESCRIPTOR(30), */
    /* MK_TCD_DESCRIPTOR(31), */
};

static const MemoryRegionOps nxp_edma_mg_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void edma_init(Object *obj)
{
    NXPEDMAState *s = NXP_EDMA(obj);
    
    s->reg_array = register_init_block32(DEVICE(obj), edma_mg_regs_info,
                              ARRAY_SIZE(edma_mg_regs_info),
                              s->regs_info, s->regs,
                              &nxp_edma_mg_ops,
                              DEBUG_NXP_EDMA_MG,
                              NXP_EDMA_NUM_MG_REGS * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->reg_array->mem);
}

static const VMStateDescription vmstate_edma_mg = {
    .name = TYPE_NXP_EDMA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, NXPEDMAState, NXP_EDMA_NUM_MG_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void edma_reset_enter(Object *obj, ResetType type)
{
    NXPEDMAState *s = NXP_EDMA(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void edma_finalize(Object *obj)
{
    NXPEDMAState *s = NXP_EDMA(obj);
    register_finalize_block(s->reg_array);
}

static void edma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    
    dc->vmsd = &vmstate_edma_mg;
    rc->phases.enter = edma_reset_enter;
    dc->desc = "NXP Enhanced Direct Memory Access Management Interface";
}

static Property edma_tcd_properties[] = {
    DEFINE_PROP_UINT32("number-channels", NXPEDMATCDState, number_channels, 32),
    DEFINE_PROP_UINT32("sbr-reset", NXPEDMATCDState, sbr_reset, 0x00008006),
    DEFINE_PROP_END_OF_LIST(),
};

static void edma_tcd_reset(DeviceState *dev)
{
    NXPEDMATCDState *s = NXP_EDMA_TCD(dev);
    int i, j;
    for (i = 0; i < NXP_EDMA_MAX_NCHANS; ++i) {
        EdmaTCGRegisterInfo *ri = &s->regs[i];
        for (j = 0; j < NXP_EDMA_NUM_TCG_REGS; ++j)
            register_reset(&ri->regs_info[j]);
    }
}

static uint64_t edma_tcd_read(void *opaque, hwaddr offset, unsigned size)
{
    EdmaTCGRegisterInfo *ch = opaque;
    uint64_t value = 0;
    char name[30];
    snprintf(name, sizeof(name), "%s CH%i", TYPE_NXP_EDMA_TCD, ch->chan_no);
    
    switch(offset) {
    case A_TCD_BITER_ELINKYES:
        value = ch->tcd_biter_elink;
        break;
    case A_TCD_CSR:
        value = ch->tcd_csr;
        break;
    case A_TCD_CITER_ELINKYES:
        value = ch->tcd_citer_elink;
        break;
    case A_TCD_DOFF:
        value = ch->tcd_doff;
        break;
    case A_TCD_ATTR:
        value = ch->tcd_attr;
        break;
    case A_TCD_SOFF:
        value = ch->tcd_soff;
        break;
    default:
        value = register_read(&ch->regs_info[offset / 4], ~0, name, DEBUG_NXP_EDMA_MG);
        break;
    }

    return value;
}

static void tcd_transfer_on_channel(EdmaTCGRegisterInfo *ch)
{
    uint32_t data, saddr, daddr;
    int bytes_to_transfer;

    ch->tcd_csr &= ~R_TCD_CSR_START_MASK;
    ch->tcd_csr |= R_CH_CSR_ACTIVE_MASK;
    bytes_to_transfer = ch->regs[R_TCD_NBYTES_MLOFFNO] & R_TCD_NBYTES_MLOFFNO_NBYTES_MASK;

    saddr = ch->regs[R_TCD_SADDR];
    daddr = ch->regs[R_TCD_DADDR];
            
    while(bytes_to_transfer > 0) {
        data = ldl_le_phys(&ch->s->dma_as, saddr);
        stl_le_phys(&ch->s->dma_as, daddr, data);
        bytes_to_transfer -= 4;
        saddr += 4;
        daddr += 4;
    }
    ch->tcd_csr &= ~R_CH_CSR_ACTIVE_MASK;
    ch->regs[R_CH_CSR] |= R_CH_CSR_DONE_MASK;
}

static void edma_tcd_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    EdmaTCGRegisterInfo *ch = opaque;
    char name[30];
    
    snprintf(name, sizeof(name), "%s CH%i", TYPE_NXP_EDMA_TCD, ch->chan_no);
    
    switch(offset) {
    case A_TCD_BITER_ELINKYES:
        ch->tcd_biter_elink = value;
        break;
    case A_TCD_CSR:
        ch->tcd_csr = value;
        if (ch->tcd_csr & R_TCD_CSR_START_MASK) {
            tcd_transfer_on_channel(ch);
        }
        break;
    case A_TCD_CITER_ELINKYES:
        ch->tcd_citer_elink = value;
        break;
    case A_TCD_DOFF:
        ch->tcd_doff = value;
        break;
    case A_TCD_ATTR:
        ch->tcd_attr = value;
        break;
    case A_TCD_SOFF:
        ch->tcd_soff = value;
        break;
    default:
        register_write(&ch->regs_info[offset / 4], value, ~0, name, DEBUG_NXP_EDMA_MG);
        break;
    }
}

static const struct MemoryRegionOps edma_tcd_ops = {
    .read = edma_tcd_read,
    .write = edma_tcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void edma_tcd_realize(DeviceState *dev, Error **errp)
{
    NXPEDMATCDState *s = NXP_EDMA_TCD(dev);
    int i;
    Object *obj;
    
    obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);

    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, TYPE_NXP_EDMA_TCD "-memory");

    for (i = 0; i < s->number_channels; ++i) {
        s->regs[i].chan_no = i;
        s->regs[i].s = s;
        s->regs[i].reg_array = register_init_block32(dev, edma_tcd_regs_info,
                                             ARRAY_SIZE(edma_tcd_regs_info),
                                             s->regs[i].regs_info, s->regs[i].regs,
                                             &edma_tcd_ops,
                                             DEBUG_NXP_EDMA_MG,
                                             NXP_EDMA_NUM_TCG_REGS * 4);
        s->regs[i].reg_array->mem.opaque = &s->regs[i];
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->regs[i].reg_array->mem);

    }
}

static void edma_tcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = edma_tcd_realize;
    dc->reset = edma_tcd_reset;
    dc->desc = "NXP Enhanced Direct Memory Access Transfer Control Descriptor Interface";
    device_class_set_props(dc, edma_tcd_properties);
}

static const TypeInfo edma_info = {
    .name          = TYPE_NXP_EDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NXPEDMAState),
    .class_init    = edma_class_init,
    .instance_init = edma_init,
    .instance_finalize = edma_finalize,
};

static const TypeInfo edma_tcd_info = {
    .name          = TYPE_NXP_EDMA_TCD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NXPEDMATCDState),
    .class_init    = edma_tcd_class_init,
};

static void edma_register_types(void)
{
    type_register_static(&edma_info);
    type_register_static(&edma_tcd_info);
}

type_init(edma_register_types)
