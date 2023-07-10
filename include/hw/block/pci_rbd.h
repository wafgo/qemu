/*
 * Remote PCI Block Device
 *
 * Copyright Continental Automotive Technologies
 *
 * Authors:
 *  Wadim Mueller   <wadim.mueller@continental.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PCI_REMOTE_BD_H
#define QEMU_PCI_REMOTE_BD_H

#include "hw/block/block.h"
#include "sysemu/iothread.h"
#include "sysemu/block-backend.h"
#include "sysemu/block-ram-registrar.h"
#include "qom/object.h"
#include "hw/pci/pci_device.h"

struct PCIRemoteBdState {
    PCIDevice dev;
    BlockBackend *blk;
    MemoryRegion mmio;
    MemoryRegion portio;
};

#define TYPE_PCI_REMOTE_BLK "pci-remote-blk"
OBJECT_DECLARE_SIMPLE_TYPE(PCIRemoteBdState, PCI_REMOTE_BLK)
#endif
