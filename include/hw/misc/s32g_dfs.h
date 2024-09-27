/*
 * S32 Digital Frequency Synthesizer
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef S32G_DFS_H_
#define S32G_DFS_H_

#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

#define DFS_PORTSR 0xc
#define DFS_PORTLOLSR 0x10
#define DFS_PORTRESET 0x14
#define DFS_CTL 0x18
#define DFS_DVPORT0 0x1C
#define DFS_DVPORT1 0x20
#define DFS_DVPORT2 0x24
#define DFS_DVPORT3 0x28
#define DFS_DVPORT4 0x2C
#define DFS_DVPORT5 0x30

#define DFS_NUM_PORTS 6

#define TYPE_S32_DFS "s32.dfs"

OBJECT_DECLARE_SIMPLE_TYPE(S32DFSState, S32_DFS)

struct S32DFSState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    uint32_t portsr;
    uint32_t portlolsr;
    uint32_t portreset;
    uint32_t ctl;
    uint32_t dvport[DFS_NUM_PORTS];
    uint32_t no_divs;
};

#endif
