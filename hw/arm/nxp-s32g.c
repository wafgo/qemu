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

static void nxp_s32g_init(Object *obj)
{
    /* MachineState *mms = MACHINE(qdev_get_machine()); */
    NxpS32GState *s = NXP_S32G(obj);
    
    object_initialize_child(obj, "m7-cpu", &s->m7_cpu, TYPE_ARMV7M);
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);

    object_initialize_child(obj, "pcie", &s->pcie, TYPE_DESIGNWARE_PCIE_HOST);
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
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 4);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m7"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);

    if (!memory_region_init_rom(&s->qspi_nor, OBJECT(dev), "s32.qspi-nor",
                                (128 * MiB), errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), 0x0, &s->qspi_nor);

    if (!memory_region_init_ram(&s->standby_ram, NULL, "s32.standby-ram", NXP_S32G_STANDBY_RAM_BASE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), NXP_S32G_STANDBY_RAM_SIZE, &s->standby_ram);

    if (!memory_region_init_ram(&s->sram, NULL, "s32.sram", NXP_S32G_SRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), NXP_S32G_SRAM_BASE, &s->sram);
    object_property_set_link(OBJECT(&s->m7_cpu), "memory", OBJECT(get_system_memory()), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->m7_cpu), &error_abort);
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
