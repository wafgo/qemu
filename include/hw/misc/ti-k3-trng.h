/*
 * TI K3 SA2UL TRNG (EIP-76) stub (AM64x)
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_TRNG_H
#define HW_MISC_TI_K3_TRNG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_TRNG "ti-k3-trng"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3TrngState, TI_K3_TRNG)

/*
 * EIP-76 register window size, confirmed two ways:
 *  - OP-TEE 4.1.0's core/arch/arm/plat-k3/drivers/sa2ul_rng.c reads/writes
 *    offsets 0x00..0x7C (RNG_EIP_REV is the last register, at 0x7C).
 *  - hw/arm/ti-am64x.c already carried a TRM-derived
 *    ADD_MAIN_UNIMP("SA2_UL0_EIP_76", 0x40910000ULL, 0x80ULL) placeholder
 *    (removed by this device) with that exact size.
 */
#define TI_K3_TRNG_REGS_SIZE 0x80

struct TIK3TrngState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* xorshift64 generator state, never zero (seeded at reset). */
    uint64_t rng_state;
    /* Cached 64-bit halves backing OUTPUT_0/1 and OUTPUT_2/3. */
    uint64_t pair_a;
    uint64_t pair_b;

    /* RAM-backed register file for everything but OUTPUT_x and STATUS. */
    uint32_t regs[TI_K3_TRNG_REGS_SIZE / 4];
};

#endif
