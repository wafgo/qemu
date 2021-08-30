#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"

#define TYPE_ARMV8_MM_GC "armv8-mm-gc"
OBJECT_DECLARE_SIMPLE_TYPE(ARMV8_MM_GCState, ARMV8_MM_GC)

struct ARMV8_MM_GCState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    MemoryRegion ctrl_base;
    uint32_t freq;
    qemu_irq irq;
};
