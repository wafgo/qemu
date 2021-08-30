/*
 *  Copyright (c) 2020 Wadim Mueller <wadim.mueller@mail.de>
 *
 *  Rcar Mode Montior Register emulation
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */



#include "hw/misc/rcar_rst.h"
#include "hw/qdev-properties.h"
#include "arm-powerctl.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"

static uint64_t rcar_rst_read(void *opaque, hwaddr offset, unsigned size)
{
    RCarRstRegisterState *s = (RCarRstRegisterState*) opaque;
    uint32_t reg_offset = offset/4;
    if (reg_offset >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s invalid register read access to offset 0x%lx\n", __FUNCTION__, offset);
        abort();
    }
    return s->regs[reg_offset];
}

static void rcar_rst_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    RCarRstRegisterState *s = (RCarRstRegisterState*) opaque;
    uint32_t reg_offset = offset/4;
    if (reg_offset >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,"%s invalid register write access to offset 0x%lx = 0x%lx\n", __FUNCTION__, offset, value);
        abort();
    }
    s->regs[reg_offset] = (uint32_t)value;
    if (offset == 0x40) {
        hwaddr secondary_entry = (hwaddr)s->regs[0xd0/4] << 32UL | s->regs[0xd4/4];
        arm_set_cpu_on(1, secondary_entry, 0, 3, 1);
    }
}

                           
static const MemoryRegionOps rcar_rst_ops = {
    .read = rcar_rst_read,
    .write = rcar_rst_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void rcar_rst_realize(DeviceState *dev, Error **errp)
{
    RCarRstRegisterState *s = RCAR_RST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    memory_region_init_io(&s->iomem, OBJECT(dev), &rcar_rst_ops, s,
                          "rcar mode register", 0x1000);
    s->regs[0x60/4] = s->mode_mr;
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property rcar_rst_properties[] = {
  DEFINE_PROP_UINT32("modemr", RCarRstRegisterState, mode_mr, (0xd << 1)),
  DEFINE_PROP_END_OF_LIST(),
};

static void rcar_rst_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = rcar_rst_realize;
    device_class_set_props(k, rcar_rst_properties);
}

static const TypeInfo rcar_rst_info = {
    .name          = TYPE_RCAR_RST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RCarRstRegisterState),
    .class_init    = rcar_rst_class_init,
};

static void rcar_rst_register_types(void)
{
  type_register_static(&rcar_rst_info);
}

type_init(rcar_rst_register_types)

