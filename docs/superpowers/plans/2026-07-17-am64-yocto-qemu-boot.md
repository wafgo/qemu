# AM64x Yocto/meta-cmblu QEMU-Boot Implementation Plan (Full-Boot-Chain Phase 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The **same FluxOS WIC** boots to a Linux login prompt in QEMU
(`am64-virt`) and unchanged on a real phyCORE-AM64x CoreNode — QEMU is
detected in u-boot via the emulated DMSC's TISCI firmware description and
selects a QEMU-target device tree.

**Architecture (revised 2026-07-17 after the Task-1 de-risk gate).** The
original "boot the stock DTB unchanged, no new DT artifact" premise was
**disproven**: the stock `k3-am642-cmblu-battery-node.dtb` enables the AM64
I2C controllers, which QEMU models only as read-as-zero stubs; the
`omap-i2c` driver reads revision 0, derives a bogus register map, and takes
a fatal unaligned-access Oops that kills deferred probe → SDHCI never
probes → `rootwait` hangs (never reaches login). Modelling every unmodelled
peripheral (I2C, CPSW, PCIe, ICSSG/PRU, McSPI, USB…) is an unbounded
rathole and is explicitly rejected. Instead: ship a small **QEMU disable
overlay** `k3-am642-qemu-disable.dtbo` that sets every node QEMU does NOT
model to `status="disabled"`, keeping only the modelled set (A53, GICv3,
UART0, both SDHCI, DDR, arch timer, sec-proxy/mailbox/DMSC). A one-line
u-boot patch exports the QEMU marker as `is_qemu`; the boot env applies the
disable overlay only when `is_qemu=1`. HW (`is_qemu=0`) is byte-identical
to today. "One image, two targets" holds — HW uses the stock DTB, QEMU the
same DTB + the disable overlay, both selected at runtime from the one WIC.

**Tech Stack:** meta-cmblu (Yocto layer, gitlab.cmblu.net) + the cmblu
kernel fork for the overlay source, u-boot-phytec 2025.01-phy2, bitbake on
yoctoklaus-embedded, QEMU fork (branch `cmblu/corenode`) to drive/observe.

**Spec:** `docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md`
(Phase 4). This plan supersedes the spec's Phase-4 sketch on two points,
per user decisions: (a) Linux is a plain `Image` + raw `oftree` DTB +
`.dtbo` overlays (NOT a fitImage), so DT selection is via `fdtfile`/
`overlays` env, not a FIT config node; (b) a QEMU-target DT **is** required
(a disable overlay), because the stock DTB faults in QEMU — this reverses
the earlier "minimal, no new DTB" decision on the evidence of the I2C Oops.

## Global Constraints

- **Repos.** QEMU fork `~/devel/git/qemu` (branch `cmblu/corenode`) drives/observes and holds the functional test + spec docs. The product changes land in **meta-cmblu** (authoritative checkout on `yoctoklaus-embedded:~cmbluadmin/devel/yocto/sources/meta-cmblu`, remote `git@gitlab.cmblu.net:software/power-pilot-next/secondary-bms/meta-cmblu.git`) and possibly the **cmblu kernel fork** (`git@gitlab.cmblu.net:power-pilot/firmware/batterynode/linux-phytec.git`, branch `cmblu/devel/master`) if the overlay source must live beside the other DT overlays. Prefer keeping the overlay in meta-cmblu (compiled by a recipe) to keep Phase 4 in one product repo — Task 3 confirms the mechanism. **Never push; the user pushes / opens MRs.** meta-phytec is upstream, do not modify.
- **Builds run on yoctoklaus-embedded** (`ssh yoctoklaus-embedded`, user cmbluadmin), never locally. See the `cmblu-yocto-image` skill for exact bitbake syntax. Artifacts land in `~cmbluadmin/devel/yocto/build_am64xx-cmblu-core-node-1/deploy-ti/images/am64xx-cmblu-core-node-1/`.
- **Hard product-safety constraint:** the change ships to real hardware. On HW `is_qemu=0` → the disable overlay is NOT applied and the boot path is byte-identical to today. Real-HW acceptance is **user-assisted** (the agent cannot flash a CoreNode). If a UANT is used, the neoflux `test_running` MQTT deploy gate applies.
- **QEMU marker is a substring:** the DMSC returns `"QEMU_TI_DMSC (Wadims DMSC)"` (`hw/misc/ti-dmsc.c:1182`); detection uses `strstr(desc, "QEMU_TI_DMSC")`, never equality.
- Commit style (meta-cmblu / kernel fork follow neoflux Conventional Commits): `<type>(<scope>): <subject>`, ≤72 chars, lowercase, **no AI co-author trailers**.
- Fast local loop for the DT work: decompile/patch/recompile the DTB with `dtc`, put it on the WIC FAT with `mtools` (`mcopy -o -i <wic>@@$((16384*512)) …`) or apply an overlay live at the u-boot prompt (`fdt addr`, `fdt apply`), and boot with the pexpect console driver from Task 0 (`scratchpad/drive_uboot_console.py`). No Yocto build is needed to converge the DT delta.
- QEMU sd-card needs a 512K-multiple image → qcow2 overlay (`qemu-img create -f qcow2 -b <wic> -F raw … <rounded-size>`), as in `tests/functional/aarch64/test_am64_bootrom.py`.

## Verified reference facts (research 2026-07-17 + Task-1 gate — do not re-derive)

| Fact | Value |
| --- | --- |
| **Task-1 finding (the pivot)** | stock `oftree` DTB → Linux Oops at ~2.9s in `omap-i2c __omap_i2c_init` (unaligned access on read-as-zero I2C stub) → deferred-probe kworker dies → SDHCI never probes → `rootwait` hang. u-boot side is correct (see env below); crash precedes MMC. |
| QEMU-modelled peripherals (keep `okay`) | 2×A53 + GICv3 (GICD 0x01800000 / GICR 0x01840000), main UART0 `serial@2800000` (=`ttyS2`), both SDHCI (`mmc@fa10000` eMMC, `mmc@fa00000` SD), DDR (`memory@80000000`, 2 GiB — matches am64-virt default), arch timer, sec-proxy/mailbox/DMSC (TISCI). Everything else → `disabled`. |
| Known crash node | `i2c@20000000` (+ the other `i2c@2xxxxxxx` instances) `compatible = ti,am64-i2c`/`ti,omap4-i2c`. Proactively also disable: `ethernet@8000000` (cpsw), `pcie@…`, `icssg`/PRU nodes, McSPI, USB — converge the exact list empirically in Task 2. |
| Boot flow (HW, unchanged) | `bootcmd=run ${boot}boot; …`; `board_late_init()` sets `boot=mmc`; `mmcboot=run mmcargs; mmc dev ${mmcdev}; mmc rescan; run mmcloadimage; run mmcloadfdt; run mmc_apply_overlays; booti …` |
| Verified working QEMU env (Task 1) | `mmcdev=1 mmcpart=1 fdtfile=oftree` (SD = `mmc@fa00000` = index 1; p1 FAT, p2 ext4); `bootargs=console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000 root=/dev/mmcblk1p2 rootwait rw`; `kernel_addr_r=0x82000000 fdt_addr_r=0x88000000`; HW-variant overlays are empty/skipped under QEMU |
| Overlay-apply machinery | `mmc_apply_overlays` iterates `${overlays}` and `fdt apply`s each `.dtbo` from the FAT — the existing hook the QEMU disable overlay rides on (set `overlays=k3-am642-qemu-disable.dtbo` when `is_qemu=1`) |
| Kernel format | plain `Image` (KERNEL_IMAGETYPE=Image, UBOOT_SIGN_ENABLE=0); DTB `oftree`≡`k3-am642-cmblu-battery-node.dtb`; DTS source in the cmblu kernel fork; 6 HW `.dtbo` already shipped on the FAT via `KERNEL_OVERLAYS` |
| u-boot patch site | `board/phytec/common/k3/board.c :: board_late_init()` (A53, `CONFIG_BOARD_LATE_INIT=y`; already `env_set("boot",…)`); desc reachable via `get_ti_sci_handle()->version.firmware_description` (`drivers/firmware/ti_sci.c`) |
| meta-cmblu u-boot bbappend | `recipes-bsp/u-boot/u-boot-phytec_%.bbappend` + files dir `recipes-bsp/u-boot/u-boot-phytec/` (already carries the NeoVisor patch + a `.cfg` — the model to copy) |
| Machine conf | `meta-cmblu/conf/machine/am64xx-cmblu-core-node-1.conf` (KERNEL_DEVICETREE, KERNEL_OVERLAYS, UBOOT_MACHINE=phycore_am64x_a53_defconfig) |
| Decompiled stock DTS (for the disable list) | `/tmp/claude-1000/-home-sefo-devel-git-neoflux/60fe955a-7832-4f61-b6be-bf3c7aa65d03/scratchpad/cmblu.dts` |

---

### Task 0: u-boot prompt baseline — DONE

Reached the interactive u-boot `=>` prompt in QEMU; pexpect console driver at `scratchpad/drive_uboot_console.py`. (Ledger: complete, no commit.)

### Task 1: De-risk gate — DONE (finding drove the pivot)

Established that the stock DTB faults at I2C (see the facts table). u-boot env recipe verified correct. This is why the plan now uses a QEMU disable overlay. (Ledger: DONE_WITH_CONCERNS → pivot approved by user.)

---

### Task 2: Converge the QEMU disable overlay interactively (local, no Yocto)

**Files:**
- Create: `scratchpad/k3-am642-qemu-disable.dtso` (working source, promoted to the product repo in Task 3)
- Produce: the converged disabled-node list + a `.dtbo`/patched `.dtb` that boots Linux to login in QEMU.

**Interfaces:**
- Consumes: Task 0's console driver; the decompiled stock DTS; the verified QEMU env.
- Produces: `scratchpad/qemu-disable-list.md` — the exact set of nodes set to `status="disabled"` — and a proven-booting artifact. Task 3 ships exactly this list.

- [ ] **Step 1: Author a first disable overlay**

From the decompiled DTS, write `scratchpad/k3-am642-qemu-disable.dtso` disabling the known-and-suspected unmodelled nodes by label or path — at minimum all I2C, plus cpsw ethernet, pcie, icssg/PRU, mcspi, usb. Overlay form:

```dts
/dts-v1/;
/plugin/;
&main_i2c0 { status = "disabled"; };
&main_i2c1 { status = "disabled"; };
/* … all i2c instances … */
&cpsw3g   { status = "disabled"; };   /* ethernet@8000000 */
&pcie0_rc { status = "disabled"; };
/* icssg / pru / mcspi / usb labels as found in the DTS … */
```
(Use the exact labels/paths from `scratchpad/cmblu.dts`. If a node has no label, use its full path in an `&{/path}` override.) Compile: `dtc -@ -I dts -O dtb -o /tmp/qemu-disable.dtbo scratchpad/k3-am642-qemu-disable.dtso`.

- [ ] **Step 2: Boot with the overlay applied, iterate to login**

Two equivalent ways to apply it; prefer (a) for fast iteration:
- (a) **Live at the u-boot prompt:** load Image, oftree, and the overlay, then `fdt`-apply and boot:
  ```
  load mmc 1:1 ${kernel_addr_r} Image
  load mmc 1:1 ${fdt_addr_r} oftree
  load mmc 1:1 0x8a000000 k3-am642-qemu-disable.dtbo   # after mcopy'ing it onto the FAT
  fdt addr ${fdt_addr_r}; fdt resize 8192; fdt apply 0x8a000000
  setenv bootargs console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000 root=/dev/mmcblk1p2 rootwait rw
  booti ${kernel_addr_r} - ${fdt_addr_r}
  ```
  (mcopy the overlay onto a qcow2-overlay copy's FAT, or onto a scratch raw WIC copy, with `mtools`.)
- (b) Or bake the disables into a copy of the DTS, recompile to a `.dtb`, `mcopy` as `oftree`, and boot plain.

Drive it with the pexpect harness. Expect: past the old 2.9s I2C Oops, MMC/SDHCI enumeration, rootfs mount from `mmcblk1p2`, systemd, **login prompt**. If it dies at the *next* active-on-garbage driver (same signature: fault/hang in some `*_init`/`*_probe`), add that node to the disable list and re-run. Converge (expect ≤ a handful of iterations — the modelled set is small and known). Record every node added and why.

- [ ] **Step 3: Record the converged result**

Write `scratchpad/qemu-disable-list.md`: the final disabled-node set, the boot log excerpt showing the login prompt, and the final `.dtso`. If, after disabling everything unmodelled down to the kept set, Linux still cannot reach login (e.g. a *modelled* peripheral misbehaves, or rootfs won't mount), STOP and report — that is a QEMU-model gap, not a DT issue, and needs its own decision. No commit (scratch only; promotion happens in Task 3).

---

### Task 3: meta-cmblu — u-boot is_qemu detection, disable overlay, env selection

**Files (yoctoklaus meta-cmblu checkout; confirm write access first — else stage diffs in scratchpad for the user):**
- Create: `recipes-bsp/u-boot/u-boot-phytec/0002-board-phytec-export-is-qemu-env.patch`
- Create: the overlay source `k3-am642-qemu-disable.dtso` in its chosen home (meta-cmblu recipe-provided, or the kernel fork's `arch/arm64/boot/dts/ti/` + `KERNEL_DEVICETREE`/overlays) — Task 3 Step 1 decides which and documents it.
- Modify: `recipes-bsp/u-boot/u-boot-phytec_%.bbappend`; the env source or a `.env` fragment; `IMAGE_BOOT_FILES`/`KERNEL_OVERLAYS` so the `.dtbo` lands on the FAT.

**Interfaces:**
- Consumes: Task 2's converged `.dtso` + disable list; the marker substring.
- Produces: a built image where, under QEMU, `is_qemu=1` → `overlays` includes `k3-am642-qemu-disable.dtbo` → Linux boots; on HW `is_qemu=0` → unchanged.

- [ ] **Step 1: Decide + set the overlay's home**

Determine the lightest mechanism that gets a `.dtso` compiled to a `.dtbo` onto the FAT boot partition: (a) add the `.dtso` to the cmblu kernel fork beside the existing overlays and to `KERNEL_DEVICETREE`/`KERNEL_OVERLAYS` (consistent with the 6 existing `.dtbo`, but a second repo); or (b) a meta-cmblu recipe/bbappend that ships + compiles the `.dtso` and appends the `.dtbo` to `IMAGE_BOOT_FILES` (keeps Phase 4 in one repo). Pick (b) if feasible; document the choice in the report. Place Task 2's converged `.dtso` there verbatim.

- [ ] **Step 2: u-boot is_qemu patch**

`git format-patch`-style patch to `board/phytec/common/k3/board.c` `board_late_init()` (after the existing `env_set("boot",…)`), guarded to be a clean no-op off-QEMU:

```c
	{
		const struct ti_sci_handle *ti_sci = get_ti_sci_handle();

		if (!IS_ERR_OR_NULL(ti_sci)) {
			const char *desc = ti_sci->version.firmware_description;

			env_set("is_qemu", strstr(desc, "QEMU_TI_DMSC") ? "1" : "0");
		}
	}
```
(Verify the `get_ti_sci_handle()` return/`IS_ERR_OR_NULL` idiom and needed include against the real tree; mirror `k3_sysfw_print_ver()`.) Generate against SRCREV `6980061…` so it applies cleanly. Wire into the bbappend: `SRC_URI += "file://0002-board-phytec-export-is-qemu-env.patch"`.

- [ ] **Step 3: Env — apply the overlay only under QEMU**

Add a QEMU hook that, when `is_qemu=1`, sets `overlays` to the disable overlay before `mmc_apply_overlays` runs (encode exactly Task 2's result — likely `setenv overlays k3-am642-qemu-disable.dtbo`, replacing the HW-variant list which is empty/irrelevant under QEMU). Keep it a strict no-op when `is_qemu != 1` (HW path unchanged). Prefer the recipe's env-append mechanism over patching the upstream `.env`; if none exists, fold a minimal `qemu_setup` into the same u-boot patch and have `mmcboot` `run qemu_setup` first.

- [ ] **Step 4: Commit on feature branches (no push)**

meta-cmblu: `git checkout -b feat/qemu-boot-detection`; commit the patch + bbappend + overlay/recipe: `feat(u-boot): detect qemu and apply qemu disable overlay`. If the overlay went to the kernel fork, a matching feature-branch commit there. Do not push.

---

### Task 4: yoctoklaus build + end-to-end QEMU autoboot + gated test

**Files:** functional test in the QEMU repo.

**Interfaces:**
- Consumes: Task 3's committed product changes.
- Produces: a rebuilt `tiboot3.bin` + WIC that autoboots (no keypress) from power-on to a Linux login prompt in QEMU; a gated functional test.

- [ ] **Step 1: Build on yoctoklaus** — `bitbake u-boot-phytec` (+ the `k3r5` multiconfig for `tiboot3.bin`) and, if the overlay is kernel-side, `bitbake linux-cmblu`; then reassemble the WIC (`bitbake cmblu-headless-image -c do_image_wic`) or fast-path `mcopy` the new bootloader blobs + the `.dtbo` onto a copy of the existing WIC's FAT. Pull artifacts to the local machine.

- [ ] **Step 2: Full autoboot in QEMU** — boot the new `tiboot3.bin` + WIC (qcow2 overlay) with NO keypress; expect chain → `is_qemu=1` → `mmc_apply_overlays` applies the disable overlay → `Booting Linux` → rootfs mount → **login prompt**. Capture the tail to the report.

- [ ] **Step 3: Gated functional test** — in `~/devel/git/qemu/tests/functional/aarch64/test_am64_bootrom.py`, add a gated method (`QEMU_TEST_WIC_P4`, `QEMU_TEST_TIBOOT3_P4`) that autoboots and waits for `Booting Linux on physical CPU` then the login/shell marker observed in Step 2. Commit to the QEMU repo (`cmblu/corenode`): `test(am64x): gated functional test for full linux boot`; checkpatch.

---

### Task 5: HW no-op review, real-HW acceptance (user-assisted), docs + MR

**Files:** spec doc in the QEMU repo.

**Interfaces:**
- Consumes: everything above.
- Produces: the project done-criterion with HW verification recorded; MR-ready meta-cmblu (and kernel-fork, if used) branches.

- [ ] **Step 1: HW no-op review** — confirm on HW `is_qemu=0`, so `overlays` keeps its HW default, the disable overlay is never applied, and the only always-on effect is one extra `env_set`. Write the reasoning into the report.
- [ ] **Step 2: Real-HW acceptance (user-assisted)** — prepare a precise checklist: deploy the rebuilt bootloader blobs to a CoreNode (respect the UANT `test_running` gate if a UANT is used), confirm it boots to Linux exactly as before, `fw_printenv is_qemu` = `0`, boot path/overlays unchanged. Record the user's result; do NOT mark the phase done until HW boot is confirmed unchanged.
- [ ] **Step 3: Spec as-built + MR prep** — spec Phase 4 → `[IMPLEMENTED <date>]` with the as-built note: the two spec corrections (no fitImage; a QEMU disable overlay IS needed — the stock DTB faults at I2C), the converged disable-node list, the `is_qemu` mechanism, and the HW-unchanged confirmation. Resolve the Ph.-4 open item. Prepare the meta-cmblu (and kernel-fork) MR via the GitLab MCP tools with a linked SOE Jira ticket — **only when the user confirms HW acceptance and asks to open it.** Commit the spec doc: `docs: record phase 4 qemu-target-dtb as-built`.

---

## Self-review notes

- **Pivot recorded:** the stock-DTB approach is retired with the concrete Task-1 evidence (I2C Oops); modelling peripherals is explicitly out (unbounded); the disable overlay is the bounded, converging answer. All in the Architecture + facts table.
- **Spec coverage:** QEMU detection via TISCI `is_qemu` → Task 3; DT selection → Task 3 env (overlays) not a FIT node (spec correction); root stays `/dev/mmcblk*` → Task 1 verified env; same WIC both targets → Tasks 4 (QEMU) + 5 (HW); the dedicated QEMU DT is a disable overlay (lighter than a parallel DTS), converged in Task 2.
- **De-risk first, still:** Task 2 converges the whole DT delta locally with zero Yocto builds; a remaining hang after disabling everything unmodelled is an explicit stop (QEMU-model gap, not DT).
- **Execution reality:** remote builds (yoctoklaus), user-assisted HW acceptance, up to three product repos (meta-cmblu, kernel fork, QEMU test) — flagged in Global Constraints.
