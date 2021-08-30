#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/timer/renesas_tpu.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"


static uint64_t renesas_tpu_read(void *opaque, hwaddr offset, unsigned size)
{
  printf("%s reading from offset 0x%lx (len = 0x%x)\n", __FUNCTION__, offset, size);
  return 0;
}

static void renesas_tpu_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    printf("%s writing at offset 0x%lx (len = 0x%x) value = 0x%lx\n", __FUNCTION__, offset, size, value);
}

static const MemoryRegionOps renesas_tpu_ops = {
    .read = renesas_tpu_read,
    .write = renesas_tpu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    }
};


static void renesas_tpu_realize(DeviceState *dev, Error **errp)
{
    RenesasTPUState *s = RENESAS_TPU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(dev), &renesas_tpu_ops, s,
                          "armv8_memory_mapped_generic_counter", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}


static void renesas_tpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = renesas_tpu_realize;
    //device_class_set_props(k, armv8_mm_gc_properties);
}


static void renesas_tpu_init(Object *obj)
{
    /* RenesasTPUState *s = RENESAS_TPU(obj); */
}


static const TypeInfo renesas_tpu_info = {
    .name          = TYPE_RENESAS_TPU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasTPUState),
    .instance_init = renesas_tpu_init,
    .class_init    = renesas_tpu_class_init,
};

static void renesas_tpu_register_types(void)
{
  type_register_static(&renesas_tpu_info);
}

type_init(renesas_tpu_register_types)
