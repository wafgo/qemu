/*
 * TI AM64x SoC family
 *
 * Copyright (c) 2025 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_ARM_TI_AM64X_H
#define HW_ARM_TI_AM64X_H

#include "qemu/osdep.h"
#include "system/memory.h"
#include "hw/arm/armv7m.h"
#include "cpu.h"
#include "hw/intc/arm_gic.h"
#include "hw/clock.h"
#include "qom/object.h"
#include "hw/misc/ti-rat.h"
#include "hw/misc/ti-sec-proxy.h"
#include "hw/misc/ti-dmsc.h"
#include "hw/misc/ti-mailbox.h"
#include "hw/char/ti-am64-uart.h"

#define TYPE_TI_AM64X "ti-am64x"
OBJECT_DECLARE_SIMPLE_TYPE(TIAM64xState, TI_AM64X)

#define TI_AM64X_MCU_UART_NUM 2
#define TI_AM64X_MAILBOX_NUM 8
#define TI_AM64X_A53_NUM 2
#define TI_AM64X_GIC_NUM_SPI 256
    
struct TIAM64xState {
    SysBusDevice parent_obj;
    ARMv7MState armv7m;
    ARMCPU a53[TI_AM64X_A53_NUM];
    GICState gic;
    MemoryRegion mcu_iram;
    MemoryRegion mcu_dram;
    MemoryRegion mcu_ddr;
    MemoryRegion mcu_iram_sysmem;
    MemoryRegion mcu_dram_sysmem;
    MemoryRegion mcu_root;
    Clock *sysclk;
    Clock *refclk;
    TIRATState rat;
    TISecProxyState sec_proxy;
    TIDmscState dmsc;
    TIMailboxState mailbox[TI_AM64X_MAILBOX_NUM];
    AM64Uart mcu_uart[TI_AM64X_MCU_UART_NUM];
    uint64_t main_ram_base;
    uint64_t main_ram_size;
    uint8_t a53_cpus;
};

#endif
