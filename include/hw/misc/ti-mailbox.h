#ifndef TI_MAILBOX_H
#define TI_MAILBOX_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_MAILBOX "ti-mailbox"
OBJECT_DECLARE_SIMPLE_TYPE(TIMailboxState, TI_MAILBOX)

#define TI_MAILBOX_NUM_USERS_MAX 4
#define TI_MAILBOX_NUM_USERS_DEFAULT 4
#define TI_MAILBOX_FIFO_DEPTH_MAX 4
#define TI_MAILBOX_FIFO_DEPTH_DEFAULT 4

typedef struct TIMailboxFifo {
    uint32_t fifo[TI_MAILBOX_FIFO_DEPTH_MAX];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} TIMailboxFifo;

struct TIMailboxState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    TIMailboxFifo mbox[16];
    uint32_t irq_enable[TI_MAILBOX_NUM_USERS_MAX];
    uint32_t raw_set[TI_MAILBOX_NUM_USERS_MAX];
    bool irq_level[TI_MAILBOX_NUM_USERS_MAX];
    qemu_irq irq[TI_MAILBOX_NUM_USERS_MAX];
    uint8_t num_users;
    uint8_t fifo_depth;
    uint8_t mailbox_id;
};

#endif
