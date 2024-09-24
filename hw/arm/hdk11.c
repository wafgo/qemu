/*
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * HDK 1.1 emulation.
 *
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
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/arm/boot.h"
#include "hw/i2c/i2c.h"
#include "hw/arm/boot.h"
#include "hw/qdev-clock.h"
#include "hw/arm/armv7m.h"
#include "exec/address-spaces.h"
#include "hw/arm/nxp-s32g.h"

struct HDK11MachineClass {
    MachineClass parent;
};

struct HDK11MachineState {
    MachineState       parent;
    MemoryRegion       sram;
    Clock              *xtal;
};

#define TYPE_HDK11_MACHINE MACHINE_TYPE_NAME("hdk11")

OBJECT_DECLARE_TYPE(HDK11MachineState, HDK11MachineClass, HDK11_MACHINE)

#define HDK_XTAL_FREQ 16666666UL    
#define SYSCLK_FRQ 200000000ULL
#define REFCLK_FRQ 200000000ULL
    
static void hdk11_init(MachineState *machine)
{

    HDK11MachineState *hdk = HDK11_MACHINE(machine);
    NxpS32GState *s32;

    /* HDK11MachineClass *mmc = HDK11_MACHINE_GET_CLASS(machine); */
    /* MemoryRegion *system_memory = get_system_memory();x */
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    Error *err = NULL;

    /* BIOS is not supported by this board */
    if (machine->firmware) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    memory_region_add_subregion(get_system_memory(), 0x40000000, machine->ram);
    
    s32 = NXP_S32G(object_new(TYPE_NXP_S32G));
    hdk->xtal = clock_new(OBJECT(hdk), "XTAL");
    clock_set_hz(hdk->xtal, HDK_XTAL_FREQ);
    qdev_connect_clock_in(DEVICE(s32), "sysclk", hdk->xtal);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(s32));
    
    if (!qdev_realize_and_unref(DEVICE(s32), NULL, &err)) {
        error_reportf_err(err, "Couldn't realize S32G M7 Core: ");
        exit(1);
    }

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       0, 0x400000);
}

static void hdk11_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m7"),
        NULL
    };

    mc->desc = "HDK1.1 (Cortex-M7 + Cortex-A53)";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m7");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 4 * GiB;
    mc->init = hdk11_init;
    mc->block_default_type = IF_MTD;
    mc->units_per_default_bus = 1;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "hdk11.ram";
}

DEFINE_MACHINE("hdk11", hdk11_machine_init)
