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
    TIAM64xState *soc;
} K3BootRomReset;

static void k3_bootrom_cpu_reset(void *opaque)
{
    K3BootRomReset *r = opaque;
    CPUState *cs = CPU(r->cpu);

    /*
     * On the AM64x a real SoC/warm reset returns every core to its
     * reset state and only the R5F boot core runs out of the mask ROM;
     * the A53 cluster stays powered off until the R5 SPL cold-starts it
     * again through TISCI (see ti_dmsc SET_DEVICE -> arm_set_cpu_on()).
     *
     * QEMU's machine reset does not walk the A53 cores here (only the R5
     * boot core is re-armed by this hook), so after the first boot they
     * are left in PSCI_ON with the state BL31/OP-TEE/u-boot left behind.
     * On the next boot the R5 SPL's arm_set_cpu_on() then returns
     * ALREADY_ON and never re-enters BL31 -- the A53 chain silently
     * hangs right after "Starting ATF on ARM64 core...".
     *
     * Force each A53 core back through cpu_reset() so it lands in its
     * start-powered-off (PSCI_OFF) reset state, exactly as on a cold
     * boot, making the TISCI cold-start on the following boot effective.
     */
    if (r->soc) {
        for (unsigned i = 0; i < r->soc->a53_cpus; i++) {
            cpu_reset(CPU(&r->soc->a53[i]));
        }

        /*
         * The M4 (NeoVisor) shares the exact same gap: nothing else in
         * the -bios/ROM-boot path resets the armv7m core on system_reset
         * (armv7m_load_kernel's reset handler, the only thing that
         * normally does this for M-profile cores, is never installed
         * here). Without this the M4 keeps running the boot-1 firmware
         * across the reset while the R5 SPL reloads it into the same
         * MCU SRAM underneath it, corrupting code/vectors and causing a
         * double-fault lockup. Reset it back to its start-powered-off
         * state, exactly as at cold boot, so it re-parks until TISCI
         * restarts it.
         */
        if (r->soc->armv7m.cpu) {
            cpu_reset(CPU(r->soc->armv7m.cpu));
        }
    }

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
        /*
         * SYSFW / SYSFW-DATA target DMSC-internal memory: discarded,
         * ti-dmsc emulates TIFS.
         */
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
    r->soc = soc;
    qemu_register_reset(k3_bootrom_cpu_reset, r);
    trace_k3_bootrom_boot(sbl->dest_addr);
}
