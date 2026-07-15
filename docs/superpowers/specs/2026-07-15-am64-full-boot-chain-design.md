# AM64x Full Boot Chain + Yocto WIC Boot — Design

**Date:** 2026-07-15
**Repos:** `wafgo/qemu` (branch work on top of `feat/am64-tiboot3-bootrom`) and
`meta-cmblu` (Yocto layer, phase 4)
**Status:** Approved design, pre-implementation
**Builds on:** `docs/superpowers/specs/2026-07-14-am64-tiboot3-bootrom-design.md`
(implemented: tiboot3 boots on the R5F up to DDR init)

## Goal

Boot a complete, **unmodified** FluxOS Yocto SD-card image (WIC) on the
`am64-virt` machine through the **real boot chain**, exactly like on the
phyCORE-AM64x hardware:

```text
tiboot3.bin (R5 SPL, via emulated boot ROM — already working)
  → DDR init succeeds
  → loads tispl.bin from the emulated eMMC/SD (WIC image)
  → starts the A53 via TISCI proc-boot
  → ATF (BL31) + OP-TEE + A53 SPL → u-boot proper
  → u-boot detects QEMU via the TISCI firmware description
  → loads kernel + matching DTB (qemu-corenode.dtb in QEMU, stock DTB on HW)
  → Linux boots from the WIC rootfs to a login prompt
```

**One image, two targets:** the same WIC boots unmodified on real hardware
and in QEMU. QEMU detection happens at the u-boot level via data the
emulated DMSC already provides — no QEMU-specific image build.

**Primary purpose:** a virtual CoreNode development environment (kernel +
real rootfs, login/SSH). Secondary: the full chain makes the M4-first /
NeoVisor boot flow (stage 1+1b in tiboot3) testable in QEMU later.

**Done criterion:** `qemu-system-aarch64 -machine am64-virt -bios
tiboot3.bin -drive file=<fluxos.wic>,...` reaches a Linux login prompt on
UART0, with the rootfs mounted from the WIC image as `/dev/mmcblk*`; the
identical WIC still boots on a physical CoreNode.

## Key decisions (from brainstorming)

1. **Full chain, no shortcuts:** ATF and OP-TEE run for real. Rationale:
   PSCI fidelity, and the strategic goal of testing the NeoVisor M4-first
   chain virtually. The direct-kernel `arm_load_kernel` path remains
   available but is not this project's mechanism.
2. **GICv2 → GICv3 migration is accepted** as the prerequisite for ATF
   (AM64x has a GIC-500; BL31 initialises GICv3 unconditionally). This
   also makes the SoC model more faithful.
3. **Storage appears as `/dev/mmcblk*`:** QEMU models the AM64x SDHCI
   controllers (plus TI-PHY stub registers) so the unmodified
   `sdhci_am654` driver works in u-boot *and* Linux. Device names, fstab
   and RAUC paths match real hardware. virtio-blk is the documented
   fallback only if the am654 driver turns out to need unemulatable PHY
   behaviour.
4. **QEMU detection via TISCI:** the emulated DMSC answers the version
   query with `firmware_description = "QEMU_TI_DMSC"`. A small u-boot
   patch (maintained in the Yocto layer like other u-boot patches)
   exports that string as an env variable (e.g. `sysfw_desc`); the boot
   script branches on it to select the DTB / FIT configuration. No magic
   registers, no probing heuristics.
5. **QEMU-side DT for Linux:** a dedicated `k3-am642-qemu-corenode.dts`
   (maintained in meta-cmblu, shipped as an extra fitImage
   configuration) describes only what QEMU models. With GICv3 done, it
   stays close to the real DT (GIC matches; UART/SDHCI nodes point at
   the modelled devices).

## Phases

Each phase has an independently testable milestone and its own
implementation plan. Phases 1 and 2 are independent of each other;
phase 3 needs both; phase 4 needs 3.

### Phase 1 — GICv3 migration (QEMU) [IMPLEMENTED 2026-07-15]

Replace the SoC's `arm_gic` (v2) with `arm-gicv3`: distributor +
redistributors at the real AM64x addresses (GICD `0x01800000`, GICR
`0x01840000` per TRM — pin exact map in the plan), CPU sysreg interface
on the A53s, rewire all existing SPIs (UARTs, sec-proxy, mailboxes,
PCIe, virt peripherals). Regenerate `pc-bios/dtb/am64-virt.dtb` for v3 so
the existing direct-Linux boot keeps working.

**Milestone:** all existing tests green (qtests, unit, functional
am64_bootrom incl. real tiboot3 to SYSFW ABI); direct-Linux boot on the
machine still works with the regenerated DTB.

**Risks:** wide blast radius in `ti-am64x.c` (every IRQ wiring site);
the fork's existing dtb consumers. Mitigated by the existing test net.

**As built (2026-07-15):**
GICv3 (TYPE_ARM_GICV3) instantiated at GICD `0x01800000`, GICR `0x01840000`
(2 × `0x20000` redistributor frame). Security extensions enabled, maintenance
IRQ wired. ITS (`0x01820000`) not modelled, deferred until PCIe-MSI integration.
`pc-bios/dtb/am64-virt.dtb` regenerated with `arm,gic-v3` node. New qtests
validate PIDR2 ArchRev on GICD/GICR. Functional test_linux_gicv3 added with
upstream kernel Asset + regenerated DTB; kernel confirms GICv3 redistributor at
`0x0000000001840000`. Adjacent fix: cmblu-corenode machine smp-headroom bug
(pre-existing tcg_register_thread flake from R5F addition). Commits:
753ef79682, ac6d778648, 9bd1456838, e9186fea8b.

### Phase 2 — DDRSS stub + SDHCI (QEMU) [IMPLEMENTED 2026-07-15]

- **DDRSS stub:** RAM-backed register block for the DDR controller
  (base/size from TRM; the k3-ddrss driver's polled status/training
  bits return "complete" — same technique as the CTRL_MMR stub). Root
  cause of today's `DRAM init failed: -22` gets triaged first; the stub
  covers what the driver actually reads.
- **SDHCI:** two `sysbus-sdhci` instances at the real addresses
  (`main_sdhci0` = eMMC 8-bit, `main_sdhci1` = SD, bases per
  k3-am64-main.dtsi), each with a TI-PHY stub window for the am654
  driver's PHY registers (CALDONE etc.). WIC image attached via
  standard `-drive`; wiring decides which controller it lands on
  (default: sdhci0/eMMC to match CoreNode `mmcblk0` numbering).

**Milestone:** R5 SPL passes DRAM init and loads `tispl.bin` from the
WIC boot partition (visible in its console output); u-boot `mmc part`
level access verified via qtests where practical.

**As built (2026-07-15):**

- **DDRSS stub contract** (`hw/misc/ti-k3-ddrss.c`,
  `include/hw/misc/ti-k3-ddrss.h`): a 32 KiB (`TI_K3_DDRSS_CFG_SIZE =
  0x8000`) RAM-backed register file (`TIK3DdrssState`) mapped at the
  real DENALI CTL/PI base `0x0f308000`. Writes are stored verbatim;
  reads OR five status bits into the stored value so both the driver's
  init-done poll and, when `CONFIG_K3_INLINE_ECC` is enabled (it is, in
  the FluxOS SPL build), its BIST-priming poll complete immediately —
  no PHY/DDR training and no ECC scrubbing is modelled:
  - `0x214C` bit 0 — `DENALI_PI_83`, PI init done.
  - `0x0538` bit 13 — `DENALI_CTL_334`, `MASTER` MC_INIT group.
  - `0x0558` bit 25 — `DENALI_CTL_342`, `INT_STATUS_INIT` bit 1.
  - `0x0538` bit 8 — `DENALI_CTL_334`, `MASTER` BIST group (the
    ECC/BIST pair; needed because inline ECC makes the SPL additionally
    poll `checkctlinterrupt(LPDDR4_INTR_BIST_DONE)`).
  - `0x0554` bit 16 — `DENALI_CTL_341`, `INT_STATUS_BIST` field, `BIST_DONE`.

  Root cause of the original `DRAM init failed: -22` was pinned before
  stubbing: the k3-ddrss driver reads `DENALI_CTL_0` for the DRAM class
  and rejects anything it doesn't recognize. The stub does NOT seed that
  value — the driver's own DT-config bulk write stores `CTL_0 = 0xA00`
  (DDR4) into the RAM-backed register file, and the subsequent class
  read-back simply returns it.
- **ECAM shrink (256 MiB → 32 MiB):** `AM64_VIRT_PCIE_ECAM_SIZE` was
  `0x10000000` (256 MiB), covering `0x0D000000`–`0x1CFFFFFF` and
  shadowing the DDRSS cfg window (`0x0f308000`) and both MMCSD windows
  (`0x0fa00000`/`0x0fa10000`) — reads there returned the ECAM's
  empty-slot `0xffffffff` instead of reaching the stubs, which is what
  actually manifested as the DRAM-init failure above. Fixed by shrinking
  to `0x02000000` (32 MiB / 32 PCI buses, ending at `0x0EFFFFFF`,
  clear of `0x0F000000+`). `pc-bios/dtb/am64-virt.dts`/`.dtb` updated to
  match (pcie node `reg` size and `bus-range` capped). Commit:
  `a3ea575848`.
- **SD-FS-boot decision:** the WIC is attached via a plain `-drive`
  against the SD controller (`main_sdhci1`, GIC SPI 134) when passed
  together with `-bios <tiboot3.bin>`; DEVSTAT `0x240` is the boot-mode
  encoding the R5 boot ROM model reports for "SD, primary bootmode",
  which routes tiboot3's own MMC/FAT driver at the FS-boot (not
  raw-eMMC-boot0) code path. This is a deliberate deviation from the
  spec's original eMMC-default framing (§ Key decisions): the FluxOS WIC
  is a plain SD-card-style FAT layout and carries no eMMC boot0/RBL
  content, so eMMC-boot0 emulation would have no image data to load
  regardless. SD-FS-boot is both what the WIC actually contains and
  what the physical CoreNode's SD-card boot path already validates.
- **DMSC additions** (`hw/misc/ti-dmsc.c`, `include/hw/misc/ti-dmsc.h`),
  added incrementally as each one unblocked the next SPL stall:
  - `TISCI_MSG_GET_FREQ` (`0x010e`): returns a fixed 200 MHz "unit
    clock" rate — the SPL only sanity-checks that `GET_FREQ` succeeds,
    it does not need the real PLL topology.
  - `TISCI_MSG_SET_CONFIG` (`0xc100`, the PROC_SET_CONFIG message):
    bare-header ACK, same shape as the other proc-boot stubs.
  - `TISCI_MSG_SET_CLOCK_PARENT` (`0x0102`): bare-header ACK; the
    response additionally sets `num_parents = 2` on the clock's
    `GET_NUM_CLOCK_PARENTS` answer, because u-boot's clock core rejects
    *any* `clk_set_parent()` call with `-EINVAL` unless the reported
    parent count is ≥ 2.
  - `TISCI_MSG_PROC_HANDOVER` (`0xc005`): bare-header ACK, completing
    the R5→A53 proc-boot handover sequence started by `SET_CONFIG`.
  - Security-Manager Control MMR stub (`hw/misc/ti-k3-ctrlmmr.c`,
    `K3_SEC_MGR_SYS_STATUS` at offset `0x100` of the sec-ctrlmmr
    instance, absolute `0x44234100`): consumed by u-boot's
    `get_device_type()` during the same handoff; decodes to a fixed
    "safe" device-type value via the `sec-mgr-sys-status` property.
- **SDHCI:** two real `TYPE_SYSBUS_SDHCI` instances (`sdhci[0]` =
  `main_sdhci0`/eMMC at ctl `0x0fa10000`, `sdhci[1]` =
  `main_sdhci1`/SD at ctl `0x0fa00000`; GIC SPIs 133 and 134
  respectively, per k3-am64-main.dtsi), each with `capareg =
  0x057c34b4` (SDHCI 3.0, ADMA2/SDMA, 8-bit and voltage bits matching
  the am654 controller) and a paired `ti-k3-sdhci-phy` stub window
  (`sdhci_phy[i]`) at the SS_CFG address for the am654 driver's PHY
  register poke sequence. The PHY stub stores all writes and ORs
  `PHY_STAT1` (offset `0x130`) bits `CALDONE` (bit 1) and `DLLRDY`
  (bit 0) into reads, so the driver's IO-calibration and DLL-ready poll
  loops complete immediately — no PHY calibration or delay-line
  behaviour is modelled.
- **512 KiB padding / qcow2-overlay requirement:** QEMU's `sd-card`
  device rejects a raw backing image whose size is not a 512 KiB
  multiple; a real FluxOS WIC is not guaranteed to satisfy that. The
  functional test (`test_fluxos_spl_loads_tispl`,
  `tests/functional/aarch64/test_am64_bootrom.py`) never attaches the
  WIC directly — it creates a qcow2 overlay in its scratch dir with the
  WIC as a read-only raw backing file and a virtual size rounded up to
  the next 512 KiB boundary (`(size + 0x7ffff) & ~0x7ffff`, a no-op
  when the WIC already happens to be aligned), then attaches the
  overlay via `-drive if=sd,format=qcow2,file=<overlay>`. This also
  means guest writes during the test never touch the operator's WIC.
- **End state / where Phase 2 stops:** with the above in place the R5
  SPL performs a byte-exact FAT read of `tispl.bin` from the WIC boot
  partition (independently verified against a manual FAT16 parse and
  `sdhci` trace events), prints `Starting ATF on ARM64 core...`, and
  parks the R5 core in WFE — the correct end state for this phase, since
  starting the A53 core itself (TISCI proc-boot `PROC_AUTH_BOOT`/entry
  point handover, ATF execution) is Phase 3's job. Commits:
  `13c1c79b56`, `a3ea575848`, `45139e056e`, `5edd545343`, `754e3d0dbb`,
  `dd014bd0b9`, `d3b4f4be0d`.

### Phase 3 — A53 handover: ATF + OP-TEE + u-boot (QEMU)

- **DMSC proc-boot:** implement the TISCI messages the R5 SPL uses to
  start the A53 (`PROC_SET_CONFIG`, `PROC_AUTH_BOOT`/handover, device
  power-on for the A53 cores) — powering the A53 on via `arm-powerctl`
  with the configured entry point, mirroring the existing M4 boot
  control. Plus the PSCI-path messages ATF sends at runtime (CPU_ON for
  the second A53, idle/off requests: ACK generously).
- **ATF (BL31):** runs from OCSRAM as loaded out of the tispl FIT by
  the R5 SPL. Needs phase 1 (GICv3) and whatever triage uncovers
  (expected: more TISCI coverage, arch-timer fine, UART fine).
- **OP-TEE:** attempted as-is. Known risk: dependence on SA2UL crypto /
  firewall hardware. Decision tree if it blocks: (a) minimal SA2UL
  stub, (b) documented QEMU-only tispl without OP-TEE (breaks
  "unmodified artifact" — last resort, explicitly flagged to the user).

**Milestone:** u-boot proper prompt on UART0, loaded through the
complete real chain from the WIC image; `mmc part` lists the WIC
partitions from u-boot.

### Phase 4 — Yocto integration (meta-cmblu)

- **u-boot patch:** export the SYSFW `firmware_description` (already
  known to u-boot from the version query) as env var `sysfw_desc`.
- **Boot script:** branch on `sysfw_desc == "QEMU_TI_DMSC"` → select
  the QEMU fitImage configuration / DTB and QEMU-appropriate bootargs
  (console + `root=` stays `/dev/mmcblk*`); otherwise the unchanged HW
  path.
- **`k3-am642-qemu-corenode.dts`:** new DT in the kernel recipe,
  added to `KERNEL_DEVICETREE` → lands as an extra fitImage config.
  Contents: 2× A53, GICv3 (real addresses), arch timer, UART0 as the
  console (alias `serial2` like on HW so `console=` stays identical),
  the two SDHCI nodes (compatible + properties matching what QEMU
  stubs), memory node. Nothing QEMU doesn't model.
- **Kernel config verification:** GICv2 concern disappears with phase 1
  (GICv3 matches HW); verify `sdhci_am654` and (for the interim SSH
  story) virtio/PCI support in the FluxOS kernel config — add a small
  fragment only if needed.

**Milestone (project done):** the standard FluxOS WIC boots in QEMU to
a login prompt with rootfs on `/dev/mmcblk*`; the same WIC boots
unchanged on a physical CoreNode (validated on real HW before merge).

## Out of scope (deliberate, future work)

- M4/NeoVisor in the dev environment (remoteproc/RPMsg bring-up) — the
  full chain built here is the enabler; separate project.
- CPSW ethernet model. Interim networking: the machine's existing
  virtio-net PCIe NIC (kernel support verified in phase 4).
- RAUC update flows in QEMU (eMMC boot-partition semantics).
- SA2UL crypto beyond what OP-TEE minimally demands (see phase 3
  decision tree).
- Second R5F core / second cluster; ICSSG; OSPI.

## Error handling & debuggability (project-wide conventions)

Carried over from phase-1 project: unknown TISCI messages → NAK +
trace; unmodelled registers → `LOG_UNIMP`; boot-chain progress
observable via existing `k3_bootrom*`/`ti_dmsc*`/`ti_sec_proxy*` trace
events plus new ones for proc-boot and SDHCI-PHY stubs; loader/machine
errors fail fast before the guest starts.

## Testing

- Every phase keeps the full existing net green (8 qtests, 6 unit, 2
  functional subtests) — regressions gate every commit.
- New qtests per phase: GICv3 presence/IRQ delivery (ph. 1), SDHCI
  register-level + DDRSS stub reads (ph. 2), proc-boot TISCI exchange
  (ph. 3).
- Functional tests extended stepwise, all gated on env vars where they
  need real artifacts: `QEMU_TEST_TIBOOT3` (exists),
  `QEMU_TEST_TISPL`/`QEMU_TEST_WIC` (new): banner → tispl load → u-boot
  prompt → login prompt.
- Phase 4 acceptance additionally requires a boot test on physical
  hardware (user-assisted; the deploy gate rules for UANT targets apply
  if a lab unit is used).

## Open items pinned during planning (per phase)

- Ph. 1: ✓ RESOLVED — GIC-500 address map (GICD `0x01800000`, GICR
  `0x01840000` × 2, ITS deferred) pinned from k3-am64-main.dtsi; dtb
  regeneration mechanism validated.
- Ph. 2: ✓ RESOLVED — root cause of `DRAM init failed: -22` was the
  256 MiB PCIe ECAM window shadowing the DDRSS/MMCSD MMIO (fixed by the
  32 MiB ECAM shrink), not a DDRSS register gap by itself; DDRSS base
  (`0x0f308000`) + five polled/OR status bits (incl. the ECC/BIST pair
  for inline-ECC builds) pinned from the k3-ddrss driver; SDHCI reg
  layout (ctl at `0x0fa1_0000`/`0x0fa0_0000`, PHY stub at the SS_CFG
  window, `capareg 0x057c34b4`, GIC SPIs 133/134) pinned from
  k3-am64-main.dtsi + the am654 driver; SD-FS-boot (DEVSTAT `0x240`)
  confirmed as the correct path since the WIC carries no eMMC boot0
  content; 512 KiB SD-card image alignment handled via a qcow2 overlay
  in the functional test rather than mutating the WIC.
- Ph. 3: exact TISCI proc-boot sequence of u-boot v2025.01-phy2
  (`arch/arm/mach-k3/r5/...`, `common.c: start_non_linux_remote_cores`
  / `k3_sysfw_...`); ATF k3 platform's TISCI usage for PSCI; OP-TEE
  AM64 platform hardware dependencies.
- Ph. 4: mechanism for the `sysfw_desc` u-boot patch (env injection
  point); fitImage configuration naming; FluxOS kernel config flags
  (`sdhci_am654`, virtio, PCI).
