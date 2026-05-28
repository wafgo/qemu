/*
 * TI K3 AM64x SoC
 *
 * Copyright (c) 2026 CMBlu
 * Written by Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/bsa.h"
#include "hw/arm/ti-am64x.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "system/kvm.h"
#include "system/system.h"

#define TI_AM64X_GIC_DIST_ADDR  0x01800000
#define TI_AM64X_GIC_CPU_ADDR   0x01810000
#define TI_AM64X_MAIN_UART0_ADDR 0x02800000
#define TI_AM64X_MAIN_UART1_ADDR 0x02810000
#define TI_AM64X_MAIN_UART_SIZE  0x100
#define TI_AM64X_MAIN_UART_REGSHIFT 2

/* SPI = AM64x TRM GIC INTID - GIC_INTERNAL. */
#define TI_AM64X_MAIN_UART0_SPI 146  /* MAIN_UART0_USART_IRQ */
#define TI_AM64X_MAIN_UART1_SPI 147  /* MAIN_UART1_USART_IRQ */

static const struct {
    hwaddr addr;
    unsigned int spi;
} ti_am64x_main_uart_table[TI_AM64X_NUM_MAIN_UARTS] = {
    { TI_AM64X_MAIN_UART0_ADDR, TI_AM64X_MAIN_UART0_SPI },
    { TI_AM64X_MAIN_UART1_ADDR, TI_AM64X_MAIN_UART1_SPI },
};

static void ti_am64x_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    TIAM64xState *s = TI_AM64X(obj);
    unsigned int num_cpus = MIN(ms->smp.cpus, TI_AM64X_NUM_A53_CPUS);
    unsigned int i;

    for (i = 0; i < num_cpus; i++) {
        object_initialize_child(obj, "a53[*]", &s->a53[i],
                                ARM_CPU_TYPE_NAME("cortex-a53"));
    }

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);

    for (i = 0; i < TI_AM64X_NUM_MAIN_UARTS; i++) {
        object_initialize_child(obj, "main-uart[*]", &s->main_uart[i],
                                TYPE_SERIAL_MM);
    }
}

static bool ti_am64x_realize_cpus(TIAM64xState *s, unsigned int num_cpus,
                                  Error **errp)
{
    unsigned int i;

    for (i = 0; i < num_cpus; i++) {
        Object *cpuobj = OBJECT(&s->a53[i]);

        if (!object_property_set_int(cpuobj, "mp-affinity", i, errp)) {
            return false;
        }

        if (object_property_find(cpuobj, "has_el3")) {
            object_property_set_bool(cpuobj, "has_el3", !kvm_enabled(),
                                     &error_abort);
        }
        if (object_property_find(cpuobj, "has_el2")) {
            object_property_set_bool(cpuobj, "has_el2", !kvm_enabled(),
                                     &error_abort);
        }
        object_property_set_bool(cpuobj, "start-powered-off", i != 0,
                                 &error_abort);

        if (!qdev_realize(DEVICE(cpuobj), NULL, errp)) {
            return false;
        }
    }

    return true;
}

static bool ti_am64x_realize_gic(TIAM64xState *s, unsigned int num_cpus,
                                 Error **errp)
{
    DeviceState *gicdev = DEVICE(&s->gic);
    SysBusDevice *gicsbd = SYS_BUS_DEVICE(&s->gic);
    unsigned int i;

    qdev_prop_set_uint32(gicdev, "revision", 2);
    qdev_prop_set_uint32(gicdev, "num-cpu", num_cpus);
    qdev_prop_set_uint32(gicdev, "num-irq",
                         TI_AM64X_NUM_SPIS + GIC_INTERNAL);

    if (!sysbus_realize(gicsbd, errp)) {
        return false;
    }

    sysbus_mmio_map(gicsbd, 0, TI_AM64X_GIC_DIST_ADDR);
    sysbus_mmio_map(gicsbd, 1, TI_AM64X_GIC_CPU_ADDR);

    for (i = 0; i < num_cpus; i++) {
        DeviceState *cpudev = DEVICE(&s->a53[i]);
        int ppi_base = TI_AM64X_NUM_SPIS + i * GIC_INTERNAL;
        static const int timer_irqs[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
        };
        int j;

        for (j = 0; j < ARRAY_SIZE(timer_irqs); j++) {
            qdev_connect_gpio_out(cpudev, j,
                                  qdev_get_gpio_in(gicdev,
                                                   ppi_base + timer_irqs[j]));
        }

        sysbus_connect_irq(gicsbd, i,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicsbd, i + num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicsbd, i + 2 * num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicsbd, i + 3 * num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    return true;
}

static bool ti_am64x_realize_uarts(TIAM64xState *s, Error **errp)
{
    DeviceState *gicdev = DEVICE(&s->gic);
    int i;

    for (i = 0; i < TI_AM64X_NUM_MAIN_UARTS; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->main_uart[i]);

        qdev_prop_set_uint8(DEVICE(sbd), "regshift",
                            TI_AM64X_MAIN_UART_REGSHIFT);
        qdev_prop_set_uint32(DEVICE(sbd), "baudbase", 48000000);
        qdev_prop_set_chr(DEVICE(sbd), "chardev", serial_hd(i));

        if (!sysbus_realize(sbd, errp)) {
            return false;
        }

        sysbus_mmio_map(sbd, 0, ti_am64x_main_uart_table[i].addr);
        sysbus_connect_irq(sbd, 0,
                           qdev_get_gpio_in(gicdev,
                                            ti_am64x_main_uart_table[i].spi));
    }

    return true;
}

static void ti_am64x_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    TIAM64xState *s = TI_AM64X(dev);
    unsigned int num_cpus = ms->smp.cpus;

    if (num_cpus < 1 || num_cpus > TI_AM64X_NUM_A53_CPUS) {
        error_setg(errp,
                   "%s: only between 1 and %u Cortex-A53 CPUs are supported "
                   "(%u requested)",
                   TYPE_TI_AM64X, TI_AM64X_NUM_A53_CPUS, num_cpus);
        return;
    }

    if (!ti_am64x_realize_cpus(s, num_cpus, errp)) {
        return;
    }

    if (!ti_am64x_realize_gic(s, num_cpus, errp)) {
        return;
    }

    if (!ti_am64x_realize_uarts(s, errp)) {
        return;
    }
}

static void ti_am64x_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ti_am64x_realize;
    /* Mapped at fixed locations on the system bus by board code. */
    dc->user_creatable = false;
    dc->desc = "TI K3 AM64x SoC";
}

static const TypeInfo ti_am64x_types[] = {
    {
        .name = TYPE_TI_AM64X,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TIAM64xState),
        .instance_init = ti_am64x_init,
        .class_init = ti_am64x_class_init,
    },
};

DEFINE_TYPES(ti_am64x_types)
