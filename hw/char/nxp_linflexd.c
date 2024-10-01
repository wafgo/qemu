/*
 * NXP LinFlexD Serial
 *
 * Copyright (c) 2024 Sriram Neelakantan <sriram.neelakantan@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This is a `bare-bones' implementation of the S32 series LinFlexD ports, in UART Mode Only
 */

#include "qemu/osdep.h"
#include "hw/char/nxp_linflexd.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define LINCR1_SET_MASK (BIT(7))
#define LINCR1_CLR_MASK (BIT(13) | (0xfffe << 16))

#define LINIER_CLR_MASK (BIT(10) | BIT(9) | (0xffff << 16))

#define LINSR_SET_MASK ((0xf << 12))
#define LINSR_CLR_MASK ((0xfff8 << 16) | (0x3 << 3) | (0x3 << 10))

#define LINESR_CLR_MASK ((0xffff << 16) | (0x3f << 1))

#define UARTSR_CLR_MASK ((0xffff << 16) | BIT(4) | BIT(6))

#define LINTCSR_CLR_MASK ((0x1f << 11) | 0xff)

#define LINOCR_CLR_MASK (0xffff << 16)

#define LINTOCR_CLR_MASK ((0xfffff << 12) | BIT(7))

#define LINFBRR_CLR_MASK (0xfffffff << 4)
#define LINIBRR_CLR_MASK (0xfff << 20)

#define BIDR_CLR_MASK ((0xffff << 16) | BIT(7) | BIT(6))

#define DMATXE_DRE0	BIT(0)
#define DMARXE_DRE0	BIT(0)

#define UART_RX_FIFO_MODE BIT(9)
#define UART_TX_FIFO_MODE BIT(8x)

#define LINFLEXD_UARTCR_RXEN BIT(5)

#define DEBUG_NXP_LINFLEXD 0

#ifndef DEBUG_NXP_LINFLEXD
#define DEBUG_NXP_LINFLEXD 0
#endif

static const VMStateDescription vmstate_linflexd = {
    .name = TYPE_LINFLEXD,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER(ageing_timer, LinFlexDState),
        VMSTATE_UINT32(lincr1, LinFlexDState),
        VMSTATE_UINT32(linier, LinFlexDState),
        VMSTATE_UINT32(linsr, LinFlexDState),
        VMSTATE_UINT32(linesr, LinFlexDState),
        VMSTATE_UINT32(uartcr, LinFlexDState),
        VMSTATE_UINT32(uartsr, LinFlexDState),
        VMSTATE_UINT32(lintcsr, LinFlexDState),
        VMSTATE_UINT32(linocr, LinFlexDState),
        VMSTATE_UINT32(lintocr, LinFlexDState),
        VMSTATE_UINT32(linfbrr, LinFlexDState),
        VMSTATE_UINT32(linibrr, LinFlexDState),
        VMSTATE_UINT32(lincfr, LinFlexDState),
        VMSTATE_UINT32(lincr2, LinFlexDState),
        VMSTATE_UINT32(bidr, LinFlexDState),
        VMSTATE_UINT32(bdrl, LinFlexDState),
        VMSTATE_UINT32(bdrm, LinFlexDState),
        VMSTATE_UINT32(ifer, LinFlexDState),
        VMSTATE_UINT32(ifmi, LinFlexDState),
        VMSTATE_UINT32(ifmr, LinFlexDState),
        VMSTATE_UINT32(gcr, LinFlexDState),
        VMSTATE_UINT32(uartpto, LinFlexDState),
        VMSTATE_UINT32(uartcto, LinFlexDState),
        VMSTATE_UINT32(dmatxe, LinFlexDState),
        VMSTATE_UINT32(dmarxe, LinFlexDState),
        VMSTATE_END_OF_LIST()
    },
};

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_NXP_LINFLEXD) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_LINFLEXD, \
                                             __func__, ##args); \
        } \
    } while (0)

static void linflexd_update_irq(LinFlexDState *s)
{
    // TODO: Implement proper interrupt handling
    qemu_set_irq(s->irq, 0);
}

static void linflexd_write_console(LinFlexDState *s, uint32_t ret)
{
    unsigned char ch = ret & 0xFF;
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        /* If there's no backend, we can just say we consumed all data. */
        return;
    }

    qemu_chr_fe_write_all(&s->chr, &ch, 1);
}

static uint64_t linflexd_read(void *opaque, hwaddr offset, unsigned size)
{
    LinFlexDState *s = (LinFlexDState *)opaque;
    uint32_t ret = 0;
    
    switch (offset) {
    case LINFLEXD_LINCR1:
        ret = s->lincr1;
        break;
    case LINFLEXD_LINIER:
        ret = s->linier;
        break;
    case LINFLEXD_LINSR:
        ret = s->linsr;
        break;
    case LINFLEXD_LINESR:
        ret = s->linesr;
        break;
    case LINFLEXD_UARTCR:
        ret = s->uartcr;
        break;
    case LINFLEXD_UARTSR:
        ret = s->uartsr;
        break;
    case LINFLEXD_LINTCSR:
        ret = s->lintcsr;
        break;
    case LINFLEXD_LINOCR:
        ret = s->linocr;
        break;
    case LINFLEXD_LINTOCR:
        ret = s->lintocr;
        break;
    case LINFLEXD_LINFBRR:
        ret = s->linfbrr;
        break;
    case LINFLEXD_LINIBRR:
        ret = s->linibrr;
        break;
    case LINFLEXD_LINCFR:
        ret = s->lincfr;
        break;
    case LINFLEXD_LINCR2:
        ret = s->lincr2;
        break;
    case LINFLEXD_BIDR:
        ret = s->bidr;
        break;
        //FIXME:
    case LINFLEXD_BDRL:
        ret = s->bdrl;
        break;
    case LINFLEXD_BDRM:
        ret = s->bdrm;
        break;
    case LINFLEXD_IFER:
        ret = s->ifer;
        break;
    case LINFLEXD_IFMI:
        ret = s->ifmi;
        break;
    case LINFLEXD_IFMR:
        ret = s->ifmr;
        break;
    case LINFLEXD_GCR:
        ret = s->gcr & ~BIT(0);
        break;
    case LINFLEXD_UARTPTO:
        ret = s->uartpto;
        break;
    case LINFLEXD_UARTCTO:
        ret = s->uartcto;
        break;
    case LINFLEXD_DMATXE:
        ret = s->dmatxe;
        break;
    case LINFLEXD_DMARXE:
        ret = s->dmarxe;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linflexd_read: Bad offset %"HWADDR_PRIx"\n", offset);
        break;
    }

    DPRINTF("%s offset: 0x%" HWADDR_PRIx ", ret: 0x%lx \n", __FUNCTION__, offset, ret);
    return ret;
}

static void linflexd_clear_w1cbits(uint32_t *reg, int *w1c, int num)
{
    for(int i = 0; i < num; ++i) {
        if (*reg & BIT(w1c[i])) 
            *reg &= ~BIT(w1c[i]);
    }
}

static void linflexd_update_state(LinFlexDState *s, hwaddr offset,
                                  uint64_t ret, unsigned size)
{
    switch (s->mode) {
    case LINFLEXD_SLEEP:
        if ((s->lincr1 & BIT(0)) && !(s->lincr1 & BIT(1))) {
            s->mode = LINFLEXD_INIT;
            s->linsr &= ~(0xf << 12);
            s->linsr |= (1 << 12);
            DPRINTF("Entering INIT MODE\n");
        }
        break;
    case LINFLEXD_INIT:
        if ((s->lincr1 & BIT(1)) && !(s->lincr1 & BIT(0))) {
            s->mode = LINFLEXD_SLEEP;
            s->linsr &= ~(0xf << 12);
            DPRINTF("Entering SLEEP MODE\n");
        }
        else if (!(s->lincr1 & BIT(1)) && !(s->lincr1 & BIT(0))) {
            s->mode = LINFLEXD_NORMAL;
            s->linsr &= ~(0xf << 12);
            s->linsr |= (2 << 12);
            DPRINTF("Entering NORMAL MODE\n");
        }
        break;
    case LINFLEXD_NORMAL:
        if ((s->lincr1 & BIT(1)) && !(s->lincr1 & BIT(0))) {
            s->mode = LINFLEXD_SLEEP;
            s->linsr &= ~(0xf << 12);
            DPRINTF("Entering SLEEP MODE\n");
        }

        break;
    }
    
}

static void linflexd_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    LinFlexDState *s = (LinFlexDState *)opaque;

    switch (offset) {
    case LINFLEXD_LINCR1:
        s->lincr1 = ((value | LINCR1_SET_MASK) & ~LINCR1_CLR_MASK);
        linflexd_update_state(s, offset, value, size);
        break;
    case LINFLEXD_LINIER:
        s->linier = value & ~LINIER_CLR_MASK;
        break;
    case LINFLEXD_LINSR:
    {
        static int linsr_w1c_bits[] = {0,1,2,5,8,9};
        s->linsr = (value | LINSR_SET_MASK) & ~LINSR_CLR_MASK;
        // Clear all W1C Bits if set
        linflexd_clear_w1cbits(&s->linsr, linsr_w1c_bits, ARRAY_SIZE(linsr_w1c_bits));
    }
    break;
    case LINFLEXD_LINESR:
    {
        static int linesr_w1c_bits[] = {0,7,8,9,10,11,12,13,14,15};
        s->linesr = value & ~LINESR_CLR_MASK;
        // Clear all W1C Bits if set
        linflexd_clear_w1cbits(&s->linesr, linesr_w1c_bits, ARRAY_SIZE(linesr_w1c_bits));
    }
        break;
    case LINFLEXD_UARTCR:
        if (s->mode == LINFLEXD_INIT) {
            bool uart_mode = value & BIT(0);
            if (!uart_mode && (value & BIT(4)))
                value &= ~BIT(4);
            if (!uart_mode && (value & BIT(5)))
                value &= ~BIT(5);
            s->uartcr = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Try write Register UARTCR in non-init mode 0x%"
                          HWADDR_PRIx " value: 0x%lx\n", TYPE_LINFLEXD, __func__, offset, value);
        }
        break;

    case LINFLEXD_UARTSR:
    {
        static int uartsr_w1c_bits[] = {0,1,2,3,5,7,8,9,10,11,12,13,14,15};
        s->uartsr = value & ~UARTSR_CLR_MASK;
        // Clear all W1C Bits if set
        linflexd_clear_w1cbits(&s->uartsr, uartsr_w1c_bits, ARRAY_SIZE(uartsr_w1c_bits));
    }
        break;
    case LINFLEXD_LINTCSR:
        s->lintcsr = value & ~LINTCSR_CLR_MASK;
        break;
    case LINFLEXD_LINOCR:
        s->linocr = value & ~LINOCR_CLR_MASK;
        break;
    case LINFLEXD_LINTOCR:
        s->lintocr = value & ~LINTOCR_CLR_MASK;
        break;
    case LINFLEXD_LINFBRR:
        s->linfbrr = value & ~LINFBRR_CLR_MASK;
        break;
    case LINFLEXD_LINIBRR:
        s->linibrr = value & ~LINIBRR_CLR_MASK;
        break;
    case LINFLEXD_LINCFR:
        s->lincfr = value & 0xff;
        break;
    case LINFLEXD_LINCR2:
        s->lincr2 = value & (0xff << 8);
        break;
    case LINFLEXD_BIDR:
        s->bidr = value & ~BIDR_CLR_MASK;
        break;
    case LINFLEXD_IFER:
        s->ifer = value;
        break;
    case LINFLEXD_IFMI:
        s->ifmi = value;
        break;
    case LINFLEXD_IFMR:
        s->ifmr = value;
        break;
    case LINFLEXD_GCR:
        if (s->mode == LINFLEXD_INIT) {
            s->gcr = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Try writing Register GCR in non-init mode 0x%"
                          HWADDR_PRIx " ret: 0x%lx\n", TYPE_LINFLEXD, __func__, offset, value);
        }
        break;
    case LINFLEXD_UARTPTO:
        s->uartpto = value & 0xfff;
        break;
    case LINFLEXD_UARTCTO:
        s->uartcto = value & 0xfff;
        break;
    case LINFLEXD_DMATXE:
        s->dmatxe = value & DMATXE_DRE0;
        break;
    case LINFLEXD_DMARXE:
        s->dmarxe = value & DMARXE_DRE0;
        break;
    case LINFLEXD_BDRL:
    case LINFLEXD_BDRM:
        linflexd_write_console(s, value);
        s->uartsr |= BIT(1);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linflexd_write: Bad offset %"HWADDR_PRIx"\n", offset);
        break;
    }

    linflexd_update_irq(s);
}

static const MemoryRegionOps linflexd_ops = {
    .read = linflexd_read,
    .write = linflexd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int linflexd_can_receive(void *opaque)
{
    LinFlexDState *s = (LinFlexDState *)opaque;
    return (s->mode == LINFLEXD_NORMAL) && (s->uartcr & LINFLEXD_UARTCR_RXEN);
}

static void linflexd_receive(void *opaque, const uint8_t *buf, int size)
{
    LinFlexDState *s = (LinFlexDState *)opaque;
    // TODO: Implement proper receive logic
    if (s->uartcr & UART_RX_FIFO_MODE) {
        DPRINTF("Received Char in FIFO MODE\n");
    } else {
        DPRINTF("Received Char in Buffer MODE\n");
    }
    if (size > 0) {
        s->regs[LINFLEXD_BDRL / 4] = buf[0];
        linflexd_write_console(s, buf[0]);
    }
    linflexd_update_irq(s);
}

static void linflexd_event(void *opaque, QEMUChrEvent event)
{
    switch(event) {
    case CHR_EVENT_BREAK:
        DPRINTF("%s: ---> CHR_EVENT_BREAK\n", __FUNCTION__);
        break;
    case CHR_EVENT_OPENED:
        DPRINTF("%s: ---> CHR_EVENT_OPENED\n", __FUNCTION__);
        break;
    case CHR_EVENT_MUX_IN:
        DPRINTF("%s: ---> CHR_EVENT_MUX_IN\n", __FUNCTION__);
        break;
    case CHR_EVENT_MUX_OUT:
        DPRINTF("%s: ---> CHR_EVENT_MUX_OUT\n", __FUNCTION__);
        break;
    case CHR_EVENT_CLOSED:
        DPRINTF("%s: ---> CHR_EVENT_CLOSED\n", __FUNCTION__);
        break;
    }
}

static void linflexd_realize(DeviceState *dev, Error **errp)
{
    LinFlexDState *s = LINFLEXD(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &linflexd_ops, s,
                          TYPE_LINFLEXD, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qemu_chr_fe_set_handlers(&s->chr, linflexd_can_receive, linflexd_receive,
                             linflexd_event, NULL, s, NULL, true);
}

static void linflexd_reset(DeviceState *dev)
{
    LinFlexDState *s = LINFLEXD(dev);

    s->mode = LINFLEXD_SLEEP;
    s->lincr1 = 0x82;
    s->linier = 0;
    s->linsr = 0x40;
    s->linesr = 0;
    s->uartcr = 0;
    s->uartsr = 0;
    s->lintcsr = 0x200;
    s->linocr = 0xffff;
    s->lintocr = 0xe2c;
    s->linfbrr = 0;
    s->linibrr = 0;
    s->lincfr = 0;
    s->lincr2 = 0x6000;
    s->bidr = 0;
    s->bdrl = 0;
    s->bdrm = 0;
    s->ifer = 0;
    s->ifmi = 0;
    s->ifmr = 0;
    s->gcr = 0;
    s->uartpto = 0xfff;
    s->uartcto = 0;
    s->dmatxe = 0;
    s->dmarxe = 0;
    timer_del(&s->ageing_timer);
}

static Property linflexd_serial_properties[] = {
    DEFINE_PROP_CHR("chardev", LinFlexDState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void linflexd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = linflexd_realize;
    dc->vmsd = &vmstate_linflexd;
    dc->reset = linflexd_reset;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "LinFlexD UART";
    device_class_set_props(dc, linflexd_serial_properties);
}

static const TypeInfo linflexd_info = {
    .name          = TYPE_LINFLEXD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LinFlexDState),
    .class_init    = linflexd_class_init,
};

static void linflexd_register_types(void)
{
    type_register_static(&linflexd_info);
}

type_init(linflexd_register_types)
