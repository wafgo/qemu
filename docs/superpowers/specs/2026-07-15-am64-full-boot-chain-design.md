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

### Phase 2 — DDRSS stub + SDHCI (QEMU)

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
- Ph. 2: root cause of `DRAM init failed: -22`; DDRSS base + polled
  bits from the k3-ddrss driver; SDHCI reg layout (ctl vs PHY region)
  from k3-am64-main.dtsi + am654 driver.
- Ph. 3: exact TISCI proc-boot sequence of u-boot v2025.01-phy2
  (`arch/arm/mach-k3/r5/...`, `common.c: start_non_linux_remote_cores`
  / `k3_sysfw_...`); ATF k3 platform's TISCI usage for PSCI; OP-TEE
  AM64 platform hardware dependencies.
- Ph. 4: mechanism for the `sysfw_desc` u-boot patch (env injection
  point); fitImage configuration naming; FluxOS kernel config flags
  (`sdhci_am654`, virtio, PCI).
