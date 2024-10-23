/*
 * S32 System Module Timer
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

#ifndef HW_S32STM_TIMER_H
#define HW_S32STM_TIMER_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "hw/irq.h"

#define STM_CR       0x00
#define STM_CNT      0x04

#define STM_CCR0     0x10
#define STM_CIR0     0x14
#define STM_CMP0     0x18

#define STM_CCR1     0x20
#define STM_CIR1     0x24
#define STM_CMP1     0x28

#define STM_CCR2     0x30
#define STM_CIR2     0x34
#define STM_CMP2     0x38

#define STM_CCR3     0x40
#define STM_CIR3     0x44
#define STM_CMP3     0x48

#define STM_NUM_CHANNELS 4
#define TYPE_S32STM_TIMER "s32.stm"
OBJECT_DECLARE_SIMPLE_TYPE(S32STMTimerState, S32STM_TIMER)

struct S32STMTimerState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    QEMUTimer *timer;
    qemu_irq irq;
    int irq_channel;

    uint32_t irq_count;
    int64_t tick_offset;
    uint64_t hit_time;
    uint64_t prev_int;
    uint64_t freq_hz;
    int prescaler;

    uint32_t stm_cr;
    uint32_t stm_cnt;
    
    uint32_t stm_ccr0;
    uint32_t stm_cir0;
    uint32_t stm_cmp0;

    uint32_t stm_ccr1;
    uint32_t stm_cir1;
    uint32_t stm_cmp1;

    uint32_t stm_ccr2;
    uint32_t stm_cir2;
    uint32_t stm_cmp2;

    uint32_t stm_ccr3;
    uint32_t stm_cir3;
    uint32_t stm_cmp3;

};

#endif /* HW_S32STM_TIMER_H */
