/*
 * Renesas RCar Gen3 Clock Pulse Generator Emulation
 *
 * Copyright (C) 2021 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/misc/rcar3_clk.h"

typedef struct Rcar3CPGReg {
    const char   *name; 
    uint32_t     offset;
    uint32_t     reset_value;
    void     (*modify_before_read)(RCar3ClkState *s, int idx);
} RCar3CPGReg;

static void rcar3_clk_pllecr_modify_before_read(RCar3ClkState *s, int idx);

/* CPG Register Base: 0xe6150000 */
static const RCar3CPGReg rcar3_clk_regs[] = {
    {"CPGWPCR",                    0x904, 0x0},
    {"CPGWPR",                     0x900, 0x0},
    {"FRQCRB",                     0x4, 0x0},
    {"FRQCRC",                     0xe0, 0x0},
    {"PLLECR",                     0xd0, 0x0, .modify_before_read = rcar3_clk_pllecr_modify_before_read},
    {"PLL0CR",                     0xd8, 0x0},
    {"PLL2CR",                     0x2c, 0x0},
    {"PLL3CR",                     0xdc, 0x0},
    {"PLL4CR",                     0x1f4, 0x0},
    {"PLL0STPCR",                  0xf0, 0x0},
    {"PLL2STPCR",                  0xf8, 0x0},
    {"PLL3STPCR",                  0xfc, 0x0},
    {"PLL4STPCR",                  0x1f8, 0x0},
    {"SD0CKCR",                    0x74, 0x0},
    {"SD1CKCR",                    0x78, 0x0},
    {"SD2CKCR",                    0x268, 0x0},
    {"SD3CKCR",                    0x26c, 0x0},
    {"RPCCKCR",                    0x238, 0x0},
    {"SSPSRCCKCR",                 0x248, 0x0},
    {"SSPRSCKCR",                  0x24c, 0x0},
    {"CANFDCKCR",                  0x244, 0x0},
    {"MSOCKCR",                    0x14, 0x0},
    {"HDMICKCR",                   0x250, 0x0},
    {"CSI0CKCR",                   0xc, 0x0},
    {"RCKCR",                      0x240, 0x0},
    {"POSTCKCR",                   0x8c, 0x0},
    {"POST2CKCR",                  0x9c, 0x0},
    {"LV0CKCR",                    0x4cc, 0x0},
    {"LV1CKCR",                    0x4d0, 0x0},
    {"ZA2CKCR",                    0x4dc, 0x0},
    {"ZA8CKCR",                    0x4e0, 0x0},
    {"Z2DCKCR",                    0x4e8, 0x0},
    {"FRQCRD",                     0xe4, 0x0},
    {"ZB3CKCR",                    0x380, 0x0},
    {"POST4CKCR",                  0x260, 0x0},
    {"STAEMON",                    0x108, 0x0},
};



static void rcar3_clk_pllecr_modify_before_read(RCar3ClkState *s, int idx)
{
  s->reg[idx] |= BIT(9);
  /* if (s->reg[idx] & BIT(0)) */
    s->reg[idx] |= BIT(8);

  /* if (s->reg[idx] & BIT(2)) */
    s->reg[idx] |= BIT(10);
  
  /* if (s->reg[idx] & BIT(3)) */
    s->reg[idx] |= BIT(11);

  /* if (s->reg[idx] & BIT(4)) */
    s->reg[idx] |= BIT(12);

    //  printf("-----------> PLLECR returns 0x%x\n",s->reg[idx]);
}

static uint64_t rcar3_clk_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    RCar3ClkState *s = (RCar3ClkState *)opaque;
    const RCar3CPGReg *regs = rcar3_clk_regs;
    unsigned int i;

    for (i = 0; i < s->num_reg; i++) {
        if (regs->offset == offset) {
	  if (regs->modify_before_read != NULL)
	    regs->modify_before_read(s, i);
	  return s->reg[i];
        }
        regs++;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%04x\n",
                  __func__, (uint32_t)offset);
    return 0;
}

static void rcar3_clk_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    RCar3ClkState *s = (RCar3ClkState *)opaque;
    const RCar3CPGReg *regs = rcar3_clk_regs;
    unsigned int i;

    for (i = 0; i < s->num_reg; i++) {
        if (regs->offset == offset) {
            s->reg[i] = val;
            return;
        }
        regs++;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%04x\n",
                  __func__, (uint32_t)offset);
}

static const MemoryRegionOps rcar3_clk_ops = {
    .read = rcar3_clk_read,
    .write = rcar3_clk_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    }
};

static void rcar3_clk_reset(DeviceState *dev)
{
    RCar3ClkState *s = RCAR3_CLK(dev);
    unsigned int i;

    /* Set default values for registers */
    for (i = 0; i < s->num_reg; i++) {
        s->reg[i] = rcar3_clk_regs[i].reset_value;
    }
}

static void rcar3_clk_init(Object *obj)
{
    RCar3ClkState *s = RCAR3_CLK(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    s->reg = g_new0(uint32_t, ARRAY_SIZE(rcar3_clk_regs));
    s->num_reg = ARRAY_SIZE(rcar3_clk_regs);
    /* memory mapping */
    memory_region_init_io(&s->iomem, obj, &rcar3_clk_ops, s,
                          TYPE_RCAR3_CLK, 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void rcar3_clk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = rcar3_clk_reset;
}

static const TypeInfo rcar3_clk_info = {
    .name          = TYPE_RCAR3_CLK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RCar3ClkState),
    .instance_init = rcar3_clk_init,
    .class_init    = rcar3_clk_class_init,
};

static void rcar3_clk_register(void)
{
    qemu_log_mask(LOG_GUEST_ERROR, "Clock init\n");
    type_register_static(&rcar3_clk_info);
}

type_init(rcar3_clk_register)
