/*
 * TI K3 AM64-virt machine
 *
 * Copyright (c) 2026 CMBlu
 * Written by Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/ti-am64x.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"

#define AM64_VIRT_DRAM_BASE 0x80000000ULL

typedef struct AM64VirtMachineState {
    MachineState parent_obj;
    TIAM64xState soc;
    struct arm_boot_info bootinfo;
} AM64VirtMachineState;

#define TYPE_AM64_VIRT_MACHINE MACHINE_TYPE_NAME("am64-virt")
OBJECT_DECLARE_SIMPLE_TYPE(AM64VirtMachineState, AM64_VIRT_MACHINE)

static void am64_virt_init(MachineState *machine)
{
    AM64VirtMachineState *ams = AM64_VIRT_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();

    if (machine->ram_size > 2 * GiB) {
        error_report("am64-virt: maximum supported RAM size is 2 GiB");
        exit(1);
    }

    object_initialize_child(OBJECT(machine), "soc", &ams->soc, TYPE_TI_AM64X);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(&ams->soc), &error_fatal);

    memory_region_add_subregion(sysmem, AM64_VIRT_DRAM_BASE, machine->ram);

    ams->bootinfo.ram_size = machine->ram_size;
    ams->bootinfo.loader_start = AM64_VIRT_DRAM_BASE;
    ams->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    ams->bootinfo.board_id = -1;
    arm_load_kernel(&ams->soc.a53[0], machine, &ams->bootinfo);
}

static void am64_virt_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "TI K3 AM64x virt machine";
    mc->init = am64_virt_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->default_ram_id = "am64-virt.ram";
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = TI_AM64X_NUM_A53_CPUS;
    mc->max_cpus = TI_AM64X_NUM_A53_CPUS;
    mc->min_cpus = 1;
}

static const TypeInfo am64_virt_machine_info = {
    .name = TYPE_AM64_VIRT_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(AM64VirtMachineState),
    .class_init = am64_virt_machine_class_init,
    .interfaces = arm_aarch64_machine_interfaces,
};

static void am64_virt_machine_register_types(void)
{
    type_register_static(&am64_virt_machine_info);
}

type_init(am64_virt_machine_register_types)
