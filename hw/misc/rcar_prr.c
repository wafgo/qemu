/*
 * Copyright (c) 2020 Wadim Mueller <wadim.mueller@mail.de>
 *
 * rcar PRR (Product Revision Register) emulation
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
#include <stdbool.h>
#include "hw/misc/rcar_prr.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"

static uint64_t rcar_prr_read(void *opaque, hwaddr offset,
                           unsigned size)
{
  RCarPrrRegisterState *s = (RCarPrrRegisterState*) opaque;
  return s->reg_val;
}

static void rcar_prr_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
  printf("PRR Register is RO\n");
}

                           
static const MemoryRegionOps rcar_prr_ops = {
    .read = rcar_prr_read,
    .write = rcar_prr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void rcar_prr_realize(DeviceState *dev, Error **errp)
{
    RCarPrrRegisterState *s = RCAR_PRR(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    uint8_t ca57en = 0xff, ca53en = 0xff;
    uint8_t ca57_max = 4;
    uint8_t ca53_max = 4;
    bool no_ca57 = false;
    bool no_ca53 = false;
    
    int i;

    switch(s->product_id) {
    case RCAR_M3N_PROD_ID:
      s->a53_num = 0;
      s->a57_num = 2;
      ca53_max = 0;
      ca57_max = 2;
      no_ca53 = true;
      break;
    case RCAR_M3W_PROD_ID:
      ca57_max = 2;
      break;
    case RCAR_V3M_PROD_ID:
      s->a57_num = 0;
      ca57_max = 0;
      ca53_max = 2;
      no_ca57 = true;
      break;
    case RCAR_V3H_PROD_ID:
      s->a57_num = 0;
      ca57_max = 0;
      ca53_max = 4;
      no_ca57 = true;
      break;
     case RCAR_D3_PROD_ID:
      s->a57_num = 0;
      s->a53_num = 1;
      ca57_max = 0;
      ca53_max = 1;
      no_ca57 = true;
      break;
    case RCAR_E3_PROD_ID:
      s->a57_num = 0;
      ca57_max = 0;
      ca53_max = 2;
      no_ca57 = true;
      break;
    }
    
    if (no_ca57 == false && s->a57_num == ca57_max)
      ca57en &= ~(1 << 4);
    if (no_ca53 == false && s->a53_num == ca53_max)
      ca53en &= ~(1 << 4);
      
    for (i = 0; i < s->a57_num; ++i) {
      ca57en &= ~(1 << i);
    }
    for (i = 0; i < s->a53_num; ++i) {
      ca53en &= ~(1 << i);
    }

    s->reg_val = ((ca57en << 27) & (0x1f << 27)) | ((ca53en << 22) & (0x1f << 22)) | ((!!!s->cr7_available << 21) & (1 << 21)) | ((s->product_id << 8) & 0x0000ff00) | (s->cut & 0xff);
    memory_region_init_io(&s->iomem, OBJECT(dev), &rcar_prr_ops, s,
                          "rcar product register", 4);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property rcar_prr_properties[] = {
    DEFINE_PROP_UINT8("chip-id", RCarPrrRegisterState, product_id, RCAR_H3_PROD_ID),
    DEFINE_PROP_UINT8("cut", RCarPrrRegisterState, cut, 0x10),
    DEFINE_PROP_UINT8("cr7", RCarPrrRegisterState, cr7_available, 1),
    DEFINE_PROP_UINT8("ca57-cores", RCarPrrRegisterState, a57_num, 4),
    DEFINE_PROP_UINT8("ca53-cores", RCarPrrRegisterState, a53_num, 4),
    DEFINE_PROP_END_OF_LIST(),
};

static void rcar_prr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = rcar_prr_realize;
    device_class_set_props(k, rcar_prr_properties);
}

static const TypeInfo rcar_prr_info = {
    .name          = TYPE_RCAR_PRR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RCarPrrRegisterState),
    .class_init    = rcar_prr_class_init,
};

static void rcar_prr_register_types(void)
{
  type_register_static(&rcar_prr_info);
}

type_init(rcar_prr_register_types)

