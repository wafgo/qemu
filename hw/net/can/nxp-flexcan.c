/*
 * NXP Flexcan CAN and CANFD model
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/register.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/cutils.h"
#include "qemu/event_notifier.h"
#include "hw/qdev-properties.h"
#include "qom/object_interfaces.h"
#include "migration/vmstate.h"
#include "hw/net/nxp-flexcan.h"
#include "trace.h"

REG32(MCR, 0x0)
    FIELD(MCR, MAXMB, 0, 7) 
    FIELD(MCR, IDAM, 8, 2)
    FIELD(MCR, FDEN, 11, 1)
    FIELD(MCR, AEN, 12, 1)
    FIELD(MCR, LPRIOEN, 13, 1)
    FIELD(MCR, DMA, 15, 1)
    FIELD(MCR, IRMQ, 16, 1)
    FIELD(MCR, SRXDIS, 17, 1)
    FIELD(MCR, LPDMACK, 20, 1)
    FIELD(MCR, WRNEN, 21, 1)
    FIELD(MCR, FRZACK, 24, 1)
    FIELD(MCR, SOFTRST, 25, 1)
    FIELD(MCR, NOTRDY, 27, 1)
    FIELD(MCR, HALT, 28, 1)
    FIELD(MCR, RFEN, 29, 1)
    FIELD(MCR, FRZ, 30, 1)
    FIELD(MCR, MDIS, 31, 1)

REG32(FDCTRL, 0xc00)
    FIELD(FDCTRL, TDCVAL, 0, 6)
    FIELD(FDCTRL, TDCOFF, 8, 5)
    FIELD(FDCTRL, TDCFAIL, 14, 1)
    FIELD(FDCTRL, TDCEN, 15, 1)
    FIELD(FDCTRL, MBDSR0, 16, 2)
    FIELD(FDCTRL, MBDSR1, 19, 2)
    FIELD(FDCTRL, MBDSR2, 22, 2)
    FIELD(FDCTRL, MBDSR3, 25, 2)
    FIELD(FDCTRL, FDRATE, 31, 1)    

REG32(ERFCR, 0xc0c)
    FIELD(ERFCR, ERFWM, 0, 5)
    FIELD(ERFCR, NFE, 8, 6)
    FIELD(ERFCR, NEXIF, 16, 7)
    FIELD(ERFCR, DMALW, 26, 5)
    FIELD(ERFCR, ERFEN, 31, 1)

static void flexcan_reset(DeviceState *dev)
{
    FlexCanState *s = FLEXCAN(dev);
    
    s->mcr = 0xd890000f;
    s->ctrl1 = 0;
    s->timer = 0;
    s->rxmgmask = 0;
    s->rx14mask = 0;
    s->rx15mask = 0;
    s->ecr = 0;
    s->esr1 = 0;
    s->imask2 = 0;
    s->imask1 = 0;
    s->iflag2 = 0;
    s->iflag1 = 0;
    s->ctrl2 = 0x00100000;
    s->esr2 = 0;
    s->crcr = 0;
    s->rxfgmask = 0;
    s->rxfir = 0;
    s->cbt = 0;
    s->mecr = 0x800c0080;
    s->erriar = 0;
    s->erridpr = 0;
    s->errippr = 0;
    s->rerrar = 0;
    s->rerrdr = 0;
    s->rerrsynr = 0;
    s->errsr = 0;
    s->eprs = 0;
    s->encbt = 0;
    s->edcbt = 0;
    s->etdc = 0;
    s->fdctrl = 0x80000100;
    s->fdcbt = 0;
    s->fdcrc = 0;
    s->erfcr = 0;
    s->erfier = 0;
    s->erfsr = 0;
    memset(s->hr_time_stamp, 0, sizeof(s->hr_time_stamp));
    memset(s->erffel, 0, sizeof(s->erffel));
}

static uint64_t flexcan_read(void *opaque, hwaddr addr, unsigned size)
{
    FlexCanState *s = opaque;
    uint32_t value = 0;
    uint64_t pc = current_cpu->cc->get_pc(current_cpu);

    switch (addr) {
    case 0x0:
        value = s->mcr;
        break;
    case 0x04:
        value = s->ctrl1;
        break;
    case 0x08:
        value = s->timer;
        break;
    case 0x10:
        value = s->rxmgmask;
        break;
    case 0x14:
        value = s->rx14mask;
        break;
    case 0x18:
        value = s->rx15mask;
        break;
    case 0x1C:
        value = s->ecr;
        break;
    case 0x20:
        value = s->esr1;
        break;
    case 0x24:
        value = s->imask2;
        break;
    case 0x28:
        value = s->imask1;
        break;
    case 0x2C:
        value = s->iflag2;
        break;
    case 0x30:
        value = s->iflag1;
        break;
    case 0x34:
        value = s->ctrl2;
        break;
    case 0x38:
        value = s->esr2;
        break;
    case 0x44:
        value = s->crcr;
        break;
    case 0x48:
        value = s->rxfgmask;
        break;
    case 0x4C:
        value = s->rxfir;
        break;
    case 0x50:
        value = s->cbt;
        break;
    case 0x68:
        value = s->imask4;
        break;
    case 0x6C:
        value = s->imask3;
        break;
    case 0x70:
        value = s->iflag4;
        break;
    case 0x74:
        value = s->iflag3;
        break;
    case 0x80 ... 0x87f:
        /* printf("--------------> READING FROM MESSAGE BUFFER\n"); */
        break;
    case 0x880 ... 0xa7c:
        value = s->rximr[(addr - 0x880) / 4];
        break;
    case 0xae0:
        value = s->mecr;
        break;
    case 0xae4:
        value = s->erriar;
        break;
    case 0xae8:
        value = s->erridpr;
        break;
    case 0xaec:
        value = s->errippr;
        break;
    case 0xaf0:
        value = s->rerrar;
        break;
    case 0xaf4:
        value = s->rerrdr;
        break;
    case 0xaf8:
        value = s->rerrsynr;
        break;
    case 0xafc:
        value = s->errsr;
        break;
    case 0xbf0:
        value = s->eprs;
        break;
    case 0xbf4:
        value = s->encbt;
        break;
    case 0xbf8:
        value = s->edcbt;
        break;
    case 0xbfc:
        value = s->etdc;
        break;
    case 0xc00:
        value = s->fdctrl;
        break;
    case 0xc04:
        value = s->fdcbt;
        break;
    case 0xc08:
        value = s->fdcrc;
        break;
    case 0xc0c:
        value = s->erfcr;
        break;
    case 0xc10:
        value = s->erfier;
        break;
    case 0xc14:
        value = s->erfsr;
        break;
    case 0xc30 ... 0xe2c:
        value = s->hr_time_stamp[(addr - 0xc30) / 4];
        break;
    case 0x1000 ... 0x17ff:
        /* printf("--------------> READING FROM  CNAFD MESSAGE BUFFER\n"); */
        break;
    case 0x2000 ... 0x29fc:
        /* printf("--------------> READING FROM ENHANCED RXFIFO MESSAGE BUFFER\n"); */
        break;
    case 0x3000 ... 0x31FC:
        value = s->erffel[(addr - 0x3000) / 4];
        break;
    default:
        break;
    }
    
    trace_flexcan_can_read_register(object_get_canonical_path(OBJECT(s)), addr, size, value, pc, s->freeze_mode, s->low_power_mode, s->fd_en, s->rx_fifo_en, s->enh_rx_fifo_en);
    return value;
}

#define verbose_trace_flexcan_message_box(STATE, N, BLOCK_NO, DNAME, PREFIX)     \
{ \
        int i = 0;                                                       \
        int left_to_read = 512; \
        flexcan_message_##N##_t *msg = (flexcan_message_##N##_t *)(char *)STATE->can_msg_area + (BLOCK_NO * 512); \
        while (left_to_read > 0) { \
        if (msg->msg.cs.code == MB_RX_INACTIVE || msg->msg.cs.code == MB_TX_INACTIVE) \
            trace_flexcan_message_box_code_inactive(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext); \
        if (msg->msg.cs.code == MB_RX_EMPTY) \
           trace_flexcan_message_box_code_rx_empty(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext); \
        if (msg->msg.cs.code == MB_RX_RANSWER) \
            trace_flexcan_message_box_code_rx_ranswer(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext); \
        if (msg->msg.cs.code == MB_TX_DATA_FRAME) \
            trace_flexcan_message_box_code_data_frame(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext); \
            i++; \
            left_to_read -= sizeof(*msg); \
        } \
}

static void verbose_trace_flexcanfd_message_box(FlexCanState *s, char *obj_name, const char *prefix, int bno)
{
    int i = 0;
    int left_to_read = 512;
    flexcan_message_64_t *msg = (flexcan_message_64_t *)(char *)s->canfd_msg_area + ((bno - 4) * 512);
    while (left_to_read > 0) {
        if (msg->msg.cs.code == MB_RX_INACTIVE || msg->msg.cs.code == MB_TX_INACTIVE)
            trace_flexcan_fd_message_box_code_inactive(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext);
        if (msg->msg.cs.code == MB_RX_EMPTY)
           trace_flexcan_fd_message_box_code_rx_empty(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext);
        if (msg->msg.cs.code == MB_RX_RANSWER)
            trace_flexcan_fd_message_box_code_rx_ranswer(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext);
        if (msg->msg.cs.code == MB_TX_DATA_FRAME)
            trace_flexcan_fd_message_box_code_data_frame(obj_name, i, bno, msg->msg.cs.brs, msg->msg.cs.code, msg->msg.cs.dlc, msg->msg.cs.ide, msg->msg.cs.prio, msg->msg.cs.id_std, msg->msg.cs.id_ext);
        i++;
        left_to_read -= sizeof(*msg);
    }
}

    
static void verbose_trace_flexcan_ram_block(FlexCanState *s, char *obj_name, const char *prefix, uint32_t msg_size, int bno)
{
    if (bno > 3) {
        verbose_trace_flexcanfd_message_box(s, obj_name, prefix, bno);
    } else {
        switch(msg_size) {
        case 8:
            verbose_trace_flexcan_message_box(s, 8, bno, obj_name, prefix)
                break;
        case 16:
            verbose_trace_flexcan_message_box(s, 16, bno, obj_name, prefix)
                break;
        case 32:
            verbose_trace_flexcan_message_box(s, 32, bno, obj_name, prefix)
                break;
        case 64:
            verbose_trace_flexcan_message_box(s, 64, bno, obj_name, prefix)
                break;
        }
    }
}

static void flexcan_update_state(FlexCanState *s)
{
    int r0s, r1s, r2s, r3s;
    char obj_name[60];
    snprintf(obj_name, sizeof(obj_name), "%s", object_get_canonical_path(OBJECT(s)));
    if (s->fd_en) {
        r0s = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR0_MASK) >> R_FDCTRL_MBDSR0_SHIFT));
        r1s = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR1_MASK) >> R_FDCTRL_MBDSR1_SHIFT));
        r2s = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR2_MASK) >> R_FDCTRL_MBDSR2_SHIFT));
        r3s = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR3_MASK) >> R_FDCTRL_MBDSR3_SHIFT));
        /* printf("%s: FD Enabled with R0 = %i, R1 = %i, R2 = %i, R3 = %i\n", object_get_canonical_path(OBJECT(s)), r0s, r1s, r2s, r3s); */
        verbose_trace_flexcan_ram_block(s, obj_name, "r0", r0s, 0);
        verbose_trace_flexcan_ram_block(s, obj_name, "r1", r1s, 1);
        verbose_trace_flexcan_ram_block(s, obj_name, "r2", r2s, 2);
        verbose_trace_flexcan_ram_block(s, obj_name, "r3", r3s, 3);
        verbose_trace_flexcan_ram_block(s, obj_name, "r4", 64, 4);
        verbose_trace_flexcan_ram_block(s, obj_name, "r5", 64, 5);
        verbose_trace_flexcan_ram_block(s, obj_name, "r6", 64, 6);
        verbose_trace_flexcan_ram_block(s, obj_name, "r7", 64, 7);
    }
}


static void flexcan_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    FlexCanState *s = opaque;
    uint64_t pc = current_cpu->cc->get_pc(current_cpu);
    char *data;
    
    trace_flexcan_can_write_register(object_get_canonical_path(OBJECT(s)), addr, size, value);
    
    switch (addr) {
        case 0x00:
            /* Handle SoftReset */
            if (value & R_MCR_SOFTRST_MASK) {
                flexcan_reset(DEVICE(s));
                value &= ~R_MCR_SOFTRST_MASK;
            }
            if (value & R_MCR_HALT_MASK && s->mcr & R_MCR_FRZ_MASK) {
                if (!s->freeze_mode)
                    trace_flexcan_enter_freeze_mode(object_get_canonical_path(OBJECT(s)));
                s->freeze_mode = true;
                value |= R_MCR_FRZACK_MASK;
            }            
            if (value & R_MCR_MDIS_MASK) {
                if (!s->low_power_mode)
                    trace_flexcan_enter_low_power_mode(object_get_canonical_path(OBJECT(s)));
                s->low_power_mode = true;
                value |= R_MCR_LPDMACK_MASK;
            } else {
                if (s->low_power_mode)
                    trace_flexcan_exit_low_power_mode(object_get_canonical_path(OBJECT(s)), (value & R_MCR_MAXMB_MASK) + 1);
                s->low_power_mode = false;
                value &= ~R_MCR_LPDMACK_MASK;
            }
            if (s->freeze_mode && !(s->mcr & R_MCR_HALT_MASK) && !(value & R_MCR_FRZ_MASK)) {
                trace_flexcan_exit_freeze_mode(object_get_canonical_path(OBJECT(s)));
                s->freeze_mode = false;
                value &= ~R_MCR_FRZACK_MASK;
            }
            if (s->freeze_mode == true) {
                s->fd_en = !!(value & R_MCR_FDEN_MASK);
                s->rx_fifo_en = !!(value & R_MCR_RFEN_MASK);
            }
            s->mcr = value;
            break;
        case 0x04:
            s->ctrl1 = value;
            break;
        case 0x08:
            s->timer = value;
            break;
        case 0x10:
            s->rxmgmask = value;
            break;
        case 0x14:
            s->rx14mask = value;
            break;
        case 0x18:
            s->rx15mask = value;
            break;
        case 0x1C:
            s->ecr = value;
            break;
        case 0x20:
            s->esr1 = value;
            break;
        case 0x24:
            s->imask2 = value;
            break;
        case 0x28:
            s->imask1 = value;
            break;
        case 0x2C:
            s->iflag2 = value;
            break;
        case 0x30:
            s->iflag1 = value;
            break;
        case 0x34:
            s->ctrl2 = value;
            break;
        case 0x38:
            s->esr2 = value;
            break;
        case 0x44:
            s->crcr = value;
            break;
        case 0x48:
            s->rxfgmask = value;
            break;
        case 0x4C:
            s->rxfir = value;
            break;
        case 0x50:
            s->cbt = value;
            break;
        case 0x68:
            s->imask4 = value;
            break;
        case 0x6C:
            s->imask3 = value;
            break;
        case 0x70:
            s->iflag4 = value;
            break;
        case 0x74:
            s->iflag3 = value;
            break;
        case 0x80 ... 0x87f:
            trace_flexcan_can_message_buffer_write(object_get_canonical_path(OBJECT(s)), addr, size, value, pc);
            data = (char *)s->can_msg_area + (addr - 0x80);
            memcpy(data, &value, size);
            break;
        case 0x880 ... 0xa7c:
            s->rximr[(addr - 0x880) / 4] = value;
            break;
        case 0xae0:
            s->mecr = value;
            break;
        case 0xae4:
            s->erriar = value;
            break;
        case 0xae8:
            s->erridpr = value;
            break;
        case 0xaec:
            s->errippr = value;
            break;
        case 0xaf0:
            s->rerrar = value;
            break;
        case 0xaf4:
            s->rerrdr = value;
            break;
        case 0xaf8:
            s->rerrsynr = value;
            break;
        case 0xafc:
            s->errsr = value;
            break;
        case 0xbf0:
            s->eprs = value;
            break;
        case 0xbf4:
            s->encbt = value;
            break;
        case 0xbf8:
            s->edcbt = value;
            break;
        case 0xbfc:
            s->etdc = value;
            break;
        case 0xc00:
            s->fdctrl = value;
            break;
        case 0xc04:
            s->fdcbt = value;
            break;
        case 0xc08:
            s->fdcrc = value;
            break;
        case 0xc0c:
            if (s->freeze_mode) {
                s->enh_rx_fifo_en = !!(value & R_ERFCR_ERFEN_MASK);
            }
            s->erfcr = value;
            break;
        case 0xc10:
            s->erfier = value;
            break;
        case 0xc14:
            s->erfsr = value;
            break;
        case 0xc30 ... 0xe2c:
            s->hr_time_stamp[(addr - 0xc30) / 4] = value;
            break;
        case 0x1000 ... 0x17ff:
            trace_flexcan_canfd_message_buffer_write(object_get_canonical_path(OBJECT(s)), addr, size, value, pc);
            data = (char *)s->canfd_msg_area + (addr - 0x1000);
            memcpy(data, &value, size);
            break;
        case 0x2000 ... 0x29fc:
            trace_flexcan_canfd_message_buffer_write(object_get_canonical_path(OBJECT(s)), addr, size, value, pc);
            break;
        case 0x3000 ... 0x31FC:
            s->erffel[(addr - 0x3000) / 4] = value;
            break;
        default:
            break;
    }
    flexcan_update_state(s);
}

static const MemoryRegionOps flexcan_ops = {
    .read = flexcan_read,
    .write = flexcan_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static bool flexcan_can_receive(CanBusClientState *client)
{
    FlexCanState *s = container_of(client, FlexCanState, bus_client);
    return true;
}

static ssize_t flexcan_canfd_receive(CanBusClientState *client,
                                    const qemu_can_frame *buf,
                                    size_t buf_size)
{
    FlexCanState *s = container_of(client, FlexCanState, bus_client);
    printf("-----> RECEIVED CAN FRAME\n");
    return 1;
}

static CanBusClientInfo canfd_flexcan_bus_client_info = {
    .can_receive = flexcan_can_receive,
    .receive = flexcan_canfd_receive,
};

static int flexcan_canfd_connect_to_bus(FlexCanState *s,
                                     CanBusState *bus)
{
    s->bus_client.info = &canfd_flexcan_bus_client_info;
    return can_bus_insert_client(bus, &s->bus_client);
}


static void flexcan_realize(DeviceState *dev, Error **errp)
{
    FlexCanState *s = FLEXCAN(dev);
    memory_region_init_io(&s->iomem, OBJECT(s), &flexcan_ops, s, "flexcan", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_bus_off);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_err);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_msg_lower);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_msg_upper);
    
    if (s->canfdbus) {
        if (flexcan_canfd_connect_to_bus(s, s->canfdbus) < 0) {
            g_autofree char *path = object_get_canonical_path(OBJECT(s));
            error_setg(errp, "%s: flexcan_canfd_connect_to_bus failed", path);
            return;
        }
    } /* else { */
    /*     error_setg(errp, "%s: no bus assigned", object_get_canonical_path(OBJECT(s))); */
    /* } */
}

static Property canfd_core_properties[] = {
    DEFINE_PROP_UINT64("ext_clk_freq", FlexCanState, ext_clk_hz,1e6),
    DEFINE_PROP_LINK("canfdbus", FlexCanState, canfdbus, TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void flexcan_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = flexcan_reset;
    dc->realize = flexcan_realize;
    device_class_set_props(dc, canfd_core_properties);
}

static const TypeInfo flexcan_info = {
    .name = TYPE_FLEXCAN,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FlexCanState),
    .class_init = flexcan_class_init,
};

static void flexcan_register_types(void)
{
    type_register_static(&flexcan_info);
}

type_init(flexcan_register_types);

