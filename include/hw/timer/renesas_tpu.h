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

#define TYPE_RENESAS_TPU "renesas.tpu"
OBJECT_DECLARE_SIMPLE_TYPE(RenesasTPUState, RENESAS_TPU)

struct RenesasTPUState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
};
