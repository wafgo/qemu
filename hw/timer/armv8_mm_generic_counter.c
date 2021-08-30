/*
 * Memory mapped ARM-v8 generic counter implementation
 *
 * Copyright (c) 2021 Wadim Mueller (wadim.mueller@continental-corporation.com)
 *
 * This code is licensed under the GNU LGPL.
 */


#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/timer/armv8_mm_generic_counter.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"

//#define DEBUG_ARMV8_MM_GC

static uint64_t armv8_mm_gc_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    ARMV8_MM_GCState *s = ARMV8_MM_GC(opaque);
    uint64_t reg_val;
    switch(offset) {
    case 0x08:
	reg_val = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / (NANOSECONDS_PER_SECOND / (uint64_t) s->freq);
#ifdef DEBUG_ARMV8_MM_GC
        printf("Reading Timer value 0x%lx 0x%lx\n", offset, reg_val);
#endif
        break;
    case 0xfd0:
        reg_val = 0x4;
        break;
    case 0xfe0:
        reg_val = 0x1;
        break;
    case 0xfe4:
    case 0xffc:
        reg_val = 0xb1;
        break;
    case 0xfe8:
        reg_val = 0x1b;
        break;
    case 0xff0:
        reg_val = 0xd;
        break;
    case 0xff4:
        reg_val = 0xf0;
        break;
    case 0xff8:
        reg_val = 0x5;
        break;
    case 0x20: // CNTFID0
        reg_val = s->freq;
        break;
    default:
        reg_val = 0;
        break;
    }
#ifdef DEBUG_ARMV8_MM_GC
    printf("%s reading at offset 0x%lx (len = 0x%x)\n", __FUNCTION__, offset, size);
#endif
    
    return reg_val;
}

static void armv8_mm_gc_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    //ARMV8_MM_GCState *s = ARMV8_MM_GC(opaque);
    //printf("%s writing at offset 0x%lx (len = 0x%x) value = 0x%lx\n", __FUNCTION__, offset, size, value);
}

static const MemoryRegionOps armv8_mm_gc_ops = {
    .read = armv8_mm_gc_read,
    .write = armv8_mm_gc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.max_access_size = 8,
};

static void armv8_mm_gc_init(Object *obj)
{
    /* ARMV8_MM_GCState *s = ARMV8_MM_GC(obj); */
}

static void armv8_mm_gc_realize(DeviceState *dev, Error **errp)
{
    ARMV8_MM_GCState *s = ARMV8_MM_GC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(dev), &armv8_mm_gc_ops, s,
                          "armv8_memory_mapped_generic_counter", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property armv8_mm_gc_properties[] = {
    DEFINE_PROP_UINT32("freq", ARMV8_MM_GCState, freq, 1000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void armv8_mm_gc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = armv8_mm_gc_realize;
    device_class_set_props(k, armv8_mm_gc_properties);
}

static const TypeInfo armv8_mm_gc_info = {
    .name          = TYPE_ARMV8_MM_GC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMV8_MM_GCState),
    .instance_init = armv8_mm_gc_init,
    .class_init    = armv8_mm_gc_class_init,
};

static void armv8_mm_generic_timer_register_types(void)
{
  type_register_static(&armv8_mm_gc_info);
}

type_init(armv8_mm_generic_timer_register_types)
