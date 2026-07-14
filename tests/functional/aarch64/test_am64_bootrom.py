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

from qemu_test import QemuSystemTest, wait_for_console_pattern
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
        # The R5 SPL banner is the primary done-criterion; the SYSFW ABI line
        # additionally proves the DMSC boot-notification + TISCI VERSION path
        # works end-to-end (boot then proceeds into unmodelled DDR init).
        wait_for_console_pattern(self, 'U-Boot SPL')
        wait_for_console_pattern(self, 'SYSFW ABI:')


if __name__ == '__main__':
    QemuSystemTest.main()
