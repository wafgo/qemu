#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_RCAR3_SYSC             "rcar3.sysc"
OBJECT_DECLARE_SIMPLE_TYPE(RCar3SyscState, RCAR3_SYSC)
     
struct RCar3SyscState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t reg[0x8000/4];
};
