/*
 * Renault LShape Board System emulation.
 *
 * Copyright (c) 2021 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates a lshape board, with a Renesas RCar3 r8a77965 (M3N) SoC
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/arm/rcar3.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "hw/loader.h"

static void lshape_init(MachineState *machine)
{
  DriveInfo *di_user_part;
  DriveInfo *di_boot_part1;
  DriveInfo *di_boot_part2;
  BlockBackend *blk_user_part;
  BlockBackend *blk_boot_part1;
  BlockBackend *blk_boot_part2;
  BusState *bus;
  DeviceState *carddev;
  RCar3State *rc3 = RCAR3(object_new(TYPE_R8A77965));
  machine->smp.max_cpus = 4;
  object_initialize_child(OBJECT(machine), "soc", rc3, TYPE_R8A77965);
    
  memory_region_add_subregion(get_system_memory(), 0x40000000, machine->ram);
  qdev_realize(DEVICE(rc3), NULL, &error_fatal);

  di_boot_part1 = drive_get_next(IF_SD);
  di_boot_part2 = drive_get_next(IF_SD);
  di_user_part = drive_get_next(IF_SD);

  blk_user_part = di_user_part ? blk_by_legacy_dinfo(di_user_part) : NULL;
  blk_boot_part1 = di_boot_part1 ? blk_by_legacy_dinfo(di_boot_part1) : NULL;
  blk_boot_part2 = di_boot_part2 ? blk_by_legacy_dinfo(di_boot_part2) : NULL;
  
  bus = qdev_get_child_bus(DEVICE(&rc3->sdhi[2]), "sd-bus");
  if (bus == NULL) {
    error_report("No SD bus found in SOC object");
    exit(1);
  }
  carddev = qdev_new(TYPE_SD_CARD);
  
  qdev_prop_set_drive_err(carddev, "boot1", blk_boot_part1, &error_fatal);
  qdev_prop_set_drive_err(carddev, "boot2", blk_boot_part2, &error_fatal);
  qdev_prop_set_drive_err(carddev, "user_part", blk_user_part, &error_fatal);

  qdev_prop_set_bit(DEVICE(carddev), "mmc", true);
  qdev_prop_set_uint8(DEVICE(carddev), "mid", 0x90);
  qdev_prop_set_string(DEVICE(carddev), "pnm", "H8G4a2");
  qdev_prop_set_uint8(DEVICE(carddev), "oid", 0x4a);
  qdev_realize_and_unref(carddev, bus, &error_fatal);

#if 0
  static struct arm_boot_info lshape_bootinfo;
  lshape_bootinfo.ram_size = machine->ram_size;
  lshape_bootinfo.nb_cpus = 2;
  lshape_bootinfo.board_id = -1;
  lshape_bootinfo.loader_start = 0xe6303a00;
  lshape_bootinfo.skip_dtb_autoload = true;
  lshape_bootinfo.firmware_loaded = false;
  arm_load_kernel(ARM_CPU(first_cpu), machine, &lshape_bootinfo);
#else

#endif
}

static void lshape_machine_init(MachineClass *mc)
{
    mc->desc = "Renault LShape Cluster (RCar M3N)";
    mc->init = lshape_init;
    mc->default_ram_size = 2 * GiB;
    mc->max_cpus = 8;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "lshape.ram";
}

DEFINE_MACHINE("lshape", lshape_machine_init)
