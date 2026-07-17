# AM64x Yocto/meta-cmblu QEMU-Boot Implementation Plan (Full-Boot-Chain Phase 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The **same unmodified FluxOS WIC** boots to a Linux login prompt in
QEMU (`am64-virt`) and unchanged on a real phyCORE-AM64x CoreNode — QEMU is
detected in u-boot via the emulated DMSC's TISCI firmware description.

**Architecture:** Minimal, kernel-artifact-free (decided with the user
2026-07-17). The Linux DTB, kernel and boot flow ship unchanged; the only
product change is a small u-boot patch + env in the existing meta-cmblu
u-boot bbappend: `board_late_init()` exports the TISCI firmware description
as an env var, and the boot env branches on the `QEMU_TI_DMSC` marker to
run the normal MMC boot path under QEMU (skipping HW-EEPROM-variant
overlays). The stock cmblu DTB boots on `am64-virt` because console/GICv3/
SDHCI/TISCI already match the emulated SoC (phases 1-3).

**Tech Stack:** meta-cmblu (Yocto layer, gitlab.cmblu.net), u-boot-phytec
2025.01-phy2, bitbake on yoctoklaus-embedded, QEMU fork (branch
`cmblu/corenode`, phases 1-3 done) for driving the boot.

**Spec:** `docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md`
(Phase 4). **Two spec assumptions were disproven by research and this plan
overrides them, per the user's 2026-07-17 decision:** (a) Linux is NOT a
fitImage — it is a plain `Image` + raw `oftree` DTB + `.dtbo` overlays, so
there is no FIT config node to add; (b) no dedicated `k3-am642-qemu-corenode.dts`
is built — the stock `k3-am642-cmblu-battery-node.dtb` is used as-is.

## Global Constraints

- **Two repos.** QEMU fork `~/devel/git/qemu` (branch `cmblu/corenode`, read-only here except docs) is only used to *drive/observe* the boot. The product change lands in **meta-cmblu** — authoritative checkout on `yoctoklaus-embedded` at `~cmbluadmin/devel/yocto/sources/meta-cmblu` (git remote `git@gitlab.cmblu.net:software/power-pilot-next/secondary-bms/meta-cmblu.git`). Work on a feature branch off the current tip; **never push** — the user pushes / opens the MR manually (same discipline as neoflux). meta-phytec is upstream, **do not modify**.
- **Builds run on yoctoklaus-embedded** (`ssh yoctoklaus-embedded`, user cmbluadmin). Do NOT build locally. u-boot-only rebuild: `bitbake u-boot-phytec` **plus** the k3r5 multiconfig (`am64xx-cmblu-core-node-1-k3r5`) so `tiboot3.bin`/`tispl.bin`/`u-boot.img` are re-emitted; artifacts land in `~cmbluadmin/devel/yocto/build_am64xx-cmblu-core-node-1/deploy-ti/images/am64xx-cmblu-core-node-1/`. Consult the `cmblu-yocto-image` skill for exact bitbake invocation.
- **Hard product-safety constraint:** the u-boot change ships to real hardware too. It must be a no-op on HW (env var set, boot path unchanged when the marker is absent). Real-HW acceptance is **user-assisted** — the agent cannot flash/boot a CoreNode; it prepares the change and the verification steps, the user runs them on hardware. **Deploy gate:** if a UANT is used for HW verification, the `test_running` MQTT check from neoflux/CLAUDE.md applies — never flash a UANT mid-test.
- **QEMU marker is a substring:** the emulated DMSC returns `"QEMU_TI_DMSC (Wadims DMSC)"` (`hw/misc/ti-dmsc.c:1182`), so all detection must use a substring/`strstr` match on `QEMU_TI_DMSC`, never string equality.
- Commit style (meta-cmblu follows neoflux Conventional Commits): `<type>(<scope>): <subject>`, ≤72 chars, lowercase, **no AI co-author trailers**. u-boot patch files carry a normal author line, no AI attribution.
- Artifacts for QEMU driving: `~/devel/git/qemu/build/tiboot3.bin` and `build/fluxos.wic` (SD image; FAT boot partition at sector 16384). QEMU sd-card needs a 512K-multiple image → use the qcow2-overlay trick already in `tests/functional/aarch64/test_am64_bootrom.py`.

## Verified reference facts (research 2026-07-17 — do not re-derive)

| Fact | Value |
| --- | --- |
| Linux boot flow (HW) | phytec env: `bootcmd=run ${boot}boot; bootflow scan -lb`; `board_late_init()` sets `boot=mmc`; `mmcboot=run mmcargs; mmc dev ${mmcdev}; mmc rescan; run mmcloadimage; run mmcloadfdt; run mmc_apply_overlays; booti ${kernel_addr_r} - ${fdt_addr_r}` |
| Load addrs / env | `kernel_addr_r=0x82000000`, `fdt_addr_r=0x88000000`, `fdtfile=oftree`, `mmcdev=1`, `mmcpart=1`, `mmcroot=2`, `console=ttyS2,115200n8`, `earlycon=ns16550a,mmio32,0x02800000` |
| Kernel format | **plain `Image`** (KERNEL_IMAGETYPE=Image, UBOOT_SIGN_ENABLE=0). NOT a fitImage |
| DTB on FAT | `oftree` ≡ `ti/k3-am642-cmblu-battery-node.dtb` (byte-identical); + 6 `.dtbo` overlays. DTS source = cmblu kernel fork `gitlab.cmblu.net/power-pilot/firmware/batterynode/linux-phytec.git` (not in meta-cmblu) |
| FAT boot partition | `Image`, `oftree`, `k3-am642-cmblu-battery-node.dtb`, 6 `.dtbo`, `TIBOOT3.BIN`, `TISPL.BIN`, `U-BOOT.IMG`, `EFI/` — at sector 16384, FAT16 "boot" |
| root device | HW: `root=/dev/mmcblk1p2` (mmcdev=1). QEMU: WIC on sdhci1 (SD); DT aliases `mmc0=&sdhci0`(eMMC) `mmc1=&sdhci1`(SD) → SD should enumerate as `mmcblk1` → same `p2`. Confirm empirically in Task 1 |
| DMSC firmware desc reachable | Stored in `get_ti_sci_handle()->version.firmware_description` (`drivers/firmware/ti_sci.c`); printed by `arch/arm/mach-k3/common.c:k3_sysfw_print_ver()`; NOT exported to env today |
| Patch site | `board/phytec/common/k3/board.c :: board_late_init()` (runs on A53, `CONFIG_BOARD_LATE_INIT=y`; already calls `env_set("boot", …)`) |
| u-boot default env source | compiled from `board/phytec/phycore_am64x/phycore_am64x.env` (`CONFIG_ENV_SOURCE_FILE`) |
| meta-cmblu u-boot bbappend | `recipes-bsp/u-boot/u-boot-phytec_%.bbappend` + files dir `recipes-bsp/u-boot/u-boot-phytec/` (already carries the NeoVisor SPL patch + early-neovisor.cfg — the model to copy) |
| Machine conf | `meta-cmblu/conf/machine/am64xx-cmblu-core-node-1.conf` (KERNEL_DEVICETREE, UBOOT_MACHINE=phycore_am64x_a53_defconfig) |
| Unmodeled-but-nonfatal DT nodes | `ethernet@8000000` (cpsw), `i2c@20000000` (PMIC/RTC/EEPROM), McSPI, PCIe — drivers fail/defer probe, no panic. EEPROM read fails → 2 GB DDR default (matches am64-virt's 2 GiB default RAM) |
| QEMU full-chain invocation | `qemu-system-aarch64 -M am64-virt -bios tiboot3.bin -drive if=sd,format=raw,file=fluxos.wic -serial stdio -display none` (currently reaches u-boot `=>`) |

---

### Task 0: Baseline — reach the u-boot prompt in QEMU

**Files:** none (driving only).

**Interfaces:**
- Produces: a confirmed interactive u-boot `=>` prompt in QEMU off the current `build/tiboot3.bin` + `build/fluxos.wic`, with a known way to type commands (serial stdio + a pexpect/socket driver).

- [ ] **Step 1: Boot to the u-boot prompt and get an interactive console**

```bash
cd /home/sefo/devel/git/qemu/build
qemu-img create -f qcow2 -b "$PWD/fluxos.wic" -F raw /tmp/p4.qcow2 \
    $(( ( $(stat -c%s fluxos.wic) + 0x7ffff ) & ~0x7ffff ))
./qemu-system-aarch64 -M am64-virt -bios tiboot3.bin \
    -drive if=sd,format=qcow2,file=/tmp/p4.qcow2 \
    -serial mon:stdio -display none
```
Expected: the full chain runs to `Hit any key to stop autoboot`; press a key within the countdown to drop to `=>`. Confirm you can type and see `version` output. (For scripted control, drive via a pty/pexpect wrapper in the scratchpad — the functional test's console handling is a reference.)

No commit.

---

### Task 1: Prove Linux boots from the WIC at the u-boot prompt (no code change)

**This is the de-risking task — it validates the entire minimal premise before any Yocto work, and produces the exact env the Task 2 patch must encode.**

**Files:** none (interactive verification; capture findings to the report).

**Interfaces:**
- Consumes: Task 0's u-boot prompt.
- Produces: (a) confirmation that the stock `oftree` DTB boots Linux on `am64-virt` to a login prompt; (b) the exact, minimal command/env sequence that works under QEMU (which `mmcdev`/`mmcpart`, whether the `.dtbo` overlays must be skipped, the working `root=`); (c) if it does NOT boot, the precise failure (hang vs panic vs probe-noise) — a genuine hang is an escalation trigger per the user's minimal-only decision.

- [ ] **Step 1: Manually run the MMC boot path**

At the `=>` prompt, first inspect what u-boot sees, then drive the documented path explicitly:

```
=> mmc list
=> mmc dev 1 && mmc rescan && mmc part
=> setenv fdtfile oftree
=> load mmc 1:1 ${kernel_addr_r} Image
=> load mmc 1:1 ${fdt_addr_r} oftree
=> setenv bootargs console=ttyS2,115200n8 earlycon=ns16550a,mmio32,0x02800000 root=/dev/mmcblk1p2 rootwait rw
=> booti ${kernel_addr_r} - ${fdt_addr_r}
```
Expected: kernel decompresses, `Booting Linux on physical CPU 0x0`, GICv3 init, ttyS2 console, mmcblk enumeration, rootfs mount from `mmcblk1p2`, systemd/login. Capture the full boot log to the report.

- [ ] **Step 2: Resolve the two likely snags empirically**

- **mmc index / root:** if `mmc dev 1` / `mmcblk1` is wrong under QEMU, try `mmc list` output and `mmc dev 0`; determine which index the WIC (sdhci1) actually is and which `/dev/mmcblkNp2` mounts. Record the working values.
- **overlays:** the HW path runs `mmc_apply_overlays` over `${overlays}`. Determine whether `${overlays}` is empty/needs skipping under QEMU (the `.dtbo` are HW-EEPROM-variant overlays). Boot WITHOUT applying overlays first (as above); only investigate overlays if boot fails. Record whether overlays must be explicitly skipped.
- If Linux **hangs** (not just probe-error spam for cpsw/pcie/i2c, which is expected and non-fatal): capture where, and STOP — report it as the escalation trigger (the minimal premise failed; the user must decide on a DTB/overlay per the earlier options). Do NOT silently pivot to building a DTB.

- [ ] **Step 3: Record the working recipe**

Write to the report the exact minimal env delta that made autoboot → Linux work under QEMU, expressed as u-boot env assignments (this is verbatim what Task 2 bakes in). Typically: ensure `boot=mmc`, `mmcdev`/`mmcpart` correct, `fdtfile=oftree`, `overlays=` empty (skip), `bootargs` root device. No commit (QEMU repo is not the product).

---

### Task 2: meta-cmblu u-boot QEMU-detection patch + env

**Files (on the yoctoklaus meta-cmblu checkout):**
- Create: `recipes-bsp/u-boot/u-boot-phytec/0002-board-phytec-export-sysfw-desc-and-qemu-env.patch`
- Modify: `recipes-bsp/u-boot/u-boot-phytec_%.bbappend`

**Interfaces:**
- Consumes: Task 1's verified working env delta; the DMSC marker substring `QEMU_TI_DMSC`.
- Produces: a patched u-boot that, in `board_late_init()`, sets `env_set("sysfw_desc", <firmware_description>)` and `env_set("is_qemu", strstr(desc,"QEMU_TI_DMSC") ? "1" : "0")`, and default env that branches on `is_qemu` to apply exactly Task 1's delta before the normal `mmcboot`. Consumed by Task 3 (build) and Task 4 (HW check).

- [ ] **Step 1: Create the u-boot source patch**

Add to `board/phytec/common/k3/board.c` inside `board_late_init()` (after the existing `env_set("boot", …)`), guarded so a missing/!QEMU handle is a clean no-op:

```c
	{
		const struct ti_sci_handle *ti_sci = get_ti_sci_handle();

		if (!IS_ERR_OR_NULL(ti_sci)) {
			char desc[sizeof(ti_sci->version.firmware_description) + 1] = {0};

			strncpy(desc, ti_sci->version.firmware_description,
				sizeof(desc) - 1);
			env_set("sysfw_desc", desc);
			env_set("is_qemu",
				strstr(desc, "QEMU_TI_DMSC") ? "1" : "0");
		}
	}
```
(Verify the exact `get_ti_sci_handle()` return type / `IS_ERR_OR_NULL` idiom against the tree; include `<linux/soc/ti/ti_sci_protocol.h>` if the struct field access needs it — mirror how `k3_sysfw_print_ver()` reaches the handle.) Format as a proper `git format-patch` file with a descriptive message (no AI trailer). Generate it from a real u-boot-phytec checkout at SRCREV `6980061…` so it applies cleanly.

- [ ] **Step 2: Add the QEMU env branch**

The default env comes from `board/phytec/phycore_am64x/phycore_am64x.env`. Rather than patch that upstream file, prefer the recipe's env-append mechanism if one exists; otherwise extend the same patch to add, in the `.env`, a QEMU hook that the existing `mmcboot` respects. Minimal form — a `qemu_setup` that runs from `board_late_init`/`preboot` or is invoked at the top of `mmcboot` when `is_qemu=1`:

```
qemu_setup=if test "${is_qemu}" = "1"; then setenv overlays; setenv fdtfile oftree; fi
```
plus ensure `mmcboot` runs `run qemu_setup` before `mmc_apply_overlays` (encode exactly the delta Task 1 proved — if Task 1 showed the stock path already works unchanged under QEMU except for overlays, this one line is the whole change). Keep it a strict no-op when `is_qemu != 1` (HW path byte-identical).

- [ ] **Step 3: Wire the patch into the bbappend**

In `recipes-bsp/u-boot/u-boot-phytec_%.bbappend`, add (mirroring the existing NeoVisor patch line):

```
SRC_URI += "file://0002-board-phytec-export-sysfw-desc-and-qemu-env.patch"
```

- [ ] **Step 4: Commit on a meta-cmblu feature branch**

```bash
# on yoctoklaus, in ~cmbluadmin/devel/yocto/sources/meta-cmblu
git checkout -b feat/qemu-boot-detection   # off the current tip
git add recipes-bsp/u-boot/u-boot-phytec_%.bbappend \
        recipes-bsp/u-boot/u-boot-phytec/0002-*.patch
git commit -m "feat(u-boot): detect qemu via sysfw desc, steer mmc boot"
```
Do not push. (The agent edits files on the yoctoklaus checkout via ssh; confirm write access first. If the agent cannot write there, prepare the patch + bbappend diff as files in the scratchpad for the user to apply — state which.)

---

### Task 3: Build on yoctoklaus + end-to-end QEMU autoboot

**Files:** none (build + verify).

**Interfaces:**
- Consumes: Task 2's committed meta-cmblu change.
- Produces: a rebuilt `tiboot3.bin` + WIC (or a WIC with the new `u-boot.img`/`tispl.bin`/`tiboot3.bin` on its FAT) that autoboots — with NO manual u-boot typing — from power-on to a Linux login prompt in QEMU.

- [ ] **Step 1: Build the patched u-boot**

On yoctoklaus (see `cmblu-yocto-image` skill for exact syntax):
```
bitbake u-boot-phytec
# + the k3r5 multiconfig so tiboot3.bin is regenerated:
bitbake mc:am64xx-cmblu-core-node-1-k3r5:u-boot-phytec   # (verify multiconfig name)
```
Confirm new `tiboot3.bin`, `tispl.bin`, `u-boot.img`, `ti-boot-container.img` in the deploy dir with fresh timestamps.

- [ ] **Step 2: Assemble a testable image for QEMU**

Two options — pick the faster that works:
- (a) Rebuild the WIC: `bitbake cmblu-headless-image -c do_image_wic`, pull the new `…rootfs.wic.xz`.
- (b) Fast path: copy the new `tiboot3.bin` to `~/devel/git/qemu/build/`, and `mcopy -o` the new `TISPL.BIN`/`U-BOOT.IMG` onto the FAT boot partition of a copy of the existing `fluxos.wic` (sector 16384). (The kernel/DTB/rootfs are unchanged in the minimal approach, so only the bootloader blobs differ.)

Pull the artifacts to the local machine (scp).

- [ ] **Step 3: Full autoboot in QEMU**

```bash
cd /home/sefo/devel/git/qemu/build
qemu-img create -f qcow2 -b "$PWD/<new-or-patched.wic>" -F raw /tmp/p4.qcow2 \
    $(( ( $(stat -c%s <wic>) + 0x7ffff ) & ~0x7ffff ))
timeout 180 ./qemu-system-aarch64 -M am64-virt -bios <new-tiboot3.bin> \
    -drive if=sd,format=qcow2,file=/tmp/p4.qcow2 -serial stdio -display none \
    2>&1 | tee /tmp/p4-boot.log | tail -40
```
Expected — WITHOUT any keypress: full chain → `is_qemu=1` set → autoboot runs `mmcboot` → `Booting Linux` → rootfs mount → **login prompt** (`login:` or a shell). Capture the tail. Verify `printenv is_qemu` semantics indirectly (boot succeeded via the QEMU branch).

- [ ] **Step 4: Add a gated functional test (QEMU repo)**

In `~/devel/git/qemu/tests/functional/aarch64/test_am64_bootrom.py`, add a gated method (env var `QEMU_TEST_WIC_P4` → the patched WIC, `QEMU_TEST_TIBOOT3_P4` → the patched tiboot3) that autoboots (no keypress) and waits for the kernel + login markers (`Booting Linux on physical CPU`, then the login/shell prompt string observed in Step 3). This commits to the QEMU repo (branch `cmblu/corenode`), `test(am64x): gated functional test for full linux boot`. checkpatch. (The product change itself lives in meta-cmblu; this test lives with the machine model that enables it.)

---

### Task 4: HW-safety review, real-HW acceptance (user-assisted), docs

**Files:**
- Modify: `~/devel/git/qemu/docs/superpowers/specs/2026-07-15-am64-full-boot-chain-design.md` (Phase 4 as-built)

**Interfaces:**
- Consumes: everything above.
- Produces: the project's done-criterion (one WIC, both targets) with HW verification recorded; MR-ready meta-cmblu branch.

- [ ] **Step 1: HW no-op review**

Re-read the Task 2 patch + env with fresh eyes for the product-safety constraint: on real HW `is_qemu=0`, so `qemu_setup` is a no-op and `mmcboot` is byte-identical to today; the only always-on effect is two extra `env_set` calls in `board_late_init` (harmless, non-persistent). Confirm no path changes the HW `overlays`/`fdtfile`/`bootargs`. Write the reasoning into the report.

- [ ] **Step 2: Real-HW acceptance (user-assisted)**

The agent cannot flash a CoreNode. Prepare a precise checklist for the user and ask them to run it: (1) deploy the rebuilt `tiboot3.bin`/`tispl.bin`/`u-boot.img` to a CoreNode (respecting the UANT `test_running` deploy gate if a UANT is used), (2) confirm it boots to Linux exactly as before, (3) `fw_printenv is_qemu` shows `0` on HW and the boot path/overlays are unchanged. Record the user's result. Do NOT mark the phase done until HW boot is confirmed unchanged.

- [ ] **Step 3: Spec as-built + MR prep**

Update the spec Phase 4 section → `[IMPLEMENTED <date>]` with the as-built note: the two spec corrections (no fitImage, no dedicated DTB), the actual mechanism (`board_late_init` env export + `is_qemu` `qemu_setup` branch), the working QEMU env delta, and the HW-unchanged confirmation. Resolve the Ph.-4 open item. Then prepare the meta-cmblu MR: per neoflux/CLAUDE.md use the GitLab MCP tools, target the meta-cmblu default branch, assignee = the user; link a Jira ticket (SOE project, Team PowerPilot / component sBMS / current sprint) — **do this only when the user confirms HW acceptance and asks to open it.** Commit the spec doc to the QEMU repo (`docs: record phase 4 yocto qemu-boot as-built`).

---

## Self-review notes (done at plan time)

- **Spec coverage (Phase 4), with the user-approved overrides:** QEMU detection via TISCI `sysfw_desc` → Task 2; boot-script branch → Task 2 (`qemu_setup`/`is_qemu`, lighter than the spec's fitImage-config idea, which research disproved); "root stays /dev/mmcblk*" → Task 1 verifies the stock DTB's `mmcblk1p2` works under QEMU; "same WIC on HW + QEMU" → Tasks 3 (QEMU) + 4 (HW). The dedicated `k3-am642-qemu-corenode.dts` is **dropped by user decision** (minimal, no new DTB) — recorded in Task 4's as-built.
- **De-risking first:** Task 1 proves the whole premise interactively in QEMU with zero Yocto builds; a genuine Linux hang there is an explicit escalation point, not a silent pivot.
- **Placeholder scan:** the u-boot patch's exact `get_ti_sci_handle()` idiom and the `overlays`/`mmcdev` values carry verify-against-tree / verify-in-Task-1 instructions because they depend on live inspection; every file path, recipe name, load address and env key is concrete from research.
- **Execution reality flagged:** builds are remote (yoctoklaus), HW acceptance is user-assisted, and the product change (meta-cmblu) is a different repo with MR discipline — all in Global Constraints so the executor doesn't assume the fast local QEMU-C loop.
