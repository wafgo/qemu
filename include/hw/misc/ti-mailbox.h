#ifndef TI_MAILBOX_H
#define TI_MAILBOX_H

#include "qemu/osdep.h"
#include "qemu/fifo32.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_MAILBOX "ti-mailbox"
OBJECT_DECLARE_SIMPLE_TYPE(TIMailboxState, TI_MAILBOX)

#define TI_MAILBOX_NUM_MBOX 16
#define TI_MAILBOX_NUM_USERS_MAX 4
#define TI_MAILBOX_NUM_USERS_DEFAULT 4
#define TI_MAILBOX_FIFO_DEPTH_MAX 4
#define TI_MAILBOX_FIFO_DEPTH_DEFAULT 4

typedef struct TIMailboxPulse TIMailboxPulse;

typedef struct TIMailboxUser {
    uint32_t irq_enable;
    uint32_t raw_set;
    uint32_t raw_clear_mask;
    /*
     * AM64xx User Manual: RAW write-1 is a short pulse; CLR write-1 clears for ~2 cycles
     * and then the interrupt reasserts if the condition is still pending.
     * We model both behaviors with a timed pulse/clear.
     */
    TIMailboxPulse *raw_pulse;
    TIMailboxPulse *clr_pulse;
    QEMUTimer *raw_pulse_timer;
    QEMUTimer *clr_pulse_timer;
    bool irq_level;
    qemu_irq irq;
} TIMailboxUser;

struct TIMailboxState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    Fifo32 mbox[TI_MAILBOX_NUM_MBOX];
    TIMailboxUser users[TI_MAILBOX_NUM_USERS_MAX];
    uint8_t num_users;
    uint8_t fifo_depth;
    uint8_t mailbox_id;
};

#endif
