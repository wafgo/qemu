# AM64x GICv3 Migration Implementation Plan (Full-Boot-Chain Phase 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ti-am64x SoC's GICv2 with a GICv3 at the real AM64x
GIC-500 addresses, keeping every existing test and boot path green — the
prerequisite for running ATF (BL31) in phase 3.

**Architecture:** Swap `TYPE_ARM_GIC` for `TYPE_ARM_GICV3` in the SoC
(dist `0x01800000`, redistributors at the real GICR base `0x01840000`),
rewrite the per-CPU PPI wiring to the v3 scheme (ppibase + INTID_TO_PPI,
plus the previously-unwired maintenance interrupt), leave every SPI
consumer untouched (SPI input indices are identical on both models), and
regenerate the convenience DTB. A new qtest pins the GICv3 identity
registers; a new functional test boots a Linux kernel to prove end-to-end
interrupt delivery.

**Tech Stack:** QEMU 10.2.50 fork (`wafgo/qemu`), C, meson/ninja, qtest,
tests/functional (Python), dtc.

**Spec:** `docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md` (Phase 1).

## Global Constraints

- Repo `/home/sefo/devel/git/qemu`, work directly on branch `feat/am64-tiboot3-bootrom` (phases stack on it). **Never push; never pull.** Commit per task.
- Commit style: `type(scope): lowercase subject` ≤72 chars, no AI trailers. New-code style via `git format-patch -1 --stdout | ./scripts/checkpatch.pl --no-signoff -` (the `-g` form does not work in this tree).
- Build dir `build/` exists (`--disable-werror` covers pre-existing fork warnings only — new code stays warning-clean). meson binary: `build/pyvenv/bin/meson`.
- **Behaviour-preserving migration:** every existing SPI number, every consumer wiring call, and all machine modes (direct-Linux, `-kernel`, `m4boot-cpu=0`, `-bios` ROM-boot, cmblu-corenode) must work unchanged. Do not "fix" unrelated quirks (see the mailbox note in Task 1).
- Regression net that must stay green after every task: qtest `am64-virt-test` (8 tests, becomes 9), unit `test-k3-bootrom` (6), functional `func-aarch64-am64_bootrom` with `QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin` (2 subtests; the real image sits at `build/tiboot3.bin`), smoke `timeout 10 ./qemu-system-aarch64 -machine am64-virt -display none -serial null` → exit 124.

## Verified reference facts (do not re-derive)

| Fact | Value | Source |
| --- | --- | --- |
| Real GIC-500 map | GICD `0x01800000` size 0x10000; GICR `0x01840000` size 0xC0000 (stride 0x20000/redist); ITS `0x01820000` (**not modelled — YAGNI**, PCIe MSI later) | `k3-am64-main.dtsi` gic500 node |
| Current GIC block | `hw/arm/ti-am64x.c:842-857` (props+map), `:859-873` (timer PPIs), `:875-882` (IRQ/FIQ/VIRQ/VFIQ) | exploration 2026-07-15 |
| v3 property set | `revision`=3, `num-cpu`, `num-irq` (SPIs+32), `has-security-extensions`=true, array prop `redist-region-count`=[num_cpu]; **no** `has-virtualization-extensions` on v3 | `hw/arm/xlnx-versal.c:772-788`, `sbsa-ref.c:451` |
| v3 MMIO regions | sysbus region 0 = distributor, region 1 = redistributors (`0x20000 × num_cpu`); no CPU-interface MMIO (sysreg-based) | `xlnx-versal.c:790-801`, `GICV3_REDIST_SIZE` in `arm_gicv3_common.h:46` |
| v3 PPI wiring | `ppibase = <num SPIs> + idx*GIC_INTERNAL + GIC_NR_SGIS`, then `+ INTID_TO_PPI(x)`; maintenance IRQ via named GPIO `"gicv3-maintenance-interrupt"` → `INTID_TO_PPI(ARCH_GIC_MAINT_IRQ)` (=9) | `xlnx-versal.c:840-885` |
| Macros available | `INTID_TO_PPI`, `ARCH_TIMER_*`, `ARCH_GIC_MAINT_IRQ` from `hw/arm/bsa.h` (already included at `ti-am64x.c:19`); `GIC_NR_SGIS`=16, `GIC_INTERNAL`=32 from `arm_gic_common.h` (still available via `arm_gicv3_common.h`) | headers |
| Kconfig | **no change needed** — `TI_AM64X` selects `ARM_GIC`, which selects `ARM_GICV3` under TCG | `hw/intc/Kconfig:24-32` |
| CPUs | A53s realized before the GIC (required by gicv3 realize); `has_el3=true`, `has_el2=true` already set — correct for v3, unchanged | `ti-am64x.c:797-841` |
| qtests | none touch GIC addresses or IRQ delivery — no expected breakage | test file review |
| `am64-virt.dtb` | pure convenience artifact for manual `-dtb`; not referenced by code, not built by meson — regenerate manually with dtc | grep + `pc-bios/dtb/meson.build` |
| SPI input indices | identical semantics on v2/v3 models (`qdev_get_gpio_in(gic, n)` = SPI n) — sec-proxy 34, main-uart0 178, mailbox loop 76-85, am64-virt.c peripherals 1-8 all stay untouched | QEMU GIC GPIO layout |

---

### Task 0: Baseline verification

**Files:** none.

**Interfaces:**
- Produces: confirmed-green baseline on the current branch HEAD, and the FluxOS `tiboot3.bin` present at `build/tiboot3.bin` (re-fetch from yoctoklaus-embedded `~cmbluadmin/devel/yocto/build_am64xx-cmblu-core-node-1/deploy-ti/images/am64xx-cmblu-core-node-1-k3r5/tiboot3.bin` if missing).

- [ ] **Step 1: Confirm branch + run the full net**

```bash
cd /home/sefo/devel/git/qemu
git branch --show-current      # expect: feat/am64-tiboot3-bootrom
cd build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
./tests/unit/test-k3-bootrom
QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin ./pyvenv/bin/meson test \
    --suite thorough func-aarch64-am64_bootrom --print-errorlogs
timeout 10 ./qemu-system-aarch64 -machine am64-virt -display none -serial null; echo "exit: $?"
```

Expected: 8/8, 6/6, functional OK (2 subtests), exit 124. No commit.

---

### Task 1: GICv3 in the SoC model (+ identity qtest)

**Files:**
- Modify: `include/hw/arm/ti-am64x.h` (GIC include + member type)
- Modify: `hw/arm/ti-am64x.c` (initfn type, realize props/map, PPI wiring)
- Modify: `tests/qtest/am64-virt-test.c`

**Interfaces:**
- Consumes: current GIC block locations listed in the fact table.
- Produces: `TIAM64xState.gic` of type `GICv3State`; GICD at `0x01800000`, GICR window at `0x01840000` (size `0x20000 × a53_cpus`); define `MAIN_GIC_REDIST_ADDRESS 0x01840000` replacing `MAIN_GIC_CPU_ADDRESS`. All consumer wiring (`qdev_get_gpio_in(DEVICE(&s->gic), n)`) keeps working unchanged — Tasks 2/3 and phase-3 work rely on exactly these addresses.

- [ ] **Step 1: Write the failing qtest**

Add to `tests/qtest/am64-virt-test.c`:

```c
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
```

Register in `main()`: `qtest_add_func("/am64-virt/gicv3", test_gicv3_present);`

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/sefo/devel/git/qemu/build && ninja tests/qtest/am64-virt-test
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
```

Expected: `/am64-virt/gicv3` FAILs (the v2 model has no PIDR2 with ArchRev 3 at either address; reads return 0 or v2 values).

- [ ] **Step 3: Switch the header**

`include/hw/arm/ti-am64x.h`: replace `#include "hw/intc/arm_gic.h"` with
`#include "hw/intc/arm_gicv3_common.h"` and change the member
`GICState gic;` → `GICv3State gic;`.

- [ ] **Step 4: Switch type, properties, and mapping in `ti-am64x.c`**

Replace `#include "hw/intc/arm_gic.h"` with
`#include "hw/intc/arm_gicv3_common.h"` (top of file). Add
`#include "qobject/qlist.h"` if not already present (check — the DMSC
thread QLists in this file already use it).

In `ti_am64x_initfn` (line ~74):

```c
    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GICV3);
```

Address defines (~line 42): replace `MAIN_GIC_CPU_ADDRESS` with:

```c
#define MAIN_GIC_DIST_ADDRESS   0x01800000ULL
#define MAIN_GIC_REDIST_ADDRESS 0x01840000ULL /* GIC-500 GICR, per TRM/DT */
```

Replace the creation block (`ti_am64x_realize`, currently lines 842-857):

```c
    DeviceState *gicdev = DEVICE(&s->gic);
    SysBusDevice *gicsbd = SYS_BUS_DEVICE(&s->gic);
    QList *redist_region_count;

    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", s->a53_cpus);
    qdev_prop_set_uint32(gicdev, "num-irq",
                         TI_AM64X_GIC_NUM_SPI + GIC_INTERNAL);
    qdev_prop_set_bit(gicdev, "has-security-extensions", true);
    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, s->a53_cpus);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);

    if (!sysbus_realize(gicsbd, errp)) {
        return;
    }
    sysbus_mmio_map(gicsbd, 0, MAIN_GIC_DIST_ADDRESS);
    sysbus_mmio_map(gicsbd, 1, MAIN_GIC_REDIST_ADDRESS);
```

(v2's `has-virtualization-extensions` line is dropped — v3 has no such
property. `has-security-extensions` flips to **true**: ATF needs it in
phase 3, and Linux runs non-secure regardless.)

- [ ] **Step 5: Rewrite the per-CPU PPI wiring**

Replace the timer-GPIO block (currently lines 859-873) inside the
per-CPU loop; the IRQ/FIQ/VIRQ/VFIQ `sysbus_connect_irq` block below it
stays byte-identical:

```c
        int ppibase = TI_AM64X_GIC_NUM_SPI + i * GIC_INTERNAL + GIC_NR_SGIS;
        const int timer_irq[] = {
            [GTIMER_PHYS] = INTID_TO_PPI(ARCH_TIMER_NS_EL1_IRQ),
            [GTIMER_VIRT] = INTID_TO_PPI(ARCH_TIMER_VIRT_IRQ),
            [GTIMER_HYP]  = INTID_TO_PPI(ARCH_TIMER_NS_EL2_IRQ),
            [GTIMER_SEC]  = INTID_TO_PPI(ARCH_TIMER_S_EL1_IRQ),
        };

        for (int j = 0; j < ARRAY_SIZE(timer_irq); j++) {
            qdev_connect_gpio_out(cpudev, j,
                                  qdev_get_gpio_in(gicdev,
                                                   ppibase + timer_irq[j]));
        }
        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                    0,
                                    qdev_get_gpio_in(gicdev,
                                        ppibase +
                                        INTID_TO_PPI(ARCH_GIC_MAINT_IRQ)));
```

**Do NOT touch** any `qdev_get_gpio_in(DEVICE(&s->gic), n)` consumer site
(sec-proxy 34, main-uart0 178, the mailbox `gic_map` loop) — SPI input
indices are identical on the v3 model. Note for the reviewer, not to fix:
the mailbox loop wires `spi - GIC_INTERNAL` while sec-proxy/uart0 pass raw
numbers — a pre-existing fork inconsistency; changing it is out of scope
for a behaviour-preserving migration (flag it in the commit message body
as a known quirk if you like, but leave the code).

- [ ] **Step 6: Check for MMIO overlap with the new redistributor window**

```bash
cd /home/sefo/devel/git/qemu
grep -n "0x0018[0-9a-fA-F]\|0x018[0-9a-fA-F]" hw/arm/ti-am64x.c | grep -i "unimp\|ADD_MAIN"
```

The redist window is `0x01840000`-`0x0187ffff` (2 CPUs). If any active
`ADD_MAIN_UNIMP` entry overlaps it (TRM blocks around the GIC), remove or
shrink that entry; commented-out entries stay untouched. Then confirm via
monitor: `echo -e 'info mtree -f\nquit' | ./build/qemu-system-aarch64 -M
am64-virt -S -display none -monitor stdio | grep -A2 018` shows dist and
redist regions without conflicting siblings.

- [ ] **Step 7: Build, run the new qtest + full regression net**

```bash
cd /home/sefo/devel/git/qemu/build && ninja
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
./tests/unit/test-k3-bootrom
QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin ./pyvenv/bin/meson test \
    --suite thorough func-aarch64-am64_bootrom --print-errorlogs
timeout 10 ./qemu-system-aarch64 -machine am64-virt -display none -serial null; echo "exit: $?"
timeout 5 ./qemu-system-aarch64 -machine cmblu-corenode -display none -serial null; echo "exit: $?"
```

Expected: 9/9 qtests (incl. `/am64-virt/gicv3`), 6/6 unit, functional OK,
both smoke runs exit 124. The ROM-boot functional test matters
especially: the R5 SPL path must be entirely GIC-agnostic.

- [ ] **Step 8: checkpatch + commit**

```bash
cd /home/sefo/devel/git/qemu
git add include/hw/arm/ti-am64x.h hw/arm/ti-am64x.c tests/qtest/am64-virt-test.c
git commit -m "feat(am64x): migrate soc to gicv3 at real gic-500 addresses"
git format-patch -1 --stdout | ./scripts/checkpatch.pl --no-signoff -
```

Expected: 0 errors (the generic MAINTAINERS warning for changed files is accepted).

---

### Task 2: Regenerate `pc-bios/dtb/am64-virt.dtb`

**Files:**
- Modify: `pc-bios/dtb/am64-virt.dts` (gic node)
- Modify: `pc-bios/dtb/am64-virt.dtb` (recompiled)

**Interfaces:**
- Consumes: GICv3 at `0x01800000`/`0x01840000` (Task 1).
- Produces: a `-dtb`-usable DT whose gic node matches the new model; Task 3's Linux boot uses exactly this file.

- [ ] **Step 1: Edit the gic node**

In `pc-bios/dtb/am64-virt.dts` (node `intc@1800000`, ~line 238), replace
the node body with (keep the existing `phandle` value):

```dts
	intc@1800000 {
		phandle = <0x8003>;
		reg = <0x00 0x1800000 0x00 0x10000
		       0x00 0x1840000 0x00 0x40000>;
		compatible = "arm,gic-v3";
		ranges;
		#size-cells = <0x02>;
		#address-cells = <0x02>;
		interrupt-controller;
		#interrupt-cells = <0x03>;
	};
```

(`0x40000` = 2 redistributors × `0x20000`.) The timer node's PPI numbers
(13/14/11/10) are already v3-correct — leave it. Leave every other node
untouched.

- [ ] **Step 2: Recompile**

```bash
command -v dtc || ls /usr/bin/dtc
cd /home/sefo/devel/git/qemu/pc-bios/dtb
dtc -I dts -O dtb -o am64-virt.dtb am64-virt.dts
```

If `dtc` is not installed, report BLOCKED (host package
`device-tree-compiler` needed) rather than improvising. Warnings about
missing `#address-cells` etc. are pre-existing dts style — acceptable if
the dtb builds.

- [ ] **Step 3: Sanity-check the dtb**

```bash
dtc -I dtb -O dts am64-virt.dtb | grep -A4 "arm,gic-v3"
```

Expected: the compiled dtb round-trips with `compatible = "arm,gic-v3"`
and the two reg tuples.

- [ ] **Step 4: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add pc-bios/dtb/am64-virt.dts pc-bios/dtb/am64-virt.dtb
git commit -m "feat(am64x): regenerate am64-virt dtb for gicv3"
```

---

### Task 3: Linux boot functional test (end-to-end IRQ proof)

**Files:**
- Modify: `tests/functional/aarch64/test_am64_bootrom.py` (new test method) — or a new `test_am64_linux.py` if the Asset pattern fits better there; keep it in one file unless the framework forces a split.
- Modify: `tests/functional/aarch64/meson.build` only if a new file is added.

**Interfaces:**
- Consumes: Task 2's dtb (path `pc-bios/dtb/am64-virt.dtb` relative to the source tree), the machine's direct-Linux path (`-kernel` + `-dtb`), PL011 UART0 at `0x09000000` (= `serial_hd(0)` console in non-ROM-boot mode).
- Produces: a functional test proving a mainline arm64 kernel initialises the GICv3 and reaches an interrupt-driven console — the spec's "direct-Linux boot keeps working" milestone.

- [ ] **Step 1: Find the kernel asset this tree already uses**

```bash
grep -rn "Asset(" tests/functional/aarch64/test_virt.py tests/functional/aarch64/test_xlnx_versal.py | head -5
```

Reuse an existing aarch64 kernel `Asset` (same URL+hash — the framework
caches downloads); a generic distro kernel has PL011 + GICv3 drivers
built in. Do not add a new external URL if an existing aarch64 kernel
asset exists in the tree.

- [ ] **Step 2: Write the failing test**

Add to `tests/functional/aarch64/test_am64_bootrom.py` (adapt the Asset
name to Step 1's finding; class attribute `ASSET_KERNEL = Asset(...)`
copied verbatim from the donor test):

```python
    def test_linux_gicv3(self):
        kernel_path = self.ASSET_KERNEL.fetch()
        dtb = self.build_file('pc-bios', 'dtb', 'am64-virt.dtb')
        self.set_machine('am64-virt')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb,
                         '-append', 'console=ttyAMA0 earlycon')
        self.vm.launch()
        wait_for_console_pattern(self, 'GICv3')
        wait_for_console_pattern(self, 'ttyAMA0')
```

(Helper for locating the dtb: check how other tests reference build/source
files — `self.build_file` exists in this framework for files under the
build dir; pc-bios dtbs are copied into the build dir. Verify with
`grep -rn "build_file" tests/functional/qemu_test/*.py` and adjust;
fallback: path relative to the source tree via `os.path`.)

- [ ] **Step 3: Run to verify current behavior**

```bash
cd /home/sefo/devel/git/qemu/build
./pyvenv/bin/meson test --suite thorough func-aarch64-am64_bootrom --print-errorlogs
```

On the Task-1/2 code this should PASS immediately (the migration is
done); the "failing" state for this test is the pre-Task-1 tree, so run
it once to confirm it passes now and — important — check the log
actually contains the kernel's `GICv3:` init line, not a coincidental
match. If the asset needs network and the environment has none, the
framework skips — note that in the report.

- [ ] **Step 4: Full regression + commit**

```bash
QTEST_QEMU_BINARY=./qemu-system-aarch64 ./tests/qtest/am64-virt-test
QEMU_TEST_TIBOOT3=$PWD/tiboot3.bin ./pyvenv/bin/meson test \
    --suite thorough func-aarch64-am64_bootrom --print-errorlogs
cd /home/sefo/devel/git/qemu
git add tests/functional/aarch64/test_am64_bootrom.py tests/functional/aarch64/meson.build
git commit -m "test(am64x): boot linux kernel to verify gicv3 delivery"
git format-patch -1 --stdout | ./scripts/checkpatch.pl --no-signoff -
```

---

### Task 4: Spec bookkeeping

**Files:**
- Modify: `docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md`

**Interfaces:**
- Consumes: Tasks 1-3 outcomes.
- Produces: spec Phase 1 marked implemented with the as-built facts.

- [ ] **Step 1: Update the spec**

In the Phase 1 section add a short "As built (<date>)" note: GICD
`0x01800000`, GICR `0x01840000` (2 × `0x20000`), security extensions on,
maintenance IRQ wired, ITS not modelled (deferred until PCIe-MSI need),
dtb regenerated, Linux functional test added. Resolve the phase-1 open
item in "Open items pinned during planning".

- [ ] **Step 2: Commit**

```bash
cd /home/sefo/devel/git/qemu
git add docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md
git commit -m "docs: record gicv3 migration as-built state"
```

---

## Self-review notes (done at plan time)

- **Spec coverage (Phase 1):** GICv3 at real addresses → Task 1; rewire
  interrupt sources → Task 1 Steps 5 (PPIs change) + explicit
  keep-unchanged instruction for SPIs; regenerate dtb → Task 2; "all
  existing tests green" → every task's regression steps; "direct-Linux
  boot keeps working" → Task 3 (upgraded from manual check to a
  functional test). Phase-1 open item (exact GIC map) resolved in the
  fact table from `k3-am64-main.dtsi`.
- **Type consistency:** `GICv3State gic` (header) ↔ `TYPE_ARM_GICV3`
  (initfn) ↔ `MAIN_GIC_REDIST_ADDRESS` used in realize and referenced by
  the qtest constants (`0x01840000`).
- **Known uncertainty, by design:** the functional-test file-location
  helper (`build_file` vs source-relative path) and the kernel Asset
  choice have explicit discovery steps; redist-window overlap has a
  grep + mtree verify step.
