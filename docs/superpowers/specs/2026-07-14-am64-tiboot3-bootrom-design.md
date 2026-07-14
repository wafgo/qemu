# AM64x tiboot3 ROM-Boot Emulation — Design

**Date:** 2026-07-14
**Branch:** `feat/am64-tiboot3-bootrom` (fork `wafgo/qemu`)
**Status:** Implemented (`d0123eb051..a3f0c8f28b`)

## Goal

Boot an **unmodified** `tiboot3.bin` (TI AM64x combined boot image, HS-FS
variant, as produced by the CMBlu FluxOS Yocto build for the phyCORE-AM64x)
on the `am64-virt` QEMU machine. ROM-boot mode (`-bios`) is wired into
`am64-virt` only; `cmblu-corenode` is unaffected and keeps its existing
`-kernel`/`m4boot-cpu` boot path unchanged (see "Machine integration"
below):

```sh
qemu-system-aarch64 -machine am64-virt -bios tiboot3.bin -serial stdio
```

QEMU takes over the role of the TI mask ROM (RBL): it parses the X.509
combined-image header, loads the contained segments to their certified
destination addresses, and starts the Cortex-R5F boot core at the
certified entry point.

**Done criterion:** the R5 SPL contained in the FluxOS `tiboot3.bin` runs
up to and including its `U-Boot SPL 20xx.xx …` banner on main-domain
UART0. The banner is printed in `board_init_f` *before* DDR
initialisation, so the DDR controller is intentionally out of scope; the
SPL may hang or fault afterwards.

## Out of scope (deliberate)

- The `tispl.bin` → `u-boot.img` → Linux chain (follow-up project).
- Executing the real TIFS/SYSFW binary. The existing `ti-dmsc.c`
  emulation keeps answering TISCI; the SYSFW payload inside tiboot3 is
  parsed but **discarded**.
- X.509 signature verification / crypto. Only DER structure walking.
- The R5 interrupt controller (VIM). The SPL polls until the banner.
- A second R5F core or the second R5F cluster.
- DDR controller (DDRSS) modelling.

## Current state (for context)

The `cmblu/corenode` branch models: 2× Cortex-A53 + GIC, 1× Cortex-M4F
(ARMv7M cluster), RAT, Sec-Proxy, 8 mailboxes, DMSC/TISCI emulation
(`hw/misc/ti-dmsc.c`), MCU UARTs (`hw/char/ti-am64-uart.c`). Boot paths
today: `arm_load_kernel()` on the A53s, or `m4boot-cpu=0` for M4-only.
There is **no R5F core, no OCSRAM, no main-domain UART0, and no boot-image
loader** yet.

## Architecture

### 1. SoC additions (`hw/arm/ti-am64x.[ch]`)

- New R5F cluster with 1× `cortex-r5f` CPU (R5F0_0), structured like the
  existing M4 cluster. New property `r5-start-powered-off`
  (default `true`; the machine clears it in ROM-boot mode).
- OCSRAM: 2 MiB at `0x70000000` (visible to R5, A53 and M4 via system
  memory). R5 ATCM/BTCM regions.
- Main-domain **UART0 at `0x02800000`** as an additional `ti-am64-uart`
  instance (the SPL early console).
- Sec-Proxy reachable from the R5 with the thread pairs the R5 SPL uses
  (exact thread IDs to be taken from the u-boot device tree /
  `k3-am64-*` sec-proxy bindings during implementation).

### 2. Boot-ROM loader (`hw/arm/k3-bootrom.c`, new)

The core deliverable. Runs at machine-init time when `-bios` is given:

- **Parser:** walks the DER structure of the X.509 combined-image
  certificate to the TI proprietary extensions and extracts the
  component table: per component (SYSFW, board-config, R5 SPL) the
  destination address, size, and the image entry point. No signature
  check. Reference for the layout: u-boot's `k3_gen_x509_cert` / binman templates
  and TI's boot-image documentation, cross-checked against the actual
  FluxOS binary.
- **Loader:** copies board-config and SPL segments to their destination
  addresses; discards the SYSFW segment. Rejects (fatal, before the
  guest starts) any segment whose destination is outside modelled
  memory.
- **Boot-params structure:** synthesises the ROM boot-parameter table at
  the top of OCSRAM (address and layout from the u-boot sources matching
  the FluxOS version — `store_boot_info_from_rom()` /
  `CONFIG_SYS_K3_BOOT_PARAM_TABLE_INDEX`). Boot medium reported as eMMC.
- **Reset hook:** sets the R5F0_0 reset PC to the certified entry point.

### 3. Peripheral stubs (`hw/misc/`)

Minimal models for everything the SPL touches before the banner:

| Block | Behaviour |
| ----- | --------- |
| CTRL_MMR / pinmux | accept writes, implement partition lock/unlock semantics |
| PLL MMRs | writes accepted; lock/status bits read back as "locked" immediately |
| PSC | module/LPSC transitions complete immediately ("done" status) |
| DM timer | real counting timer (the SPL's `udelay` must actually elapse) |

Registers that the SPL does not touch stay unimplemented and log via
`LOG_UNIMP`/trace events, so a hang is diagnosable with
`-d unimp,guest_errors`.

### 4. DMSC/TISCI extensions (`hw/misc/ti-dmsc.c`)

- Proactively queue the **TIFS boot notification** message into the R5
  SPL's sec-proxy RX thread at machine start (the SPL blocks on it in
  the combined-image flow).
- ACK the board-configuration messages (PM/RM/SEC), answer the TISCI
  version query, and serve the clock/device requests the SPL issues
  before the banner. Existing handlers are reused; gaps are found by
  tracing the actual message flow.
- Unknown TISCI messages get an explicit NAK plus a trace event — never
  silently dropped.

### 5. Machine integration (`hw/arm/am64-virt.c`)

- If `-bios` is set → ROM-boot mode: A53s and M4 start powered off, R5
  powered on, `k3-bootrom` loads the image. `arm_load_kernel()` is not
  called in this mode.
- `-bios` and `m4boot-cpu` are mutually exclusive (error out).
- `cmblu-corenode` inherits the behaviour unchanged.

## Boot flow (target state)

1. Machine init: `k3-bootrom` reads `-bios` file, parses the X.509
   header, extracts the component table.
2. Board-config + R5 SPL copied to their destination addresses in
   OCSRAM; SYSFW discarded; boot-params table written to top of OCSRAM.
3. Reset: A53 + M4 powered off, R5F0_0 starts at the certified entry.
4. SPL: CTRL_MMR unlock → `store_boot_info_from_rom()` reads our
   boot-params → waits for the TIFS boot notification, which the DMSC
   emulation has already queued.
5. SPL sends board configs + version query → DMSC ACKs.
6. Clock/PSC setup runs against the stubs; `udelay` against the DM
   timer.
7. `preloader_console_init()` → banner on UART0. ✅

## Error handling

- **Fail fast at init:** unparseable `-bios` file (not DER, missing TI
  extensions, segment outside modelled memory) → `error_report()` +
  exit before the guest starts.
- **Unknown TISCI:** explicit NAK + trace event.
- **Unmodelled registers:** `LOG_UNIMP` + trace (QEMU standard pattern).
- New trace events for the bootrom parser: discovered segments,
  destination addresses, entry point.

## Testing

- **Unit test for the cert parser** (`tests/unit/`): fixture generated
  synthetically (u-boot `k3_gen_x509_cert` scheme with dummy payloads).
  The FluxOS binary is **not** committed to the public fork.
- **Functional boot test:** boots a real tiboot3 when provided via env
  var (`TEST_TIBOOT3=…`), otherwise skipped. Runs internally against the
  FluxOS artifact without publishing it.
- **Manual acceptance:** FluxOS `tiboot3.bin` → `U-Boot SPL` banner on
  UART0.

## Key facts to pin down during implementation (from u-boot / TI docs)

- Exact boot-param table address + struct layout for AM64x R5 SPL.
- Sec-proxy thread IDs the R5 SPL uses on AM64x.
- Exact set of MMRs the FluxOS SPL version touches pre-banner (derive by
  running with `-d unimp` and iterating).
- X.509 extension OIDs/field order of the combined image as produced by
  the FluxOS u-boot version.

## As-built (2026-07-14)

The unmodified FluxOS `tiboot3.bin` (u-boot `v2025.01-phy2`, HS-FS combined
image, 538402 bytes) boots on `am64-virt` up to and past its `U-Boot SPL`
banner and the `SYSFW ABI` line, then stops at DDR init as designed.

### Final invocation

```sh
qemu-system-aarch64 -machine am64-virt -display none \
    -bios tiboot3.bin -serial stdio
```

`-serial`/console semantics: in ROM-boot mode (`-bios`) `serial0` is the
main-domain UART0 at `0x02800000` — the R5 SPL's preloader console. No
`-smp` flags are needed (the machine sizes its own A53+M4+R5 topology).

### How far boot proceeds

```text
U-Boot SPL 2025.01-00054-g6980061fa875-dirty (Feb 12 2025 - 05:47:04 +0000)
SYSFW ABI: 4.0 (firmware rev 0x000a 'QEMU_TI_DMSC (Wadims DMSC)')
NeoVisor: M4F started before DDR init (41596 bytes)
...
Unrecognized dram_class cannot init frequency!
DRAM init failed: -22
```

The banner + `SYSFW ABI` line is the done-criterion (both reached). The SPL
then runs the DMSC-driven M4 bring-up, fails the i2c EEPROM probe (no i2c
model — harmless) and finally fails DDR init because the DDRSS is not
modelled (deliberately out of scope). This is the expected stopping point.

### Design deviations from the pre-implementation plan

1. **No PLL/PSC stubs.** No PLL or PSC register access happens before the
   banner: the R5 SPL device tree runs UART0 and the DM timer clock-less,
   and clock/device management is served entirely by TISCI to the DMSC.
   §3's "PLL MMRs" and "PSC" stub rows were therefore dropped; only the
   DM timer stub from that table was actually needed (added as
   `hw/misc/ti-k3-dmtimer.c`).
2. **Boot notification is *not* queued speculatively "for the SPL to
   maybe wait on" — this FluxOS build genuinely blocks on it.** The plan
   (§4) hedged that u-boot might never wait; in fact the combined-image
   (ROM-loaded-sysfw) flow goes straight to `rproc_start()` →
   `k3_sysctrler_start()`, which does a blocking `mbox_recv()` for
   `TISCI_MSG_BOOT_NOTIFICATION` (0x000A) and aborts with `ret = -110` if
   it never arrives. The DMSC now pre-queues that message for every secure
   client at realize time.
3. **R5 boot-core MPID affinity must be pinned to 0.** The SPL's
   `lowlevel_init` parks (WFI) any core whose `MPIDR & 0xff != 0`; the
   QEMU R5F's default affinity (derived from `cpu_index`) is nonzero, so
   the boot core is now given `mp-affinity = 0` explicitly.
4. **Errata i2331 cold-boot reset is suppressed via a warm-reset
   `MCU_RST_SRC`.** The AM64x SPL reads `CTRLMMR_MCU_RST_SRC`
   (MCU_CTRL_MMR0 + 0x18178) and, on a cold POR (value 0) or a SW_POR,
   issues a full device reset for the CPSW errata i2331 workaround. QEMU
   has no such erratum, so MCU_CTRL_MMR0 is now a ctrlmmr stub reporting a
   plain warm-reset source (`rst-src = 0x1`), which skips the reset loop.
   (Without this the SPL loops on `SYS_RESET`, which the DMSC does not
   implement.)

### Files added/changed during acceptance (Task 9)

- `hw/arm/ti-am64x.c` — pin R5 `mp-affinity = 0`; second ctrlmmr stub for
  MCU_CTRL_MMR0 at `0x04500000` (replacing an unimplemented window).
- `hw/misc/ti-dmsc.c` — emit the boot notification for secure clients.
- `hw/misc/ti-k3-ctrlmmr.c` + header — configurable `MCU_RST_SRC`.
- `tests/functional/aarch64/test_am64_bootrom.py` — the real-image test
  now additionally asserts the `SYSFW ABI:` line.
