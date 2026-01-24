/*
 * AM64 virt machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/virt.h"
#include "hw/arm/machines-qom.h"
#include "hw/misc/ti-dmsc.h"
#include "hw/misc/ti-sec-proxy.h"
#include "system/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/arm/ti-am64x.h"
#include "hw/qdev-clock.h"

#define AM64_VIRT_DRAM_BASE 0x80000000ULL

#define MAIN_SEC_PROXY_MMRS_ADDRESS 0x48250000ULL
#define MAIN_SEC_PROXY_SCFG_ADDRESS 0x4A400000ULL
#define MAIN_SEC_PROXY_RT_ADDRESS 0x4A600000ULL
#define MAIN_SEC_PROXY_TARGET_ADDRESS 0x4D000000ULL

#define SYSCLK_FRQ 168000000ULL

static MemoryRegion m4f_iram, m4f_dram;

static void am64_virt_create_am64_devs(MachineState *machine)
{
    VirtMachineState *virt = VIRT_MACHINE(machine);
    DeviceState *sec_proxy = qdev_new(TYPE_TI_SEC_PROXY);
    DeviceState *dmsc = qdev_new(TYPE_TI_DMSC);
    SysBusDevice *sbd = SYS_BUS_DEVICE(sec_proxy);
    MemoryRegion *sysmem = get_system_memory();
    Clock *sysclk = clock_new(OBJECT(machine), "SYSCLK");

    DeviceState *am64_m4_dev = qdev_new(TYPE_TI_AM64X);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(am64_m4_dev));
    qdev_connect_clock_in(am64_m4_dev, "sysclk", sysclk);
    
    sysbus_realize_and_unref(SYS_BUS_DEVICE(am64_m4_dev), &error_fatal);

    object_property_add_child(OBJECT(machine), "sec-proxy", OBJECT(sec_proxy));
    object_property_add_child(OBJECT(machine), "dmsc", OBJECT(dmsc));

    qdev_prop_set_uint16(dmsc, "rx-thread", 13);
    qdev_prop_set_uint16(dmsc, "tx-thread", 12);

    sysbus_realize(sbd, &error_fatal);


    
    memory_region_init_ram_nomigrate(&m4f_iram, OBJECT(machine),
                                     "am64.m4f-iram", 0x30000,
                                     &error_fatal);
    memory_region_init_ram_nomigrate(&m4f_dram, OBJECT(machine),
                                     "am64.m4f-dram", 0x10000,
                                     &error_fatal);

    memory_region_add_subregion(sysmem, 0x5000000, &m4f_iram);
    memory_region_add_subregion(sysmem, 0x5040000, &m4f_dram);

    
    memory_region_add_subregion(sysmem, MAIN_SEC_PROXY_MMRS_ADDRESS,
                                sysbus_mmio_get_region(sbd, 0));
    memory_region_add_subregion(sysmem, MAIN_SEC_PROXY_SCFG_ADDRESS,
                                sysbus_mmio_get_region(sbd, 1));
    memory_region_add_subregion(sysmem, MAIN_SEC_PROXY_RT_ADDRESS,
                                sysbus_mmio_get_region(sbd, 2));
    memory_region_add_subregion(sysmem, MAIN_SEC_PROXY_TARGET_ADDRESS,
                                sysbus_mmio_get_region(sbd, 3));
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(virt->gic, 34));

    
    object_property_set_link(OBJECT(dmsc), "sec-proxy", OBJECT(sec_proxy),
                             &error_abort);
    qdev_realize(dmsc, NULL, &error_fatal);
}

static void am64_virt_init(MachineState *machine)
{
    virt_machine_init(machine);
    am64_virt_create_am64_devs(machine);
}

static void am64_virt_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(oc);

    mc->desc = "AM64 virt machine";
    mc->init = am64_virt_init;
    vmc->ram_base_override = AM64_VIRT_DRAM_BASE;
}

static const TypeInfo am64_virt_machine_info = {
    .name = MACHINE_TYPE_NAME("am64-virt"),
    .parent = TYPE_VIRT_MACHINE,
    .class_init = am64_virt_machine_class_init,
    .interfaces = arm_aarch64_machine_interfaces,
};

static void am64_virt_machine_init_register_types(void)
{
    type_register_static(&am64_virt_machine_info);
}

type_init(am64_virt_machine_init_register_types)
