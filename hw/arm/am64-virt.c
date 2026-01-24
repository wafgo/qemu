/*
 * AM64 virt machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/char/pl011.h"
#include "hw/block/flash.h"
#include "hw/rtc/pl031.h"
#include "hw/qdev-properties.h"
#include "hw/pci-host/gpex.h"
#include "hw/pci/pci.h"
#include "hw/arm/ti-am64x.h"
#include "hw/qdev-clock.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "qemu/error-report.h"
#include "chardev/char.h"
#include "qemu/units.h"

#define AM64_VIRT_DRAM_BASE 0x80000000ULL
#define AM64_VIRT_UART0_BASE 0x09000000ULL
#define AM64_VIRT_UART1_BASE 0x09040000ULL
#define AM64_VIRT_UART0_IRQ 1
#define AM64_VIRT_UART1_IRQ 8
#define AM64_VIRT_RTC_BASE 0x09010000ULL
#define AM64_VIRT_RTC_IRQ 2
#define AM64_VIRT_GPIO_BASE 0x09030000ULL
#define AM64_VIRT_GPIO_IRQ 7
#define AM64_VIRT_FLASH_BASE 0x050000000ULL
#define AM64_VIRT_FLASH_SIZE 0x08000000ULL
#define AM64_VIRT_FLASH_SECTOR_SIZE (256 * KiB)
#define AM64_VIRT_PCIE_MMIO_BASE 0x68000000ULL
#define AM64_VIRT_PCIE_MMIO_SIZE 0x08000000ULL
#define AM64_VIRT_PCIE_PIO_BASE 0x3EFF0000ULL
#define AM64_VIRT_PCIE_PIO_SIZE 0x00010000ULL
#define AM64_VIRT_PCIE_ECAM_BASE 0x0D000000ULL
#define AM64_VIRT_PCIE_ECAM_SIZE 0x10000000ULL
#define AM64_VIRT_PCIE_IRQ_BASE 3

#define SYSCLK_FRQ 168000000ULL

typedef struct AM64VirtMachineState {
    MachineState parent_obj;
    TIAM64xState *soc;
    struct arm_boot_info bootinfo;
} AM64VirtMachineState;

#define TYPE_AM64_VIRT_MACHINE MACHINE_TYPE_NAME("am64-virt")
OBJECT_DECLARE_SIMPLE_TYPE(AM64VirtMachineState, AM64_VIRT_MACHINE)

static void am64_virt_create_uart(hwaddr base, int irq, Chardev *chr,
                                  DeviceState *gic)
{
    DeviceState *dev = qdev_new(TYPE_PL011);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    MemoryRegion *sysmem = get_system_memory();

    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(sysmem, base, sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(gic, irq));
}

static void am64_virt_create_pcie(const AM64VirtMachineState *ams,
                                  DeviceState *gic)
{
    DeviceState *dev = qdev_new(TYPE_GPEX_HOST);
    PCIHostState *pci;
    MemoryRegion *mmio_alias;
    MemoryRegion *ecam_alias;
    MemoryRegion *mmio_reg;
    MemoryRegion *ecam_reg;
    int i;

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, AM64_VIRT_PCIE_ECAM_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                AM64_VIRT_PCIE_ECAM_BASE, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, AM64_VIRT_PCIE_MMIO_BASE,
                             AM64_VIRT_PCIE_MMIO_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                AM64_VIRT_PCIE_MMIO_BASE, mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, AM64_VIRT_PCIE_PIO_BASE);

    for (i = 0; i < PCI_NUM_PINS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(gic,
                                            AM64_VIRT_PCIE_IRQ_BASE + i));
        gpex_set_irq_num(GPEX_HOST(dev), i, AM64_VIRT_PCIE_IRQ_BASE + i);
    }

    pci = PCI_HOST_BRIDGE(dev);
    pci_init_nic_devices(pci->bus, MACHINE_GET_CLASS(ams)->default_nic);
}

static void am64_virt_create_rtc(hwaddr base, int irq, DeviceState *gic)
{
    sysbus_create_simple(TYPE_PL031, base, qdev_get_gpio_in(gic, irq));
}

static void am64_virt_create_gpio(hwaddr base, int irq, DeviceState *gic)
{
    sysbus_create_simple("pl061", base, qdev_get_gpio_in(gic, irq));
}

static void am64_virt_create_flash(void)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint32(dev, "num-blocks",
                         AM64_VIRT_FLASH_SIZE / AM64_VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint64(dev, "sector-length", AM64_VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", "am64-virt.flash0");

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, AM64_VIRT_FLASH_BASE);
}

static void am64_virt_init(MachineState *machine)
{
    AM64VirtMachineState *ams = AM64_VIRT_MACHINE(machine);
    DeviceState *soc = qdev_new(TYPE_TI_AM64X);
    DeviceState *gic;
    Clock *sysclk = clock_new(OBJECT(machine), "SYSCLK");

    clock_set_hz(sysclk, SYSCLK_FRQ);
    qdev_prop_set_uint8(soc, "a53-cpus", machine->smp.cpus);
    qdev_prop_set_uint64(soc, "ram-base", AM64_VIRT_DRAM_BASE);
    qdev_prop_set_uint64(soc, "ram-size", machine->ram_size);
    qdev_connect_clock_in(soc, "sysclk", sysclk);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(soc), &error_fatal);
    ams->soc = TI_AM64X(soc);
    gic = DEVICE(&ams->soc->gic);

    memory_region_add_subregion(get_system_memory(), AM64_VIRT_DRAM_BASE,
                                machine->ram);

    am64_virt_create_uart(AM64_VIRT_UART0_BASE, AM64_VIRT_UART0_IRQ,
                          serial_hd(0), gic);
    am64_virt_create_uart(AM64_VIRT_UART1_BASE, AM64_VIRT_UART1_IRQ,
                          serial_hd(1), gic);
    am64_virt_create_rtc(AM64_VIRT_RTC_BASE, AM64_VIRT_RTC_IRQ, gic);
    am64_virt_create_gpio(AM64_VIRT_GPIO_BASE, AM64_VIRT_GPIO_IRQ, gic);
    am64_virt_create_flash();
    am64_virt_create_pcie(ams, gic);

    memset(&ams->bootinfo, 0, sizeof(ams->bootinfo));
    ams->bootinfo.ram_size = machine->ram_size;
    ams->bootinfo.loader_start = AM64_VIRT_DRAM_BASE;
    ams->bootinfo.board_id = -1;
    ams->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    if (!qemu_get_cpu(0)) {
        error_report("am64-virt: CPU0 not realized");
        exit(1);
    }
    arm_load_kernel(ARM_CPU(qemu_get_cpu(0)), machine, &ams->bootinfo);
}

static void am64_virt_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "AM64 virt machine";
    mc->init = am64_virt_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->default_ram_id = "am64-virt.ram";
    mc->default_cpus = TI_AM64X_A53_NUM;
    mc->max_cpus = TI_AM64X_A53_NUM + 4 + 1; /* + M4 + R5F */
    mc->default_ram_size = 2 * GiB;
}

static const TypeInfo am64_virt_machine_info = {
    .name = TYPE_AM64_VIRT_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(AM64VirtMachineState),
    .class_init = am64_virt_machine_class_init,
    .interfaces = arm_aarch64_machine_interfaces,
};

static void am64_virt_machine_init_register_types(void)
{
    type_register_static(&am64_virt_machine_info);
}

type_init(am64_virt_machine_init_register_types)
