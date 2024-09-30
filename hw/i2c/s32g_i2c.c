#include "qemu/osdep.h"
#include "hw/i2c/s32g_i2c.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"

#define I2C_IBCR_MDIS  (1 << 7)  // Module Disable
#define I2C_IBCR_IBIE  (1 << 6)  // I-Bus Interrupt Enable
#define I2C_IBCR_MSSL  (1 << 5)  // Master/Slave Mode Select Bit
#define I2C_IBCR_TXRX  (1 << 4)  // Transmit/Receive Mode Select Bit
#define I2C_IBCR_NOACK (1 << 3)  // No Acknowledge
#define I2C_IBCR_RSTA  (1 << 2)  // Repeat Start

#define I2C_IBSR_TCF   (1 << 7)  // Transfer Complete Flag
#define I2C_IBSR_IAAS  (1 << 6)  // Addressed As A Slave
#define I2C_IBSR_IBB   (1 << 5)  // I2C Bus Busy
#define I2C_IBSR_IBAL  (1 << 4)  // Arbitration Lost
#define I2C_IBSR_SRW   (1 << 2)  // Slave Read/Write
#define I2C_IBSR_IBIF  (1 << 1)  // I-Bus Interrupt
#define I2C_IBSR_RXAK  (1 << 0)  // Received Acknowledge


#define DEBUG_NXP_I2C 0

#ifndef DEBUG_NXP_I2C
#define DEBUG_NXP_I2C 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_NXP_I2C) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_S32_I2C, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *s32g_i2c_get_regname(unsigned offset)
{
    switch (offset) {
    case S32G_I2C_IBAD:
        return "IBAD";
    case S32G_I2C_IBFD:
        return "IBFD";    
    case S32G_I2C_IBCR:
        return "IBCR";
    case S32G_I2C_IBSR:
        return "IBSR";
    case S32G_I2C_IBDR:
        return "IBDR";
    case S32G_I2C_IBIC:        
        return "IBIC";
    case S32G_I2C_IBDBG:
        return "IBDBG";
    default:
        return "[?]";
    }
}


static void s32g_i2c_update_irq(S32GI2CState *s)
{
    bool irq_state = (s->regs[S32G_I2C_IBSR] & I2C_IBSR_IBIF) &&
                     (s->regs[S32G_I2C_IBCR] & I2C_IBCR_IBIE);
    qemu_set_irq(s->irq, irq_state);
}

static uint64_t s32g_i2c_read(void *opaque, hwaddr offset, unsigned size)
{
    S32GI2CState *s = S32G_I2C(opaque);
    uint64_t ret = 0;

    switch (offset) {
    case S32G_I2C_IBAD:
    case S32G_I2C_IBFD:
    case S32G_I2C_IBSR:
    case S32G_I2C_IBCR:
    case S32G_I2C_IBDR:
    case S32G_I2C_IBIC:
        ret = s->regs[offset];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "s32g_i2c_read: Bad offset %"HWADDR_PRIx"\n", offset);
        break;
    }

    DPRINTF("reg: %s, value: 0x%lx \n", s32g_i2c_get_regname(offset), ret);
    return ret;
}

static void s32g_i2c_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    S32GI2CState *s = S32G_I2C(opaque);

    DPRINTF("reg: %s, value: 0x%lx \n", s32g_i2c_get_regname(offset), value);

    switch (offset) {
    case S32G_I2C_IBAD:
    case S32G_I2C_IBFD:
    case S32G_I2C_IBIC:
        s->regs[offset] = value;
        break;
    case S32G_I2C_IBCR:
        s->regs[offset] = value;
        if (value & I2C_IBCR_MSSL) {
            // Master mode operations
            // TODO: Implement I2C master operations
        }
        break;
    case S32G_I2C_IBSR:
        // Clear flags by writing 1
        s->regs[offset] &= ~(value & (I2C_IBSR_IBIF | I2C_IBSR_IBAL));
        printf("I2C Interrupt cleared \n");
        break;
    case S32G_I2C_IBDR:
        s->regs[offset] = value;

        printf("I2C TX: %" PRIx64 "\n", value);
        // set IBIF Bus interrupt flag & Transfer complete flag
        s->regs[S32G_I2C_IBSR] = 0x0082; // S32G2 Reference Manual, Rev. 2, Pg 2136
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "s32g_i2c_write: Bad offset %"HWADDR_PRIx"\n", offset);
        break;
    }

    s32g_i2c_update_irq(s);
}

static const MemoryRegionOps s32g_i2c_ops = {
    .read = s32g_i2c_read,
    .write = s32g_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s32g_i2c_reset(DeviceState *dev)
{
    S32GI2CState *s = S32G_I2C(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[S32G_I2C_IBSR] = I2C_IBSR_TCF;
}

static void s32g_i2c_realize(DeviceState *dev, Error **errp)
{
    S32GI2CState *s = S32G_I2C(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &s32g_i2c_ops, s,
                          TYPE_S32_I2C, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->bus = i2c_init_bus(dev, "i2c");
}

static const VMStateDescription vmstate_s32g_i2c = {
    .name = TYPE_S32_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, S32GI2CState, S32G_I2C_REG_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static void s32g_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32g_i2c_realize;
    dc->reset = s32g_i2c_reset;
    dc->vmsd = &vmstate_s32g_i2c;
    dc->desc = "NXP S32G I2C Controller";
}

static const TypeInfo s32g_i2c_info = {
    .name          = TYPE_S32_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32GI2CState),
    .class_init    = s32g_i2c_class_init,
};

static void s32g_i2c_register_types(void)
{
    type_register_static(&s32g_i2c_info);
}

type_init(s32g_i2c_register_types)