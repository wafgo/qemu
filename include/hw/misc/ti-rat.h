#ifndef TI_RAT_H
#define TI_RAT_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_RAT "ti-rat"
OBJECT_DECLARE_SIMPLE_TYPE(TIRATState, TI_RAT)

#define TI_RAT_NUM_ENTRIES   16
    
typedef struct TIRATEntry {
    bool inserted;

    int idx;
    uint32_t ctrl_reg;
    uint32_t base_reg;
    uint32_t transl_reg;
    uint32_t transh_reg;
    
    uint64_t trans_base;   /* RAT Mapped Address */
    uint64_t size;    /* size in bytes */

    MemoryRegion alias;
} TIRATEntry;

typedef struct TIRATState {
    SysBusDevice parent_obj;

    uint32_t ctrl;
    MemoryRegion *window_root;
    MemoryRegion *target_root;
    MemoryRegion regs_mmio;
    MemoryRegion window_container;
    
    uint64_t window_base;
    uint64_t window_size;
    
    TIRATEntry ent[TI_RAT_NUM_ENTRIES];
} TIRATState;

#endif
