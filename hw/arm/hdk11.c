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
#include "net/can_emu.h"
#include "hw/loader.h"

#define UART_DEBUG_MODULE 1
#define HDK11_CANFD_NUM_BUSSES 3

struct HDK11MachineClass {
    MachineClass parent;
};

struct HDK11MachineState {
    MachineState       parent;
    NxpS32GState       soc;
    MemoryRegion       sram;
    Clock              *xtal;
    CanBusState        *canbus[HDK11_CANFD_NUM_BUSSES];
};

#define TYPE_HDK11_MACHINE MACHINE_TYPE_NAME("hdk11")

OBJECT_DECLARE_TYPE(HDK11MachineState, HDK11MachineClass, HDK11_MACHINE)

#define HDK_XTAL_FREQ 16666666UL    
#define SYSCLK_FRQ 200000000ULL
#define REFCLK_FRQ 200000000ULL
    
static void hdk11_init(MachineState *machine)
{
    HDK11MachineState *hdk = HDK11_MACHINE(machine);
    int size = 0;
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

    memory_region_add_subregion(get_system_memory(), 0x80000000, machine->ram);

    object_initialize_child(OBJECT(machine), "s32-soc", &hdk->soc,
                            TYPE_NXP_S32G);

    qdev_prop_set_uint32(DEVICE(&hdk->soc), "debug-uart", UART_DEBUG_MODULE);
    hdk->xtal = clock_new(OBJECT(hdk), "XTAL");
    clock_set_hz(hdk->xtal, HDK_XTAL_FREQ);
    qdev_connect_clock_in(DEVICE(&hdk->soc), "sysclk", hdk->xtal);

    object_property_set_link(OBJECT(&hdk->soc), "canbus0", OBJECT(hdk->canbus[0]),
                             &error_abort);
    object_property_set_link(OBJECT(&hdk->soc), "canbus2", OBJECT(hdk->canbus[1]),
                             &error_abort);
    object_property_set_link(OBJECT(&hdk->soc), "canbus3", OBJECT(hdk->canbus[2]),
                             &error_abort);

    if (!qdev_realize_and_unref(DEVICE(&hdk->soc), NULL, &err)) {
        error_reportf_err(err, "Couldn't realize S32G SoC");
        exit(1);
    }
    // FIXME: put hpe bootloader and CAR as Firmware into roms/ and load it here directly without specifying any -kernel parameter
    size = load_image_targphys("/home/uia67865/devel/git/m7-car/src/car_s32g/car_sw/output/bin/CORTEXM_S32G27X_car_sw.bin", 0x100000, 2*MiB);
    if (size > 0) {
        printf("Successfully loaded CORTEXM_S32G27X_car_sw.bin @ 0x100000, size: 0x%x\n", size);
    } else {
        printf("Failed loading CORTEXM_S32G27X_car_sw.bin ret: %i\n", size);
    }
    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       0, 0x400000);
}

static void hdk11_machine_instance_init(Object *obj)
{
    HDK11MachineState *s = HDK11_MACHINE(obj);    
    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&s->canbus[0],
                             object_property_allow_set_link,
                             0);

    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&s->canbus[1],
                             object_property_allow_set_link,
                             0);
    
    object_property_add_link(obj, "canbus2", TYPE_CAN_BUS,
                             (Object **)&s->canbus[2],
                             object_property_allow_set_link,
                             0);
}

static void hdk11_machine_init(MachineClass *mc)
{
    // FIXME: Add A53 cores as soon as they are available on the s32g emulation
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m7"),
        NULL
    };

    mc->desc = "HDK1.1 (Cortex-M7 + Cortex-A53)";
    mc->default_cpus = NXP_S32G_NUM_M7_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m7");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 2 * GiB;
    mc->init = hdk11_init;
    mc->block_default_type = IF_MTD;
    mc->units_per_default_bus = 1;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "hdk11.ram";
}

static void hdk11_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    hdk11_machine_init(mc);
}

static const TypeInfo hdk11_typeinfo = {
    .name = MACHINE_TYPE_NAME("hdk11"),
    .parent = TYPE_MACHINE,
    .class_init = hdk11_class_init,
    .instance_init = hdk11_machine_instance_init,
    .instance_size = sizeof(HDK11MachineState),
};

static void hdk11_register_types(void)
{
    type_register_static(&hdk11_typeinfo);
}

type_init(hdk11_register_types)
