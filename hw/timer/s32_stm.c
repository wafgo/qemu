/*
 * S32 System Timer Module
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/timer/s32_stm.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define S32_STM_ERR_DEBUG 0
#ifndef S32_STM_ERR_DEBUG
#define S32_STM_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (S32_STM_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void s32_stm_interrupt(void *opaque)
{
    S32STMTimerState *s = opaque;

    DB_PRINT("Interrupt\n");

    /* if (s->tim_dier & TIM_DIER_UIE && s->tim_cr1 & TIM_CR1_CEN) { */
    /*     s->tim_sr |= 1; */
    /*     qemu_irq_pulse(s->irq); */
    /*     stm32f2xx_timer_set_alarm(s, s->hit_time); */
    /* } */

    /* if (s->tim_ccmr1 & (TIM_CCMR1_OC2M2 | TIM_CCMR1_OC2M1) && */
    /*     !(s->tim_ccmr1 & TIM_CCMR1_OC2M0) && */
    /*     s->tim_ccmr1 & TIM_CCMR1_OC2PE && */
    /*     s->tim_ccer & TIM_CCER_CC2E) { */
    /*     /\* PWM 2 - Mode 1 *\/ */
    /*     DB_PRINT("PWM2 Duty Cycle: %d%%\n", */
    /*             s->tim_ccr2 / (100 * (s->tim_psc + 1))); */
    /* } */
}

static inline int64_t s32_stm_ns_to_ticks(S32STMTimerState *s, int64_t t)
{
    return muldiv64(t, s->freq_hz, 1000000000ULL) / (((s->stm_cr >> 8) & 0xff) + 1);
}

static void s32_stm_reset(DeviceState *dev)
{
    S32STMTimerState *s = S32STM_TIMER(dev);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->stm_cr = 0;
    s->stm_cnt = 0;
    s->stm_ccr0 = 0;
    s->stm_ccr1 = 0;
    s->stm_ccr2 = 0;
    s->stm_ccr3 = 0;
    s->stm_cir0 = 0;
    s->stm_cir1 = 0;
    s->stm_cir2 = 0;
    s->stm_cir3 = 0;
    s->stm_cmp0 = 0;
    s->stm_cmp1 = 0;
    s->stm_cmp2 = 0;
    s->stm_cmp3 = 0;

    s->tick_offset = s32_stm_ns_to_ticks(s, now);
}

static uint64_t s32_stm_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    S32STMTimerState *s = opaque;
    int64_t now;
    DB_PRINT("Read 0x%"HWADDR_PRIx"\n", offset);
    
    switch (offset) {
    case STM_CR:
        return s->stm_cr;
    case STM_CNT:
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->stm_cnt = s32_stm_ns_to_ticks(s, now);
        return s->stm_cnt;
    case STM_CCR0:
        return s->stm_ccr0;
    case STM_CIR0:
        return s->stm_cir0;
    case STM_CMP0:
        return s->stm_cmp0;
    case STM_CCR1:
        return s->stm_ccr1;
    case STM_CIR1:
        return s->stm_cir1;
    case STM_CMP1:
        return s->stm_cmp1;
    case STM_CCR2:
        return s->stm_ccr2;
    case STM_CIR2:
        return s->stm_cir2;
    case STM_CMP2:
        return s->stm_cmp2;
    case STM_CCR3:
        return s->stm_ccr3;
    case STM_CIR3:
        return s->stm_cir3;
    case STM_CMP3:
        return s->stm_cmp3;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, offset);
    }

    return 0;
}

static void s32_stm_write(void *opaque, hwaddr offset,
                        uint64_t val64, unsigned size)
{
    S32STMTimerState *s = opaque;
    uint32_t value = val64;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t timer_val = 0;

    DB_PRINT("Write 0x%x, 0x%"HWADDR_PRIx"\n", value, offset);

    switch (offset) {
    case STM_CR:
        s->stm_cr = value;
        return;
    case STM_CNT:
        s->stm_cnt = value;
        return;
    case STM_CCR0:
        s->stm_ccr0 = value;
        return;
    case STM_CIR0:
        s->stm_cir0 = value;
        return;
    case STM_CMP0:
        /* This is set by hardware and cleared by software */
        s->stm_cmp0 = value;
        return;
    case STM_CCR1:
        s->stm_ccr1 = value;
        return;
    case STM_CIR1:
        s->stm_cir1 = value;
        return;
    case STM_CMP1:
        /* This is set by hardware and cleared by software */
        s->stm_cmp1 = value;
        return;
    case STM_CCR2:
        s->stm_ccr2 = value;
        return;
    case STM_CIR2:
        s->stm_cir2 = value;
        return;
    case STM_CMP2:
        /* This is set by hardware and cleared by software */
        s->stm_cmp2 = value;
        return;
    case STM_CCR3:
        s->stm_ccr3 = value;
        return;
    case STM_CIR3:
        s->stm_cir3 = value;
        return;
    case STM_CMP3:
        /* This is set by hardware and cleared by software */
        s->stm_cmp3 = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, offset);
        return;
    }

    /* This means that a register write has affected the timer in a way that
     * requires a refresh of both tick_offset and the alarm.
     */
    s->tick_offset = s32_stm_ns_to_ticks(s, now) - timer_val;
    /* s32_stm_set_alarm(s, now); */
}

static const MemoryRegionOps s32_stm_ops = {
    .read = s32_stm_read,
    .write = s32_stm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_s32stm = {
    .name = TYPE_S32STM_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(tick_offset, S32STMTimerState),
        VMSTATE_UINT32(stm_cr, S32STMTimerState),
        VMSTATE_UINT32(stm_cnt, S32STMTimerState),
        VMSTATE_UINT32(stm_ccr0, S32STMTimerState),
        VMSTATE_UINT32(stm_cir0, S32STMTimerState),
        VMSTATE_UINT32(stm_cmp0, S32STMTimerState),
        VMSTATE_UINT32(stm_ccr1, S32STMTimerState),
        VMSTATE_UINT32(stm_cir1, S32STMTimerState),
        VMSTATE_UINT32(stm_cmp1, S32STMTimerState),
        VMSTATE_UINT32(stm_ccr2, S32STMTimerState),
        VMSTATE_UINT32(stm_cir2, S32STMTimerState),
        VMSTATE_UINT32(stm_cmp2, S32STMTimerState),
        VMSTATE_UINT32(stm_ccr3, S32STMTimerState),
        VMSTATE_UINT32(stm_cir3, S32STMTimerState),
        VMSTATE_UINT32(stm_cmp3, S32STMTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property s32_stm_properties[] = {
    DEFINE_PROP_UINT64("clock-frequency", struct S32STMTimerState,
                       freq_hz, 1000000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void s32_stm_init(Object *obj)
{
    S32STMTimerState *s = S32STM_TIMER(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &s32_stm_ops, s,
                          "s32_stm_timer", 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s32_stm_realize(DeviceState *dev, Error **errp)
{
    S32STMTimerState *s = S32STM_TIMER(dev);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, s32_stm_interrupt, s);
}

static void s32_stm_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s32_stm_reset;
    device_class_set_props(dc, s32_stm_properties);
    dc->vmsd = &vmstate_s32stm;
    dc->realize = s32_stm_realize;
}

static const TypeInfo s32_stm_timer_info = {
    .name          = TYPE_S32STM_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32STMTimerState),
    .instance_init = s32_stm_init,
    .class_init    = s32_stm_timer_class_init,
};

static void s32_stm_register_types(void)
{
    type_register_static(&s32_stm_timer_info);
}

type_init(s32_stm_register_types)
