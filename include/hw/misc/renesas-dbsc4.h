#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include <stdbool.h>

#define TYPE_RENESAS_DBSC4             "renesas.dbsc4"
OBJECT_DECLARE_SIMPLE_TYPE(RenesasDBSC4State, RENESAS_DBSC4)
     
struct RenesasDBSC4State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t *reg;
    uint32_t phy_regs[40000];
    uint32_t num_reg;
    bool phy_reg_written;
    bool dfi_started;
};
