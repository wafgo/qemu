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

#define DDRSS_CFG_BASE    0x0f308000ULL
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

static void test_dmsc_r5_get_freq(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /*
     * Secure-host TISCI GET_FREQ (0x010e) request as the R5 SPL sends it
     * (mirrors u-boot's ti_sci_msg_req_get_clock_freq: hdr; u32 dev_id;
     * u8 clk_id):
     * word0 = secure header {u16 checksum=0; u16 reserved=0}
     * word1 = {u16 type=0x010e; u8 host=35; u8 seq=0xa}
     * word2 = flags = TISCI_MSG_FLAG_AOP (0x2)
     * word3 = dev_id = 57 (MMCSD0)
     * word4 = clk_id = 1
     */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23010e);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 57);
    qtest_writel(qts, SP_TARGET(1) + 0x14, 1);
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
     * secure hdr (word0) then TISCI hdr: type must echo 0x010e,
     * flags word must have ACK set (bit 1)
     */
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x010e);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    /* freq_hz (u64) directly after the 8-byte TISCI hdr: != 0 */
    g_assert_cmpuint(qtest_readl(qts, SP_TARGET(0) + 0x10), !=, 0);
    qtest_quit(qts);
}

/*
 * TISCI no-response semantics (TI_SCI_FLAG_REQ_GENERIC_NORESPONSE): u-boot's
 * R5 shutdown path sends WAIT_PROC_BOOT_STATUS (0xc401) and SET_DEVICE
 * (0x0200) with hdr.flags = 0, i.e. without TISCI_MSG_FLAG_AOP. A real DMSC
 * pushes no reply at all in that case; a response would strand a stale
 * message in the single-slot RX thread and can corrupt the pairing of the
 * next request/response. Verify the RX thread's message count stays at 0
 * for both messages, then sanity-check that a normal AOP-flagged message
 * still gets its response.
 */
static void test_dmsc_r5_no_response_flag(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /*
     * Consume the boot notification pre-queued on thread 0 at reset: reading
     * the last data word (offset 0x3c, register 15) is what
     * ti_sec_proxy_read_target() uses to clear an inbound thread's message
     * count (see hw/misc/ti-sec-proxy.c).
     */
    if (qtest_readl(qts, SP_RT(0)) & 0xff) {
        qtest_readl(qts, SP_TARGET(0) + 0x3c);
    }

    /* WAIT_PROC_BOOT_STATUS (0xc401), hdr.flags = 0 -> NO response */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23c401);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    g_usleep(50 * 1000);
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, ==, 0);

    /* SET_DEVICE (0x0200) with flags = 0 -> NO response either */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a230200);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 121);   /* device id */
    qtest_writel(qts, SP_TARGET(1) + 0x14, 0);     /* state off */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    g_usleep(50 * 1000);
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, ==, 0);

    /* sanity: an AOP message still gets a response */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a230002);   /* VERSION */
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, >, 0);
    qtest_quit(qts);
}

static void test_dmtimer_counts(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t t0, t1;

    /* start: TCLR.ST (safe even if the model free-runs) */
    qtest_writel(qts, 0x02400038, 1);
    t0 = qtest_readl(qts, 0x0240003c);
    qtest_clock_step(qts, 1000000); /* +1 ms virtual time */
    t1 = qtest_readl(qts, 0x0240003c);
    /* 20 MHz -> 1 ms = 20000 ticks */
    g_assert_cmpuint(t1 - t0, ==, 20000);
    qtest_quit(qts);
}

static void test_dmtimer_prescaler(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t t0, t1;

    /*
     * prescaler test: PTV=2, PRE_EN, AR, ST
     * TCLR = (2<<2)|BIT(5)|BIT(1)|BIT(0) = 0x2b
     * effective rate = 20 MHz / (2 << 2) = 20 MHz / 8
     * 1 ms = 2500 ticks
     */
    qtest_writel(qts, 0x02400038, 0x2b);
    t0 = qtest_readl(qts, 0x0240003c);
    qtest_clock_step(qts, 1000000); /* +1 ms virtual time */
    t1 = qtest_readl(qts, 0x0240003c);
    g_assert_cmpuint(t1 - t0, ==, 2500);
    qtest_quit(qts);
}

static void test_dmtimer_reconfigure(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t t0, t1;

    /*
     * Changing the prescaler while the timer keeps running must only
     * affect time from the TCLR write onward, never retroactively
     * rescale already-elapsed ticks.
     */
    qtest_writel(qts, 0x02400038, 1);           /* ST, no prescaler */
    t0 = qtest_readl(qts, 0x0240003c);
    qtest_clock_step(qts, 1000000);             /* +1 ms @ 20 MHz  */
    qtest_writel(qts, 0x02400038, 0x2b);        /* PTV=2, PRE_EN, AR, ST */
    qtest_clock_step(qts, 1000000);             /* +1 ms @ 2.5 MHz */
    t1 = qtest_readl(qts, 0x0240003c);
    g_assert_cmpuint(t1 - t0, ==, 20000 + 2500);
    qtest_quit(qts);
}

#define GICD_BASE 0x01800000ULL
#define GICR_BASE 0x01840000ULL
#define GIC_PIDR2 0xffe8

static void test_gicv3_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* GICD_PIDR2.ArchRev must identify a GICv3 distributor */
    g_assert_cmphex((qtest_readl(qts, GICD_BASE + GIC_PIDR2) >> 4) & 0xf,
                    ==, 3);
    /* first redistributor frame at the real AM64x GICR base */
    g_assert_cmphex((qtest_readl(qts, GICR_BASE + GIC_PIDR2) >> 4) & 0xf,
                    ==, 3);
    qtest_quit(qts);
}

static void test_ddrss_stub(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* RAM-backed: config writes persist (DENALI_CTL_0, dram_class DDR4) */
    qtest_writel(qts, DDRSS_CFG_BASE + 0x0, 0x00000A00);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x0), ==, 0x00000A00);

    /* status offsets OR-in their done bits even after being overwritten */
    qtest_writel(qts, DDRSS_CFG_BASE + 0x214C, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x214C) & 0x1, ==, 0x1);
    qtest_writel(qts, DDRSS_CFG_BASE + 0x538, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x538) & (1u << 13),
                    ==, 1u << 13);
    qtest_writel(qts, DDRSS_CFG_BASE + 0x558, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x558) & (1u << 25),
                    ==, 1u << 25);

    /*
     * ECC-priming BIST_DONE interrupt: INT_STATUS_MASTER bit 8 (BIST
     * group) and INT_STATUS_BIST field bit 0 (raw bit 16 of CTL_341)
     * must both read back set even after being cleared, or the SPL
     * hangs forever in k3_lpddr4_bist_init_mem_region().
     */
    qtest_writel(qts, DDRSS_CFG_BASE + 0x538, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x538) & (1u << 8),
                    ==, 1u << 8);
    qtest_writel(qts, DDRSS_CFG_BASE + 0x554, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x554) & (1u << 16),
                    ==, 1u << 16);
    qtest_quit(qts);
}

/*
 * TISCI_MSG_SET_CONFIG (0xc100) capturing an A53 bootvector, followed by
 * PROC_GET_STATUS (0xc400) echoing it back.
 *
 * Request payload (struct ti_sci_msg_req_set_proc_boot_config, QEMU_PACKED,
 * unaligned): hdr(8) + processor_id(1) + bootvector_low(4) +
 * bootvector_high(4) + config_flags_set(4) + config_flags_clear(4).
 * bootvector_low = 0x701c0000 (OCSRAM, matches u-boot's a53 entry point)
 * as little-endian bytes 00 00 1c 70, landing at payload bytes 1..4 (right
 * after the 1-byte processor_id at payload byte 0):
 *   bytes 8..11  = 20 00 00 1c   (proc_id=0x20, bvlow[7:0..23:16]=00,00,1c)
 *   bytes 12..15 = 70 00 00 00   (bvlow[31:24]=0x70, bvhigh[23:0]=0)
 *   bytes 16..19 = 00 00 00 00   (bvhigh[31:24], cfg_set[23:0])
 *   bytes 20..23 = 00 00 00 00   (cfg_set[31:24] .. )
 * As little-endian 32-bit register writes that byte stream is
 * 0x1c000020 / 0x00000070 / 0 / 0.
 */
static void test_dmsc_r5_bootvector_capture(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t reg4, reg5, bootvector_lo;

    /* drain the boot notification pre-queued on thread 0 at reset */
    if (qtest_readl(qts, SP_RT(0)) & 0xff) {
        qtest_readl(qts, SP_TARGET(0) + 0x3c);
    }

    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);      /* sec hdr */
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23c100);      /* hdr */
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);      /* AOP */
    qtest_writel(qts, SP_TARGET(1) + 0x10, 0x1c000020);      /* see ^ */
    qtest_writel(qts, SP_TARGET(1) + 0x14, 0x00000070);
    qtest_writel(qts, SP_TARGET(1) + 0x18, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x1c, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    for (int i = 0; i < 100 && !(qtest_readl(qts, SP_RT(0)) & 0xff); i++) {
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    qtest_readl(qts, SP_TARGET(0) + 0x3c);                    /* drain */

    /* PROC_GET_STATUS (0xc400), proc 32: bootvector_low must echo */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23c400);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 32);               /* proc_id */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    for (int i = 0; i < 100 && !(qtest_readl(qts, SP_RT(0)) & 0xff); i++) {
        g_usleep(10 * 1000);
    }

    /*
     * struct TisciMsgProcGetStatusResp (ti-dmsc.h), QEMU_PACKED:
     *   hdr(8) + processor_id(1) + bootvector_lo(4) + bootvector_hi(4) + ...
     * Framing: ti_dmsc_client_respond() prepends a 4-byte zero word ahead
     * of the response for secure clients (see ti_sec_proxy_push_msg(),
     * which writes into current_message[1..]), so the response bytes land
     * on SP_TARGET registers as:
     *   +0x04 = secure zero prefix        (register 1)
     *   +0x08 = resp bytes 0-3: hdr type/host/seq   (register 2)
     *   +0x0c = resp bytes 4-7: hdr.flags           (register 3)
     *   +0x10 = resp bytes 8-11                     (register 4)
     *   +0x14 = resp bytes 12-15                    (register 5)
     * resp byte 8 = processor_id, so bootvector_lo (resp bytes 9..12) is
     * NOT register-aligned -- it straddles register 4 (bytes 9-11, i.e.
     * bootvector_lo[23:0]) and register 5 (byte 12, i.e. bootvector_lo
     * [31:24]). A single aligned 32-bit MMIO read cannot recover the packed
     * field directly (ti_sec_proxy_read_target()'s size==4 path returns
     * the whole register, ignoring any intra-register byte offset), so
     * reconstruct it from both registers:
     *   bootvector_lo = (reg4 >> 8) | ((reg5 & 0xff) << 24)
     */
    reg4 = qtest_readl(qts, SP_TARGET(0) + 0x10);
    reg5 = qtest_readl(qts, SP_TARGET(0) + 0x14);
    bootvector_lo = (reg4 >> 8) | ((reg5 & 0xff) << 24);
    g_assert_cmphex(bootvector_lo, ==, 0x701c0000);
    qtest_quit(qts);
}

#define SDHCI_SD_BASE   0x0fa00000ULL
#define SDHCI_EMMC_BASE 0x0fa10000ULL

static void test_sdhci_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* SDHC capabilities register (0x40) reflects our capareg */
    g_assert_cmphex(qtest_readl(qts, SDHCI_SD_BASE + 0x40), ==, 0x057c34b4);
    g_assert_cmphex(qtest_readl(qts, SDHCI_EMMC_BASE + 0x40), ==, 0x057c34b4);
    /* host controller version (0xFE, 16-bit): spec 3.00 = 0x0002 */
    g_assert_cmphex(qtest_readw(qts, SDHCI_SD_BASE + 0xFE) & 0xff, ==, 2);
    /* PHY window: PHY_STAT1 reads CALDONE|DLLRDY */
    g_assert_cmphex(qtest_readl(qts, 0x0fa08000ULL + 0x130) & 0x3, ==, 0x3);
    g_assert_cmphex(qtest_readl(qts, 0x0fa18000ULL + 0x130) & 0x3, ==, 0x3);
    qtest_quit(qts);
}

#define TRNG_BASE 0x40910000ULL

static void test_trng_stub(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* readiness bit permanently set */
    g_assert_cmphex(qtest_readl(qts, TRNG_BASE + 0x10) & 0x1, ==, 0x1);
    /* output words nonzero and changing between reads */
    uint32_t a = qtest_readl(qts, TRNG_BASE + 0x00);
    uint32_t b = qtest_readl(qts, TRNG_BASE + 0x00);

    g_assert_cmpuint(a, !=, 0);
    g_assert_true(a != b || qtest_readl(qts, TRNG_BASE + 0x04) != a);
    /* INTACK write is accepted (no crash / guest error) */
    qtest_writel(qts, TRNG_BASE + 0x10, 0x1);
    /* CONTROL is RAM-backed: read back what was written */
    qtest_writel(qts, TRNG_BASE + 0x14, 0x400);
    g_assert_cmphex(qtest_readl(qts, TRNG_BASE + 0x14), ==, 0x400);
    qtest_quit(qts);
}

/*
 * A53_0's TISCI transport (ATF/BL31's SP_HIGH_PRIORITY(9)/SP_RESPONSE(8)
 * thread pair, TF-A lts-v2.10.4 plat/ti/k3/common/drivers/sec_proxy/
 * sec_proxy.c) carries the same 4-byte {checksum=0,reserved=0} secure
 * prefix as the R5 SPL's thread pair, but was not marked as a "secure" DMSC
 * client -- ti_dmsc_handle_one() would misparse the header by one word and
 * ti_dmsc_client_respond() would omit the matching prefix on the response,
 * which is the root cause behind BL31's "Timeout waiting for thread
 * SP_RESPONSE to fill" / OP-TEE's "Queue is busy" (see hw/arm/ti-am64x.c
 * for the fix and full citation). This exercises A53_0's channel exactly
 * like test_dmsc_r5_version() exercises the R5's, just on threads 9/8
 * with host id TISCI_HOST_ID_A53_0 (10) instead of 1/0 with host 35.
 */
static void test_dmsc_a53_secure_version(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    if (qtest_readl(qts, SP_RT(8)) & 0xff) {
        qtest_readl(qts, SP_TARGET(8) + 0x3c);
    }

    /*
     * Secure-host TISCI VERSION request as ATF's k3_sec_proxy_send() sends
     * it on its TX (write) thread:
     * word0 = secure header {u16 checksum=0; u16 reserved=0}
     * word1 = {u16 type=0x0002; u8 host=10 (TISCI_HOST_ID_A53_0); u8 seq}
     * word2 = flags = TISCI_MSG_FLAG_AOP (0x2)
     */
    qtest_writel(qts, SP_TARGET(9) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(9) + 0x08, 0x010a0002);
    qtest_writel(qts, SP_TARGET(9) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(9) + 0x3c, 0x00000000);

    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(8)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_RT(8)) & 0xff, >, 0);

    /* type echoed back correctly (0x0002) and the ACK bit is set */
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(8) + 0x08) & 0xffff,
                    ==, 0x0002);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(8) + 0x0c) & 0x2, ==, 0x2);
    qtest_quit(qts);
}

/*
 * Brief Step 4: send TISCI_MSG_FWL_SET (0x9000) with AOP via the R5's
 * secure thread (1) and confirm the ACK bit is set in the response --
 * without the bare-ACK handler this falls through to the unknown-message
 * NAK path in ti_dmsc_handle_one().
 */
static void test_dmsc_fwl_set_ack(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    if (qtest_readl(qts, SP_RT(0)) & 0xff) {
        qtest_readl(qts, SP_TARGET(0) + 0x3c);
    }

    /*
     * word0 = secure header (0)
     * word1 = {type=0x9000; host=35 (MAIN_0_R5_0); seq=0x0b}
     * word2 = flags = AOP
     * word3 = fwl_id=0x23, region=3
     * word4 = n_permission_regs = 1
     * remaining words (control/permissions/start/end): left 0
     */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0b239000);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 0x00030023);
    qtest_writel(qts, SP_TARGET(1) + 0x14, 0x00000001);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);

    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, >, 0);

    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x9000);
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
    qtest_add_func("/am64-virt/dmsc-r5-get-freq", test_dmsc_r5_get_freq);
    qtest_add_func("/am64-virt/dmsc-no-response",
                   test_dmsc_r5_no_response_flag);
    qtest_add_func("/am64-virt/dmsc-bootvector",
                   test_dmsc_r5_bootvector_capture);
    qtest_add_func("/am64-virt/dmtimer", test_dmtimer_counts);
    qtest_add_func("/am64-virt/dmtimer-prescaler", test_dmtimer_prescaler);
    qtest_add_func("/am64-virt/dmtimer-reconfigure", test_dmtimer_reconfigure);
    qtest_add_func("/am64-virt/gicv3", test_gicv3_present);
    qtest_add_func("/am64-virt/ddrss-stub", test_ddrss_stub);
    qtest_add_func("/am64-virt/sdhci", test_sdhci_present);
    qtest_add_func("/am64-virt/trng", test_trng_stub);
    qtest_add_func("/am64-virt/dmsc-a53-secure-version",
                   test_dmsc_a53_secure_version);
    qtest_add_func("/am64-virt/dmsc-fwl-set", test_dmsc_fwl_set_ack);
    return g_test_run();
}
