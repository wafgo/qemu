/*
 * TI K3 DM timer (dmtimer, am654 register layout) — counting model
 *
 * Models exactly what u-boot's omap-timer driver needs for udelay on the
 * AM64x R5 SPL: a counter (TCRR) advancing at freq-hz virtual time while
 * TCLR.ST is set. No interrupts, no compare/PWM.
 *
 * The prescaler (TCLR.PRE_EN bit 5, TCLR.PTV bits 2-4) divides the effective
 * clock rate: effective_freq = freq_hz / (2 << PTV) when PRE_EN is set.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/qdev-properties.h"
#include "hw/timer/ti-k3-dmtimer.h"

#define R_TIDR  0x00
#define R_TCLR  0x38
#define R_TCRR  0x3c
#define R_TLDR  0x40
#define R_TTGR  0x44
#define R_TWPS  0x48

#define TCLR_ST     (1u << 0)
#define TCLR_AR     (1u << 1)
#define TCLR_PTV    (7u << 2)
#define TCLR_PRE_EN (1u << 5)

static uint32_t dmtimer_effective_freq(TIK3DmTimerState *s)
{
    uint32_t freq = s->freq_hz;

    if (s->tclr & TCLR_PRE_EN) {
        uint32_t ptv = (s->tclr >> 2) & 7;
        freq = s->freq_hz / (2u << ptv);
    }
    return freq;
}

static uint32_t dmtimer_tcrr(TIK3DmTimerState *s)
{
    int64_t now;
    uint32_t eff_freq;

    if (!s->running) {
        return s->tcrr_base;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    eff_freq = dmtimer_effective_freq(s);

    return s->tcrr_base +
           (uint32_t)muldiv64(now - s->base_ns, eff_freq,
                              NANOSECONDS_PER_SECOND);
}

static void dmtimer_set_tcrr(TIK3DmTimerState *s, uint32_t val)
{
    s->tcrr_base = val;
    s->base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static uint64_t ti_k3_dmtimer_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3DmTimerState *s = TI_K3_DMTIMER(opaque);

    switch (addr) {
    case R_TIDR:
        return 0x0;
    case R_TCLR:
        return s->tclr;
    case R_TCRR:
        return dmtimer_tcrr(s);
    case R_TLDR:
        return s->tldr;
    case R_TWPS:
        return 0; /* no write pending, ever */
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented read @0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
}

static void ti_k3_dmtimer_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    TIK3DmTimerState *s = TI_K3_DMTIMER(opaque);

    switch (addr) {
    case R_TCLR:
        /*
         * Rebase the counter using the OLD tclr before storing the new
         * one: a PTV/PRE_EN change while running must only affect time
         * from now on, not retroactively rescale elapsed ticks. On a
         * stopped->started edge this re-anchors base_ns to now.
         */
        dmtimer_set_tcrr(s, dmtimer_tcrr(s));
        s->tclr = val;
        s->running = (val & TCLR_ST) != 0;
        break;
    case R_TCRR:
        dmtimer_set_tcrr(s, val);
        break;
    case R_TLDR:
        s->tldr = val;
        break;
    case R_TTGR:
        dmtimer_set_tcrr(s, s->tldr);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented write @0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps ti_k3_dmtimer_ops = {
    .read = ti_k3_dmtimer_read,
    .write = ti_k3_dmtimer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ti_k3_dmtimer_reset(DeviceState *dev)
{
    TIK3DmTimerState *s = TI_K3_DMTIMER(dev);

    s->tclr = 0;
    s->tldr = 0;
    s->tcrr_base = 0;
    s->base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /*
     * Counter free-runs from reset: u-boot's omap-timer get_count()
     * only ever reads TCRR, so the model must count without requiring
     * a TCLR.ST write first.
     */
    s->running = true;
}

static void ti_k3_dmtimer_init(Object *obj)
{
    TIK3DmTimerState *s = TI_K3_DMTIMER(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_dmtimer_ops, s,
                          TYPE_TI_K3_DMTIMER, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const Property ti_k3_dmtimer_properties[] = {
    /* AM64 main_timer0 free-runs at 20 MHz before SYSFW clock setup */
    DEFINE_PROP_UINT32("freq-hz", TIK3DmTimerState, freq_hz, 20000000),
};

static void ti_k3_dmtimer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, ti_k3_dmtimer_properties);
    device_class_set_legacy_reset(dc, ti_k3_dmtimer_reset);
}

static const TypeInfo ti_k3_dmtimer_info = {
    .name = TYPE_TI_K3_DMTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3DmTimerState),
    .instance_init = ti_k3_dmtimer_init,
    .class_init = ti_k3_dmtimer_class_init,
};

static void ti_k3_dmtimer_register_types(void)
{
    type_register_static(&ti_k3_dmtimer_info);
}

type_init(ti_k3_dmtimer_register_types)
