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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "block/block_int.h"
#include "trace.h"
#include "hw/block/block.h"
#include "hw/qdev-properties.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-ram-registrar.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "hw/virtio/virtio-blk.h"
#include "dataplane/virtio-blk.h"
#include "scsi/constants.h"
#include "migration/qemu-file-types.h"
#include "hw/pci/pci_device.h"

#include "hw/block/pci_rbd.h"
#include "qemu/coroutine.h"

static void pci_remote_bd_instance_init(Object *obj)
{
    /* VirtIOBlock *s = VIRTIO_BLK(obj); */

    /* device_add_bootindex_property(obj, &s->conf.conf.bootindex, */
    /*                               "bootindex", "/disk@0,0", */
    /*                               DEVICE(obj)); */
}

/* static const VMStateDescription vmstate_virtio_blk = { */
/*     .name = "virtio-blk", */
/*     .minimum_version_id = 2, */
/*     .version_id = 2, */
/*     .fields = (VMStateField[]) { */
/*         VMSTATE_VIRTIO_DEVICE, */
/*         VMSTATE_END_OF_LIST() */
/*     }, */
/* }; */

/* static Property virtio_blk_properties[] = { */
/*     DEFINE_BLOCK_PROPERTIES(VirtIOBlock, conf.conf), */
/*     DEFINE_BLOCK_ERROR_PROPERTIES(VirtIOBlock, conf.conf), */
/*     DEFINE_BLOCK_CHS_PROPERTIES(VirtIOBlock, conf.conf), */
/*     DEFINE_PROP_STRING("serial", VirtIOBlock, conf.serial), */
/*     DEFINE_PROP_BIT64("config-wce", VirtIOBlock, host_features, */
/*                       VIRTIO_BLK_F_CONFIG_WCE, true), */
/* #ifdef __linux__ */
/*     DEFINE_PROP_BIT64("scsi", VirtIOBlock, host_features, */
/*                       VIRTIO_BLK_F_SCSI, false), */
/* #endif */
/*     DEFINE_PROP_BIT("request-merging", VirtIOBlock, conf.request_merging, 0, */
/*                     true), */
/*     DEFINE_PROP_UINT16("num-queues", VirtIOBlock, conf.num_queues, */
/*                        VIRTIO_BLK_AUTO_NUM_QUEUES), */
/*     DEFINE_PROP_UINT16("queue-size", VirtIOBlock, conf.queue_size, 256), */
/*     DEFINE_PROP_BOOL("seg-max-adjust", VirtIOBlock, conf.seg_max_adjust, true), */
/*     DEFINE_PROP_LINK("iothread", VirtIOBlock, conf.iothread, TYPE_IOTHREAD, */
/*                      IOThread *), */
/*     DEFINE_PROP_BIT64("discard", VirtIOBlock, host_features, */
/*                       VIRTIO_BLK_F_DISCARD, true), */
/*     DEFINE_PROP_BOOL("report-discard-granularity", VirtIOBlock, */
/*                      conf.report_discard_granularity, true), */
/*     DEFINE_PROP_BIT64("write-zeroes", VirtIOBlock, host_features, */
/*                       VIRTIO_BLK_F_WRITE_ZEROES, true), */
/*     DEFINE_PROP_UINT32("max-discard-sectors", VirtIOBlock, */
/*                        conf.max_discard_sectors, BDRV_REQUEST_MAX_SECTORS), */
/*     DEFINE_PROP_UINT32("max-write-zeroes-sectors", VirtIOBlock, */
/*                        conf.max_write_zeroes_sectors, BDRV_REQUEST_MAX_SECTORS), */
/*     DEFINE_PROP_BOOL("x-enable-wce-if-config-wce", VirtIOBlock, */
/*                      conf.x_enable_wce_if_config_wce, true), */
/*     DEFINE_PROP_END_OF_LIST(), */
/* }; */

#define PCI_RBD_IO_MEMSIZE 2048

static uint64_t pci_rbd_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    printf("------------> READING FROM 0x%lx @ 0x%x\n", addr, size);
    return 0;
}

static void pci_rbd_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                       unsigned size)
{
    printf("------------> WRITING TO ADDR 0x%lx = 0x%lx @ 0x%x\n", addr, val, size);
}

static const MemoryRegionOps pci_rbd_mmio_ops = {
    .read = pci_rbd_mmio_read,
    .write = pci_rbd_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pci_remote_bd_realize(PCIDevice *dev, Error **errp)
{
    PCIRemoteBdState *prb = DO_UPCAST(PCIRemoteBdState, dev, dev);

    uint8_t *pci_conf;

    pci_conf = prb->dev.config;
    pci_conf[PCI_INTERRUPT_PIN] = 0; /* no interrupt pin */

    memory_region_init_io(&prb->mmio, OBJECT(prb), &pci_rbd_mmio_ops, prb,
                          "pci-rbd-mmio", PCI_RBD_IO_MEMSIZE * 2);
    pci_register_bar(&prb->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &prb->mmio);
}

static void pci_remote_bd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    
    pc->realize = pci_remote_bd_realize;
    pc->vendor_id = PCI_VENDOR_ID_QEMU;
    pc->device_id = 0x0005;
    pc->revision = 0x00;
    pc->class_id = PCI_CLASS_OTHERS;
    dc->desc = "PCI Remote Block Device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pci_remote_blk_info = {
    .name = TYPE_PCI_REMOTE_BLK,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIRemoteBdState),
    .instance_init = pci_remote_bd_instance_init,
    .class_init = pci_remote_bd_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};


static void pci_rbd_register_types(void)
{
    type_register_static(&pci_remote_blk_info);
}

type_init(pci_rbd_register_types)
