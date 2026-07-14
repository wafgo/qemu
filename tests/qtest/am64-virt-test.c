/*
 * QTests for the AM64 virt machine (cmblu/corenode fork)
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define OCSRAM_BASE 0x70000000ULL
#define OCSRAM_SIZE (2 * 1024 * 1024)
#define MAIN_UART0_BASE 0x02800000ULL

static void test_ocsram_rw(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    qtest_writel(qts, OCSRAM_BASE, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts, OCSRAM_BASE), ==, 0xdeadbeef);
    qtest_writel(qts, OCSRAM_BASE + OCSRAM_SIZE - 4, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, OCSRAM_BASE + OCSRAM_SIZE - 4), ==,
                    0x12345678);
    /* boot-param area is inside OCSRAM */
    qtest_writel(qts, 0x701bebfc, 0x0);
    g_assert_cmphex(qtest_readl(qts, 0x701bebfc), ==, 0x0);
    qtest_quit(qts);
}

static void test_main_uart0_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* LSR of an idle 16550: transmitter empty bits set */
    g_assert_cmphex(qtest_readl(qts, MAIN_UART0_BASE + (5 << 2)) & 0x60,
                    ==, 0x60);
    qtest_quit(qts);
}

static void test_r5f_cpu_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    QDict *resp = qtest_qmp(qts, "{'execute': 'query-cpus-fast'}");
    QList *cpus = qdict_get_qlist(resp, "return");

    /* 2x A53 + 1x M4 + 1x R5F */
    g_assert_cmpint(qlist_size(cpus), ==, 4);
    qobject_unref(resp);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/am64-virt/ocsram", test_ocsram_rw);
    qtest_add_func("/am64-virt/main-uart0", test_main_uart0_present);
    qtest_add_func("/am64-virt/r5f-present", test_r5f_cpu_present);
    return g_test_run();
}
