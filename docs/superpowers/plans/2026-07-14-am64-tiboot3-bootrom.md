# AM64x tiboot3 ROM-Boot Emulation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `qemu-system-aarch64 -machine am64-virt -bios tiboot3.bin -serial stdio` boots the unmodified FluxOS `tiboot3.bin` on an emulated Cortex-R5F up to the `U-Boot SPL` banner on main-domain UART0.

**Architecture:** QEMU takes the mask-ROM (RBL) role: a new `k3-bootrom` loader parses the X.509 combined-image header at machine init, copies the R5-SPL segment into new OCSRAM, synthesizes the ROM boot-parameter words, and a reset hook starts a new Cortex-R5F core at the certified entry point. The existing `ti-dmsc.c` TISCI emulation keeps playing TIFS (extended with an R5 secure-transport client and board-config ACKs — all post-banner robustness).

**Tech Stack:** QEMU 10.2.50 fork (`wafgo/qemu`, branch `cmblu/corenode`), C, meson/ninja, qtest (C), tests/unit (C/glib), tests/functional (Python).

**Spec:** `docs/superpowers/specs/2026-07-14-am64-tiboot3-bootrom-design.md` (in this repo).

## Ground truth (verified against u-boot v2025.01-phy2 = `github.com/phytec/u-boot-phytec` @ `6980061fa875`, the exact source of the FluxOS tiboot3)

These constants are **facts, not guesses** — sources in parentheses. A working copy of the u-boot sources is at `/tmp/claude-1000/-home-sefo-devel-git-neoflux/60fe955a-7832-4f61-b6be-bf3c7aa65d03/scratchpad/uboot/` (re-fetch from the repo/rev above if gone).

| Fact | Value | Source |
| --- | --- | --- |
| R5 SPL text base / ROM jump target | `0x70000000` (OCSRAM) | `configs/phycore_am64x_r5_defconfig` `CONFIG_SPL_TEXT_BASE` |
| Boot-param word read by SPL | `0x701bebfc`, value `0x0` = `K3_PRIMARY_BOOTMODE` | `arch/arm/mach-k3/Kconfig` `SYS_K3_BOOT_PARAM_TABLE_INDEX`, `am64x/boot.c` |
| ROM extended boot data | `0x701beb00`: 8-byte `"EXTBOOT"` magic + `u32 num_components` (must be `> 1`) | `mach/am64_hardware.h` `ROM_EXTENDED_BOOT_DATA_INFO`, `common.c:is_rom_loaded_sysfw()` |
| SPL stack/BSS | `0x7019b800` (inside OCSRAM) | defconfig `CUSTOM_SYS_INIT_SP_ADDR` |
| DEVSTAT register | `0x43000030`; eMMC primary boot = bits[6:3]=0x9 → value `0x48` | `am64_hardware.h` `CTRLMMR_MAIN_DEVSTAT`, `am64x/boot.c` |
| ctrl_mmr_unlock writes | kick0 `0x68ef3490` @ `part_base+0x1008`, kick1 `0xd172bc5a` @ `+0x100c`; partition stride `0x4000`; bases `0x000f0000` (part 1), `0x04080000` (part 1), `0x04500000` (parts 0,1,2,3,4,6), `0x43000000` (parts 0,1,2,3,5,6) | `am642_init.c:ctrl_mmr_unlock`, `common.c:mmr_unlock` |
| Pre-banner `board_init_f` sequence | `setup_k3_mpu_regions` → `store_boot_info_from_rom` → `ctrl_mmr_unlock` → `spl_early_init` → `preloader_console_init` (**banner**). **No TISCI traffic, no PLL/PSC setup before the banner.** | `am64x/am642_init.c:168-191` |
| SPL console | MAIN UART0 @ `0x02800000`, ns16550-compatible, reg-shift 2, fclk 48 MHz, **no clock/PM setup needed** (R5 DTS deletes `clocks`/`power-domains`) | `k3-am64-main.dtsi`, `k3-am642-r5-phycore-som-2gb.dts` |
| udelay timer (post-banner) | `main_timer0` @ `0x02400000`, free-running 20 MHz, `ti,am654-timer`, u-boot `omap-timer` driver | dts + defconfig `CONFIG_OMAP_TIMER` |
| Sec-proxy (post-banner) | R5 TX = thread 1, RX = thread 0, host-id 35, **secure host** (every message prefixed with 4-byte `{u16 checksum; u16 reserved}`) | `k3-am642-r5-phycore-som-2gb.dts` `&dmsc`, `drivers/firmware/ti_sci.c` |
| First TISCI messages (all post-banner) | `0x0002 VERSION`, then board cfg `0x000B`, PM `0x000E`, RM `0x000C`, SEC `0x000D`. **`0x000A BOOT_NOTIFICATION` is never waited for** — do NOT queue it (deviation from spec §"DMSC extensions", confirmed against the ti_sci driver). | `drivers/firmware/ti_sci.h`, `r5/sysfw-loader.c` |
| X.509 cert extension | OID `1.3.6.1.4.1.294.1.9` = `ext_boot_info` SEQUENCE: `INTEGER extImgSize, INTEGER numComp`, then per component SEQUENCE `{INTEGER compType, INTEGER bootCore, INTEGER compOpts, OCTETSTRING destAddr(4B BE), INTEGER compSize, OID shaType, OCTETSTRING shaValue}`. compType: SBL=1, SYSFW=2, SYSFW-DATA=18. Payloads concatenated after the cert in component order. | `tools/binman/btool/openssl.py:x509_cert_rom_combined` |
| Combined-image destinations | SBL→`0x70000000` (load), SYSFW→`0x44000`, SYSFW-DATA→`0x7b000` (both DMSC-internal — **discard**) | `arch/arm/dts/k3-am64x-binman.dtsi` |
| TISCI msg header | `struct TISciMsgHdr {u16 type; u8 host; u8 seq; u32 flags}` packed; ACK logic via `ti_dmsc_set_resp_flags()` | `include/hw/misc/ti-dmsc.h:335`, `hw/misc/ti-dmsc.c:648` |

## Global Constraints

- Repo: `/home/sefo/devel/git/qemu`, work on branch `feat/am64-tiboot3-bootrom` off `cmblu/corenode`. **Never push; never pull.** Commit locally per task.
- Commit style (matches fork history): `feat(am64x): lowercase subject`, ≤72 chars, no AI co-author trailers.
- New C files: SPDX `GPL-2.0-or-later`, copyright `Wadim Mueller <wadim.mueller@cmblu.de>`.
- Style check before every commit: `cd /home/sefo/devel/git/qemu && ./scripts/checkpatch.pl --no-signoff -g HEAD` (fix errors; warnings about existing patterns may stay).
- Build dir: `/home/sefo/devel/git/qemu/build` (Task 0 creates it). All `ninja`/`meson test` commands run from there.
- Includes in this fork are the QEMU 11-dev layout: `"system/address-spaces.h"`, `"system/memory.h"` etc. (NOT `"exec/..."` / `"sysemu/..."`). When an include fails to resolve, grep an existing fork file (`hw/arm/am64-virt.c`) for the correct path.
- The R5F core sees `get_system_memory()` directly (no TCM modelling — the SPL links/runs entirely in OCSRAM; spec §out-of-scope).

---

### Task 0: Build baseline + branch

**Files:** none (build setup)

**Interfaces:**
- Produces: configured `build/` dir; branch `feat/am64-tiboot3-bootrom`; verified baseline (`am64-virt` machine exists, tests runnable).

- [ ] **Step 1: Create branch**

```bash
cd /home/sefo/devel/git/qemu
git checkout -b feat/am64-tiboot3-bootrom cmblu/corenode
```

- [ ] **Step 2: Configure and build**

```bash
cd /home/sefo/devel/git/qemu
mkdir -p build && cd build
../configure --target-list=aarch64-softmmu
ninja
```

Expected: build completes; `./qemu-system-aarch64 -machine help | grep am64` shows `am64-virt`.

- [ ] **Step 3: Baseline smoke test**

```bash
cd /home/sefo/devel/git/qemu/build
timeout 10 ./qemu-system-aarch64 -machine am64-virt -display none -serial null; echo "exit: $?"
```

Expected: runs until timeout kills it (exit 124) without error output — machine instantiates cleanly.

No commit (nothing changed).

---

### Task 1: OCSRAM + boot-window MMIO coverage + qtest scaffold

**Files:**
- Modify: `hw/arm/ti-am64x.c` (realize + unimplemented tables), `include/hw/arm/ti-am64x.h`
- Create: `tests/qtest/am64-virt-test.c`
- Modify: `tests/qtest/meson.build`

**Interfaces:**
- Produces: 2 MiB RAM `am64x.ocsram` at `0x70000000` in system memory; unimplemented-device coverage for `PADCFG_MMR` (`0x000f0000`, 64 KiB) and `CTRL_MMR0` (`0x43000000`, 128 KiB — replaced by a real stub in Task 6); qtest binary `am64-virt-test` that later tasks extend.

- [ ] **Step 1: Write the failing qtest**

`tests/qtest/am64-virt-test.c`:

```c
/*
 * QTests for the AM64 virt machine (cmblu/corenode fork)
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

#define OCSRAM_BASE 0x70000000ULL
#define OCSRAM_SIZE (2 * 1024 * 1024)

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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/am64-virt/ocsram", test_ocsram_rw);
    return g_test_run();
}
```

Register in `tests/qtest/meson.build`: find the `qtests_aarch64 = ...` list and add, following the existing conditional pattern used by entries there:

```meson
  (config_all_devices.has_key('CONFIG_AM64_VIRT') ? ['am64-virt-test'] : []) + \
```

(Confirm pattern with `grep -n "config_all_devices.has_key" tests/qtest/meson.build | head -5` and mimic exactly, including list concatenation style.)

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build
ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: FAIL — reads return 0, not the written values (no RAM at 0x70000000 yet).

- [ ] **Step 3: Add OCSRAM to the SoC**

`include/hw/arm/ti-am64x.h` — add to `struct TIAM64xState` after `mcu_root`:

```c
    MemoryRegion ocsram;
```

`hw/arm/ti-am64x.c` — in `ti_am64x_realize()`, after the existing MCU RAM regions are set up (near the `mcu.dram.sysmem` alias), add:

```c
    /* Main-domain on-chip SRAM (OCSRAM / MSRAM), R5 SPL runs from here */
    memory_region_init_ram(&s->ocsram, OBJECT(dev_soc), "am64x.ocsram",
                           2 * MiB, errp);
    memory_region_add_subregion(sysmem, 0x70000000, &s->ocsram);
```

(`dev_soc`/`sysmem` are the names in scope in this function; check surrounding code.)

- [ ] **Step 4: Verify no overlapping unimplemented regions, add missing boot windows**

```bash
cd /home/sefo/devel/git/qemu
grep -n "0x0700000\|0x70000000\|MSRAM\|OCSRAM" hw/arm/ti-am64x.c
grep -n "0x0000f0000\|0x000f0000\|PADCFG" hw/arm/ti-am64x.c
grep -n "0x043000000\|0x43000000\|CTRL_MMR" hw/arm/ti-am64x.c
```

- If an *active* (non-commented) `ADD_MAIN_UNIMP` overlaps `0x70000000`, remove it.
- If there is no coverage for main PADCFG (`0x000f0000`) or CTRL_MMR0 (`0x43000000`), add to `ti_am64_create_main_unimplemented()` (pinmux + MMR-unlock writes from the SPL must not fault — unassigned accesses abort on this machine):

```c
    ADD_MAIN_UNIMP("PADCFG_MMR",  0x0000f0000ULL, 0x00010000ULL);
    ADD_MAIN_UNIMP("CTRL_MMR0",   0x043000000ULL, 0x00020000ULL);
```

(Note the fork's 9-hex-digit literal convention. If entries already exist, skip.)

- [ ] **Step 5: Run test to verify it passes**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: PASS (`/am64-virt/ocsram OK`).

- [ ] **Step 6: Commit**

```bash
cd /home/sefo/devel/git/qemu
./scripts/checkpatch.pl --no-signoff -g HEAD~0 2>/dev/null || true
git add hw/arm/ti-am64x.c include/hw/arm/ti-am64x.h tests/qtest/am64-virt-test.c tests/qtest/meson.build
git commit -m "feat(am64x): add 2MiB ocsram and boot mmr windows"
```

---

### Task 2: Main-domain UART0 @ 0x02800000

**Files:**
- Modify: `hw/arm/ti-am64x.c`, `include/hw/arm/ti-am64x.h`
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Consumes: existing `ti_am64x_uart_realize(s, memory, au, addr, errp)` helper (`hw/arm/ti-am64x.c:712`), `AM64Uart` type.
- Produces: SoC child `main-uart0` (`AM64Uart main_uart0` in `TIAM64xState`), mapped at `0x02800000` in system memory, IRQ → GIC SPI 178. Board can set its chardev via `qdev_prop_set_chr(DEVICE(&soc->main_uart0), "chardev", ...)` **before SoC realize** (Task 5 does).

- [ ] **Step 1: Write the failing qtest**

Add to `tests/qtest/am64-virt-test.c` (16550 with reg-shift 2: LSR at offset `5 << 2`; an idle UART reads `0x60` = THRE|TEMT):

```c
#define MAIN_UART0_BASE 0x02800000ULL

static void test_main_uart0_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* LSR of an idle 16550: transmitter empty bits set */
    g_assert_cmphex(qtest_readl(qts, MAIN_UART0_BASE + (5 << 2)) & 0x60,
                    ==, 0x60);
    qtest_quit(qts);
}
```

and in `main()`:

```c
    qtest_add_func("/am64-virt/main-uart0", test_main_uart0_present);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build && ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: FAIL — read returns 0 (nothing mapped at 0x02800000).

- [ ] **Step 3: Add the UART instance**

`include/hw/arm/ti-am64x.h` — add to `struct TIAM64xState`:

```c
    AM64Uart main_uart0;
```

`hw/arm/ti-am64x.c`:
- In `ti_am64x_initfn()`, next to the `mcu-uart[*]` init, add:

```c
    object_initialize_child(obj, "main-uart0", &s->main_uart0,
                            TYPE_AM64_UART);
```

- In `ti_am64x_realize()`, after the MCU UART loop, add (GIC SPI 178 per `k3-am64-main.dtsi`):

```c
    /* Main-domain UART0 — R5 SPL early console */
    if (!ti_am64x_uart_realize(s, sysmem, &s->main_uart0, 0x02800000,
                               errp)) {
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->main_uart0), 0,
                       qdev_get_gpio_in(DEVICE(&s->gic), 178));
```

- Check for an overlapping active unimp entry: `grep -n "0x002800000\|MAIN_UART0" hw/arm/ti-am64x.c` — remove it if present (line ~188 has a commented-out one; leave comments alone).

- [ ] **Step 4: Run test to verify it passes**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: PASS (both tests).

- [ ] **Step 5: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add hw/arm/ti-am64x.c include/hw/arm/ti-am64x.h tests/qtest/am64-virt-test.c
git commit -m "feat(am64x): add main-domain uart0 at 0x02800000"
```

---

### Task 3: Cortex-R5F boot core + DMSC client wiring

**Files:**
- Modify: `hw/arm/ti-am64x.c`, `include/hw/arm/ti-am64x.h`
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Consumes: cluster/CPU pattern from `hw/arm/xlnx-zynqmp.c:xlnx_zynqmp_create_rpu` (upstream, same tree); sec-proxy enums `MAIN_0_R5_0_WRITE_THREAD_ID`(=1) / `MAIN_0_R5_0_READ_RESPONSE_THREAD_ID`(=0) from `include/hw/misc/ti-sec-proxy.h:22-23`; DMSC `rx-threads`/`tx-threads` array props (wired at `hw/arm/ti-am64x.c:909-923`).
- Produces: `ARMCPU r5[1]` + `CPUClusterState r5_cluster` in `TIAM64xState`; SoC bool prop `r5-start-powered-off` (default **true**); R5F0_0 visible as 4th CPU (`cpu_index = a53_cpus + 1`); DMSC listens on sec-proxy thread 1 and answers on thread 0 (needed post-banner; secure-header handling comes in Task 7).

- [ ] **Step 1: Write the failing qtest**

Add to `tests/qtest/am64-virt-test.c`:

```c
#include "qobject/qdict.h"
#include "qobject/qlist.h"
```

(Verify include paths: `grep -rn "qobject/qdict.h\|qapi/qmp/qdict.h" tests/qtest/*.c | head -3` and use whatever this tree uses.)

```c
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
```

Register: `qtest_add_func("/am64-virt/r5f-present", test_r5f_cpu_present);`

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build && ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: FAIL — 3 CPUs, not 4.

- [ ] **Step 3: Add the R5F cluster**

`include/hw/arm/ti-am64x.h`:

```c
#define TI_AM64X_R5_NUM 1
```

and in `struct TIAM64xState` (near the M4 members):

```c
    CPUClusterState r5_cluster;
    ARMCPU r5[TI_AM64X_R5_NUM];
    bool r5_start_powered_off;
```

`hw/arm/ti-am64x.c` — in `ti_am64x_initfn()` (after the M4 cluster init; cluster-ids 0 and 1 are taken):

```c
    object_initialize_child(obj, "r5-cluster", &s->r5_cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->r5_cluster), "cluster-id", 2);
    object_initialize_child(OBJECT(&s->r5_cluster), "r5-cpu[*]", &s->r5[0],
                            ARM_CPU_TYPE_NAME("cortex-r5f"));
```

In `ti_am64x_realize()`, after the M4/armv7m block (which sets the M4's `cpu_index = s->a53_cpus`):

```c
    /* Cortex-R5F0_0 — the AM64x boot core (runs tiboot3 / R5 SPL) */
    object_property_set_bool(OBJECT(&s->r5[0]), "start-powered-off",
                             s->r5_start_powered_off, &error_abort);
    /* SPL vectors live low (image at 0x70000000), not hivecs */
    object_property_set_bool(OBJECT(&s->r5[0]), "reset-hivecs", false,
                             &error_abort);
    if (!qdev_realize(DEVICE(&s->r5[0]), NULL, errp)) {
        return;
    }
    CPU(&s->r5[0])->cpu_index = s->a53_cpus + 1;
    qdev_realize(DEVICE(&s->r5_cluster), NULL, &error_abort);
```

Property table (`ti_am64x_properties`):

```c
    DEFINE_PROP_BOOL("r5-start-powered-off", TIAM64xState,
                     r5_start_powered_off, true),
```

- [ ] **Step 4: Wire the R5 client into the DMSC thread lists**

In `ti_am64x_realize()` at the QList block (`hw/arm/ti-am64x.c:909-923`), after the M4 pair:

```c
    /* R5F0_0 (R5 SPL, secure host 35): writes on thread 1, reads on 0 */
    qlist_append_int(rx_threads, MAIN_0_R5_0_WRITE_THREAD_ID);
    qlist_append_int(tx_threads, MAIN_0_R5_0_READ_RESPONSE_THREAD_ID);
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: PASS (3 tests). Also re-run the Task 0 smoke test — machine must still boot with the R5 powered off by default.

- [ ] **Step 6: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add hw/arm/ti-am64x.c include/hw/arm/ti-am64x.h tests/qtest/am64-virt-test.c
git commit -m "feat(am64x): add cortex-r5f boot core and dmsc r5 client"
```

---

### Task 4: k3-bootrom X.509 combined-image parser (+ unit test)

**Files:**
- Create: `hw/arm/k3-bootrom-parse.c` (pure — parseable without QOM/sysbus so the unit test can link it)
- Create: `include/hw/arm/k3-bootrom.h`
- Create: `tests/unit/test-k3-bootrom.c`
- Modify: `tests/unit/meson.build`, `hw/arm/meson.build`, `hw/arm/Kconfig`

**Interfaces:**
- Produces:

```c
#define K3_BOOTROM_MAX_COMPS 8
#define K3_COMP_TYPE_SBL 1
#define K3_COMP_TYPE_SYSFW 2
#define K3_COMP_TYPE_SYSFW_DATA 18

typedef struct K3BootComponent {
    uint32_t comp_type;
    uint32_t boot_core;
    uint32_t comp_opts;
    uint64_t dest_addr;
    uint32_t comp_size;
    size_t payload_offset;   /* offset of this component's payload in the image file */
} K3BootComponent;

typedef struct K3BootImage {
    uint32_t num_comps;
    uint64_t ext_img_size;
    size_t cert_len;
    K3BootComponent comps[K3_BOOTROM_MAX_COMPS];
} K3BootImage;

bool k3_bootrom_parse(const uint8_t *buf, size_t len, K3BootImage *out,
                      Error **errp);
```

(Task 5 consumes exactly these names.)

- [ ] **Step 1: Create the header**

`include/hw/arm/k3-bootrom.h`:

```c
/*
 * TI K3 boot-ROM (RBL) emulation — X.509 combined boot image loading
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_ARM_K3_BOOTROM_H
#define HW_ARM_K3_BOOTROM_H

#include "qapi/error.h"

#define K3_BOOTROM_MAX_COMPS 8

/* comp_type values from TI's combined-image certificate */
#define K3_COMP_TYPE_SBL        1
#define K3_COMP_TYPE_SYSFW      2
#define K3_COMP_TYPE_SYSFW_DATA 18

typedef struct K3BootComponent {
    uint32_t comp_type;
    uint32_t boot_core;
    uint32_t comp_opts;
    uint64_t dest_addr;
    uint32_t comp_size;
    size_t payload_offset;
} K3BootComponent;

typedef struct K3BootImage {
    uint32_t num_comps;
    uint64_t ext_img_size;
    size_t cert_len;
    K3BootComponent comps[K3_BOOTROM_MAX_COMPS];
} K3BootImage;

bool k3_bootrom_parse(const uint8_t *buf, size_t len, K3BootImage *out,
                      Error **errp);

typedef struct TIAM64xState TIAM64xState;
void k3_bootrom_load(TIAM64xState *soc, const char *filename, Error **errp);

#endif
```

(`k3_bootrom_load` is implemented in Task 5; declaring it now keeps the header stable.)

- [ ] **Step 2: Write the failing unit test**

`tests/unit/test-k3-bootrom.c` — builds a synthetic combined image with local DER emitters (mirrors u-boot's `openssl.py` template shape):

```c
/*
 * Unit tests for the K3 boot-ROM combined-image parser
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/k3-bootrom.h"

/* --- minimal DER emitters ------------------------------------------- */

static GByteArray *der_tlv(uint8_t tag, const uint8_t *data, size_t len)
{
    GByteArray *a = g_byte_array_new();

    g_byte_array_append(a, &tag, 1);
    if (len < 0x80) {
        uint8_t l = len;
        g_byte_array_append(a, &l, 1);
    } else if (len <= 0xffff) {
        uint8_t l[3] = { 0x82, len >> 8, len & 0xff };
        g_byte_array_append(a, l, 3);
    } else {
        g_assert_not_reached();
    }
    if (data) {
        g_byte_array_append(a, data, len);
    }
    return a;
}

static GByteArray *der_wrap(uint8_t tag, GByteArray *inner)
{
    GByteArray *a = der_tlv(tag, inner->data, inner->len);
    g_byte_array_unref(inner);
    return a;
}

static void der_append(GByteArray *dst, GByteArray *src)
{
    g_byte_array_append(dst, src->data, src->len);
    g_byte_array_unref(src);
}

static GByteArray *der_uint(uint64_t v)
{
    uint8_t buf[9];
    int n = 0;
    uint64_t t = v;

    do {
        n++;
        t >>= 8;
    } while (t);
    if (v >> (n * 8 - 1) & 1) {
        n++; /* leading zero to keep it positive */
    }
    for (int i = 0; i < n; i++) {
        buf[i] = v >> ((n - 1 - i) * 8);
    }
    return der_tlv(0x02, buf, n);
}

/* OID 1.3.6.1.4.1.294.1.9 (ext_boot_info), pre-encoded TLV */
static const uint8_t ext_boot_oid[] = {
    0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x26, 0x01, 0x09
};
/* OID 2.16.840.1.101.3.4.2.3 (sha512), pre-encoded TLV */
static const uint8_t sha512_oid[] = {
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03
};

static GByteArray *der_component(uint32_t ctype, uint32_t core,
                                 uint32_t opts, uint32_t dest, uint32_t size)
{
    GByteArray *seq = g_byte_array_new();
    uint8_t dest_be[4] = { dest >> 24, dest >> 16, dest >> 8, dest };
    uint8_t sha[64] = { 0 };

    der_append(seq, der_uint(ctype));
    der_append(seq, der_uint(core));
    der_append(seq, der_uint(opts));
    der_append(seq, der_tlv(0x04, dest_be, sizeof(dest_be)));
    der_append(seq, der_uint(size));
    g_byte_array_append(seq, sha512_oid, sizeof(sha512_oid));
    der_append(seq, der_tlv(0x04, sha, sizeof(sha)));
    return der_wrap(0x30, seq);
}

/*
 * Layout mirrors u-boot tools/binman/btool/openssl.py
 * x509_cert_rom_combined(): SBL, SYSFW, SYSFW-DATA payloads concatenated
 * after the certificate.
 */
static GByteArray *make_image(const uint8_t *sbl, size_t sbl_len)
{
    static const uint8_t sysfw_blob[16] = "SYSFW-payload";
    static const uint8_t cfg_blob[8] = "BCFG";
    GByteArray *info = g_byte_array_new();
    GByteArray *ext, *cert, *img;

    der_append(info, der_uint(sbl_len + sizeof(sysfw_blob)
                              + sizeof(cfg_blob)));      /* extImgSize */
    der_append(info, der_uint(3));                       /* numComp */
    der_append(info, der_component(K3_COMP_TYPE_SBL, 16, 0,
                                   0x70000000, sbl_len));
    der_append(info, der_component(K3_COMP_TYPE_SYSFW, 0, 0,
                                   0x44000, sizeof(sysfw_blob)));
    der_append(info, der_component(K3_COMP_TYPE_SYSFW_DATA, 0, 0,
                                   0x7b000, sizeof(cfg_blob)));
    info = der_wrap(0x30, info);

    /* extension = SEQ { OID, OCTETSTRING { info } } */
    ext = g_byte_array_new();
    g_byte_array_append(ext, ext_boot_oid, sizeof(ext_boot_oid));
    der_append(ext, der_wrap(0x04, info));
    ext = der_wrap(0x30, ext);

    /* fake cert: top-level SEQUENCE wrapping the extension */
    cert = der_wrap(0x30, ext);

    img = g_byte_array_new();
    g_byte_array_append(img, cert->data, cert->len);
    g_byte_array_unref(cert);
    g_byte_array_append(img, sbl, sbl_len);
    g_byte_array_append(img, sysfw_blob, sizeof(sysfw_blob));
    g_byte_array_append(img, cfg_blob, sizeof(cfg_blob));
    return img;
}

static void test_parse_ok(void)
{
    static const uint8_t sbl[32] = "SBL-payload";
    GByteArray *img = make_image(sbl, sizeof(sbl));
    K3BootImage out;
    Error *err = NULL;

    g_assert_true(k3_bootrom_parse(img->data, img->len, &out, &err));
    g_assert_no_error(NULL);
    g_assert_null(err);
    g_assert_cmpuint(out.num_comps, ==, 3);
    g_assert_cmpuint(out.comps[0].comp_type, ==, K3_COMP_TYPE_SBL);
    g_assert_cmphex(out.comps[0].dest_addr, ==, 0x70000000);
    g_assert_cmpuint(out.comps[0].comp_size, ==, sizeof(sbl));
    g_assert_cmpuint(out.comps[0].payload_offset, ==, out.cert_len);
    g_assert_cmphex(out.comps[1].dest_addr, ==, 0x44000);
    g_assert_cmpuint(out.comps[2].payload_offset, ==,
                     out.cert_len + sizeof(sbl) + 16);
    g_assert_cmpint(memcmp(img->data + out.comps[0].payload_offset,
                           sbl, sizeof(sbl)), ==, 0);
    g_byte_array_unref(img);
}

static void test_parse_not_der(void)
{
    static const uint8_t junk[64] = { 0xff, 0x00, 0x41 };
    K3BootImage out;
    Error *err = NULL;

    g_assert_false(k3_bootrom_parse(junk, sizeof(junk), &out, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_parse_no_extension(void)
{
    /* valid DER SEQUENCE but no ext_boot_info OID inside */
    static const uint8_t seq[] = { 0x30, 0x03, 0x02, 0x01, 0x05 };
    K3BootImage out;
    Error *err = NULL;

    g_assert_false(k3_bootrom_parse(seq, sizeof(seq), &out, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_parse_truncated_payload(void)
{
    static const uint8_t sbl[32] = "SBL-payload";
    GByteArray *img = make_image(sbl, sizeof(sbl));
    K3BootImage out;
    Error *err = NULL;

    /* cut off half the payload: comp_size claims exceed the file */
    g_assert_false(k3_bootrom_parse(img->data, img->len - 20, &out, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_byte_array_unref(img);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/k3-bootrom/parse-ok", test_parse_ok);
    g_test_add_func("/k3-bootrom/not-der", test_parse_not_der);
    g_test_add_func("/k3-bootrom/no-extension", test_parse_no_extension);
    g_test_add_func("/k3-bootrom/truncated", test_parse_truncated_payload);
    return g_test_run();
}
```

Register in `tests/unit/meson.build` — inside the main `tests = { ... }` dict add (path style: other entries use paths relative to `tests/unit`, e.g. `'../qtest/libqos/...'`; verify with `grep -n "\.\./" tests/unit/meson.build | head`):

```meson
  'test-k3-bootrom': [files('../../hw/arm/k3-bootrom-parse.c')],
```

- [ ] **Step 3: Run test to verify it fails to build**

```bash
cd /home/sefo/devel/git/qemu/build
ninja tests/unit/test-k3-bootrom 2>&1 | tail -5
```

Expected: FAIL — `k3-bootrom-parse.c` does not exist yet.

- [ ] **Step 4: Implement the parser**

`hw/arm/k3-bootrom-parse.c`:

```c
/*
 * TI K3 boot-ROM emulation: X.509 combined boot image parser
 *
 * Parses the DER certificate TI's boot images are wrapped in, extracting
 * the ext_boot_info extension (OID 1.3.6.1.4.1.294.1.9) that describes
 * each payload component (type, destination, size). No signature
 * verification — QEMU plays a ROM with security fused off.
 *
 * Reference: u-boot tools/binman/btool/openssl.py, x509_cert_rom_combined()
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/k3-bootrom.h"

typedef struct DerSlice {
    const uint8_t *p;
    const uint8_t *end;
} DerSlice;

static bool der_read_tlv(DerSlice *s, uint8_t *tag, DerSlice *content,
                         Error **errp)
{
    uint64_t len;

    if (s->end - s->p < 2) {
        error_setg(errp, "k3-bootrom: truncated DER structure");
        return false;
    }
    *tag = *s->p++;
    len = *s->p++;
    if (len & 0x80) {
        unsigned n = len & 0x7f;

        if (n == 0 || n > 4 || (size_t)(s->end - s->p) < n) {
            error_setg(errp, "k3-bootrom: bad DER length encoding");
            return false;
        }
        len = 0;
        while (n--) {
            len = (len << 8) | *s->p++;
        }
    }
    if ((uint64_t)(s->end - s->p) < len) {
        error_setg(errp, "k3-bootrom: DER length exceeds buffer");
        return false;
    }
    content->p = s->p;
    content->end = s->p + len;
    s->p += len;
    return true;
}

static bool der_read_uint(DerSlice *s, uint64_t *out, Error **errp)
{
    DerSlice c;
    uint8_t tag;
    uint64_t v = 0;

    if (!der_read_tlv(s, &tag, &c, errp)) {
        return false;
    }
    if (tag != 0x02) {
        error_setg(errp, "k3-bootrom: expected INTEGER, got tag 0x%02x",
                   tag);
        return false;
    }
    if (c.end - c.p > 9 || (c.end - c.p == 9 && c.p[0] != 0)) {
        error_setg(errp, "k3-bootrom: INTEGER too large");
        return false;
    }
    for (const uint8_t *q = c.p; q < c.end; q++) {
        v = (v << 8) | *q;
    }
    *out = v;
    return true;
}

/* big-endian OCTET STRING (<= 8 bytes) -> uint64 */
static bool der_read_addr(DerSlice *s, uint64_t *out, Error **errp)
{
    DerSlice c;
    uint8_t tag;
    uint64_t v = 0;

    if (!der_read_tlv(s, &tag, &c, errp)) {
        return false;
    }
    if (tag != 0x04 || c.end - c.p > 8) {
        error_setg(errp, "k3-bootrom: bad destAddr field (tag 0x%02x)",
                   tag);
        return false;
    }
    for (const uint8_t *q = c.p; q < c.end; q++) {
        v = (v << 8) | *q;
    }
    *out = v;
    return true;
}

/* DER TLV of OID 1.3.6.1.4.1.294.1.9 (TI ext_boot_info) */
static const uint8_t k3_ext_boot_oid[] = {
    0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x26, 0x01, 0x09
};

static const uint8_t *find_bytes(const uint8_t *hay, size_t hay_len,
                                 const uint8_t *needle, size_t needle_len)
{
    if (hay_len < needle_len) {
        return NULL;
    }
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (memcmp(hay + i, needle, needle_len) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

bool k3_bootrom_parse(const uint8_t *buf, size_t len, K3BootImage *out,
                      Error **errp)
{
    DerSlice top = { buf, buf + len };
    DerSlice cert, rest, octets, info;
    const uint8_t *oid;
    uint8_t tag;
    uint64_t v;
    size_t payload_off;

    memset(out, 0, sizeof(*out));

    if (!der_read_tlv(&top, &tag, &cert, errp)) {
        return false;
    }
    if (tag != 0x30) {
        error_setg(errp,
                   "k3-bootrom: not an X.509 boot image (tag 0x%02x)", tag);
        return false;
    }
    out->cert_len = cert.end - buf;

    oid = find_bytes(cert.p, cert.end - cert.p, k3_ext_boot_oid,
                     sizeof(k3_ext_boot_oid));
    if (!oid) {
        error_setg(errp, "k3-bootrom: ext_boot_info extension "
                   "(OID 1.3.6.1.4.1.294.1.9) not found");
        return false;
    }
    rest.p = oid + sizeof(k3_ext_boot_oid);
    rest.end = cert.end;

    /* optional BOOLEAN 'critical' between OID and extnValue */
    if (rest.p < rest.end && rest.p[0] == 0x01) {
        DerSlice skip;

        if (!der_read_tlv(&rest, &tag, &skip, errp)) {
            return false;
        }
    }
    if (!der_read_tlv(&rest, &tag, &octets, errp)) {
        return false;
    }
    if (tag != 0x04) {
        error_setg(errp, "k3-bootrom: extension value is not an "
                   "OCTET STRING (tag 0x%02x)", tag);
        return false;
    }
    if (!der_read_tlv(&octets, &tag, &info, errp)) {
        return false;
    }
    if (tag != 0x30) {
        error_setg(errp, "k3-bootrom: ext_boot_info is not a SEQUENCE");
        return false;
    }

    if (!der_read_uint(&info, &out->ext_img_size, errp)) {
        return false;
    }
    if (!der_read_uint(&info, &v, errp)) {
        return false;
    }
    if (v == 0 || v > K3_BOOTROM_MAX_COMPS) {
        error_setg(errp, "k3-bootrom: unsupported component count %"
                   PRIu64, v);
        return false;
    }
    out->num_comps = v;

    payload_off = out->cert_len;
    for (uint32_t i = 0; i < out->num_comps; i++) {
        K3BootComponent *c = &out->comps[i];
        DerSlice comp;

        if (!der_read_tlv(&info, &tag, &comp, errp)) {
            return false;
        }
        if (tag != 0x30) {
            error_setg(errp, "k3-bootrom: component %u is not a SEQUENCE",
                       i);
            return false;
        }
        if (!der_read_uint(&comp, &v, errp)) {
            return false;
        }
        c->comp_type = v;
        if (!der_read_uint(&comp, &v, errp)) {
            return false;
        }
        c->boot_core = v;
        if (!der_read_uint(&comp, &v, errp)) {
            return false;
        }
        c->comp_opts = v;
        if (!der_read_addr(&comp, &c->dest_addr, errp)) {
            return false;
        }
        if (!der_read_uint(&comp, &v, errp)) {
            return false;
        }
        c->comp_size = v;
        /* shaType / shaValue intentionally ignored */
        c->payload_offset = payload_off;
        payload_off += c->comp_size;
    }

    if (payload_off > len) {
        error_setg(errp, "k3-bootrom: image truncated (components need "
                   "%zu bytes, file has %zu)", payload_off, len);
        return false;
    }
    return true;
}
```

Add to `hw/arm/meson.build` next to the existing `ti-am64x.c` line:

```meson
arm_common_ss.add(when: 'CONFIG_TI_AM64X', if_true: files('k3-bootrom-parse.c'))
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cd /home/sefo/devel/git/qemu/build
ninja tests/unit/test-k3-bootrom && ./tests/unit/test-k3-bootrom
```

Expected: PASS, 4/4 tests OK. Also `ninja` (full) still builds.

- [ ] **Step 6: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add hw/arm/k3-bootrom-parse.c include/hw/arm/k3-bootrom.h tests/unit/test-k3-bootrom.c tests/unit/meson.build hw/arm/meson.build
git commit -m "feat(am64x): add k3 bootrom combined-image parser"
```

---

### Task 5: Loader, boot params, reset PC, `-bios` machine integration (+ functional test)

**Files:**
- Create: `hw/arm/k3-bootrom.c`
- Modify: `hw/arm/am64-virt.c`, `hw/arm/meson.build`, `hw/arm/trace-events`
- Create: `tests/functional/aarch64/test_am64_bootrom.py`
- Modify: `tests/functional/aarch64/meson.build`

**Interfaces:**
- Consumes: `k3_bootrom_parse()` + `K3BootImage` (Task 4), `TIAM64xState.r5[0]` + `r5-start-powered-off` (Task 3), `main_uart0` chardev prop (Task 2), OCSRAM (Task 1).
- Produces: `void k3_bootrom_load(TIAM64xState *soc, const char *filename, Error **errp)`; `-machine am64-virt -bios <tiboot3.bin>` boots the R5 at the certified entry; in ROM-boot mode `serial_hd(0)` is the SPL console (main UART0), PL011s shift to `serial_hd(1)`/`serial_hd(2)`.

- [ ] **Step 1: Write the failing functional test**

`tests/functional/aarch64/test_am64_bootrom.py`:

```python
#!/usr/bin/env python3
#
# Boot-ROM emulation test for the am64-virt machine: build a synthetic
# TI combined boot image (X.509 cert + bare-metal R5 payload printing a
# magic string on main UART0) and check it runs from the certified entry.
# Optionally boots a real FluxOS tiboot3.bin when QEMU_TEST_TIBOOT3 is set.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import struct

from qemu_test import QemuSystemTest
from qemu_test.cmd import wait_for_console_pattern
from unittest import skipUnless


def der(tag, payload):
    n = len(payload)
    if n < 0x80:
        hdr = bytes([tag, n])
    else:
        hdr = bytes([tag, 0x82, n >> 8, n & 0xff])
    return hdr + payload


def der_int(v):
    out = v.to_bytes((v.bit_length() + 7) // 8 or 1, 'big')
    if out[0] & 0x80:
        out = b'\x00' + out
    return der(0x02, out)


EXT_BOOT_OID = bytes.fromhex('06092b0601040182260109')
SHA512_OID = bytes.fromhex('0609608648016503040203')


def component(ctype, core, opts, dest, size):
    return der(0x30,
               der_int(ctype) + der_int(core) + der_int(opts) +
               der(0x04, dest.to_bytes(4, 'big')) + der_int(size) +
               SHA512_OID + der(0x04, bytes(64)))


# Bare-metal A32 stub, linked at 0x70000000: prints a magic string on
# main UART0 (0x02800000, 16550 THR at offset 0), then parks.
SBL_STUB = struct.pack(
    '<10I',
    0xe59f001c,  # ldr r0, [pc, #0x1c]   ; r0 = 0x02800000
    0xe28f101c,  # add r1, pc, #0x1c     ; r1 = msg
    0xe4d12001,  # loop: ldrb r2, [r1], #1
    0xe3520000,  # cmp r2, #0
    0x0a000001,  # beq hang
    0xe5802000,  # str r2, [r0]
    0xeafffffa,  # b loop
    0xeafffffe,  # hang: b hang
    0x00000000,  # (pad)
    0x02800000,  # UART0 literal
) + b'K3BOOTROM-OK\r\n\x00'


def make_tiboot3():
    sbl = SBL_STUB
    sysfw = b'FAKE-SYSFW-PAYLOAD'
    cfg = b'FAKE-CFG'
    info = der(0x30,
               der_int(len(sbl) + len(sysfw) + len(cfg)) + der_int(3) +
               component(1, 16, 0, 0x70000000, len(sbl)) +
               component(2, 0, 0, 0x44000, len(sysfw)) +
               component(18, 0, 0, 0x7b000, len(cfg)))
    ext = der(0x30, EXT_BOOT_OID + der(0x04, info))
    cert = der(0x30, ext)
    return cert + sbl + sysfw + cfg


class Am64BootRom(QemuSystemTest):

    timeout = 60

    def boot_bios(self, path):
        self.set_machine('am64-virt')
        self.vm.set_console()
        self.vm.add_args('-bios', path)
        self.vm.launch()

    def test_synthetic_image(self):
        path = os.path.join(self.workdir, 'tiboot3-synth.bin')
        with open(path, 'wb') as f:
            f.write(make_tiboot3())
        self.boot_bios(path)
        wait_for_console_pattern(self, 'K3BOOTROM-OK')

    @skipUnless(os.getenv('QEMU_TEST_TIBOOT3'),
                'set QEMU_TEST_TIBOOT3=<path to tiboot3.bin>')
    def test_fluxos_tiboot3(self):
        self.boot_bios(os.getenv('QEMU_TEST_TIBOOT3'))
        wait_for_console_pattern(self, 'U-Boot SPL')


if __name__ == '__main__':
    QemuSystemTest.main()
```

Register in `tests/functional/aarch64/meson.build`: add `'am64_bootrom'` (alphabetically) to the `tests_aarch64_system_thorough = [` list.

Note: verify the import path first — `grep -rn "wait_for_console_pattern" tests/functional/aarch64/test_xlnx_versal.py` and mimic (it may be `from qemu_test import ... wait_for_console_pattern` directly).

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build
ninja
meson test --suite thorough func-aarch64-am64_bootrom --print-errorlogs 2>&1 | tail -15
```

(If the test name differs, discover it: `meson test --list | grep am64`.)
Expected: FAIL/TIMEOUT — `-bios` is currently ignored, nothing prints.

- [ ] **Step 3: Implement the loader**

`hw/arm/k3-bootrom.c`:

```c
/*
 * TI K3 boot-ROM (RBL) emulation for the AM64x
 *
 * Takes the mask-ROM's role at machine init: parses the -bios combined
 * image, copies the SBL (R5 SPL) into OCSRAM, discards the SYSFW payloads
 * (the ti-dmsc device emulates TIFS), synthesizes the ROM boot-parameter
 * words the SPL reads, and starts R5F0_0 at the certified entry point.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/loader.h"
#include "hw/core/cpu.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "hw/arm/ti-am64x.h"
#include "hw/arm/k3-bootrom.h"
#include "trace.h"

#define K3_OCSRAM_BASE              0x70000000ULL
#define K3_OCSRAM_SIZE              (2 * MiB)
/* u-boot: CONFIG_SYS_K3_BOOT_PARAM_TABLE_INDEX (AM642) */
#define K3_BOOT_PARAM_TABLE_INDEX   0x701bebfcULL
#define K3_PRIMARY_BOOTMODE         0x0
/* u-boot: ROM_EXTENDED_BOOT_DATA_INFO (AM64) */
#define K3_ROM_EXTENDED_BOOT_DATA   0x701beb00ULL

typedef struct K3BootRomReset {
    ARMCPU *cpu;
    uint64_t entry;
} K3BootRomReset;

static void k3_bootrom_cpu_reset(void *opaque)
{
    K3BootRomReset *r = opaque;
    CPUState *cs = CPU(r->cpu);

    cpu_reset(cs);
    cpu_set_pc(cs, r->entry);
}

void k3_bootrom_load(TIAM64xState *soc, const char *filename, Error **errp)
{
    g_autofree uint8_t *buf = NULL;
    gsize len;
    GError *gerr = NULL;
    K3BootImage img;
    const K3BootComponent *sbl = NULL;
    K3BootRomReset *r;
    struct {
        char magic[8];
        uint32_t num_components;
    } QEMU_PACKED extboot = { "EXTBOOT", 0 };
    uint32_t bootindex = cpu_to_le32(K3_PRIMARY_BOOTMODE);

    if (!g_file_get_contents(filename, (char **)&buf, &len, &gerr)) {
        error_setg(errp, "k3-bootrom: cannot read '%s': %s", filename,
                   gerr->message);
        g_error_free(gerr);
        return;
    }
    if (!k3_bootrom_parse(buf, len, &img, errp)) {
        return;
    }

    for (uint32_t i = 0; i < img.num_comps; i++) {
        const K3BootComponent *c = &img.comps[i];

        trace_k3_bootrom_component(c->comp_type, c->dest_addr,
                                   c->comp_size);
        if (c->comp_type == K3_COMP_TYPE_SBL && !sbl) {
            sbl = c;
        }
        /* SYSFW / SYSFW-DATA target DMSC-internal memory: discarded,
         * ti-dmsc emulates TIFS. */
    }
    if (!sbl) {
        error_setg(errp, "k3-bootrom: image has no SBL component");
        return;
    }
    if (sbl->dest_addr < K3_OCSRAM_BASE ||
        sbl->dest_addr + sbl->comp_size > K3_OCSRAM_BASE + K3_OCSRAM_SIZE) {
        error_setg(errp, "k3-bootrom: SBL 0x%" PRIx64 "+0x%x outside "
                   "OCSRAM", sbl->dest_addr, sbl->comp_size);
        return;
    }

    rom_add_blob_fixed_as("k3.sbl", buf + sbl->payload_offset,
                          sbl->comp_size, sbl->dest_addr,
                          &address_space_memory);
    rom_add_blob_fixed_as("k3.bootindex", &bootindex, sizeof(bootindex),
                          K3_BOOT_PARAM_TABLE_INDEX, &address_space_memory);
    extboot.num_components = cpu_to_le32(img.num_comps);
    rom_add_blob_fixed_as("k3.extboot", &extboot, sizeof(extboot),
                          K3_ROM_EXTENDED_BOOT_DATA, &address_space_memory);

    r = g_new0(K3BootRomReset, 1);
    r->cpu = &soc->r5[0];
    r->entry = sbl->dest_addr;
    qemu_register_reset(k3_bootrom_cpu_reset, r);
    trace_k3_bootrom_boot(sbl->dest_addr);
}
```

Add to `hw/arm/trace-events` (bottom):

```
# k3-bootrom.c
k3_bootrom_component(uint32_t comp_type, uint64_t dest, uint32_t size) "comp_type %u dest 0x%" PRIx64 " size 0x%x"
k3_bootrom_boot(uint64_t entry) "starting R5F0_0 at 0x%" PRIx64
```

Add to `hw/arm/meson.build` next to the parser line:

```meson
arm_common_ss.add(when: 'CONFIG_TI_AM64X', if_true: files('k3-bootrom.c'))
```

If include paths fail, check what `hw/arm/am64-virt.c`/`hw/arm/boot.c` use for `qemu_register_reset` (`system/reset.h`), `rom_add_blob_fixed_as` (`hw/loader.h`) and `address_space_memory` (`system/address-spaces.h`) in this tree and adjust.

- [ ] **Step 4: Machine integration in am64-virt.c**

In `am64_virt_init()`:

a) After the `m4boot_cpu` validation block, add mutual exclusion + power config (before `sysbus_realize_and_unref` of the SoC):

```c
    if (machine->firmware && ams->m4boot_cpu >= 0) {
        error_report("am64-virt: -bios and m4boot-cpu are mutually "
                     "exclusive");
        exit(1);
    }
    if (machine->firmware) {
        /* ROM-boot mode: only the R5F boot core runs */
        qdev_prop_set_bit(soc, "a53-start-powered-off", true);
        qdev_prop_set_bit(soc, "m4-start-powered-off", true);
        qdev_prop_set_bit(soc, "r5-start-powered-off", false);
        qdev_prop_set_chr(DEVICE(&TI_AM64X(soc)->main_uart0),
                          "chardev", serial_hd(0));
    }
```

b) The two PL011 calls: in ROM-boot mode shift them off `serial_hd(0)`:

```c
    am64_virt_create_uart(AM64_VIRT_UART0_BASE, AM64_VIRT_UART0_IRQ,
                          machine->firmware ? serial_hd(1) : serial_hd(0),
                          gic);
    am64_virt_create_uart(AM64_VIRT_UART1_BASE, AM64_VIRT_UART1_IRQ,
                          machine->firmware ? serial_hd(2) : serial_hd(1),
                          gic);
```

c) The boot dispatch at the end (replacing the current `if (ams->m4boot_cpu < 0) arm_load_kernel(...)`):

```c
    if (machine->firmware) {
        g_autofree char *fn =
            qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);

        k3_bootrom_load(ams->soc, fn ? fn : machine->firmware,
                        &error_fatal);
    } else if (ams->m4boot_cpu < 0) {
        arm_load_kernel(ARM_CPU(qemu_get_cpu(0)), machine, &ams->bootinfo);
    }
```

Add `#include "hw/arm/k3-bootrom.h"` at the top.

- [ ] **Step 5: Run the functional test to verify it passes**

```bash
cd /home/sefo/devel/git/qemu/build
ninja
meson test --suite thorough func-aarch64-am64_bootrom --print-errorlogs
```

Expected: PASS — synthetic test sees `K3BOOTROM-OK`; the FluxOS test reports SKIP (env var unset). Also re-run qtests and the Task 0 smoke test (normal kernel-less boot must be unchanged):

```bash
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

- [ ] **Step 6: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add hw/arm/k3-bootrom.c hw/arm/am64-virt.c hw/arm/meson.build hw/arm/trace-events tests/functional/aarch64/test_am64_bootrom.py tests/functional/aarch64/meson.build
git commit -m "feat(am64x): boot combined tiboot3 images via -bios"
```

---

### Task 6: CTRL_MMR stub with DEVSTAT

**Files:**
- Create: `hw/misc/ti-k3-ctrlmmr.c`, `include/hw/misc/ti-k3-ctrlmmr.h`
- Modify: `hw/arm/ti-am64x.c`, `include/hw/arm/ti-am64x.h`, `hw/misc/Kconfig`, `hw/misc/meson.build`, `hw/arm/Kconfig`
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Consumes: unimp region `CTRL_MMR0` from Task 1 (replaced by this device).
- Produces: device `TYPE_TI_K3_CTRLMMR` (`"ti.k3-ctrlmmr"`), 128 KiB region mapped at `0x43000000`; reads: offset `0x30` returns prop `devstat` (default `0x48` = primary boot eMMC), everything else RAZ; writes ignored (kick unlocks succeed silently). SoC member `TIK3CtrlMmrState ctrlmmr`.

- [ ] **Step 1: Write the failing qtest**

Add to `tests/qtest/am64-virt-test.c`:

```c
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
```

Register: `qtest_add_func("/am64-virt/devstat", test_devstat);`

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build && ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: FAIL — unimp region reads 0, not 0x48.

- [ ] **Step 3: Implement the device**

`include/hw/misc/ti-k3-ctrlmmr.h`:

```c
/*
 * TI K3 CTRL_MMR stub (DEVSTAT + lock-kick sink)
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_CTRLMMR_H
#define HW_MISC_TI_K3_CTRLMMR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_CTRLMMR "ti.k3-ctrlmmr"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3CtrlMmrState, TI_K3_CTRLMMR)

struct TIK3CtrlMmrState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t devstat;
};

#endif
```

`hw/misc/ti-k3-ctrlmmr.c`:

```c
/*
 * TI K3 CTRL_MMR stub
 *
 * Minimal model of the AM64x main-domain control MMRs: returns a
 * configurable MAIN_DEVSTAT (boot-mode pins) at offset 0x30 and accepts
 * (ignores) all writes, so u-boot's mmr_unlock() kick sequences succeed.
 * Everything else reads as zero.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/misc/ti-k3-ctrlmmr.h"

#define CTRLMMR_MAIN_DEVSTAT 0x30
#define CTRLMMR_SIZE 0x20000 /* partitions 0-7 */

static uint64_t ti_k3_ctrlmmr_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3CtrlMmrState *s = TI_K3_CTRLMMR(opaque);

    if (addr == CTRLMMR_MAIN_DEVSTAT) {
        return s->devstat;
    }
    qemu_log_mask(LOG_UNIMP,
                  "%s: unimplemented read @0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void ti_k3_ctrlmmr_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    /* lock-kick and pinmux writes: accept and ignore */
}

static const MemoryRegionOps ti_k3_ctrlmmr_ops = {
    .read = ti_k3_ctrlmmr_read,
    .write = ti_k3_ctrlmmr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ti_k3_ctrlmmr_init(Object *obj)
{
    TIK3CtrlMmrState *s = TI_K3_CTRLMMR(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_ctrlmmr_ops, s,
                          TYPE_TI_K3_CTRLMMR, CTRLMMR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const Property ti_k3_ctrlmmr_properties[] = {
    /* default: primary bootmode = eMMC (0x9 << 3) */
    DEFINE_PROP_UINT32("devstat", TIK3CtrlMmrState, devstat, 0x48),
};

static void ti_k3_ctrlmmr_class_init(ObjectClass *klass, const void *data)
{
    device_class_set_props(DEVICE_CLASS(klass), ti_k3_ctrlmmr_properties);
}

static const TypeInfo ti_k3_ctrlmmr_info = {
    .name = TYPE_TI_K3_CTRLMMR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3CtrlMmrState),
    .instance_init = ti_k3_ctrlmmr_init,
    .class_init = ti_k3_ctrlmmr_class_init,
};

static void ti_k3_ctrlmmr_register_types(void)
{
    type_register_static(&ti_k3_ctrlmmr_info);
}

type_init(ti_k3_ctrlmmr_register_types)
```

(Check `Property`/`device_class_set_props` conventions against `hw/misc/ti-rat.c` in this tree — some QEMU versions use `DEFINE_PROP_END_OF_LIST()`-terminated arrays; mimic the fork's own devices.)

Wiring:
- `hw/misc/Kconfig`: add `config TI_K3_CTRLMMR` (plain `bool`) next to `TI_DMSC`.
- `hw/misc/meson.build`: `system_ss.add(when: 'CONFIG_TI_K3_CTRLMMR', if_true: files('ti-k3-ctrlmmr.c'))`.
- `hw/arm/Kconfig`: in `config TI_AM64X`, add `select TI_K3_CTRLMMR`.
- `include/hw/arm/ti-am64x.h`: add `#include "hw/misc/ti-k3-ctrlmmr.h"` and member `TIK3CtrlMmrState ctrlmmr;`.
- `hw/arm/ti-am64x.c`: `object_initialize_child(obj, "ctrlmmr", &s->ctrlmmr, TYPE_TI_K3_CTRLMMR);` in initfn; in realize:

```c
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ctrlmmr), errp)) {
        return;
    }
    memory_region_add_subregion(sysmem, 0x43000000,
        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ctrlmmr), 0));
```

- **Remove** the `ADD_MAIN_UNIMP("CTRL_MMR0", ...)` entry added in Task 1 (would overlap).

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
meson test --suite thorough func-aarch64-am64_bootrom
```

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add hw/misc/ti-k3-ctrlmmr.c include/hw/misc/ti-k3-ctrlmmr.h hw/misc/Kconfig hw/misc/meson.build hw/arm/Kconfig hw/arm/ti-am64x.c include/hw/arm/ti-am64x.h tests/qtest/am64-virt-test.c
git commit -m "feat(am64x): add ctrl_mmr stub with devstat boot pins"
```

---

### Task 7: DMSC — R5 secure transport, board-config ACKs, NAK for unknowns

**Files:**
- Modify: `hw/misc/ti-dmsc.c`, `include/hw/misc/ti-dmsc.h`, `hw/arm/ti-am64x.c`
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Consumes: R5 client wiring (Task 3: rx=thread 1, tx=thread 0); existing `ti_dmsc_set_resp_flags()`, `msg_handler[]` dispatch (`ti-dmsc.c:619-622`), `ti_sec_proxy_push_msg()`.
- Produces: DMSC prop `secure-rx-threads` (uint16 array) — clients whose rx thread is listed strip a 4-byte `{u16 checksum; u16 reserved}` prefix on receive and prepend 4 zero bytes on respond; generic-ACK handlers for `0x000B/0x000C/0x000D/0x000E`; unknown message types get a header-only response with no ACK flag (NAK) instead of being dropped.

- [ ] **Step 1: Read the current dispatch/respond code paths**

```bash
cd /home/sefo/devel/git/qemu
sed -n '580,660p' hw/misc/ti-dmsc.c
grep -n "TISCI_MSG_BOARD_CONFIG\|TISCI_MSG_FLAG" include/hw/misc/ti-dmsc.h
grep -n "ti_sec_proxy_push_msg" hw/misc/ti-dmsc.c | head -3
```

Confirm: exact macro names for the board-config message IDs (`0x000B`-`0x000E`) in the header (add them if only some exist), the `TISCI_MSG_FLAG_AOP`/`ACK` values, and how an existing handler (e.g. `ti_dmsc_get_version` at `ti-dmsc.c:827`) pushes its response — mirror that exactly in the new code.

- [ ] **Step 2: Write the failing qtest**

TISCI VERSION request through the R5 secure path, driven purely via sec-proxy MMIO (target_data window = `0x4D000000 + thread*0x1000`, payload words at `+0x04..+0x3C`, writing offset `0x3C` commits; RX message count = RT `0x4A600000 + thread*0x1000` low byte). Add to `tests/qtest/am64-virt-test.c`:

```c
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

    /* secure hdr (word0) then TISCI hdr: type must echo 0x0002,
     * flags word must have ACK set (bit 1) */
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x0002);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    qtest_quit(qts);
}
```

Register: `qtest_add_func("/am64-virt/dmsc-r5-version", test_dmsc_r5_version);`

Notes for the implementer:
- The DMSC handles messages in a bottom-half; the poll loop gives the main loop time to run it. If the BH never fires under qtest, check how existing DMSC behaviour is exercised (trace `-trace 'ti_dmsc*'`) — the qtest main loop does dispatch BHs between commands.
- The exact flag constant: use the header's `TISCI_MSG_FLAG_AOP` value (verify with grep from Step 1; adjust the literal `0x2` in the test if it differs).
- The exact response offsets depend on how `ti_sec_proxy_push_msg` fills `current_message[1..]` and how reads map registers (`ti-sec-proxy.c:397-405` region read path). Read that code first; adjust the read offsets (`+0x08`/`+0x0c`) if word0 of the pushed message appears at `+0x04`.

- [ ] **Step 3: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build && ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: FAIL — DMSC parses the secure header word as the TISCI header (type 0x0000) and drops/misroutes the message; no ACK on thread 0. (If it accidentally passes, the assertions on the echoed type will catch the misparse.)

- [ ] **Step 4: Implement**

a) `include/hw/misc/ti-dmsc.h`: ensure these exist (add missing ones near the other `TISCI_MSG_*` defines):

```c
#define TISCI_MSG_BOARD_CONFIG          0x000B
#define TISCI_MSG_BOARD_CONFIG_RM       0x000C
#define TISCI_MSG_BOARD_CONFIG_SECURITY 0x000D
#define TISCI_MSG_BOARD_CONFIG_PM       0x000E
```

b) Add `bool secure;` to `struct TIDmscClient` and prop plumbing in `TIDmscState`: `uint16_t *secure_rx_threads; uint32_t num_secure_rx_threads;` plus

```c
    DEFINE_PROP_ARRAY("secure-rx-threads", TIDmscState,
                      num_secure_rx_threads, secure_rx_threads,
                      qdev_prop_uint16, uint16_t),
```

In `realize`, after clients are created, mark `client->secure = true` for every client whose `rx_thread_id` appears in `secure_rx_threads`.

c) In the message entry point (`ti_dmsc_handle_one`, around `ti-dmsc.c:609`): if `client->secure`, skip the first 4 bytes (one word) of the incoming message before reading the `TISciMsgHdr` (length check first: `nwords >= 1 + hdr_words`).

d) In the response path: for secure clients, prepend one zero word (the `{checksum; reserved}` header) before the response payload when pushing via `ti_sec_proxy_push_msg`. Implement centrally: a small `ti_dmsc_client_respond(client, words, nbytes)` wrapper that all handlers use — refactor the existing handlers' direct `ti_sec_proxy_push_msg` calls onto it (mechanical, keeps secure handling in one place).

e) Board-config ACK handler + registration:

```c
static void ti_dmsc_handle_board_config(TIDmscClient *client,
                                        TISciMsgHdr *hdr,
                                        uint32_t thread_id,
                                        uint32_t *words, int nwords)
{
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp));
}
```

registered for all four IDs in `realize`:

```c
    s->msg_handler[TISCI_MSG_BOARD_CONFIG] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_RM] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_SECURITY] =
        ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_PM] = ti_dmsc_handle_board_config;
```

(Match the handler signature to the actual `TiDmscMsgHandler` typedef at `ti-dmsc.h:478` — verify parameter list.)

f) NAK unknowns: in the dispatch else-branch (currently trace + drop), send a header-only response with no ACK:

```c
        TISciMsgHdr resp = *hdr;

        resp.flags = 0; /* no ACK = NAK: unblocks the sender's rx wait */
        ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp));
```

(keep the existing trace/log).

g) `hw/arm/ti-am64x.c`: mark the R5 client secure — next to the QList wiring from Task 3:

```c
    QList *secure_rx = qlist_new();

    qlist_append_int(secure_rx, MAIN_0_R5_0_WRITE_THREAD_ID);
    qdev_prop_set_array(DEVICE(&s->dmsc), "secure-rx-threads", secure_rx);
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: all PASS, including `/am64-virt/dmsc-r5-version`. Existing M4/A53 clients (non-secure) must be unaffected — re-run the full qtest binary and the functional suite.

- [ ] **Step 6: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add hw/misc/ti-dmsc.c include/hw/misc/ti-dmsc.h hw/arm/ti-am64x.c tests/qtest/am64-virt-test.c
git commit -m "feat(dmsc): r5 secure transport, board-config acks, nak unknowns"
```

---

### Task 8: DM timer (main_timer0, 20 MHz free-running)

**Files:**
- Create: `hw/timer/ti-k3-dmtimer.c`, `include/hw/timer/ti-k3-dmtimer.h`
- Modify: `hw/timer/Kconfig`, `hw/timer/meson.build`, `hw/arm/Kconfig`, `hw/arm/ti-am64x.c`, `include/hw/arm/ti-am64x.h`
- Modify: `tests/qtest/am64-virt-test.c`

Needed by u-boot's `udelay` (post-banner, but the SPL touches it immediately after the banner during `rproc_start`; a missing timer would read 0 forever and hang `udelay` loops — cheap insurance for the acceptance run).

**Interfaces:**
- Produces: device `TYPE_TI_K3_DMTIMER` (`"ti.k3-dmtimer"`), am654-layout DM timer at `0x02400000`: `TIDR@0x00` (RO id), `TCLR@0x38` (bit0 ST starts/stops), `TCRR@0x3c` (free-counting at prop `freq-hz`, default 20000000, writable), `TLDR@0x40`, `TTGR@0x44` (write reloads TCRR from TLDR), `TWPS@0x48` (always 0). SoC member `TIK3DmTimerState main_timer0`.

- [ ] **Step 1: Verify the register layout against the actual u-boot driver**

```bash
grep -n "struct omap_gptimer\|tclr\|tcrr\|TIMER" \
  /tmp/claude-1000/-home-sefo-devel-git-neoflux/60fe955a-7832-4f61-b6be-bf3c7aa65d03/scratchpad/uboot/drivers/timer/omap-timer.c 2>/dev/null \
  || echo "re-fetch: raw.githubusercontent.com/phytec/u-boot-phytec/6980061fa87574c69f5f37a9d0948545eb958552/drivers/timer/omap-timer.c"
```

Confirm which offsets the driver reads for the `ti,am654-timer` compatible (the register struct + any version-dependent offset adjustment) and whether it writes TCLR to start the timer or assumes it running. **Adjust the model's offsets to match what the driver actually touches.** If the driver only ever reads the counter, the model may keep the counter always-running regardless of TCLR.ST — note the choice in a comment.

- [ ] **Step 2: Write the failing qtest**

```c
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
```

Register: `qtest_add_func("/am64-virt/dmtimer", test_dmtimer_counts);`

- [ ] **Step 3: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build && ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: FAIL (reads 0).

- [ ] **Step 4: Implement**

`include/hw/timer/ti-k3-dmtimer.h`:

```c
/*
 * TI K3 DM timer (am654 layout), free-running counter model
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_TIMER_TI_K3_DMTIMER_H
#define HW_TIMER_TI_K3_DMTIMER_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_DMTIMER "ti.k3-dmtimer"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3DmTimerState, TI_K3_DMTIMER)

struct TIK3DmTimerState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t freq_hz;
    uint32_t tclr;
    uint32_t tldr;
    /* counter value latched at last write/start */
    uint32_t tcrr_base;
    int64_t base_ns;
    bool running;
};

#endif
```

`hw/timer/ti-k3-dmtimer.c`:

```c
/*
 * TI K3 DM timer (dmtimer, am654 register layout) — counting model
 *
 * Models exactly what u-boot's omap-timer driver needs for udelay on the
 * AM64x R5 SPL: a counter (TCRR) advancing at freq-hz virtual time while
 * TCLR.ST is set. No interrupts, no compare/PWM.
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/qdev-properties.h"
#include "hw/timer/ti-k3-dmtimer.h"

#define R_TIDR  0x00
#define R_TCLR  0x38
#define R_TCRR  0x3c
#define R_TLDR  0x40
#define R_TTGR  0x44
#define R_TWPS  0x48

#define TCLR_ST 1u

static uint32_t dmtimer_tcrr(TIK3DmTimerState *s)
{
    int64_t now;

    if (!s->running) {
        return s->tcrr_base;
    }
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    return s->tcrr_base +
           (uint32_t)muldiv64(now - s->base_ns, s->freq_hz,
                              NANOSECONDS_PER_SECOND);
}

static void dmtimer_set_tcrr(TIK3DmTimerState *s, uint32_t val)
{
    s->tcrr_base = val;
    s->base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static uint64_t ti_k3_dmtimer_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3DmTimerState *s = TI_K3_DMTIMER(opaque);

    switch (addr) {
    case R_TIDR:
        return 0x0;
    case R_TCLR:
        return s->tclr;
    case R_TCRR:
        return dmtimer_tcrr(s);
    case R_TLDR:
        return s->tldr;
    case R_TWPS:
        return 0; /* no write pending, ever */
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented read @0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
}

static void ti_k3_dmtimer_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    TIK3DmTimerState *s = TI_K3_DMTIMER(opaque);

    switch (addr) {
    case R_TCLR:
        if ((val & TCLR_ST) && !s->running) {
            dmtimer_set_tcrr(s, s->tcrr_base);
            s->running = true;
        } else if (!(val & TCLR_ST) && s->running) {
            s->tcrr_base = dmtimer_tcrr(s);
            s->running = false;
        }
        s->tclr = val;
        break;
    case R_TCRR:
        dmtimer_set_tcrr(s, val);
        break;
    case R_TLDR:
        s->tldr = val;
        break;
    case R_TTGR:
        dmtimer_set_tcrr(s, s->tldr);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented write @0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps ti_k3_dmtimer_ops = {
    .read = ti_k3_dmtimer_read,
    .write = ti_k3_dmtimer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ti_k3_dmtimer_init(Object *obj)
{
    TIK3DmTimerState *s = TI_K3_DMTIMER(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_dmtimer_ops, s,
                          TYPE_TI_K3_DMTIMER, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const Property ti_k3_dmtimer_properties[] = {
    /* AM64 main_timer0 free-runs at 20 MHz before SYSFW clock setup */
    DEFINE_PROP_UINT32("freq-hz", TIK3DmTimerState, freq_hz, 20000000),
};

static void ti_k3_dmtimer_class_init(ObjectClass *klass, const void *data)
{
    device_class_set_props(DEVICE_CLASS(klass), ti_k3_dmtimer_properties);
}

static const TypeInfo ti_k3_dmtimer_info = {
    .name = TYPE_TI_K3_DMTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3DmTimerState),
    .instance_init = ti_k3_dmtimer_init,
    .class_init = ti_k3_dmtimer_class_init,
};

static void ti_k3_dmtimer_register_types(void)
{
    type_register_static(&ti_k3_dmtimer_info);
}

type_init(ti_k3_dmtimer_register_types)
```

(Same property-array-convention caveat as Task 6. If Step 1 showed the driver needs the timer running without a TCLR write, initialize `running = true` in a `reset` method instead.)

Wiring (mirror Task 6): `hw/timer/Kconfig` `config TI_K3_DMTIMER`; `hw/timer/meson.build` `system_ss.add(when: 'CONFIG_TI_K3_DMTIMER', if_true: files('ti-k3-dmtimer.c'))`; `select TI_K3_DMTIMER` in `TI_AM64X`; SoC member `TIK3DmTimerState main_timer0`, initfn child `"main-timer0"`, realize + map at `0x02400000`. Remove any overlapping unimp entry (`grep -n "0x002400000" hw/arm/ti-am64x.c`).

- [ ] **Step 5: Run tests to verify they pass**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add hw/timer/ti-k3-dmtimer.c include/hw/timer/ti-k3-dmtimer.h hw/timer/Kconfig hw/timer/meson.build hw/arm/Kconfig hw/arm/ti-am64x.c include/hw/arm/ti-am64x.h tests/qtest/am64-virt-test.c
git commit -m "feat(am64x): add k3 dmtimer for spl udelay"
```

---

### Task 9: Acceptance with the real FluxOS tiboot3 + docs

**Files:**
- Modify (as triage demands): `hw/arm/ti-am64x.c` (unimp coverage), possibly `hw/misc/ti-dmsc.c`
- Modify: `docs/superpowers/specs/2026-07-14-am64-tiboot3-bootrom-design.md` (status + deviations)

**Interfaces:**
- Consumes: everything above.
- Produces: `U-Boot SPL` banner from the unmodified FluxOS tiboot3.bin — the project's done-criterion.

- [ ] **Step 1: Obtain the FluxOS tiboot3.bin**

The artifact name is `tiboot3-am64x_sr2-hs-fs-phycore-som.bin` (symlinked `tiboot3.bin`), built by the FluxOS Yocto on yoctoklaus-embedded (deploy dir of MACHINE `am64xx-cmblu-core-node-1-k3r5`; the cmblu-yocto-image skill documents the build tree). Copy it to `/home/sefo/devel/git/qemu/build/tiboot3.bin`. If the deploy dir is unreachable, ask the user for the binary — do not substitute a TI-EVM image (different board config, but note it *should* also parse; useful as a secondary probe).

- [ ] **Step 2: First acceptance run with full diagnostics**

```bash
cd /home/sefo/devel/git/qemu/build
timeout 30 ./qemu-system-aarch64 -machine am64-virt -display none \
    -bios tiboot3.bin -serial stdio \
    -d unimp,guest_errors 2>&1 | tee /tmp/am64-boot.log | tail -40
```

Success: `U-Boot SPL 2025.01 ...` in the output → skip to Step 4.

- [ ] **Step 3: Triage loop (repeat until the banner appears)**

For each run, look at the *last* lines of `/tmp/am64-boot.log`:

1. **Data abort / QEMU exits**: an access hit unassigned memory. Find the address in the `guest_errors` log; add an `ADD_MAIN_UNIMP(...)` window for that peripheral block in `ti_am64_create_main_unimplemented()` (name from the AM64x TRM memory map; the fork's existing table is the naming reference).
2. **Silent hang, unimp reads in the log**: the SPL polls a status bit that reads 0. Identify the block; if it needs a real value (like DEVSTAT), extend the matching stub (Task 6 pattern) to return the "ready" value. Cross-reference which u-boot code polls it: grep the address's MMR offset in the u-boot working copy.
3. **Hang with sec-proxy traces**: run again with `-trace 'ti_dmsc*' -trace 'ti_sec_proxy*'`; if an unexpected TISCI message arrives pre-banner, add a handler per the Task 7 pattern.
4. **Garbled console output**: reg-shift/endianness mismatch on UART0 — re-check Task 2 against `k3-am64-main.dtsi` (`reg-shift = <2>` equivalent behaviour, LE).

Re-run Step 2 after each fix. Commit each root-caused fix separately:

```bash
git add -A hw/ && git commit -m "fix(am64x): <specific block> for r5 spl boot"
```

- [ ] **Step 4: Full test suite + gated functional test**

```bash
cd /home/sefo/devel/git/qemu/build
ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
./tests/unit/test-k3-bootrom
QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin \
    meson test --suite thorough func-aarch64-am64_bootrom --print-errorlogs
```

Expected: everything PASS, including `test_fluxos_tiboot3` (no longer skipped).

- [ ] **Step 5: Update the spec with as-built reality**

Edit `docs/superpowers/specs/2026-07-14-am64-tiboot3-bootrom-design.md`:
- Status line → `Implemented (<commit range>)`.
- Record the two design deviations: (1) **no boot-notification queuing** (u-boot v2025.01 never waits for it); (2) **PLL/PSC stubs not needed** pre-banner (R5 DTS runs UART/timer clock-less); DM timer added instead.
- Record the final invocation and any extra `-serial` semantics (serial0 = SPL console in ROM-boot mode).

- [ ] **Step 6: Final commit**

```bash
cd /home/sefo/devel/git/qemu
git add docs/superpowers/specs/2026-07-14-am64-tiboot3-bootrom-design.md
git commit -m "docs: record am64 tiboot3 bootrom as-built state"
```

Do **not** push — the user pushes manually.

---

## Self-review notes (done at plan time)

- **Spec coverage:** SoC additions §1 → Tasks 1-3; bootrom §2 → Tasks 4-5; stubs §3 → Tasks 6 (CTRL_MMR) + 8 (timer) — PLL/PSC stubs from the spec are *dropped* with evidence (no pre-banner PLL/PSC access in u-boot v2025.01; Task 9 triage catches any surprise); DMSC §4 → Task 7 (boot notification dropped with evidence, board-config ACKs + NAK kept); machine §5 → Task 5; error handling → Tasks 4/5 (fail-fast) + 7 (NAK) + LOG_UNIMP throughout; testing → Tasks 4 (unit), 5 (synthetic functional), 9 (gated real-image functional).
- **Known uncertainty, by design:** exact meson/prop-array idioms of this QEMU version, sec-proxy response word offsets (Task 7 Step 1/notes), omap-timer register usage (Task 8 Step 1) — each has an explicit verify step with the command to resolve it.
