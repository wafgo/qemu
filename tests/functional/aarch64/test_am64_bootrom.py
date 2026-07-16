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
from subprocess import check_call, DEVNULL

from qemu_test import Asset, QemuSystemTest, get_qemu_img, \
    wait_for_console_pattern
from unittest import skipUnless

# Root of the QEMU source tree, used to reach pc-bios/dtb/am64-virt.dtb:
# that file is a checked-in convenience blob (see
# docs/superpowers/plans/2026-07-15-am64-gicv3-migration.md), not a meson
# build product, so it is never copied into the build directory and
# self.build_file() cannot find it.
SOURCE_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), '..', '..', '..'))


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

    # The gated full-chain test (test_fluxos_boot_chain) reaches the u-boot
    # autoboot prompt in ~18 s on a dev box; 120 s leaves ample margin on
    # slower CI runners. The other (fast) subtests are unaffected -- this is
    # an upper bound, not a fixed delay.
    timeout = 120

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
        # The R5 SPL banner is the primary done-criterion; the SYSFW ABI line
        # additionally proves the DMSC boot-notification + TISCI VERSION path
        # works end-to-end (boot then proceeds into unmodelled DDR init).
        wait_for_console_pattern(self, 'U-Boot SPL')
        wait_for_console_pattern(self, 'SYSFW ABI:')

    @skipUnless(os.getenv('QEMU_TEST_TIBOOT3'),
                'set QEMU_TEST_TIBOOT3=<path to tiboot3.bin>')
    @skipUnless(os.getenv('QEMU_TEST_WIC'),
                'set QEMU_TEST_WIC=<path to fluxos.wic>')
    def test_fluxos_boot_chain(self):
        wic = os.path.abspath(os.getenv('QEMU_TEST_WIC'))

        # QEMU's sd-card device rejects raw images whose size is not a
        # 512 KiB multiple (see hw/sd/sd.c); a real FluxOS WIC generally
        # is not aligned to that. Rather than requiring the operator's
        # WIC to be resized in place (which would also expose it to
        # guest writes during the test), attach it as the read-only
        # backing file of a throwaway qcow2 overlay whose virtual size
        # is rounded UP to the next 512 KiB boundary. All guest writes
        # land in the overlay, which is discarded with the rest of
        # self.workdir; the padding is a no-op when the WIC is already
        # aligned.
        size = os.path.getsize(wic)
        padded_size = (size + 0x7ffff) & ~0x7ffff
        overlay = self.scratch_file('wic-overlay.qcow2')
        qemu_img = get_qemu_img(self)
        check_call([qemu_img, 'create', '-f', 'qcow2', '-b', wic,
                    '-F', 'raw', overlay, str(padded_size)],
                   stdout=DEVNULL, stderr=DEVNULL)

        self.set_machine('am64-virt')
        self.vm.set_console()
        self.vm.add_args('-bios', os.getenv('QEMU_TEST_TIBOOT3'),
                         '-drive',
                         'if=sd,format=qcow2,file=' + overlay)
        self.vm.launch()
        # R5 SPL milestones (phase 2): SPL banner, SD boot device, ATF handoff.
        wait_for_console_pattern(self, 'U-Boot SPL')
        wait_for_console_pattern(self, 'Trying to boot from MMC2')
        wait_for_console_pattern(self, 'Starting ATF on ARM64 core')
        # A53 handover chain (phase 3): ATF BL31 runs, then the A53-side
        # U-Boot SPL re-enumerates the SD and loads u-boot proper, which
        # reaches its autoboot prompt.
        #
        # Note: OP-TEE runs between BL31 and the A53 SPL but its
        # "I/TC: OP-TEE version:" banner is compiled out at this FluxOS
        # build's log level, so it is deliberately NOT used as a marker
        # (it would never appear and would hang the test). The second
        # "U-Boot SPL" line proves the A53 SPL started after OP-TEE.
        wait_for_console_pattern(self, 'NOTICE:  BL31:')
        wait_for_console_pattern(self, 'U-Boot SPL')       # A53-side SPL
        wait_for_console_pattern(self, 'U-Boot 2025.01')   # u-boot proper
        wait_for_console_pattern(self, 'Model: PHYTEC phyBOARD-Electra')
        wait_for_console_pattern(self, 'Hit any key to stop autoboot')

    # Standalone arm64 kernel (Ubuntu bionic-updates netboot installer),
    # same Asset used by test_xlnx_versal.py.  It ships PL011 + GICv3
    # drivers but no built-in initramfs, so after console init it will
    # panic trying to mount a root filesystem -- expected, we only care
    # about the GICv3 + console milestones reached before that point.
    ASSET_KERNEL = Asset(
        ('http://ports.ubuntu.com/ubuntu-ports/dists/bionic-updates/main/'
         'installer-arm64/20101020ubuntu543.19/images/netboot/'
         'ubuntu-installer/arm64/linux'),
        'ce54f74ab0b15cfd13d1a293f2d27ffd79d8a85b7bb9bf21093ae9513864ac79')

    def test_linux_gicv3(self):
        kernel_path = self.ASSET_KERNEL.fetch()
        dtb = os.path.join(SOURCE_DIR, 'pc-bios', 'dtb', 'am64-virt.dtb')
        self.set_machine('am64-virt')
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb,
                         '-append', 'console=ttyAMA0 earlycon')
        self.vm.launch()
        wait_for_console_pattern(
            self,
            'GICv3: CPU0: found redistributor 0 region '
            '0:0x0000000001840000')
        wait_for_console_pattern(self, 'CPU1: Booted secondary processor')
        wait_for_console_pattern(self, 'ttyAMA0')


if __name__ == '__main__':
    QemuSystemTest.main()
