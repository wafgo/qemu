/*
 * QTests for the AM64x main_i2c0 controller (cmblu/corenode fork)
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

/* main_i2c0 window (k3-am64-main.dtsi: i2c@20000000, len 0x100) */
#define I2C0_BASE 0x20000000ULL

/* OMAP I2C IP rev V2 (ti,omap4-i2c / ti,am64-i2c) register offsets */
#define I2C_V2_SYSC          0x10
#define I2C_V2_IRQSTATUS_RAW 0x24
#define I2C_V2_IRQSTATUS     0x28
#define I2C_V2_SYSS          0x90
#define I2C_V2_CNT           0x98
#define I2C_V2_DATA          0x9c
#define I2C_V2_CON           0xa4
#define I2C_V2_SA            0xac

/* register bits */
#define I2C_SYSC_SRST   (1 << 1)  /* SYSCONFIG soft reset */
#define I2C_SYSS_RDONE  (1 << 0)  /* reset done */
#define I2C_STAT_NACK   (1 << 1)  /* no acknowledgement */
#define I2C_STAT_ARDY   (1 << 2)  /* register access ready */
#define I2C_STAT_RRDY   (1 << 3)  /* receive data ready */

#define I2C_CON_EN  (1 << 15)     /* module enable */
#define I2C_CON_MST (1 << 10)     /* master mode */
#define I2C_CON_STP (1 << 1)      /* stop condition */
#define I2C_CON_STT (1 << 0)      /* start condition */

/*
 * u-boot's __omap24_i2c_init() writes SRST to SYSCONFIG and then spins on
 * SYSS.RDONE until it reads back set; if RDONE never asserts it prints
 * "ERROR: Timeout in soft-reset". Model that RDONE becomes 1.
 */
static void test_soft_reset_completes(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t syss = 0;

    qtest_writew(qts, I2C0_BASE + I2C_V2_SYSC, I2C_SYSC_SRST);

    for (int i = 0; i < 16; i++) {
        syss = qtest_readw(qts, I2C0_BASE + I2C_V2_SYSS);
        if (syss & I2C_SYSS_RDONE) {
            break;
        }
    }
    g_assert_cmphex(syss & I2C_SYSS_RDONE, ==, I2C_SYSS_RDONE);
    qtest_quit(qts);
}

/*
 * A master transfer to an address with no attached slave must raise NACK
 * promptly in IRQSTATUS_RAW so u-boot's probe returns -EREMOTEIO instead of
 * hanging in wait_for_event() ("Timed out in wait_for_event: status=0000").
 * The am64-virt machine populates only 0x50 (the SoM EEPROM, Task 5b), so an
 * unused address such as 0x51 NACKs.
 */
static void test_nack_on_absent_slave(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t stat;

    /* bring the controller out of reset the way u-boot does */
    qtest_writew(qts, I2C0_BASE + I2C_V2_SYSC, I2C_SYSC_SRST);
    (void)qtest_readw(qts, I2C0_BASE + I2C_V2_SYSS);

    /* target address 0x51 (no slave attached), 1-byte master read transfer */
    qtest_writew(qts, I2C0_BASE + I2C_V2_SA, 0x51);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CNT, 1);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP);

    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS_RAW);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, I2C_STAT_NACK);

    /* STAT (IRQSTATUS) mirrors the same NACK and is write-1-to-clear */
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, I2C_STAT_NACK);
    qtest_writew(qts, I2C0_BASE + I2C_V2_IRQSTATUS, I2C_STAT_NACK);
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, 0);

    qtest_quit(qts);
}

/*
 * The SoM identity EEPROM (Task 5b) sits at 0x50 preloaded with a valid
 * phytec api-v2 blob whose first byte (api_rev) is 0x02.  A master-receive
 * transfer must ACK, stream the byte via the (byte-wide) V2 DATA register,
 * and finish with ARDY -- this exercises the fix that lets u-boot's
 * phytec_eeprom_read() succeed instead of logging "i2c EEPROM not found".
 */
static void test_eeprom_read_first_byte(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t stat;
    uint8_t b;

    qtest_writew(qts, I2C0_BASE + I2C_V2_SYSC, I2C_SYSC_SRST);
    (void)qtest_readw(qts, I2C0_BASE + I2C_V2_SYSS);

    /* 1-byte master read from 0x50 (internal address pointer resets to 0) */
    qtest_writew(qts, I2C0_BASE + I2C_V2_SA, 0x50);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CNT, 1);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP);

    /* the slave ACKed: no NACK, data is ready */
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS_RAW);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, 0);
    g_assert_cmphex(stat & I2C_STAT_RRDY, ==, I2C_STAT_RRDY);

    /* api_rev byte at offset 0 must be 0x02 (PHYTEC_API_REV2) */
    b = qtest_readw(qts, I2C0_BASE + I2C_V2_DATA) & 0xff;
    g_assert_cmphex(b, ==, 0x02);

    /* the single-byte transfer is now complete: ARDY, no more RRDY */
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS_RAW);
    g_assert_cmphex(stat & I2C_STAT_ARDY, ==, I2C_STAT_ARDY);
    g_assert_cmphex(stat & I2C_STAT_RRDY, ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/am64/i2c/soft-reset", test_soft_reset_completes);
    qtest_add_func("/am64/i2c/nack", test_nack_on_absent_slave);
    qtest_add_func("/am64/i2c/eeprom-read", test_eeprom_read_first_byte);
    return g_test_run();
}
