# AM64x DDRSS Stub + SDHCI Implementation Plan (Full-Boot-Chain Phase 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The R5 SPL passes DRAM init and loads `tispl.bin` from the FAT boot
partition of a real FluxOS WIC image attached as an SD card — ending phase 2
at the "Starting ATF on ARM64 core..." handoff attempt.

**Architecture:** Three small additions. (1) A RAM-backed DDRSS register-file
stub whose three status offsets OR-in "done" bits on read — that alone turns
`DRAM init failed: -22` into a successful DDR4 init (the board's config data
writes 0xA00 into CTL_0; the driver reads it back and polls three interrupt
bits). (2) Two `TYPE_SYSBUS_SDHCI` controllers at the real MMCSD addresses
plus tiny PHY-window stubs (CALDONE|DLLRDY hardwired), fed by `-drive if=sd`;
the CTRL_MMR DEVSTAT flips to SD-boot (`0x240`) when a card is present in
ROM-boot mode. (3) Two DMSC gap-fills (TISCI GET_FREQ `0x010e`,
PROC_SET_CONFIG `0xc100`) so the SPL's clock query and the rproc load path
get ACKs instead of NAKs.

**Tech Stack:** QEMU 10.2.50 fork, C, meson/ninja, qtest, tests/functional.

**Spec:** `docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md` (Phase 2).

## Global Constraints

- Repo `/home/sefo/devel/git/qemu`, branch `feat/am64-tiboot3-bootrom`. **Never push; never pull.** Commit per task.
- Commit style `type(scope): lowercase subject` ≤72 chars, no AI trailers; checkpatch via `git format-patch -1 --stdout | ./scripts/checkpatch.pl --no-signoff -`.
- Build dir `build/` (--disable-werror covers pre-existing fork warnings only; new code warning-clean). meson = `build/pyvenv/bin/meson`.
- New QOM type names use dash style (`"ti-k3-..."`), device conventions per `hw/misc/ti-k3-ctrlmmr.c` (Property array without end-of-list, class_init `(ObjectClass*, const void*)`, `device_class_set_legacy_reset`).
- Regression net after every task: qtests (9 → grows), unit 6/6, functional `func-aarch64-am64_bootrom` with `QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin`, smoke am64-virt + cmblu-corenode → timeout exit.
- **Spec deviation, decided at plan time:** the spec's phase-2 sketch said "default: sdhci0/eMMC" for the WIC. The WIC is an SD-card image (FAT boot partition + ext4 rootfs; eMMC boot would need boot0 hardware-partition content the WIC does not carry). Phase 2 therefore boots **SD FS-mode**: WIC → SD card on sdhci1, DEVSTAT `0x240` (bootmode MMC=0x8<<3, port bit9=SD), SPL loads `tispl.bin` from FAT partition 1. Record this in the spec in Task 4. eMMC-card modelling (TYPE_EMMC + boot partitions) is deferred.

## Verified reference facts (u-boot v2025.01-phy2 @ 6980061fa875; QEMU tree explored 2026-07-15 — do not re-derive)

| Fact | Value |
| --- | --- |
| `-22` root cause | `k3_ddrss_init_freq()`: reads `DENALI_CTL_0` (cfg+0x0) bits[11:8] = dram_class; RAZ → 0 → `default:` → -EINVAL + `"Unrecognized dram_class cannot init frequency!"` |
| Board DRAM | **DDR4** (`ti,ctl-data` word 0 = 0x00000A00 → class 0xA). LPDDR4 freq-handshake region (0x43014000) is never touched for DDR4 |
| DDRSS DT regions | cfg `0x0f308000` (declared 0x4000 but registers reach +0x55F8 — **model 0x8000**), ctrl_mmr_lp4 `0x43014000`/0x100 (inside existing ctrlmmr device — fine), ss_cfg `0x0f300000`/0x200 (write-only from the driver: V2A_CTL @+0x20, ECC_CTRL @+0x120 — existing unimp entry suffices) |
| Stub contract (cfg region) | RAM-backed read/write; on READ, OR-in: `+0x214C \|= BIT(0)` (PI_INT_STATUS init-done), `+0x538 \|= BIT(13)` (INT_STATUS_MASTER), `+0x558 \|= BIT(25)` (INT_STATUS_INIT). All config writes (423+345+1406 words) just stored; CTL_0's written 0xA00 must read back (RAM backing does that). No PHY training/mailbox exists in this driver |
| DDR sizing | from the DT memory node (2 GiB @0x80000000) — no MMIO; machine RAM already there. SPL moves stack/malloc to 0x82000000/0x84000000 after init — plain RAM, fine |
| Existing unimp entries to replace | `DDR16SS0_CTL_CFG 0x0F308000/0x8000` (line ~227 ti-am64x.c) → new stub. Keep `DDR16SS0_SS_CFG 0x0F300000/0x200`. `MMCSD1_CTL_CFG 0x0FA00000/0x1000` + `MMCSD0_CTL_CFG 0x0FA10000/0x1000` → SDHCI (+ remainder unimp), `MMCSD1_SS_CFG 0x0FA08000/0x400` + `MMCSD0_SS_CFG 0x0FA18000/0x400` → PHY stubs |
| SDHCI nodes | sdhci0 = **eMMC 8-bit** @`0x0fa10000` (+ SS/PHY @`0x0fa18000`), sdhci1 = **SD 4-bit** @`0x0fa00000` (+ SS/PHY @`0x0fa08000`); ctl window 0x260 regs of a 0x1000 window; PHY window 0x134 of 0x400 |
| am654 driver needs | second region RAM-backed for RMW; **PHY_STAT1 @+0x130 must read CALDONE\|DLLRDY = 0x3** (8-bit variant polls CALDONE with 20 µs timeout; DLL only engaged ≥50 MHz on eMMC — SD 4-bit variant never polls anything). `clk_xin` rate from TISCI (see GET_FREQ below); caps fallback needs nonzero base-clock field |
| QEMU capareg | start from default `0x057834b4` (has ADMA2/SDMA/HISPD/VDD330, base-clk 52 MHz) and OR `BIT(18)` (8-bit) → **`0x057c34b4`** for both instances; `sd-spec-version=3`. ADMA2 bit is mandatory (SPL uses ADMA) |
| Card attach pattern | machine code: `drive_get(IF_SD, 0, 0)` → `qdev_new(TYPE_SD_CARD)`, `qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(di), &error_fatal)`, realize onto `qdev_get_child_bus(DEVICE(&sdhci), "sd-bus")` (versal/aspeed pattern) |
| DEVSTAT for SD boot | **`0x240`** = bootmode MMC (0x8<<3) + PRIMARY_MMC_PORT bit9 → `BOOT_DEVICE_MMC2` → `MMCSD_MODE_FS`, mmc index 1 (= sdhci1/SD), file `tispl.bin`, FAT partition 1 |
| Console markers | `"Trying to boot from MMC2"` → (GP: `"Skipping authentication on GP device"`) → `"Starting ATF on ARM64 core..."`; then r5/common.c does TISCI proc_request → GTC writes → **proc_set_config (0xc100)** → power_domain_on → proc_release; with 0xc100 ACKed the R5 parks in WFE cleanly (no panic). A53 startup itself is phase 3 |
| DMSC gaps | `TI_SCI_MSG_GET_CLOCK_FREQ = 0x010e` (u-boot `clk_get_rate`) and `PROC_SET_CONFIG = 0xc100` are currently unhandled → NAK. Everything else the SPL needs (SET/GET_DEVICE incl. state array default-ON, SET/GET_CLOCK, SET_FREQ 0x010c, QUERY_FREQ 0x010d echo, PROC_REQUEST/RELEASE) already ACKs |
| WIC artifact | `cmblu-headless-image-am64xx-cmblu-core-node-1.rootfs.wic.xz` in the same yoctoklaus deploy dir as tiboot3 (non-k3r5 machine dir: `.../deploy-ti/images/am64xx-cmblu-core-node-1/`); boot partition contains tiboot3.bin, tispl.bin, u-boot.img, kernel, dtbs (IMAGE_BOOT_FILES, phyk3.inc) |

---

### Task 0: Baseline + WIC artifact

**Files:** none (fetch only).

**Interfaces:**
- Produces: green baseline; `build/fluxos.wic` (decompressed, NOT git-added; multi-GB sparse — check free disk first).

- [ ] **Step 1: Regression baseline**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
./tests/unit/test-k3-bootrom
QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin ./pyvenv/bin/meson test \
    --suite thorough func-aarch64-am64_bootrom --print-errorlogs
```
Expected: 9/9, 6/6, functional OK (3 subtests).

- [ ] **Step 2: Fetch + unpack the WIC**

```bash
df -h /home/sefo/devel/git/qemu/build | tail -1   # need ~8 GiB free
scp yoctoklaus-embedded:/home/cmbluadmin/devel/yocto/build_am64xx-cmblu-core-node-1/deploy-ti/images/am64xx-cmblu-core-node-1/cmblu-headless-image-am64xx-cmblu-core-node-1.rootfs.wic.xz /home/sefo/devel/git/qemu/build/
cd /home/sefo/devel/git/qemu/build && unxz -k cmblu-headless-image-am64xx-cmblu-core-node-1.rootfs.wic.xz && mv cmblu-headless-image-am64xx-cmblu-core-node-1.rootfs.wic fluxos.wic
fdisk -l fluxos.wic 2>/dev/null | tail -4
```
Expected: two partitions (vfat boot ~128 MiB @offset 8192 sectors, ext4 root).
Sanity: `mdir -i fluxos.wic@@$((8192*512)) ::` (if mtools present) lists
`tispl.bin`; if mtools is missing, skip the listing — the boot test proves it.
No commit.

---

### Task 1: DDRSS register-file stub → DRAM init passes

**Files:**
- Create: `hw/misc/ti-k3-ddrss.c`, `include/hw/misc/ti-k3-ddrss.h`
- Modify: `hw/misc/Kconfig`, `hw/misc/meson.build`, `hw/arm/Kconfig` (TI_AM64X selects TI_K3_DDRSS), `hw/arm/ti-am64x.c`, `include/hw/arm/ti-am64x.h`
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Produces: device `TYPE_TI_K3_DDRSS` (`"ti-k3-ddrss"`), 0x8000 RAM-backed region mapped at `0x0f308000`, read-ORed status bits at 0x214C/0x538/0x558; SoC member `TIK3DdrssState ddrss`. The `DDR16SS0_CTL_CFG` unimp entry is removed; `DDR16SS0_SS_CFG` stays.

- [ ] **Step 1: Write the failing qtest**

Add to `tests/qtest/am64-virt-test.c`:

```c
#define DDRSS_CFG_BASE 0x0f308000ULL

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
    qtest_quit(qts);
}
```

Register: `qtest_add_func("/am64-virt/ddrss-stub", test_ddrss_stub);`

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build && ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```
Expected: FAIL — the unimp region reads 0 (write doesn't persist).

- [ ] **Step 3: Implement the device**

`include/hw/misc/ti-k3-ddrss.h`:

```c
/*
 * TI K3 DDRSS register-file stub (AM64x)
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_DDRSS_H
#define HW_MISC_TI_K3_DDRSS_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_DDRSS "ti-k3-ddrss"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3DdrssState, TI_K3_DDRSS)

#define TI_K3_DDRSS_CFG_SIZE 0x8000

struct TIK3DdrssState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[TI_K3_DDRSS_CFG_SIZE / 4];
};

#endif
```

`hw/misc/ti-k3-ddrss.c`:

```c
/*
 * TI K3 DDRSS register-file stub (AM64x, DDR4 flavour)
 *
 * RAM-backed model of the DDRSS "cfg" register file (DENALI CTL/PI/PHY
 * blocks). u-boot's k3-ddrss driver bulk-writes the DT-provided config
 * (CTL_0 word 0xA00 = DDR4), reads DRAM_CLASS back, kicks the start
 * sequence and polls three "init done" interrupt bits. We store all
 * writes and OR the three status bits into reads so the sequence
 * completes immediately. No PHY training exists in this driver path.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/ti-k3-ddrss.h"

/* read-side OR masks: {offset, bits} */
static const struct {
    hwaddr offset;
    uint32_t bits;
} ddrss_status_bits[] = {
    { 0x214C, 1u << 0 },   /* DENALI_PI_83:  PI init done         */
    { 0x0538, 1u << 13 },  /* DENALI_CTL_334: INT_STATUS_MASTER   */
    { 0x0558, 1u << 25 },  /* DENALI_CTL_342: INT_STATUS_INIT bit1 */
};

static uint64_t ti_k3_ddrss_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3DdrssState *s = TI_K3_DDRSS(opaque);
    uint32_t val = s->regs[addr >> 2];

    for (size_t i = 0; i < ARRAY_SIZE(ddrss_status_bits); i++) {
        if (addr == ddrss_status_bits[i].offset) {
            val |= ddrss_status_bits[i].bits;
        }
    }
    return val;
}

static void ti_k3_ddrss_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    TIK3DdrssState *s = TI_K3_DDRSS(opaque);

    s->regs[addr >> 2] = val;
}

static const MemoryRegionOps ti_k3_ddrss_ops = {
    .read = ti_k3_ddrss_read,
    .write = ti_k3_ddrss_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ti_k3_ddrss_reset(DeviceState *dev)
{
    TIK3DdrssState *s = TI_K3_DDRSS(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void ti_k3_ddrss_init(Object *obj)
{
    TIK3DdrssState *s = TI_K3_DDRSS(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_ddrss_ops, s,
                          TYPE_TI_K3_DDRSS, TI_K3_DDRSS_CFG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void ti_k3_ddrss_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ti_k3_ddrss_reset);
}

static const TypeInfo ti_k3_ddrss_info = {
    .name = TYPE_TI_K3_DDRSS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3DdrssState),
    .instance_init = ti_k3_ddrss_init,
    .class_init = ti_k3_ddrss_class_init,
};

static void ti_k3_ddrss_register_types(void)
{
    type_register_static(&ti_k3_ddrss_info);
}

type_init(ti_k3_ddrss_register_types)
```

Wiring: `hw/misc/Kconfig` add `config TI_K3_DDRSS` (`bool`); `hw/misc/meson.build` add `system_ss.add(when: 'CONFIG_TI_K3_DDRSS', if_true: files('ti-k3-ddrss.c'))`; `hw/arm/Kconfig` `select TI_K3_DDRSS` in `TI_AM64X`. Header: `#include "hw/misc/ti-k3-ddrss.h"` + member `TIK3DdrssState ddrss;` in `TIAM64xState`. `ti-am64x.c`: initfn `object_initialize_child(obj, "ddrss", &s->ddrss, TYPE_TI_K3_DDRSS);`; realize (next to the ctrlmmr block):

```c
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddrss), errp)) {
        return;
    }
    memory_region_add_subregion(sysmem, 0x0f308000,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ddrss), 0));
```

Remove the `ADD_MAIN_UNIMP("DDR16SS0_CTL_CFG", 0x0F308000ULL, 0x00008000ULL)` entry (would overlap). Keep `DDR16SS0_SS_CFG`.

- [ ] **Step 4: Run qtest + real-image evidence**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
timeout 30 ./qemu-system-aarch64 -machine am64-virt -display none \
    -bios tiboot3.bin -serial stdio 2>&1 | tail -15
```
Expected: 10/10 qtests; the real-image run must NOT print
`Unrecognized dram_class` / `DRAM init failed` any more — the SPL proceeds
past DRAM init and now fails at the MMC boot stage (e.g.
`Trying to boot from MMC1`/`MMC Device 0 not found` — capture the exact
output in the report; that's Task 3's starting point).

- [ ] **Step 5: Full regression, checkpatch, commit**

```bash
QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin ./pyvenv/bin/meson test \
    --suite thorough func-aarch64-am64_bootrom --print-errorlogs
cd /home/sefo/devel/git/qemu
git add hw/misc/ti-k3-ddrss.c include/hw/misc/ti-k3-ddrss.h hw/misc/Kconfig \
        hw/misc/meson.build hw/arm/Kconfig hw/arm/ti-am64x.c \
        include/hw/arm/ti-am64x.h tests/qtest/am64-virt-test.c
git commit -m "feat(am64x): add ddrss stub so r5 spl dram init succeeds"
git format-patch -1 --stdout | ./scripts/checkpatch.pl --no-signoff -
```

---

### Task 2: DMSC gap-fill — GET_FREQ (0x010e) + PROC_SET_CONFIG (0xc100)

**Files:**
- Modify: `hw/misc/ti-dmsc.c`, `include/hw/misc/ti-dmsc.h`
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Consumes: existing handler table (`s->msg_handler[...]` registered in realize), `ti_dmsc_set_resp_flags()`, `ti_dmsc_client_respond()` (secure-aware), the existing QUERY_FREQ (0x010d) handler as the shape donor for GET_FREQ.
- Produces: `0x010e` returns a fixed sane frequency (200 MHz) in the same response layout as QUERY_FREQ; `0xc100` returns a bare ACK. u-boot `clk_get_rate(clk_xin)` and the rproc-load path stop getting NAKs.

- [ ] **Step 1: Read the donor handlers**

```bash
cd /home/sefo/devel/git/qemu
grep -n "0x010d\|QUERY_FREQ\|0x010e\|GET_CLOCK_FREQ\|0xc100\|PROC" include/hw/misc/ti-dmsc.h | head -20
sed -n '/ti_dmsc_handle_query_freq/,/^}/p' hw/misc/ti-dmsc.c
```
Confirm the QUERY_FREQ response struct (hdr + u64 freq_hz) and the exact
handler signature/registration pattern; confirm whether `0x010e`/`0xc100`
name constants exist in the header (add them if missing:
`TISCI_MSG_GET_CLOCK_FREQ 0x010e`, `TISCI_MSG_PROC_SET_CONFIG 0xc100`,
matching the header's naming convention).

- [ ] **Step 2: Write the failing qtest**

Mirror the existing `/am64-virt/dmsc-r5-version` secure-path test (same
SP_TARGET/SP_RT macros): send GET_FREQ for device 57 (MMCSD0) clock 1 via
thread 1, assert the response on thread 0 echoes type 0x010e with ACK and a
non-zero freq (read the two freq words at the response payload offset —
derive offsets from how the version test reads its payload):

```c
static void test_dmsc_r5_get_freq(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* secure hdr; TISCI hdr type=0x010e host=35 seq=0xa; flags=AOP;
     * payload: device=57 (u32), clk=1 (u8 + padding) */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23010e);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 57);
    qtest_writel(qts, SP_TARGET(1) + 0x14, 1);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);

    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x010e);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    /* freq_hz (u64) directly after the 8-byte TISCI hdr: != 0 */
    g_assert_cmpuint(qtest_readl(qts, SP_TARGET(0) + 0x10), !=, 0);
    qtest_quit(qts);
}
```

(Adjust the request payload layout to the actual `ti_sci_msg_req_get_clock_freq`
struct found in Step 1 — the u-boot struct is {hdr; u32 dev_id; u8 clk_id};
adjust response offsets analogously if the version test reads differently.)
Register: `qtest_add_func("/am64-virt/dmsc-r5-get-freq", test_dmsc_r5_get_freq);`

- [ ] **Step 3: Verify it fails** (NAK: flags word has no ACK bit → second assertion fails). Command as usual.

- [ ] **Step 4: Implement**

GET_FREQ handler — copy the QUERY_FREQ handler's shape; respond with a fixed
`200000000` Hz (comment: generic "unit clock" rate; the SPL only sanity-uses
it — DDR freqs come from DT, sdhci divides down from it):

```c
static void ti_dmsc_handle_get_clock_freq(/* match handler typedef */)
{
    /* same response struct as query_freq */
    resp.freq_hz = 200000000ULL;
    ...ti_dmsc_set_resp_flags(...); ti_dmsc_client_respond(...);
}
```

PROC_SET_CONFIG: bare-header ACK, exactly like the board-config handler
(`ti_dmsc_handle_board_config` is the donor). Register both in realize:

```c
    s->msg_handler[TISCI_MSG_GET_CLOCK_FREQ] = ti_dmsc_handle_get_clock_freq;
    s->msg_handler[TISCI_MSG_PROC_SET_CONFIG] = ti_dmsc_handle_board_config;
```

(If the handler table is indexed by full 16-bit type, 0xc100 just works —
verify the table size covers it; PROC_REQUEST 0xc000 is already registered,
so it does.)

- [ ] **Step 5: Test green + regression + commit**

qtest 11/11; functional + smoke as usual. Commit:
`feat(dmsc): handle get_clock_freq and proc_set_config`.

---

### Task 3: SDHCI controllers + PHY stubs + SD-card plumbing

**Files:**
- Create: `hw/misc/ti-k3-sdhci-phy.c`, `include/hw/misc/ti-k3-sdhci-phy.h`
- Modify: `hw/misc/Kconfig`, `hw/misc/meson.build`, `hw/arm/Kconfig` (TI_AM64X selects TI_K3_SDHCI_PHY + SDHCI)
- Modify: `hw/arm/ti-am64x.c`, `include/hw/arm/ti-am64x.h`, `hw/arm/am64-virt.c`
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Consumes: `TYPE_SYSBUS_SDHCI` (hw/sd/sdhci.h, state `SDHCIState`), versal/aspeed card-plug pattern, ctrlmmr `devstat` qdev prop (0x48 default from phase 0).
- Produces: SoC members `SDHCIState sdhci[2]` (index 0 = eMMC ctl @`0x0fa10000`, index 1 = SD ctl @`0x0fa00000`) with `sd-spec-version=3`, `capareg=0x057c34b4`; PHY stubs `TIK3SdhciPhyState sdhci_phy[2]` @`0x0fa18000`/`0x0fa08000`; machine plugs `drive_get(IF_SD, 0, 0)` as TYPE_SD_CARD onto sdhci[1] and, in ROM-boot mode with a card present, sets ctrlmmr `devstat=0x240`.

- [ ] **Step 1: Write the failing qtest**

```c
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
```

Register: `qtest_add_func("/am64-virt/sdhci", test_sdhci_present);`
Run → FAIL (unimp regions read 0).

- [ ] **Step 2: PHY stub device**

`ti-k3-sdhci-phy.[ch]` — exactly the ti-k3-ddrss shape (RAM-backed 0x400,
4-byte accesses, reset zeroes) with ONE read-OR entry: `+0x130 |= 0x3`
(`PHY_STAT1`: CALDONE bit1 | DLLRDY bit0). Reuse the ddrss code as template,
type `"ti-k3-sdhci-phy"`, `TI_K3_SDHCI_PHY_SIZE 0x400`, header comment
naming the am654 driver contract. Kconfig `TI_K3_SDHCI_PHY`; meson entry;
`select` in TI_AM64X (also add `select SDHCI` there for the controllers —
check the exact Kconfig symbol: `grep -n "config SDHCI" hw/sd/Kconfig`).

- [ ] **Step 3: SoC wiring**

Header: `#include "hw/sd/sdhci.h"`, `#include "hw/misc/ti-k3-sdhci-phy.h"`;
members:

```c
    SDHCIState sdhci[2];
    TIK3SdhciPhyState sdhci_phy[2];
```

`ti-am64x.c` initfn:

```c
    object_initialize_child(obj, "sdhci[*]", &s->sdhci[0],
                            TYPE_SYSBUS_SDHCI);
    object_initialize_child(obj, "sdhci[*]", &s->sdhci[1],
                            TYPE_SYSBUS_SDHCI);
    object_initialize_child(obj, "sdhci-phy[*]", &s->sdhci_phy[0],
                            TYPE_TI_K3_SDHCI_PHY);
    object_initialize_child(obj, "sdhci-phy[*]", &s->sdhci_phy[1],
                            TYPE_TI_K3_SDHCI_PHY);
```

realize (verify the sdhci interrupt SPI numbers first:
`grep -B2 -A8 "sdhci0\|sdhci1" /tmp/claude-1000/-home-sefo-devel-git-neoflux/60fe955a-7832-4f61-b6be-bf3c7aa65d03/scratchpad/k3-am64-main.dtsi | grep -A1 interrupts`
— use the GIC_SPI numbers found there; if the scratchpad file is gone,
re-fetch per the plan's u-boot rev):

```c
    static const struct {
        hwaddr ctl;
        hwaddr phy;
        int irq;    /* GIC SPI, from k3-am64-main.dtsi */
    } sdhci_cfg[] = {
        { 0x0fa10000, 0x0fa18000, /* sdhci0 SPI */ },
        { 0x0fa00000, 0x0fa08000, /* sdhci1 SPI */ },
    };

    for (int i = 0; i < 2; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->sdhci[i]);

        object_property_set_uint(OBJECT(&s->sdhci[i]), "sd-spec-version",
                                 3, &error_abort);
        object_property_set_uint(OBJECT(&s->sdhci[i]), "capareg",
                                 0x057c34b4, &error_abort);
        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        memory_region_add_subregion(sysmem, sdhci_cfg[i].ctl,
                                    sysbus_mmio_get_region(sbd, 0));
        sysbus_connect_irq(sbd, 0,
                           qdev_get_gpio_in(DEVICE(&s->gic),
                                            sdhci_cfg[i].irq));

        sbd = SYS_BUS_DEVICE(&s->sdhci_phy[i]);
        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        memory_region_add_subregion(sysmem, sdhci_cfg[i].phy,
                                    sysbus_mmio_get_region(sbd, 0));
    }
```

Unimp table: remove `MMCSD1_CTL_CFG`, `MMCSD0_CTL_CFG`, `MMCSD1_SS_CFG`,
`MMCSD0_SS_CFG`; add remainder coverage for the vendor tail of each ctl
window:

```c
    ADD_MAIN_UNIMP("MMCSD1_CTL_VENDOR", 0x0FA00100ULL, 0x00000F00ULL);
    ADD_MAIN_UNIMP("MMCSD0_CTL_VENDOR", 0x0FA10100ULL, 0x00000F00ULL);
```

- [ ] **Step 4: Machine plumbing (am64-virt.c)**

Add includes `hw/sd/sd.h` (TYPE_SD_CARD), `system/blockdev.h` (drive_get —
verify path: `grep -rn "drive_get" hw/arm/xlnx-versal-virt.c | head -2` and
mimic includes). In `am64_virt_init`, BEFORE SoC realize (devstat prop) and
AFTER SoC realize (card plug):

```c
    DriveInfo *sd_di = drive_get(IF_SD, 0, 0);

    /* pre-realize block, inside the existing ROM-boot section: */
    if (machine->firmware && sd_di) {
        /* boot straps: primary bootmode = MMC (0x8), port = SD */
        qdev_prop_set_uint32(DEVICE(&TI_AM64X(soc)->ctrlmmr), "devstat",
                             0x240);
    }

    /* post-realize, after k3_bootrom_load/arm_load_kernel dispatch: */
    if (sd_di) {
        DeviceState *card = qdev_new(TYPE_SD_CARD);

        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(sd_di),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(
                                   DEVICE(&ams->soc->sdhci[1]), "sd-bus"),
                               &error_fatal);
    }
```

- [ ] **Step 5: qtest green + boot evidence**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
timeout 60 ./qemu-system-aarch64 -machine am64-virt -display none \
    -bios tiboot3.bin -serial stdio \
    -drive if=sd,file=fluxos.wic,format=raw 2>&1 | tail -20
```
Expected: 12/12 qtests; the boot run shows `Trying to boot from MMC2` and —
with Task 2's ACKs — `Starting ATF on ARM64 core...` with NO panic after it
(R5 parks in WFE). Capture the full output in the report. If it stalls
between the two markers, diagnose with `-d unimp,guest_errors` and
`--trace 'sdhci*'` — likely candidates: capareg bits, missing PHY offsets
(extend the stub's OR table), TISCI clock values.

- [ ] **Step 6: Full regression + commit**

All suites incl. `QEMU_TEST_TIBOOT3` functional (still passes — no drive
attached there, devstat stays 0x48, SPL fails at MMC exactly as in Task 1 —
the test only waits for banner + SYSFW ABI). Commit:
`feat(am64x): add sdhci controllers and sd card boot path`.

---

### Task 4: Gated WIC functional test + spec as-built

**Files:**
- Modify: `tests/functional/aarch64/test_am64_bootrom.py`
- Modify: `docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md`

**Interfaces:**
- Consumes: everything above; env var gate `QEMU_TEST_WIC` (path to fluxos.wic).
- Produces: phase-2 acceptance as a repeatable test; spec updated.

- [ ] **Step 1: Add the gated test**

```python
    @skipUnless(os.getenv('QEMU_TEST_TIBOOT3'), 'real tiboot3.bin needed')
    @skipUnless(os.getenv('QEMU_TEST_WIC'), 'FluxOS .wic image needed')
    def test_fluxos_spl_loads_tispl(self):
        self.set_machine('am64-virt')
        self.vm.set_console()
        self.vm.add_args('-bios', os.getenv('QEMU_TEST_TIBOOT3'),
                         '-drive',
                         'if=sd,format=raw,file=' + os.getenv('QEMU_TEST_WIC'))
        self.vm.launch()
        wait_for_console_pattern(self, 'U-Boot SPL')
        wait_for_console_pattern(self, 'Trying to boot from MMC2')
        wait_for_console_pattern(self, 'Starting ATF on ARM64 core')
```

(Mirror the file's existing import/skip conventions; `skipUnless` is already
imported there.)

- [ ] **Step 2: Run it**

```bash
cd /home/sefo/devel/git/qemu/build
QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin QEMU_TEST_WIC=$PWD/fluxos.wic \
  ./pyvenv/bin/meson test --suite thorough func-aarch64-am64_bootrom --print-errorlogs
```
Expected: all subtests OK including the new one.

- [ ] **Step 3: Spec update + final commit**

Spec Phase 2 section: mark `[IMPLEMENTED <date>]`, add an as-built note
(DDRSS stub contract incl. the three OR-bits and the DDR4 simplification;
SD-FS boot decision + DEVSTAT 0x240 rationale — the WIC carries no eMMC
boot0 content; DMSC 0x010e/0xc100 additions; where phase 2 ends: R5 in WFE
after "Starting ATF"). Resolve the Ph.-2 open item. Commits:
`test(am64x): gated functional test for spl tispl load from wic` and
`docs: record ddrss and sdhci as-built state`.

---

## Self-review notes (done at plan time)

- **Spec coverage (Phase 2):** DDRSS stub → Task 1 (with root cause pinned,
  not just stubbed blind); SDHCI + TI-PHY stubs + WIC via -drive → Task 3;
  "R5 SPL loads tispl.bin visible in console" milestone → Tasks 3/4;
  "qtests where practical" → Tasks 1/2/3. The spec's eMMC-default wiring is
  deviated from with rationale (Global Constraints) and gets recorded in
  the spec in Task 4. DMSC gap-fill wasn't in the spec's phase-2 sketch but
  is required by the milestone (clk_get_rate + clean end state) — it
  belongs to phase 2's "what triage demands" clause.
- **Type consistency:** `TIK3DdrssState ddrss` / `TIK3SdhciPhyState
  sdhci_phy[2]` / `SDHCIState sdhci[2]` used consistently across Tasks 1/3;
  qtest constants match the SoC mapping addresses.
- **Known uncertainty, by design:** sdhci GIC SPI numbers (verify step from
  dtsi), exact TISCI GET_FREQ struct field layout (Step-1 grep + donor
  handler), Task-3 stall diagnosis toolbox provided.
