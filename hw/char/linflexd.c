/*
 * NXP LinFlexD Serial
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This is a `bare-bones' implementation of the S32 series LinFlexD ports, in UART Mode Only
 */

#include "qemu/osdep.h"
#include "hw/char/linflexd.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/fifo32.h"

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

#ifndef DEBUG_LINFLEXD_UART
#define DEBUG_LINFLEXD_UART 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_LINFLEXD_UART) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_LINFLEXD_SERIAL, \
                                             __func__, ##args); \
        } \
    } while (0)

static const VMStateDescription vmstate_linflexd_serial = {
    .name = TYPE_LINFLEXD_SERIAL,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER(ageing_timer, LINFLEXDSerialState),
        VMSTATE_UINT32(lincr1, LINFLEXDSerialState),
        VMSTATE_UINT32(linier, LINFLEXDSerialState),
        VMSTATE_UINT32(linsr, LINFLEXDSerialState),
        VMSTATE_UINT32(linesr, LINFLEXDSerialState),
        VMSTATE_UINT32(uartcr, LINFLEXDSerialState),
        VMSTATE_UINT32(uartsr, LINFLEXDSerialState),
        VMSTATE_UINT32(lintcsr, LINFLEXDSerialState),
        VMSTATE_UINT32(linocr, LINFLEXDSerialState),
        VMSTATE_UINT32(lintocr, LINFLEXDSerialState),
        VMSTATE_UINT32(linfbrr, LINFLEXDSerialState),
        VMSTATE_UINT32(linibrr, LINFLEXDSerialState),
        VMSTATE_UINT32(lincfr, LINFLEXDSerialState),
        VMSTATE_UINT32(lincr2, LINFLEXDSerialState),
        VMSTATE_UINT32(bidr, LINFLEXDSerialState),
        VMSTATE_UINT32(bdrl, LINFLEXDSerialState),
        VMSTATE_UINT32(bdrm, LINFLEXDSerialState),
        VMSTATE_UINT32(ifer, LINFLEXDSerialState),
        VMSTATE_UINT32(ifmi, LINFLEXDSerialState),
        VMSTATE_UINT32(ifmr, LINFLEXDSerialState),
        VMSTATE_UINT32(gcr, LINFLEXDSerialState),
        VMSTATE_UINT32(uartpto, LINFLEXDSerialState),
        VMSTATE_UINT32(uartcto, LINFLEXDSerialState),
        VMSTATE_UINT32(dmatxe, LINFLEXDSerialState),
        VMSTATE_UINT32(dmarxe, LINFLEXDSerialState),
        VMSTATE_END_OF_LIST()
    },
};

static void linflexd_update(LINFLEXDSerialState *s)
{
    printf("Updating Linflex Registers\n");

    /* qemu_set_irq(s->irq, usr1 || usr2); */
}


static void linflexd_serial_reset(LINFLEXDSerialState *s)
{
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

static void linflexd_serial_reset_at_boot(DeviceState *dev)
{
    LINFLEXDSerialState *s = LINFLEXD_SERIAL(dev);

    linflexd_serial_reset(s);
}

static uint64_t linflexd_serial_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    LINFLEXDSerialState *s = (LINFLEXDSerialState *)opaque;
    uint64_t value = 0;

    DPRINTF("read(offset=0x%" HWADDR_PRIx ")\n", offset);

    switch (offset) {
    case LINCR1:
        value = s->lincr1;
        break;
    case LINIER:
        value = s->linier;
        break;
    case LINSR:
        value = s->linsr;
        break;
    case LINESR:
        value = s->linesr;
        break;
    case UARTCR:
        value = s->uartcr;
        break;
    case UARTSR:
        value = s->uartsr;
        break;
    case LINTCSR:
        value = s->lintcsr;
        break;
    case LINOCR:
        value = s->linocr;
        break;
    case LINTOCR:
        value = s->lintocr;
        break;
    case LINFBRR:
        value = s->linfbrr;
        break;
    case LINIBRR:
        value = s->linibrr;
        break;
    case LINCFR:
        value = s->lincfr;
        break;
    case LINCR2:
        value = s->lincr2;
        break;
    case BIDR:
        value = s->bidr;
        break;
    case BDRL:
        value = s->bdrl;
        break;
    case BDRM:
        value = s->bdrm;
        break;
    case IFER:
        value = s->ifer;
        break;
    case IFMI:
        value = s->ifmi;
        break;
    case IFMR:
        value = s->ifmr;
        break;
    case GCR:
        value = s->gcr & ~BIT(0);
        break;
    case UARTPTO:
        value = s->uartpto;
        break;
    case UARTCTO:
        value = s->uartcto;
        break;
    case DMATXE:
        value = s->dmatxe;
        break;
    case DMARXE:
        value = s->dmarxe;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_LINFLEXD_SERIAL, __func__, offset);
        return 0;
    }
    return value;
}

static void linflexd_clear_w1cbits(uint32_t *reg, int *w1c, int num)
{
    for(int i = 0; i < num; ++i) {
        if (*reg & BIT(w1c[i])) 
            *reg &= ~BIT(w1c[i]);
    }
}

static void linflexd_serial_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    LINFLEXDSerialState *s = (LINFLEXDSerialState *)opaque;
    Chardev *chr = qemu_chr_fe_get_driver(&s->chr);
    DPRINTF("write(offset=0x%" HWADDR_PRIx ", value = 0x%x) to %s\n",
            offset, (unsigned int)value, chr ? chr->label : "NODEV");

    switch (offset) {
    case LINCR1:
        s->lincr1 = ((value | LINCR1_SET_MASK) & ~LINCR1_CLR_MASK);
        break;
    case LINIER:
        s->linier = value & ~LINIER_CLR_MASK;
        break;
    case LINSR:
    {
        static int linsr_w1c_bits[] = {0,1,2,5,8,9};
        s->linsr = (value | LINSR_SET_MASK) & ~LINSR_CLR_MASK;
        // Clear all W1C Bits if set
        linflexd_clear_w1cbits(&s->linsr, linsr_w1c_bits, ARRAY_SIZE(linsr_w1c_bits));
    }
    break;
    case LINESR:
    {
        static int linesr_w1c_bits[] = {0,7,8,9,10,11,12,13,14,15};
        s->linesr = value & ~LINESR_CLR_MASK;
        // Clear all W1C Bits if set
        linflexd_clear_w1cbits(&s->linesr, linesr_w1c_bits, ARRAY_SIZE(linesr_w1c_bits));
    }
        break;
    case UARTCR:
        if (s->mode == LINFLEXD_INIT) {
            bool uart_mode = value & BIT(0);
            if (!uart_mode && (value & BIT(4)))
                value &= ~BIT(4);
            if (!uart_mode && (value & BIT(5)))
                value &= ~BIT(5);
            s->uartcr = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Try write Register UARTCR in non-init mode 0x%"
                          HWADDR_PRIx " value: 0x%lx\n", TYPE_LINFLEXD_SERIAL, __func__, offset, value);
        }
        break;
    case UARTSR:
    {
        static int uartsr_w1c_bits[] = {0,1,2,3,5,7,8,9,10,11,12,13,14,15};
        s->uartsr = value | ~UARTSR_CLR_MASK;
        // Clear all W1C Bits if set
        linflexd_clear_w1cbits(&s->uartsr, uartsr_w1c_bits, ARRAY_SIZE(uartsr_w1c_bits));
    }
        break;
    case LINTCSR:
        s->lintcsr = value & ~LINTCSR_CLR_MASK;
        break;
    case LINOCR:
        s->linocr = value & ~LINOCR_CLR_MASK;
        break;
    case LINTOCR:
        s->lintocr = value & ~LINTOCR_CLR_MASK;
        break;
    case LINFBRR:
        s->linfbrr = value & ~LINFBRR_CLR_MASK;
        break;
    case LINIBRR:
        s->linibrr = value & ~LINIBRR_CLR_MASK;
        break;
    case LINCFR:
        s->lincfr = value & 0xff;
        break;
    case LINCR2:
        s->lincr2 = value & (0xff << 8);
        break;
    case BIDR:
        s->bidr = value & ~BIDR_CLR_MASK;
        break;
    case BDRL:
        s->bdrl = value;
        break;
    case BDRM:
        s->bdrm = value;
        break;
    case IFER:
        s->ifer = value;
        break;
    case IFMI:
        s->ifmi = value;
        break;
    case IFMR:
        s->ifmr = value;
        break;
    case GCR:
        if (s->mode == LINFLEXD_INIT) {
            s->gcr = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Try writing Register GCR in non-init mode 0x%"
                          HWADDR_PRIx " value: 0x%lx\n", TYPE_LINFLEXD_SERIAL, __func__, offset, value);
        }
        break;
    case UARTPTO:
        s->uartpto = value & 0xfff;
        break;
    case UARTCTO:
        s->uartcto = value & 0xfff;
        break;
    case DMATXE:
        s->dmatxe = value & DMATXE_DRE0;
        break;
    case DMARXE:
        s->dmarxe = value & DMARXE_DRE0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_LINFLEXD_SERIAL, __func__, offset);
    }
}

static int linflexd_can_receive(void *opaque)
{
    LINFLEXDSerialState *s = (LINFLEXDSerialState *)opaque;
    return (s->mode == LINFLEXD_NORMAL) && (s->uartcr & LINFLEXD_UARTCR_RXEN);
}

static void linflexd_put_data(void *opaque, uint32_t value)
{
    LINFLEXDSerialState *s = (LINFLEXDSerialState *)opaque;

    DPRINTF("received char\n");

    linflexd_update(s);
}

static void linflexd_receive(void *opaque, const uint8_t *buf, int size)
{
    LINFLEXDSerialState *s = (LINFLEXDSerialState *)opaque;

    if (s->uartcr & UART_RX_FIFO_MODE) {
        DPRINTF("Received Char in FIFO MODE\n");
    } else {
        DPRINTF("Received Char in Buffer MODE\n");
    }
    for (int i = 0; i < size; ++i)
        linflexd_put_data(opaque, buf[i]);
}

static void linflexd_event(void *opaque, QEMUChrEvent event)
{
    switch(event) {
    case CHR_EVENT_BREAK:
        printf("%s: ---> CHR_EVENT_BREAK\n", __FUNCTION__);
        break;
    case CHR_EVENT_OPENED:
        printf("%s: ---> CHR_EVENT_OPENED\n", __FUNCTION__);
        break;
    case CHR_EVENT_MUX_IN:
        printf("%s: ---> CHR_EVENT_MUX_IN\n", __FUNCTION__);
        break;
    case CHR_EVENT_MUX_OUT:
        printf("%s: ---> CHR_EVENT_MUX_OUT\n", __FUNCTION__);
        break;
    case CHR_EVENT_CLOSED:
        printf("%s: ---> CHR_EVENT_CLOSED\n", __FUNCTION__);
        break;
    }
}


static const struct MemoryRegionOps linflexd_serial_ops = {
    .read = linflexd_serial_read,
    .write = linflexd_serial_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void linflexd_serial_rx_fifo_ageing_timer_int(void *opaque)
{
    /* LINFLEXDSerialState *s = (LINFLEXDSerialState *) opaque; */
    DPRINTF("%s\n", __FUNCTION__);
}

static void linflexd_serial_realize(DeviceState *dev, Error **errp)
{
    LINFLEXDSerialState *s = LINFLEXD_SERIAL(dev);

    timer_init_ns(&s->ageing_timer, QEMU_CLOCK_VIRTUAL,
                  linflexd_serial_rx_fifo_ageing_timer_int, s);

    DPRINTF("char dev for uart: %p\n", qemu_chr_fe_get_driver(&s->chr));

    qemu_chr_fe_set_handlers(&s->chr, linflexd_can_receive, linflexd_receive,
                             linflexd_event, NULL, s, NULL, true);
}

static void linflexd_serial_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    LINFLEXDSerialState *s = LINFLEXD_SERIAL(obj);

    memory_region_init_io(&s->iomem, obj, &linflexd_serial_ops, s,
                          TYPE_LINFLEXD_SERIAL, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property linflexd_serial_properties[] = {
    DEFINE_PROP_CHR("chardev", LINFLEXDSerialState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void linflexd_serial_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = linflexd_serial_realize;
    dc->vmsd = &vmstate_linflexd_serial;
    dc->reset = linflexd_serial_reset_at_boot;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "LinFlexD UART";
    device_class_set_props(dc, linflexd_serial_properties);
}

static const TypeInfo linflexd_serial_info = {
    .name           = TYPE_LINFLEXD_SERIAL,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(LINFLEXDSerialState),
    .instance_init  = linflexd_serial_init,
    .class_init     = linflexd_serial_class_init,
};

static void linflexd_serial_register_types(void)
{
    type_register_static(&linflexd_serial_info);
}

type_init(linflexd_serial_register_types)
