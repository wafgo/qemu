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
#include "hw/misc/rcar3_sysc.h"

static uint64_t rcar3_sysc_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    RCar3SyscState *s = (RCar3SyscState *)opaque;
    uint32_t reg_idx = offset/4;
    //printf("----> TRYING TO READ SYSC REG 0x%lx\n", offset);
    if (reg_idx >= ARRAY_SIZE(s->reg)) {
      qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%04x\n", __func__,
                    (uint32_t)offset);
      abort();
    } else {
      switch (offset) {
      case 0x100: //PWRSR2
	return 0x7c;//(BIT(5) | BIT(6));
      case 0x180: //PWRSR4
      case 0x340: //PWRSR8
      case 0x380: //PWRSR9
	return BIT(4);
      case 0x3c0: //PWRSR10
	return BIT(3);
      default:
	return s->reg[reg_idx];

      }
    }
    return 0;
}

static void rcar3_sysc_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    RCar3SyscState *s = (RCar3SyscState *)opaque;
    uint32_t reg_idx = offset/4;
    if (reg_idx >= ARRAY_SIZE(s->reg)) {
      qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%04x\n", __func__,
                    (uint32_t)offset);
      abort();
    } else {
      s->reg[reg_idx] = val;
    }
}

static const MemoryRegionOps rcar3_sysc_ops = {
    .read = rcar3_sysc_read,
    .write = rcar3_sysc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    }
};


static void rcar3_sysc_init(Object *obj)
{
    RCar3SyscState *s = RCAR3_SYSC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    /* memory mapping */
    memory_region_init_io(&s->iomem, obj, &rcar3_sysc_ops, s,
                          TYPE_RCAR3_SYSC, 0x10000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void rcar3_sysc_class_init(ObjectClass *klass, void *data)
{
    /* DeviceClass *dc = DEVICE_CLASS(klass); */
    return;
}

static const TypeInfo rcar3_sysc_info = {
    .name          = TYPE_RCAR3_SYSC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RCar3SyscState),
    .instance_init = rcar3_sysc_init,
    .class_init    = rcar3_sysc_class_init,
};

static void rcar3_sysc_register(void)
{
    type_register_static(&rcar3_sysc_info);
}

type_init(rcar3_sysc_register)
