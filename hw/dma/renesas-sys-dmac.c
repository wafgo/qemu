/*
 * Renesas SYS DMA emulation
 *
 * Author:
 *   Wadim Mueller  <wadim.mueller@continental-corporation.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "hw/dma/renesas-sys-dmac.h"

static uint64_t renesas_sys_dmac_read(void *opaque, hwaddr offset, unsigned size)
{
  printf("---> %s try to read from 0x%lx\n", __FUNCTION__, offset);
    return 0;
}

static void renesas_sys_dmac_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
  printf("---> %s try to write to 0x%lx = 0x%lx\n", __FUNCTION__, offset, value);
}

static const MemoryRegionOps renesas_sys_dmac_ops = {
    .read = renesas_sys_dmac_read,
    .write = renesas_sys_dmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* there are 32-bit and 16-bit wide registers */
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    }
};

static void renesas_sys_dmac_realize(DeviceState *dev, Error **errp)
{
  RenesasSysDmacState *s = RENESAS_SYSDMAC(dev);

  memory_region_init_io(&s->iomem, OBJECT(dev), &renesas_sys_dmac_ops, s, TYPE_RENESAS_SYSDMAC, 0x10000);
  sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

}

static void renesas_sys_dmac_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Renesas System DMA controller";
    dc->realize = renesas_sys_dmac_realize;
}

static const TypeInfo renesas_sys_dmac_info = {
    .name          = TYPE_RENESAS_SYSDMAC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasSysDmacState),
    .class_init    = renesas_sys_dmac_class_init,
};

static void renesas_sdmac_register_types(void)
{
    type_register_static(&renesas_sys_dmac_info);
}

type_init(renesas_sdmac_register_types)
