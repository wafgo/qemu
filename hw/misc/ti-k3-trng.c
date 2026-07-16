/*
 * TI K3 SA2UL TRNG (EIP-76) stub (AM64x)
 *
 * Register contract verified against OP-TEE 4.1.0's real driver
 * (core/arch/arm/plat-k3/drivers/sa2ul_rng.c, sa2ul_rng_read128() /
 * sa2ul_rng_init_seq()):
 *
 *   +0x00 OUTPUT_0   (ro)  bits [31:0]  of a fresh 64-bit random pair
 *   +0x04 OUTPUT_1   (ro)  bits [63:32] of that same pair (cached, no
 *                          re-seed on this read)
 *   +0x08 OUTPUT_2   (ro)  bits [31:0]  of a second, independent pair
 *   +0x0C OUTPUT_3   (ro)  bits [63:32] of that second pair
 *   +0x10 STATUS     (ro on read: bit0 RNG_READY, bit1 SHUTDOWN_OFLO)
 *         INTACK     (wo on write: real HW clears STATUS bits; the guest
 *                     only ever ACKs RNG_READY, which we report as
 *                     permanently set, so the write is accepted and
 *                     discarded)
 *   +0x14 CONTROL    (rw, RAM-backed) bit10 ENABLE_TRNG
 *   +0x18 CONFIG     (rw, RAM-backed) refill-cycle thresholds
 *   +0x1C ALARMCNT   (rw, RAM-backed)
 *   +0x20 FROENABLE  (rw, RAM-backed)
 *   +0x24 FRODETUNE  (rw, RAM-backed)
 *   +0x28 ALARMMASK  (rw, RAM-backed)
 *   +0x2C ALARMSTOP  (rw, RAM-backed)
 *   +0x78 OPTIONS    (rw, RAM-backed; driver never reads it during boot)
 *   +0x7C EIP_REV    (rw, RAM-backed; ditto)
 *
 * The driver never gates on the *content* of CONFIG/FROENABLE/etc. (it
 * unconditionally writes them once at sa2ul_rng_init_seq() and never reads
 * them back), so a plain RAM-backed register file is sufficient fidelity
 * for every offset except the four OUTPUT registers and STATUS, which are
 * computed so the driver's poll-then-consume loop always makes forward
 * progress.
 *
 * This device replaces the TRM-derived
 * ADD_MAIN_UNIMP("SA2_UL0_EIP_76", 0x40910000, 0x80) placeholder in
 * hw/arm/ti-am64x.c -- the size (0x80) matches exactly (RNG_EIP_REV at
 * +0x7C is the last register), corroborating the address independently
 * of the OP-TEE source.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/misc/ti-k3-trng.h"
#include "trace.h"

#define RNG_OUTPUT_0    0x00
#define RNG_OUTPUT_1    0x04
#define RNG_OUTPUT_2    0x08
#define RNG_OUTPUT_3    0x0C
#define RNG_STATUS      0x10
#define RNG_READY       (1u << 0)
#define RNG_CONTROL     0x14

/*
 * xorshift64 step. Seeded to a nonzero constant at reset; xorshift's state
 * transition is a bijection on the nonzero 64-bit values, so it can never
 * produce (or get stuck at) zero from a nonzero seed.
 */
static uint64_t ti_k3_trng_next(TIK3TrngState *s)
{
    s->rng_state ^= s->rng_state << 13;
    s->rng_state ^= s->rng_state >> 7;
    s->rng_state ^= s->rng_state << 17;
    return s->rng_state;
}

static uint64_t ti_k3_trng_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3TrngState *s = TI_K3_TRNG(opaque);
    uint32_t val;

    switch (addr) {
    case RNG_OUTPUT_0:
        s->pair_a = ti_k3_trng_next(s);
        val = (uint32_t)s->pair_a;
        break;
    case RNG_OUTPUT_1:
        val = (uint32_t)(s->pair_a >> 32);
        break;
    case RNG_OUTPUT_2:
        s->pair_b = ti_k3_trng_next(s);
        val = (uint32_t)s->pair_b;
        break;
    case RNG_OUTPUT_3:
        val = (uint32_t)(s->pair_b >> 32);
        break;
    case RNG_STATUS:
        /* Always ready, never shut down -- the poll loop never blocks. */
        val = RNG_READY;
        break;
    default:
        val = s->regs[addr >> 2];
        break;
    }

    trace_ti_k3_trng_read(addr, val);
    return val;
}

static void ti_k3_trng_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    TIK3TrngState *s = TI_K3_TRNG(opaque);

    trace_ti_k3_trng_write(addr, value);

    switch (addr) {
    case RNG_OUTPUT_0:
    case RNG_OUTPUT_1:
    case RNG_OUTPUT_2:
    case RNG_OUTPUT_3:
        /* Output registers are read-only on real hardware; RAZ/WI here. */
        break;
    case RNG_STATUS:
        /*
         * INTACK: guest acknowledges RNG_READY/SHUTDOWN_OFLO; nothing to
         * clear since STATUS is synthesized fresh on every read.
         */
        break;
    default:
        s->regs[addr >> 2] = (uint32_t)value;
        break;
    }
}

static const MemoryRegionOps ti_k3_trng_ops = {
    .read = ti_k3_trng_read,
    .write = ti_k3_trng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ti_k3_trng_reset(DeviceState *dev)
{
    TIK3TrngState *s = TI_K3_TRNG(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->rng_state = 0x9e3779b97f4a7c15ULL;
    s->pair_a = 0;
    s->pair_b = 0;
}

static void ti_k3_trng_init(Object *obj)
{
    TIK3TrngState *s = TI_K3_TRNG(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_trng_ops, s,
                          TYPE_TI_K3_TRNG, TI_K3_TRNG_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void ti_k3_trng_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ti_k3_trng_reset);
}

static const TypeInfo ti_k3_trng_info = {
    .name = TYPE_TI_K3_TRNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3TrngState),
    .instance_init = ti_k3_trng_init,
    .class_init = ti_k3_trng_class_init,
};

static void ti_k3_trng_register_types(void)
{
    type_register_static(&ti_k3_trng_info);
}

type_init(ti_k3_trng_register_types)
