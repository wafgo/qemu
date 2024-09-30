#include "qemu/osdep.h"
#include "hw/char/nxp_linflexd.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define DEBUG_NXP_LINFLEXD 0

#ifndef DEBUG_NXP_LINFLEXD
#define DEBUG_NXP_LINFLEXD 0
#endif

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

static void linflexd_write_console(LinFlexDState *s, uint32_t value)
{
    unsigned char ch = value & 0xFF;
    printf("%c", ch);
    fflush(stdout);
}

static uint64_t linflexd_read(void *opaque, hwaddr offset, unsigned size)
{
    LinFlexDState *s = (LinFlexDState *)opaque;
    uint32_t ret = 0;

    switch (offset) {
    case LINFLEXD_LINSR:
        ret = 0x1000; //Data Reception/Data Transmission S32G2 Reference Manual, Rev. 2, April 2021 Pg 2229
        break;
    case LINFLEXD_UARTSR:
        ret = 0x0002; //Data Transmission Completed Flag/ TX FIFO Full Flag S32G2 Reference Manual, Rev. 2, April 2021 Pg 2240
        break;
    case LINFLEXD_LINCR1:
    case LINFLEXD_LINIER:
    case LINFLEXD_LINESR:
    case LINFLEXD_UARTCR:
    case LINFLEXD_LINTCSR:
    case LINFLEXD_LINOCR:
    case LINFLEXD_LINTOCR:
    case LINFLEXD_LINFBRR:
    case LINFLEXD_LINIBRR:
    case LINFLEXD_LINCFR:
    case LINFLEXD_LINCR2:
    case LINFLEXD_BIDR:
    case LINFLEXD_BDRL:
    case LINFLEXD_BDRM:
    case LINFLEXD_IFER:
    case LINFLEXD_IFMI:
    case LINFLEXD_IFMR:
    case LINFLEXD_GCR:
    case LINFLEXD_UARTPTO:
    case LINFLEXD_UARTCTO:
    case LINFLEXD_DMATXE:
    case LINFLEXD_DMARXE:
        ret = s->regs[offset / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "linflexd_read: Bad offset %"HWADDR_PRIx"\n", offset);
        break;
    }

    DPRINTF("%s offset: 0x%" HWADDR_PRIx ", value: 0x%lx \n", __FUNCTION__, offset, ret);
    return ret;
}

static void linflexd_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    LinFlexDState *s = (LinFlexDState *)opaque;

    switch (offset) {
    case LINFLEXD_LINCR1:
    case LINFLEXD_LINIER:
    case LINFLEXD_LINSR:
    case LINFLEXD_LINESR:
    case LINFLEXD_UARTCR:
    case LINFLEXD_UARTSR:
    case LINFLEXD_LINTCSR:
    case LINFLEXD_LINOCR:
    case LINFLEXD_LINTOCR:
    case LINFLEXD_LINFBRR:
    case LINFLEXD_LINIBRR:
    case LINFLEXD_LINCFR:
    case LINFLEXD_LINCR2:
    case LINFLEXD_BIDR:
    case LINFLEXD_IFER:
    case LINFLEXD_IFMI:
    case LINFLEXD_IFMR:
    case LINFLEXD_GCR:
    case LINFLEXD_UARTPTO:
    case LINFLEXD_UARTCTO:
    case LINFLEXD_DMATXE:
    case LINFLEXD_DMARXE:
        s->regs[offset / 4] = value;
        break;
    case LINFLEXD_BDRL:
    case LINFLEXD_BDRM:
        s->regs[offset / 4] = value;
        linflexd_write_console(s, value);
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
    // TODO: Implement proper flow control
    return 1;
}

static void linflexd_receive(void *opaque, const uint8_t *buf, int size)
{
    LinFlexDState *s = (LinFlexDState *)opaque;
    // TODO: Implement proper receive logic
    if (size > 0) {
        s->regs[LINFLEXD_BDRL / 4] = buf[0];
        linflexd_write_console(s, buf[0]);
    }
    linflexd_update_irq(s);
}

static void linflexd_event(void *opaque, QEMUChrEvent event)
{
    // TODO: Handle UART events
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

static void linflexd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = linflexd_realize;
    dc->desc = "LinFlexD UART";
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