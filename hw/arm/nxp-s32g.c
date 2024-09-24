/*
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * NXP S32G SOC emulation.
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
#include "hw/arm/nxp-s32g.h"
#include "hw/misc/unimp.h"
#include "hw/usb/imx-usb-phy.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "chardev/char.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/qdev-clock.h"
#include "target/arm/cpu-qom.h"
#include "hw/sysbus.h"
#include "hw/misc/s32g_mscm.h"


#define NAME_SIZE 20

static void nxp_s32g_init(Object *obj)
{
    /* MachineState *mms = MACHINE(qdev_get_machine()); */
    NxpS32GState *s = NXP_S32G(obj);
    char name[NAME_SIZE];
    
    object_initialize_child(obj, "m7-cpu", &s->m7_cpu, TYPE_ARMV7M);
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);

    object_initialize_child(obj, "mscm", &s->mscm, TYPE_S32_MSCM);

    for (int i = 0; i < NXP_S32G_NUM_STM; ++i) {
        snprintf(name, NAME_SIZE, "stm%d", i);
        object_initialize_child(obj, name, &s->stm[i], TYPE_S32STM_TIMER);
    }
    /* object_initialize_child(obj, "pcie", &s->pcie, TYPE_DESIGNWARE_PCIE_HOST); */
}

static void nxp_s32g_realize(DeviceState *dev, Error **errp)
{
    /* MachineState *ms = MACHINE(qdev_get_machine()); */
    NxpS32GState *s = NXP_S32G(dev);
    DeviceState  *armv7m;
    
    printf("%s\n", __FUNCTION__);

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    armv7m = DEVICE(&s->m7_cpu);
    qdev_prop_set_uint32(armv7m, "num-irq", 240);
    //FIXME: This is hacky, we need to get the addresses by parsing the BootImage with the s32 header
    qdev_prop_set_uint32(armv7m, "init-nsvtor", NXP_S32G_SRAM_BASE);
    qdev_prop_set_uint32(armv7m, "init-svtor", NXP_S32G_SRAM_BASE);
    
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 4);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m7"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);

    if (!memory_region_init_rom(&s->qspi_nor, OBJECT(dev), "s32.qspi-nor", NXP_S32G_QSPI_AHB_SIZE, errp)) {
        return;
    }

    memory_region_add_subregion(get_system_memory(), NXP_S32G_QSPI_AHB_BASE, &s->qspi_nor);
    if (!memory_region_init_ram(&s->standby_ram, NULL, "s32.standby-ram", NXP_S32G_STANDBY_RAM_BASE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), NXP_S32G_STANDBY_RAM_SIZE, &s->standby_ram);

    if (!memory_region_init_ram(&s->sram, NULL, "s32.sram", NXP_S32G_SRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), NXP_S32G_SRAM_BASE, &s->sram);
    object_property_set_link(OBJECT(&s->m7_cpu), "memory", OBJECT(get_system_memory()), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->m7_cpu), &error_abort);

    qdev_prop_set_uint32(DEVICE(&s->mscm), "num-application-cores", NXP_S32G_NUM_A53_CPUS);
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->mscm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mscm), 0, NXP_S32G_MSCM_BASE_ADDR);

    for (int i = 0; i < NXP_S32G_NUM_STM; ++i) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } stm_table[NXP_S32G_NUM_STM] = {
            { NXP_S32G_STM0_BASE_ADDR, NXP_S32G_STM0_IRQ },
            { NXP_S32G_STM1_BASE_ADDR, NXP_S32G_STM1_IRQ },
            { NXP_S32G_STM2_BASE_ADDR, NXP_S32G_STM2_IRQ },
            { NXP_S32G_STM3_BASE_ADDR, NXP_S32G_STM3_IRQ },
            { NXP_S32G_STM4_BASE_ADDR, NXP_S32G_STM4_IRQ },
            { NXP_S32G_STM5_BASE_ADDR, NXP_S32G_STM5_IRQ },
            { NXP_S32G_STM6_BASE_ADDR, NXP_S32G_STM6_IRQ },
            { NXP_S32G_STM7_BASE_ADDR, NXP_S32G_STM7_IRQ },
        };

        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->stm[i]), errp))
            return;
        
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->stm[i]), 0, stm_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->stm[i]), 0, qdev_get_gpio_in(armv7m, stm_table[i].irq));
    }

    create_unimplemented_device("siul2.0", 0x4009C000, 0x2000);
    create_unimplemented_device("siul2.1", 0x44010000, 0x2000);
    create_unimplemented_device("mc_cgm_0", 0x40030000, 0x4000);
    create_unimplemented_device("mc_cgm_1", 0x40034000, 0x4000);
    create_unimplemented_device("mc_cgm_2", 0x44018000, 0x4000);
    create_unimplemented_device("mc_cgm_5", 0x40068000, 0x4000);
    create_unimplemented_device("core_dfs", 0x40054000, 0x4000);
    create_unimplemented_device("periph_dfs", 0x40058000, 0x4000);
    create_unimplemented_device("accel_pll", 0x40040000, 0x4000);
    create_unimplemented_device("core_pll", 0x40038000, 0x4000);
    create_unimplemented_device("ddr_pll", 0x40044000, 0x4000);
    create_unimplemented_device("periph_pll", 0x4003C000, 0x4000);
    create_unimplemented_device("fxosc", 0x40050000, 0x4000);

    create_unimplemented_device("swt0", 0x40100000, 0x4000);
    create_unimplemented_device("swt1", 0x40104000, 0x4000);
    create_unimplemented_device("swt2", 0x40108000, 0x4000);
    create_unimplemented_device("swt3", 0x4010C000, 0x4000);
    create_unimplemented_device("swt4", 0x40200000, 0x4000);
    create_unimplemented_device("swt5", 0x40204000, 0x4000);
    create_unimplemented_device("swt6", 0x40208000, 0x4000);

    create_unimplemented_device("pit0", 0x40188000, 0x4000);
    create_unimplemented_device("pit1", 0x40288000, 0x4000);

    create_unimplemented_device("ftm0", 0x401f4000, 0x4000);
    create_unimplemented_device("ftm1", 0x402e4000, 0x4000);

    create_unimplemented_device("rtc", 0x40060000, 0x4000);

    create_unimplemented_device("i2c0", 0x401E4000, 0x4000);
    create_unimplemented_device("i2c1", 0x401E8000, 0x4000);
    create_unimplemented_device("i2c2", 0x401EC000, 0x4000);
    create_unimplemented_device("i2c3", 0x402D8000, 0x4000);
    create_unimplemented_device("i2c4", 0x402DC000, 0x4000);

    create_unimplemented_device("gmac", 0x4033C000, 0x5000);

    create_unimplemented_device("linflex0", 0x401C8000, 0x4000);
    create_unimplemented_device("linflex1", 0x401CC000, 0x4000);
    create_unimplemented_device("linflex2", 0x402BC000, 0x4000);

    create_unimplemented_device("flexcan0", 0x401B4000, 0x4000);
    create_unimplemented_device("flexcan1", 0x401BE000, 0x4000);
    create_unimplemented_device("flexcan2", 0x402A8000, 0x4000);
    create_unimplemented_device("flexcan3", 0x402B2000, 0x4000);

    create_unimplemented_device("serdes_0_gpr", 0x407C5000, 0x4000);
    create_unimplemented_device("serdes_1_gpr", 0x407CC000, 0x4000);

    create_unimplemented_device("flexray0", 0x402F8000, 0x4000);

    create_unimplemented_device("ctu", 0x401FC000, 0x4000);

    create_unimplemented_device("adc0", 0x401F8000, 0x4000);
    create_unimplemented_device("adc1", 0x402E8000, 0x4000);

    create_unimplemented_device("tmu", 0x400A8000, 0x4000);

    create_unimplemented_device("erm.cpu0", 0x40318000, 0x400);
    create_unimplemented_device("erm.cpu1", 0x40318400, 0x400);
    create_unimplemented_device("erm.cpu2", 0x40318800, 0x400);
    create_unimplemented_device("erm.per",  0x40314000, 0x400);
    
    create_unimplemented_device("erm.pfe0", 0x44034000, 0x1000);
    create_unimplemented_device("erm.pfe1", 0x44035000, 0x1000);
    create_unimplemented_device("erm.pfe2", 0x44036000, 0x1000);
    create_unimplemented_device("erm.pfe3", 0x44037000, 0x1000);
    create_unimplemented_device("erm.pfe4", 0x44038000, 0x1000);
    create_unimplemented_device("erm.pfe5", 0x44039000, 0x1000);
    create_unimplemented_device("erm.pfe6", 0x4403A000, 0x1000);
    create_unimplemented_device("erm.pfe7", 0x4403B000, 0x1000);
    create_unimplemented_device("erm.pfe8", 0x4403C000, 0x1000);
    create_unimplemented_device("erm.pfe9", 0x4403D000, 0x1000);
    create_unimplemented_device("erm.pfe10", 0x4403E000, 0x1000);
    create_unimplemented_device("erm.pfe11", 0x4403F000, 0x1000);
    create_unimplemented_device("erm.pfe12", 0x44040000, 0x1000);
    create_unimplemented_device("erm.pfe13", 0x44041000, 0x1000);
    create_unimplemented_device("erm.pfe14", 0x44042000, 0x1000);
    create_unimplemented_device("erm.pfe15", 0x44043000, 0x1000);

    create_unimplemented_device("erm.stdby.sram", 0x44040000, 0x1000);
    create_unimplemented_device("erm.edma0", 0x40314400, 0x100);
    create_unimplemented_device("erm.edma1", 0x40314800, 0x100);

    create_unimplemented_device("mu0.mua", 0x23258000, 0x4000);
    create_unimplemented_device("mu1.mua", 0x23259000, 0x4000);
    create_unimplemented_device("mu2.mua", 0x2325A000, 0x4000);
    create_unimplemented_device("mu3.mua", 0x2325B000, 0x4000);

    create_unimplemented_device("fccu", 0x4030C000, 0x200);
    create_unimplemented_device("otp", 0x400A4000, 0x4000);
    
    create_unimplemented_device("spi0", 0x401D4000, 0x4000);
    create_unimplemented_device("spi1", 0x401D8000, 0x4000);
    create_unimplemented_device("spi2", 0x401DC000, 0x4000);
    create_unimplemented_device("spi3", 0x402C8000, 0x4000);
    create_unimplemented_device("spi4", 0x402CC000, 0x4000);
    create_unimplemented_device("spi5", 0x402D0000, 0x4000);

    create_unimplemented_device("cmu.fc.0", 0x4005C000, 0x20);
    create_unimplemented_device("cmu.fc.5", 0x4005C0A0, 0x20);
    create_unimplemented_device("cmu.fc.6", 0x4005C0C0, 0x20);
    create_unimplemented_device("cmu.fc.7", 0x4005C0E0, 0x20);
    create_unimplemented_device("cmu.fc.8", 0x4005C100, 0x20);
    create_unimplemented_device("cmu.fc.9", 0x4005C120, 0x20);
    create_unimplemented_device("cmu.fc.10", 0x4005C140, 0x20);
    create_unimplemented_device("cmu.fc.11", 0x4005C160, 0x20);
    create_unimplemented_device("cmu.fc.12", 0x4005C180, 0x20);
    create_unimplemented_device("cmu.fc.13", 0x4005C1A0, 0x20);
    create_unimplemented_device("cmu.fc.14", 0x4005C1C0, 0x20);
    create_unimplemented_device("cmu.fc.15", 0x4005C1E0, 0x20);
    create_unimplemented_device("cmu.fc.16", 0x4005C200, 0x20);
    create_unimplemented_device("cmu.fc.17", 0x4005C220, 0x20);
    create_unimplemented_device("cmu.fc.18", 0x4005C240, 0x20);
    create_unimplemented_device("cmu.fc.20", 0x4005C280, 0x20);
    create_unimplemented_device("cmu.fc.21", 0x4005C2A0, 0x20);
    create_unimplemented_device("cmu.fc.22", 0x4005C2C0, 0x20);
    create_unimplemented_device("cmu.fc.27", 0x4005C360, 0x20);
    create_unimplemented_device("cmu.fc.28", 0x4005C380, 0x20);
    create_unimplemented_device("cmu.fc.39", 0x4005C4E0, 0x20);
    create_unimplemented_device("cmu.fc.46", 0x4005C5C0, 0x20);
    create_unimplemented_device("cmu.fc.47", 0x4005C5E0, 0x20);
    create_unimplemented_device("cmu.fc.48", 0x4005C600, 0x20);
    create_unimplemented_device("cmu.fc.49", 0x4005C620, 0x20);
    create_unimplemented_device("cmu.fc.50", 0x4005C640, 0x20);
    create_unimplemented_device("cmu.fc.51", 0x4005C660, 0x20);

    create_unimplemented_device("cmu.fm.1", 0x4005C020, 0x20);
    create_unimplemented_device("cmu.fm.2", 0x4005C040, 0x20);
    create_unimplemented_device("cmu.fm.3", 0x4005C060, 0x20);
    create_unimplemented_device("cmu.fm.4", 0x4005C080, 0x20);

    create_unimplemented_device("crc.0", 0x40190000, 0x200);
    
    create_unimplemented_device("mc_me", 0x40088000, 0x4000);
    create_unimplemented_device("pmc", 0x4008C000, 0x4000);
    
    create_unimplemented_device("sramc", 0x4019C000, 0x4000);
    create_unimplemented_device("sramc_1", 0x401A0000, 0x4000);
    create_unimplemented_device("stby_sram_cfg", 0x44028000, 0x4000);

    create_unimplemented_device("qspic", 0x40134000, 0x4000);
    create_unimplemented_device("usdhc", 0x402F0000, 0x4000);
}

static Property nxp_s32g_properties[] = {
    DEFINE_PROP_UINT32("serdes-phy-num", NxpS32GState, phy_num, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nxp_s32g_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_props(dc, nxp_s32g_properties);
    dc->realize = nxp_s32g_realize;
    dc->desc = "S32G SOC";
    /* Reason: Uses serial_hd() in the realize() function */
    dc->user_creatable = false;
}

static const TypeInfo nxp_s32g_type_info = {
    .name = TYPE_NXP_S32G,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NxpS32GState),
    .instance_init = nxp_s32g_init,
    .class_init = nxp_s32g_class_init,
};

static void nxp_s32g_register_types(void)
{
    type_register_static(&nxp_s32g_type_info);
}

type_init(nxp_s32g_register_types)
