/*
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 * Copyright (c) 2024 Sriram Neelakantan <sriram.neelakantan@continental-corporation.com>
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
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "chardev/char.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/qdev-clock.h"
#include "target/arm/cpu-qom.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"

#define NAME_SIZE 40

static const int cmu_fc_instances[] = {
        0, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16,
        17, 18, 20, 21, 22, 27,
        28, 39, 46, 47, 48, 49,
        50, 51
};
    
static void nxp_s32g_init(Object *obj)
{
    NxpS32GState *s = NXP_S32G(obj);
    char name[NAME_SIZE];
    char *cont_name;
    int i;

    for (i = 0; i < NXP_S32G_NUM_M7_CPUS; ++i) {
        object_initialize_child(obj, "m7-cpu[*]", &s->m7_cpu[i], TYPE_ARMV7M);
        cont_name = g_strdup_printf("arm-cpu-container%d", i);
        memory_region_init(&s->cpu_container[i], obj, cont_name, UINT64_MAX);
        g_free(cont_name);
        if (i > 0) {
            cont_name = g_strdup_printf("arm-cpu-container-alias%d", i);
            memory_region_init_alias(&s->container_alias[i - 1], obj, cont_name, get_system_memory(), 0, UINT64_MAX);
            g_free(cont_name);
        }
    }
    
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    object_initialize_child(obj, "mscm", &s->mscm, TYPE_S32_MSCM);
    object_initialize_child(obj, "mcme", &s->mod_entry, TYPE_S32_MCME);
    object_initialize_child(obj, "rdc", &s->rdc, TYPE_S32_RDC);

    object_initialize_child(obj, "cgm0", &s->cgm[0], TYPE_S32_CGM);
    object_initialize_child(obj, "cgm1", &s->cgm[1], TYPE_S32_CGM);
    object_initialize_child(obj, "cgm2", &s->cgm[2], TYPE_S32_CGM);
    object_initialize_child(obj, "cgm5", &s->cgm[3], TYPE_S32_CGM);

    object_initialize_child(obj, "core-dfs", &s->core_dfs, TYPE_S32_DFS);
    object_initialize_child(obj, "periph-dfs", &s->periph_dfs, TYPE_S32_DFS);
    object_initialize_child(obj, "fxosc", &s->fxosc, TYPE_S32_FXOSC);

    object_initialize_child(obj, "core-pll", &s->core_pll, TYPE_S32_PLL);
    object_initialize_child(obj, "periph-pll", &s->periph_pll, TYPE_S32_PLL);
    object_initialize_child(obj, "accel-pll", &s->accel_pll, TYPE_S32_PLL);
    object_initialize_child(obj, "ddr-pll", &s->ddr_pll, TYPE_S32_PLL);
    object_initialize_child(obj, "sramc", &s->sramc, TYPE_S32_SRAMC);
    object_initialize_child(obj, "sramc_1", &s->sramc_1, TYPE_S32_SRAMC);
    object_initialize_child(obj, "stdb_sram_cfg", &s->stdb_sram_cfg, TYPE_S32_SRAMC);
    object_initialize_child(obj, "sema42", &s->sema, TYPE_NXP_SEMA42);

    for (i = 0; i < NXP_S32G_NUM_STM; ++i) {
        snprintf(name, NAME_SIZE, "stm%d", i);
        object_initialize_child(obj, name, &s->stm[i], TYPE_S32STM_TIMER);
    }

    for (i = 0; i < ARRAY_SIZE(cmu_fc_instances); ++i) {
        snprintf(name, NAME_SIZE, "cmu.fc%d", cmu_fc_instances[i]);
        object_initialize_child(obj, name, &s->cmu_fc[i], TYPE_S32_CMU_FC);
    }

    for (i = 0; i < NXP_S32G_NUM_LINFLEXD; i++) {
        object_initialize_child(obj, "linflexd[*]", &s->linflexd[i], TYPE_LINFLEXD);
    }

    for (i = 0; i < NXP_S32G_NUM_I2C; i++) {
        object_initialize_child(obj, "i2c[*]", &s->i2c[i], TYPE_S32_I2C);
    }

    for (i = 0; i < NXP_S32G_NUM_EDMA; ++i) {
        object_initialize_child(obj, "edma-mg[*]", &s->edma[i].mg, TYPE_NXP_EDMA);
        object_initialize_child(obj, "edma-tcd[*]", &s->edma[i].tcd, TYPE_NXP_EDMA_TCD);
        object_property_add_const_link(OBJECT(&s->edma[i].tcd), "dma-mr",
                                   OBJECT(get_system_memory()));
    }

}

static void nxp_s32g_create_unimplemented(NxpS32GState *s, Error **errp)
{
    create_unimplemented_device("siul2.0", 0x4009C000, 0x2000);
    create_unimplemented_device("siul2.1", 0x44010000, 0x2000);

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

    /*create_unimplemented_device("i2c0", 0x401E4000, 0x4000);
    create_unimplemented_device("i2c1", 0x401E8000, 0x4000);
    create_unimplemented_device("i2c2", 0x401EC000, 0x4000);
    create_unimplemented_device("i2c3", 0x402D8000, 0x4000);
    create_unimplemented_device("i2c4", 0x402DC000, 0x4000);*/

    create_unimplemented_device("gmac", 0x4033C000, 0x5000);

    /* create_unimplemented_device("linflex0", 0x401C8000, 0x4000); */
    /* create_unimplemented_device("linflex1", 0x401CC000, 0x4000); */
    /* create_unimplemented_device("linflex2", 0x402BC000, 0x4000); */

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

    create_unimplemented_device("cmu.fm.1", 0x4005C020, 0x20);
    create_unimplemented_device("cmu.fm.2", 0x4005C040, 0x20);
    create_unimplemented_device("cmu.fm.3", 0x4005C060, 0x20);
    create_unimplemented_device("cmu.fm.4", 0x4005C080, 0x20);

    create_unimplemented_device("crc.0", 0x40190000, 0x200);
    
    create_unimplemented_device("pmc", 0x4008C000, 0x4000);
    
    /* create_unimplemented_device("sramc", 0x4019C000, 0x4000); */
    /* create_unimplemented_device("sramc_1", 0x401A0000, 0x4000); */
    /* create_unimplemented_device("stby_sram_cfg", 0x44028000, 0x4000); */

    create_unimplemented_device("qspic", 0x40134000, 0x4000);
    create_unimplemented_device("usdhc", 0x402F0000, 0x4000);

    /* create_unimplemented_device("edma0", 0x40144000, 0x200); */
    /* create_unimplemented_device("edma1", 0x40244000, 0x200); */

    create_unimplemented_device("dma_crc0", 0x4013C000, 0x100);
    create_unimplemented_device("dma_crc1", 0x4023C000, 0x100);

    create_unimplemented_device("dma_mux0", 0x4012C000, 0x10);
    create_unimplemented_device("dma_mux1", 0x4013C000, 0x10);
    create_unimplemented_device("dma_mux2", 0x4022C000, 0x10);
    create_unimplemented_device("dma_mux3", 0x40230000, 0x10);

    create_unimplemented_device("xrdc_0", 0x401A4000, 0x3C00);
    create_unimplemented_device("xrdc_1", 0x44004000, 0x2B00);

    /* create_unimplemented_device("sema42", 0x40298000, 0x50); */
    create_unimplemented_device("src", 0x4007c000, 0x100);
}

static void sema_realize(NxpS32GState *s, Error **errp)
{
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->sema), errp))
        return;
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sema), 0, NXP_S32G_SEMA42_BASE_ADDR);
}

static void dma_controller_realize(NxpS32GState *s, Error **errp)
{
    int i;
    int ch;
    for (i = 0; i < NXP_S32G_NUM_EDMA; ++i) {
        static const struct {
            hwaddr mg_addr;
            hwaddr tcd_addr;
            unsigned int m7_irq_chl;
            unsigned int m7_irq_chu;
            unsigned int m7_irq_err;
        } dma_sysmap[] = {
            {NXP_S32G_EDMA0_MG_BASE_ADDR, NXP_S32G_EDMA0_TCD_BASE_ADDR, NXP_S32G_EDMA0_CH_LOWER_IRQ, NXP_S32G_EDMA0_CH_UPPER_IRQ, NXP_S32G_EDMA0_CH_ERR_IRQ},
            {NXP_S32G_EDMA1_MG_BASE_ADDR, NXP_S32G_EDMA1_TCD_BASE_ADDR, NXP_S32G_EDMA1_CH_LOWER_IRQ, NXP_S32G_EDMA1_CH_UPPER_IRQ, NXP_S32G_EDMA1_CH_ERR_IRQ},
        };

        qdev_prop_set_uint32(DEVICE(&s->edma[i].tcd), "number-channels", NXP_S32G_NUM_EDMA_CHANNELS);
        switch(i) {
        case 0:
            qdev_prop_set_uint32(DEVICE(&s->edma[i].tcd), "sbr-reset", 0x00008006);
            break;
        case 1:
            qdev_prop_set_uint32(DEVICE(&s->edma[i].tcd), "sbr-reset", 0x00008007);
            break;
        default:
            break;
        }
        
        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->edma[i].mg), errp))
            return;
        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->edma[i].tcd), errp))
            return;
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->edma[i].mg), 0, dma_sysmap[i].mg_addr);
        for (ch = 0; ch < NXP_S32G_NUM_EDMA_CHANNELS; ++ch)
            sysbus_mmio_map(SYS_BUS_DEVICE(&s->edma[i].tcd), ch, dma_sysmap[i].tcd_addr + (ch * NXP_S32G_EDMA_CHANNEL_MMIO_SIZE));
        /* sysbus_connect_irq(SYS_BUS_DEVICE(&s->edma[i].tcd), 0, qdev_get_gpio_in(DEVICE(&s->m7_cpu), dma_sysmap[i].m7_irq_chl)); */
        /* sysbus_connect_irq(SYS_BUS_DEVICE(&s->edma[i].tcd), 0, qdev_get_gpio_in(DEVICE(&s->m7_cpu), dma_sysmap[i].m7_irq_chu)); */
        /* sysbus_connect_irq(SYS_BUS_DEVICE(&s->edma[i].tcd), 0, qdev_get_gpio_in(DEVICE(&s->m7_cpu), dma_sysmap[i].m7_irq_err)); */
    }
}

static void sram_controller_realize(NxpS32GState *s, Error **errp)
{
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->sramc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sramc), 0, NXP_S32G_SRAMC_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->sramc_1), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sramc_1), 0, NXP_S32G_SRAMC_1_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->stdb_sram_cfg), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->stdb_sram_cfg), 0, NXP_S32G_STBY_SRAMC_CFG_BASE_ADDR);
}

static void i2c_realize(NxpS32GState *s, Error **errp)
{
    int i;
      /* Initialize all I2C */
    for (i = 0; i < NXP_S32G_NUM_I2C; i++) {
        hwaddr i2c_table[] = {
            NXP_S32G_PERIPH_I2C_0_BASE_ADDR,
            NXP_S32G_PERIPH_I2C_1_BASE_ADDR,
            NXP_S32G_PERIPH_I2C_2_BASE_ADDR,
            NXP_S32G_PERIPH_I2C_3_BASE_ADDR,
            NXP_S32G_PERIPH_I2C_4_BASE_ADDR 
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[i]), 0, i2c_table[i]);
    }

}

static void linflexd_realize(NxpS32GState *s, Error **errp)
{
    int i, cpu;
    for (i = 0; i < NXP_S32G_NUM_LINFLEXD; ++i) {
        static const struct {
            hwaddr addr;
            unsigned int m7_irq;
        } serial_sysmap[] = {
            {NXP_S32G_PERIPH_LINFLEXD_0_BASE_ADDR, NXP_S32G_LINFLEXD0_M7_IRQ},
            {NXP_S32G_PERIPH_LINFLEXD_1_BASE_ADDR, NXP_S32G_LINFLEXD1_M7_IRQ},
            {NXP_S32G_PERIPH_LINFLEXD_2_BASE_ADDR, NXP_S32G_LINFLEXD2_M7_IRQ},  
        };

        /* Connect Debug UART to stdio */
        if (i == s->debug_uart)
            qdev_prop_set_chr(DEVICE(&s->linflexd[i]), "chardev", serial_hd(0));

        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->linflexd[i]), errp))
            return;
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->linflexd[i]), 0, serial_sysmap[i].addr);
        for (cpu = 0; cpu < NXP_S32G_NUM_M7_CPUS; ++cpu) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->linflexd[i]), 0, qdev_get_gpio_in(DEVICE(&s->m7_cpu[cpu]), serial_sysmap[i].m7_irq));
        }
    }
}

static void cmu_realize(NxpS32GState *s, Error **errp)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(cmu_fc_instances); ++i) {
        uint32_t addr = (NXP_S32G_CMU_FC_BASE_ADDR + (cmu_fc_instances[i] * 0x20));
        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->cmu_fc[i]), errp))
            return;
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->cmu_fc[i]), 0, addr);
    }

}
static void timer_realize(NxpS32GState *s, Error **errp)
{
    int i, cpu;
    for (i = 0; i < NXP_S32G_NUM_STM; ++i) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } stm_table[NXP_S32G_NUM_STM] = {
            { NXP_S32G_STM0_BASE_ADDR, NXP_S32G_STM0_M7_IRQ },
            { NXP_S32G_STM1_BASE_ADDR, NXP_S32G_STM1_M7_IRQ },
            { NXP_S32G_STM2_BASE_ADDR, NXP_S32G_STM2_M7_IRQ },
            { NXP_S32G_STM3_BASE_ADDR, NXP_S32G_STM3_M7_IRQ },
            { NXP_S32G_STM4_BASE_ADDR, NXP_S32G_STM4_M7_IRQ },
            { NXP_S32G_STM5_BASE_ADDR, NXP_S32G_STM5_M7_IRQ },
            { NXP_S32G_STM6_BASE_ADDR, NXP_S32G_STM6_M7_IRQ },
            { NXP_S32G_STM7_BASE_ADDR, NXP_S32G_STM7_M7_IRQ },
        };

        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->stm[i]), errp))
            return;
        
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->stm[i]), 0, stm_table[i].addr);
        for (cpu = 0; cpu < NXP_S32G_NUM_M7_CPUS; ++cpu) {
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->stm[i]), 0, qdev_get_gpio_in(DEVICE(&s->m7_cpu[cpu]), stm_table[i].irq));
        }
    }

}

static void reset_realize(NxpS32GState *s, Error **errp)
{
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->rdc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rdc), 0, NXP_S32G_RDC_BASE_ADDR);
}

static void misc_realize(NxpS32GState *s, Error **errp)
{
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->mscm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mscm), 0, NXP_S32G_MSCM_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->mod_entry), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mod_entry), 0, NXP_S32G_MCME_BASE_ADDR);

}
static void clock_subsystem_realize(NxpS32GState *s, Error **errp)
{
    int i;
    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->core_dfs), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->core_dfs), 0, NXP_S32G_CORE_DFS_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->periph_dfs), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->periph_dfs), 0, NXP_S32G_PERIPH_DFS_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->fxosc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fxosc), 0, NXP_S32G_FXOSC_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->core_pll), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->core_pll), 0, NXP_S32G_CORE_PLL_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->periph_pll), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->periph_pll), 0, NXP_S32G_PERIPH_PLL_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->accel_pll), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->accel_pll), 0, NXP_S32G_ACCEL_PLL_BASE_ADDR);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->ddr_pll), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_pll), 0, NXP_S32G_DDR_PLL_BASE_ADDR);

    for (i = 0; i < NXP_S32G_NUM_CGM; ++i) {
        hwaddr addr[] = {
            NXP_S32G_CGM0_BASE_ADDR,
            NXP_S32G_CGM1_BASE_ADDR,
            NXP_S32G_CGM2_BASE_ADDR,
            NXP_S32G_CGM5_BASE_ADDR
        };
        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->cgm[i]), errp))
            return;
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->cgm[i]), 0, addr[i]);
    }

}
static void nxp_s32g_realize(DeviceState *dev, Error **errp)
{
    NxpS32GState *s = NXP_S32G(dev);
    DeviceState  *armv7m;
    int cpu;
    
    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    for (cpu = 0; cpu < NXP_S32G_NUM_M7_CPUS; ++cpu) {
        armv7m = DEVICE(&s->m7_cpu[cpu]);
        qdev_prop_set_uint32(armv7m, "num-irq", 240);
        //FIXME: This is hacky, we need to get the addresses by parsing the BootImage with the s32 header
        qdev_prop_set_uint32(armv7m, "init-nsvtor", NXP_S32G_SRAM_BASE);
        qdev_prop_set_uint32(armv7m, "init-svtor", NXP_S32G_SRAM_BASE);
    
        qdev_prop_set_uint8(armv7m, "num-prio-bits", 4);
        qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m7"));
        qdev_prop_set_bit(armv7m, "enable-bitband", false);
        qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    }

    if (!memory_region_init_rom(&s->qspi_nor, OBJECT(dev), "s32.qspi-nor", NXP_S32G_QSPI_AHB_SIZE, errp)) {
        return;
    }

    memory_region_add_subregion(get_system_memory(), NXP_S32G_QSPI_AHB_BASE, &s->qspi_nor);
    if (!memory_region_init_ram(&s->standby_ram, NULL, "s32.standby-ram", NXP_S32G_STANDBY_RAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), NXP_S32G_STANDBY_RAM_BASE, &s->standby_ram);

    if (!memory_region_init_ram(&s->llce_as, NULL, "s32.llce-as", NXP_S32G_LLCE_AS_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), NXP_S32G_LLCE_AS_BASE, &s->llce_as);

    if (!memory_region_init_ram(&s->sram, NULL, "s32.sram", NXP_S32G_SRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), NXP_S32G_SRAM_BASE, &s->sram);
    for (cpu = 0; cpu < NXP_S32G_NUM_M7_CPUS; ++cpu) {
        if (cpu > 0) {
            memory_region_add_subregion_overlap(&s->cpu_container[cpu], 0,
                                                &s->container_alias[cpu - 1], -1);
        } else {
            memory_region_add_subregion_overlap(&s->cpu_container[cpu], 0,
                                                get_system_memory(), -1);
        }
        //memory_region_add_subregion(&s->cpu_container[cpu], 0, get_system_memory())
         /* memory_region_add_subregion_overlap(&s->cpu_container[cpu], NXP_S32G_SRAM_BASE, &s->sram, 0); */
        object_property_set_link(OBJECT(&s->m7_cpu[cpu]), "memory", OBJECT(&s->cpu_container[cpu]), &error_abort);
        if (cpu != 0)
            object_property_set_bool(OBJECT(&s->m7_cpu[cpu]), "start-powered-off", true,
                                     &error_abort);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->m7_cpu[cpu]), &error_abort);
    }

    qdev_prop_set_uint32(DEVICE(&s->mscm), "num-application-cores", NXP_S32G_NUM_A53_CPUS);
    misc_realize(s, errp);

    reset_realize(s, errp);
    clock_subsystem_realize(s, errp);
    sram_controller_realize(s, errp);
    timer_realize(s, errp);
    cmu_realize(s, errp);
    linflexd_realize(s, errp);
    i2c_realize(s, errp);
    dma_controller_realize(s, errp);
    sema_realize(s, errp);
    nxp_s32g_create_unimplemented(s, errp);
}

static Property nxp_s32g_properties[] = {
    DEFINE_PROP_UINT32("serdes-phy-num", NxpS32GState, phy_num, 0),
    DEFINE_PROP_UINT32("debug-uart", NxpS32GState, debug_uart, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nxp_s32g_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_props(dc, nxp_s32g_properties);
    dc->realize = nxp_s32g_realize;
    dc->desc = "S32G SOC";
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
