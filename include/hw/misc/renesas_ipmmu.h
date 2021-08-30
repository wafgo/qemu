/*
 * Copyright (c) 2021 Wadim Mueller <wadim.mueller@mail.de>
 *
 *  renesas ipmmu (iommu) emulation
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RENESAS_IPMMU_H
#define RENESAS_IPMMU_H

#include "hw/registerfields.h"
#include "qom/object.h"

#define TYPE_RENESAS_IPMMU "renesas.ipmmu"

OBJECT_DECLARE_SIMPLE_TYPE(RenesasIPMMUState, RENESAS_IPMMU)

struct RenesasIPMMUState {
    /* <private> */
    SysBusDevice  dev;
    const char *mrtypename;
    MemoryRegion iomem;
    IOMMUMemoryRegion iommu;
    AddressSpace iommu_as;
    bool is_main;
    char *ipmmu_type;
    uint32_t num_utlb;
    uint8_t num_hsb; /* number of highest significant bits in the address, which identify the micro tlb index*/
    struct RenesasIPMMUState *main;
    uint32_t *reg;
    uint32_t ctr_idx;
    uint32_t ttbcr_idx;
    uint32_t ttubr_idx[2];
    uint32_t ttlbr_idx[2];
};

#define TYPE_RENESAS_IPMMU_MEMORY_REGION "renesas-ipmmu-memory-region"

#endif /* RENESAS_IPMMU_H */
