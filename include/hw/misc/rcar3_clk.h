#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_RCAR3_CLK             "rcar3.clk"
OBJECT_DECLARE_SIMPLE_TYPE(RCar3ClkState, RCAR3_CLK)
     
struct RCar3ClkState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t *reg;
    uint32_t num_reg;
};
