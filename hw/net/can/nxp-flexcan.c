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
#include "qom/object.h"
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

REG32(MBCTR, 0x0)
    FIELD(MBCTR, TIMESTAMP, 0, 16)
    FIELD(MBCTR, DLC, 16, 4)
    FIELD(MBCTR, RTR, 20, 1)
    FIELD(MBCTR, IDE, 21, 1)
    FIELD(MBCTR, SRR, 22, 1)
    FIELD(MBCTR, CODE, 24, 4)
    FIELD(MBCTR, ESI, 29, 1)
    FIELD(MBCTR, BRS, 30, 1)
    FIELD(MBCTR, EDL, 30, 1)

REG32(MBID, 0x4)
    FIELD(MBID, IDEXT, 0, 29)
    FIELD(MBID, IDSTD, 18, 11)
    FIELD(MBID, PRIO, 29, 3)

    
static int can_frame_from_flexcan_mb(FlexCanState *s, qemu_can_frame *frame, uint32_t *msg)
{
    int i;
    uint32_t *id = msg + 1;
    char *data = (char *)&msg[2];
    int ide = (*msg & R_MBCTR_IDE_MASK) >> R_MBCTR_IDE_SHIFT;
    int idstd = (*id & R_MBID_IDSTD_MASK) >> R_MBID_IDSTD_SHIFT;
    int idext = (*id & R_MBID_IDEXT_MASK) >> R_MBID_IDEXT_SHIFT;
    int rtr = (*msg & R_MBCTR_RTR_MASK) >> R_MBCTR_RTR_SHIFT;
    int dlc = (*msg & R_MBCTR_DLC_MASK) >> R_MBCTR_DLC_SHIFT;
    qemu_canid_t can_id = 0;

    if (rtr)
        can_id |= BIT(30);

    can_id |= ((ide) ? (BIT(31) | idext) : idstd);
    
    frame->can_id = can_id;
    frame->can_dlc = dlc;
    
    for (i = 0; i < dlc; ++i) {
        frame->data[i] = data[i];
    }
    
    return 0;
}

static uint32_t *irq_mask_reg_from_mb_number(FlexCanState *s, int mb_num)
{
    switch(mb_num) {
    case 0 ... 31:
        return &s->imask1;
    case 32 ... 63:
        return &s->imask2;
    case 64 ... 95:
        return &s->imask3;
    case 96 ... 127:
        return &s->imask4;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s (%s): FLEXCAN: Message Box Number out of Range %i\n",
                      __FUNCTION__, object_get_canonical_path(OBJECT(s)), mb_num);
        return NULL;
    }
}

static uint32_t *irq_flag_reg_from_mb_number(FlexCanState *s, int mb_num)
{
    switch(mb_num) {
    case 0 ... 31:
        return &s->iflag1;
    case 32 ... 63:
        return &s->iflag2;
    case 64 ... 95:
        return &s->iflag3;
    case 96 ... 127:
        return &s->iflag4;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s (%s): FLEXCAN: Message Box Number out of Range %i\n",
                      __FUNCTION__, object_get_canonical_path(OBJECT(s)), mb_num);
        return NULL;
    }
}

static int update_flexcan_message_box(FlexCanState *s, int payload_size, int block_no, char *dname, char *prefix, int max_loop, int start_mb)
    {
        int left_to_read = 512;
        int cnt = 0;
        uint32_t *irq_mask_reg;
        uint32_t *irq_flag_reg;
        int irq_reg_bit;
        int mb_size = payload_size + 8;
        qemu_can_frame frame;
        int mb_number = start_mb;
        uint32_t *msg = (uint32_t *)((char *)s->can_msg_area + (block_no * 512));
        uint32_t *id = msg + 1;
        while (left_to_read >= mb_size) {
            int code = (*msg & R_MBCTR_CODE_MASK) >> R_MBCTR_CODE_SHIFT;
            int edl = (*msg & R_MBCTR_EDL_MASK) >> R_MBCTR_EDL_SHIFT;
            int dlc = (*msg & R_MBCTR_DLC_MASK) >> R_MBCTR_DLC_SHIFT;
            int ide = (*msg & R_MBCTR_IDE_MASK) >> R_MBCTR_IDE_SHIFT;
            int prio = (*id & R_MBID_PRIO_MASK) >> R_MBID_PRIO_SHIFT;
            int idstd = (*id & R_MBID_IDSTD_MASK) >> R_MBID_IDSTD_SHIFT;
            int idext = (*id & R_MBID_IDEXT_MASK) >> R_MBID_IDEXT_SHIFT;
            if (code == MB_RX_INACTIVE || code == MB_TX_INACTIVE) {
                trace_flexcan_message_box_code_inactive(dname, block_no, cnt, edl, code, dlc, ide, prio, idstd, idext);
            }
            if (code == MB_RX_EMPTY) {
                trace_flexcan_message_box_code_rx_empty(dname, block_no, cnt, edl, code, dlc, ide, prio, idstd, idext);
            }
            if (code == MB_RX_RANSWER) {
                trace_flexcan_message_box_code_rx_ranswer(dname, block_no, cnt, edl, code, dlc, ide, prio, idstd, idext);
            }
            if (code == MB_TX_DATA_FRAME) {
                trace_flexcan_message_box_code_data_frame(dname, block_no, cnt, edl, code, dlc, ide, prio, idstd, idext);
                if (s->canfdbus) {
                    irq_mask_reg = irq_mask_reg_from_mb_number(s, mb_number);
                    irq_flag_reg = irq_flag_reg_from_mb_number(s, mb_number);
                    irq_reg_bit = mb_number % 32;
                    can_frame_from_flexcan_mb(s, &frame, msg);
                    can_bus_client_send(&s->bus_client, &frame, 1);
                    *irq_flag_reg |= BIT(irq_reg_bit);
                    *msg &= ~R_MBCTR_CODE_MASK;
                    *msg |= (MB_TX_INACTIVE << R_MBCTR_CODE_SHIFT);
                    if ((*irq_mask_reg & BIT(irq_reg_bit))) {
                        /* Only raise IRQ if not masked, otherwise simply raise it when the mask bit is set */
                        qemu_irq_raise((mb_number < 8) ? s->irq_msg_lower : s->irq_msg_upper);
                    }
                }
            }
            cnt++;
            mb_number++;
            if (cnt >= max_loop)
                break;
            left_to_read -= mb_size;
            msg += (mb_size / 4);
            id = msg + 1;
        }
        return cnt;
}
    
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
    char *data;
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
        data = (char *)s->can_msg_area + (addr - 0x80);
        memcpy(&value, data, size);
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
        data = (char *)s->can_msg_area + FLEXCAN_RAM_BLOCK_ONE_SIZE + (addr - 0x1000);
        memcpy(&value, data, size);
        break;
    case 0x2000 ... 0x29fc:
        break;
    case 0x3000 ... 0x31FC:
        value = s->erffel[(addr - 0x3000) / 4];
        break;
    default:
        break;
    }
    
    trace_flexcan_can_read_register(object_get_canonical_path(OBJECT(s)), addr, size, value, s->freeze_mode, s->low_power_mode, s->fd_en, s->rx_fifo_en, s->enh_rx_fifo_en, pc);
    return value;
}

static void flexcan_update_state(FlexCanState *s)
{
    int rsz[] = {8, 8, 8, 8, 64, 64, 64, 64};
    char obj_name[60];
    char prefix[16];
    int i;
    int mb_count, mb_no = 0;
    int max_mbs =  (s->mcr & R_MCR_MAXMB_MASK) + 1;
    
    snprintf(obj_name, sizeof(obj_name), "%s", object_get_canonical_path(OBJECT(s)));
    if (s->fd_en) {
        rsz[0] = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR0_MASK) >> R_FDCTRL_MBDSR0_SHIFT));
        rsz[1] = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR1_MASK) >> R_FDCTRL_MBDSR1_SHIFT));
        rsz[2] = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR2_MASK) >> R_FDCTRL_MBDSR2_SHIFT));
        rsz[3] = 8 * (1 << ((s->fdctrl & R_FDCTRL_MBDSR3_MASK) >> R_FDCTRL_MBDSR3_SHIFT));
    }
            
    for (i = 0; i < FLEXCAN_NUM_RAM_BANKS && (max_mbs > 0); ++i) {
        snprintf(prefix, sizeof(prefix), "r%i", i);
        mb_count = update_flexcan_message_box(s, rsz[i], i, obj_name, prefix, max_mbs, mb_no);
        max_mbs -= mb_count;
        mb_no += mb_count;
    }
}


static void flexcan_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    FlexCanState *s = opaque;
    uint64_t pc = current_cpu->cc->get_pc(current_cpu);
    char *data;
    uint32_t tmp;
    
    trace_flexcan_can_write_register(object_get_canonical_path(OBJECT(s)), addr, size, value, pc);
    
    switch (addr) {
        case 0x00:
            /* Handle SoftReset */
            if (value & R_MCR_SOFTRST_MASK) {
                /* flexcan_reset(DEVICE(s)); */
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
                s->esr1 |= (BIT(18) | BIT(7));
            }
            if (s->freeze_mode == true) {
                s->fd_en = (value & R_MCR_FDEN_MASK) >> R_MCR_FDEN_SHIFT;
                s->rx_fifo_en = (value & R_MCR_RFEN_MASK) >> R_MCR_RFEN_SHIFT;
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
            s->ecr = value & 0x0000ffff;
            break;
        case 0x20:
            if (value & BIT(1))
                s->esr1 &= ~BIT(1);
            if (value & BIT(2))
                s->esr1 &= ~BIT(2);
            if (value & BIT(16))
                s->esr1 &= ~BIT(16);
            if (value & BIT(17))
                s->esr1 &= ~BIT(17);
            if (value & BIT(19))
                s->esr1 &= ~BIT(19);
            if (value & BIT(20))
                s->esr1 &= ~BIT(20);
            if (value & BIT(21))
                s->esr1 &= ~BIT(21);
            break;
        case 0x24:
            s->imask2 = value;
            if (s->imask2 & s->iflag2)
                qemu_irq_raise(s->irq_msg_upper);
            break;
        case 0x28:
            s->imask1 = value;
            tmp = s->imask1 & s->iflag1;
            if (tmp) {
                if (tmp & 0xff) 
                    qemu_irq_raise(s->irq_msg_lower);
                if (tmp & 0xffffff00) 
                    qemu_irq_raise(s->irq_msg_upper);
            }
            break;
        case 0x2C:
            tmp = ~(value & 0xffffffff);
            s->iflag2 &= tmp;
            if (value)
                qemu_irq_lower(s->irq_msg_upper);
            break;
        case 0x30:
            tmp = ~(value & 0xffffffff);
            s->iflag1 &= tmp;
            if (value & 0xff) {
                qemu_irq_lower(s->irq_msg_lower);
            }
            if (value & 0xffffff00) {
                qemu_irq_lower(s->irq_msg_upper);
            }
            break;
        case 0x34:
            s->ctrl2 = value & 0xffffbfc0;
            break;
        case 0x38:
        case 0x44:
            break;
        case 0x48:
            s->rxfgmask = value;
            break;
        case 0x4C:
            break;
        case 0x50:
            s->cbt = value;
            break;
        case 0x68:
            s->imask4 = value;
            if (s->imask4 & s->iflag4)
                qemu_irq_raise(s->irq_msg_upper);
            break;
        case 0x6C:
            s->imask3 = value;
            if (s->imask3 & s->iflag3)
                qemu_irq_raise(s->irq_msg_upper);
            break;
        case 0x70:
            tmp = ~(value & 0xffffffff);
            s->iflag4 &= tmp;
            if (value)
                qemu_irq_lower(s->irq_msg_upper);
            break;
        case 0x74:
            tmp = ~(value & 0xffffffff);
            s->iflag3 &= tmp;
            if (value)
                qemu_irq_lower(s->irq_msg_upper);
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
            s->mecr = value & 0x800de380;
            break;
        case 0xae4:
            s->erriar = value & 0x00003ffc;
            break;
        case 0xae8:
            s->erridpr = value;
            break;
        case 0xaec:
            s->errippr = value & 0x1f1f1f1f;
            break;
        case 0xaf0:
        case 0xaf4:
        case 0xaf8:
        case 0xafc:
            break;
        case 0xbf0:
            s->eprs = value & 0x03ff03ff;
            break;
        case 0xbf4:
            s->encbt = value & 0x1fc7f0ff;
            break;
        case 0xbf8:
            s->edcbt = value & 0x03c0f01f;
            break;
        case 0xbfc:
            s->etdc = value & 0xc07f8000;
            break;
        case 0xc00:
            if (value & BIT(14))
                value &= ~BIT(14);
            s->fdctrl = value & 0x86dbdf00;
            break;
        case 0xc04:
            s->fdcbt = value & 0x3ff77ce7;
            break;
        case 0xc08:
            break;
        case 0xc0c:
            if (s->freeze_mode)
                s->enh_rx_fifo_en = !!(value & R_ERFCR_ERFEN_MASK);
            s->erfcr = value & 0xfc7f3f1f;
            break;
        case 0xc10:
            s->erfier = value & 0xf0000000;
            break;
        case 0xc14:
            if (value & BIT(28))
                value &= ~BIT(28);
            if (value & BIT(29))
                value &= ~BIT(29);
            if (value & BIT(30))
                value &= ~BIT(30);
            if (value & BIT(31))
                value &= ~BIT(31);
            s->erfsr = value & 0x08000000;
            break;
        case 0xc30 ... 0xe2c:
            s->hr_time_stamp[(addr - 0xc30) / 4] = value;
            break;
        case 0x1000 ... 0x17ff:
            trace_flexcan_canfd_message_buffer_write(object_get_canonical_path(OBJECT(s)), addr, size, value, pc);
            data = (char *)s->can_msg_area + FLEXCAN_RAM_BLOCK_ONE_SIZE + (addr - 0x1000);
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

    if (s->freeze_mode == false)
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
    trace_flexcan_can_bus_connected(object_get_canonical_path(OBJECT(s)), object_get_canonical_path_component(OBJECT(bus)));
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
    }
}

static Property canfd_core_properties[] = {
    DEFINE_PROP_UINT64("ext_clk_freq", FlexCanState, ext_clk_hz, 1e6),
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

