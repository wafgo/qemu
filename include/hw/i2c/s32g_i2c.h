#ifndef HW_I2C_S32G_I2C_H
#define HW_I2C_S32G_I2C_H

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"

#define TYPE_S32_I2C "s32.i2c"
#define S32G_I2C(obj) OBJECT_CHECK(S32GI2CState, (obj), TYPE_S32_I2C)

// Register offsets
#define S32G_I2C_IBAD      0x00  // I2C Bus Address Register
#define S32G_I2C_IBFD      0x01  // I2C Bus Frequency Divider Register
#define S32G_I2C_IBCR      0x02  // I2C Bus Control Register
#define S32G_I2C_IBSR      0x03  // I2C Bus Status Register
#define S32G_I2C_IBDR      0x04  // I2C Bus Data I/O Register
#define S32G_I2C_IBIC      0x05  // I2C Bus Interrupt Config Register
#define S32G_I2C_IBDBG     0x06  // I2C Bus Interrupt Config Register

#define S32G_I2C_REG_COUNT 6

typedef struct S32GI2CState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;
    uint32_t regs[S32G_I2C_REG_COUNT];
} S32GI2CState;

#endif /* HW_I2C_NXP_S32G_I2C_H */