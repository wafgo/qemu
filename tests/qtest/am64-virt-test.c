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

static void test_devstat(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* CTRLMMR_MAIN_DEVSTAT: primary bootmode = eMMC (0x9 << 3) */
    g_assert_cmphex(qtest_readl(qts, 0x43000030), ==, 0x48);
    /* mmr_unlock kick writes must be accepted silently */
    qtest_writel(qts, 0x43008008, 0x68ef3490);
    qtest_writel(qts, 0x4300800c, 0xd172bc5a);
    qtest_quit(qts);
}

#define SP_TARGET(thread) (0x4D000000ULL + (thread) * 0x1000)
#define SP_RT(thread)     (0x4A600000ULL + (thread) * 0x1000)

static void test_dmsc_r5_version(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    /*
     * Secure-host TISCI VERSION request as the R5 SPL sends it:
     * word0 = secure header {u16 checksum=0; u16 reserved=0}
     * word1 = {u16 type=0x0002; u8 host=35; u8 seq=0xa}
     * word2 = flags = TISCI_MSG_FLAG_AOP (0x2)
     */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a230002);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    /* commit: write the last data word */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);

    /* response must land on RX thread 0 (message count > 0) */
    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmpuint(qtest_readl(qts, SP_RT(0)) & 0xff, >, 0);

    /*
     * secure hdr (word0) then TISCI hdr: type must echo 0x0002,
     * flags word must have ACK set (bit 1)
     */
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x0002);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/am64-virt/ocsram", test_ocsram_rw);
    qtest_add_func("/am64-virt/main-uart0", test_main_uart0_present);
    qtest_add_func("/am64-virt/r5f-present", test_r5f_cpu_present);
    qtest_add_func("/am64-virt/devstat", test_devstat);
    qtest_add_func("/am64-virt/dmsc-r5-version", test_dmsc_r5_version);
    return g_test_run();
}
