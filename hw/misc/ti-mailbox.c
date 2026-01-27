/*
 * TI AM64x/OMAP Mailbox
 *
 * Copyright (c) 2025 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/misc/ti-mailbox.h"
#include "trace.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "qom/object.h"

#define TI_MAILBOX_MMIO_SIZE 0x200

#define MAILBOX_REVISION         0x00
#define MAILBOX_SYSCONFIG        0x10
#define MAILBOX_MESSAGE_BASE     0x40
#define MAILBOX_FIFO_STATUS_BASE 0x80
#define MAILBOX_MSG_STATUS_BASE  0xC0
#define MAILBOX_IRQ_RAW_BASE     0x100
#define MAILBOX_IRQ_CLR_BASE     0x104
#define MAILBOX_IRQ_EN_SET_BASE  0x108
#define MAILBOX_IRQ_EN_CLR_BASE  0x10C
#define MAILBOX_IRQ_STRIDE       0x10
#define MAILBOX_IRQ_EOI          0x140

#define TI_MAILBOX_PULSE_NS_DEFAULT 1

struct TIMailboxPulse {
    TIMailboxState *s;
    int user;
    bool clear_mask;
};

static uint32_t ti_mailbox_hw_raw_status(const TIMailboxState *s)
{
    uint32_t raw = 0;

    for (int i = 0; i < TI_MAILBOX_NUM_MBOX; i++) {
        const Fifo32 *f = &s->mbox[i];
        uint32_t count = fifo32_num_used((Fifo32 *)f);

        if (count > 0) {
            raw |= BIT(i * 2);
        }
        if (count < s->fifo_depth) {
            raw |= BIT(i * 2 + 1);
        }
    }

    return raw;
}

static void ti_mailbox_update_irqs(TIMailboxState *s)
{
    uint32_t hw_raw = ti_mailbox_hw_raw_status(s);

    for (int user = 0; user < s->num_users; user++) {
        TIMailboxUser *u = &s->users[user];
        uint32_t raw = (hw_raw & ~u->raw_clear_mask) | u->raw_set;
        uint32_t masked = raw & u->irq_enable;
        bool level = masked != 0;
        if (level != u->irq_level) {
            trace_ti_mailbox_irq(s->mailbox_id, user, level, raw,
                                 u->irq_enable, masked);
            u->irq_level = level;
        }
        qemu_set_irq(u->irq, level);
    }
}

static void ti_mailbox_pulse_expire(void *opaque)
{
    TIMailboxPulse *p = opaque;
    TIMailboxState *s = p->s;
    TIMailboxUser *u = &s->users[p->user];

    if (p->clear_mask) {
        u->raw_clear_mask = 0;
    } else {
        u->raw_set = 0;
    }

    ti_mailbox_update_irqs(s);
}

static void ti_mailbox_arm_pulse(TIMailboxState *s, QEMUTimer *timer)
{
    timer_mod(timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              TI_MAILBOX_PULSE_NS_DEFAULT);
}

static bool ti_mailbox_fifo_push(Fifo32 *f, uint8_t depth, uint32_t val)
{
    if (fifo32_num_used(f) >= depth) {
        return false;
    }

    fifo32_push(f, val);
    return true;
}

static bool ti_mailbox_fifo_pop(Fifo32 *f, uint8_t depth, uint32_t *val)
{
    (void)depth;

    if (fifo32_is_empty(f)) {
        return false;
    }
    *val = fifo32_pop(f);
    return true;
}

static void ti_mailbox_fmt_opt_int(char *buf, size_t len,
                                   const char *key, int value)
{
    if (value < 0) {
        buf[0] = '\0';
        return;
    }

    snprintf(buf, len, " %s=%d", key, value);
}

static void ti_mailbox_fmt_cpu(char *buf, size_t len)
{
    if (!current_cpu) {
        snprintf(buf, len, " cpu=?");
        return;
    }

    snprintf(buf, len, " cpu=%s:%d", object_get_typename(OBJECT(current_cpu)),
             current_cpu->cpu_index);
}

static void ti_mailbox_irq_bits_to_str(uint32_t val, char *buf, size_t len)
{
    size_t pos = 0;
    bool first = true;

    for (int bit = 0; bit < TI_MAILBOX_NUM_MBOX * 2; bit++) {
        if (!(val & BIT(bit))) {
            continue;
        }
        if (pos < len) {
            int mbox = bit / 2;
            const char *event = (bit % 2 == 0) ? "newmsg" : "notfull";
            int n = snprintf(buf + pos, len - pos, "%s%s:%d",
                             first ? "" : ",", event, mbox);
            if (n < 0 || (size_t)n >= len - pos) {
                pos = len - 1;
                break;
            }
            pos += n;
            first = false;
        }
    }

    if (first) {
        g_strlcpy(buf, "-", len);
    }
}

static const char *ti_mailbox_reg_name(hwaddr off, int *mbox, int *user)
{
    *mbox = -1;
    *user = -1;

    switch (off) {
    case MAILBOX_REVISION:
        return "MAILBOX_REVISION";
    case MAILBOX_SYSCONFIG:
        return "MAILBOX_SYSCONFIG";
    case MAILBOX_IRQ_EOI:
        return "MAILBOX_IRQ_EOI";
    default:
        break;
    }

    if (off >= MAILBOX_MESSAGE_BASE &&
        off < MAILBOX_MESSAGE_BASE + TI_MAILBOX_NUM_MBOX * 4) {
        *mbox = (off - MAILBOX_MESSAGE_BASE) / 4;
        return "MAILBOX_MESSAGE_y";
    }

    if (off >= MAILBOX_FIFO_STATUS_BASE &&
        off < MAILBOX_FIFO_STATUS_BASE + TI_MAILBOX_NUM_MBOX * 4) {
        *mbox = (off - MAILBOX_FIFO_STATUS_BASE) / 4;
        return "MAILBOX_FIFO_STATUS_y";
    }

    if (off >= MAILBOX_MSG_STATUS_BASE &&
        off < MAILBOX_MSG_STATUS_BASE + TI_MAILBOX_NUM_MBOX * 4) {
        *mbox = (off - MAILBOX_MSG_STATUS_BASE) / 4;
        return "MAILBOX_MSG_STATUS_y";
    }

    if (off >= MAILBOX_IRQ_RAW_BASE &&
        off < MAILBOX_IRQ_RAW_BASE + TI_MAILBOX_NUM_USERS_MAX * MAILBOX_IRQ_STRIDE) {
        int rel;

        *user = (off - MAILBOX_IRQ_RAW_BASE) / MAILBOX_IRQ_STRIDE;
        rel = (off - MAILBOX_IRQ_RAW_BASE) % MAILBOX_IRQ_STRIDE;
        switch (rel) {
        case 0:
            return "MAILBOX_IRQ_STATUS_RAW_j";
        case 4:
            return "MAILBOX_IRQ_STATUS_CLR_j";
        case 8:
            return "MAILBOX_IRQ_ENABLE_SET_j";
        case 0x0C:
            return "MAILBOX_IRQ_ENABLE_CLR_j";
        default:
            return "MAILBOX_IRQ_UNKNOWN";
        }
    }

    return "UNKNOWN";
}


static uint64_t ti_mailbox_read(void *opaque, hwaddr off, unsigned size)
{
    TIMailboxState *s = opaque;
    uint32_t hw_raw;
    uint32_t raw;
    uint32_t val;
    int mb;
    int user;

    (void)size;

#define TI_MAILBOX_TRACE_READ(_val)                                      \
    do {                                                                 \
        char irq_bits[64];                                               \
        char trace_cpu_str[64];                                          \
        char trace_mbox_str[24];                                         \
        char trace_user_str[24];                                         \
        char trace_bits_str[96];                                         \
        int trace_mbox;                                                  \
        int trace_user;                                                  \
        const char *trace_reg =                                          \
            ti_mailbox_reg_name(off, &trace_mbox, &trace_user);          \
        const char *trace_bits = "-";                                    \
        const char *trace_bits_strp = "";                                \
        if (!strcmp(trace_reg, "MAILBOX_IRQ_STATUS_RAW_j") ||            \
            !strcmp(trace_reg, "MAILBOX_IRQ_STATUS_CLR_j") ||            \
            !strcmp(trace_reg, "MAILBOX_IRQ_ENABLE_SET_j") ||            \
            !strcmp(trace_reg, "MAILBOX_IRQ_ENABLE_CLR_j")) {            \
            ti_mailbox_irq_bits_to_str((uint32_t)(_val), irq_bits,        \
                                       sizeof(irq_bits));                \
            trace_bits = irq_bits;                                       \
            snprintf(trace_bits_str, sizeof(trace_bits_str),             \
                     " irq_bits=%s", trace_bits);                        \
            trace_bits_strp = trace_bits_str;                            \
        }                                                                \
        ti_mailbox_fmt_cpu(trace_cpu_str, sizeof(trace_cpu_str));        \
        ti_mailbox_fmt_opt_int(trace_mbox_str, sizeof(trace_mbox_str),   \
                               "mbox", trace_mbox);                      \
        ti_mailbox_fmt_opt_int(trace_user_str, sizeof(trace_user_str),   \
                               "user", trace_user);                      \
        trace_ti_mailbox_read((uint64_t)off, (uint32_t)(_val), size,      \
                              trace_reg, s->mailbox_id, trace_cpu_str,   \
                              trace_mbox_str, trace_user_str,            \
                              trace_bits_strp);                          \
        return (_val);                                                   \
    } while (0)

    switch (off) {
    case MAILBOX_REVISION:
        TI_MAILBOX_TRACE_READ(0x66FC8900);
    case MAILBOX_SYSCONFIG:
        TI_MAILBOX_TRACE_READ(0);
    case MAILBOX_IRQ_EOI:
        TI_MAILBOX_TRACE_READ(0);
    default:
        break;
    }

    if (off >= MAILBOX_MESSAGE_BASE &&
        off < MAILBOX_MESSAGE_BASE + TI_MAILBOX_NUM_MBOX * 4) {
        mb = (off - MAILBOX_MESSAGE_BASE) / 4;
        if (!ti_mailbox_fifo_pop(&s->mbox[mb], s->fifo_depth, &val)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: read empty mailbox %d\n",
                          TYPE_TI_MAILBOX, mb);
            TI_MAILBOX_TRACE_READ(0);
        }
        ti_mailbox_update_irqs(s);
        TI_MAILBOX_TRACE_READ(val);
    }

    if (off >= MAILBOX_FIFO_STATUS_BASE &&
        off < MAILBOX_FIFO_STATUS_BASE + TI_MAILBOX_NUM_MBOX * 4) {
        mb = (off - MAILBOX_FIFO_STATUS_BASE) / 4;
        TI_MAILBOX_TRACE_READ(fifo32_num_used(&s->mbox[mb]) >=
                              s->fifo_depth ? 1 : 0);
    }

    if (off >= MAILBOX_MSG_STATUS_BASE &&
        off < MAILBOX_MSG_STATUS_BASE + TI_MAILBOX_NUM_MBOX * 4) {
        mb = (off - MAILBOX_MSG_STATUS_BASE) / 4;
        TI_MAILBOX_TRACE_READ(fifo32_num_used(&s->mbox[mb]) & 0x7);
    }

    if (off >= MAILBOX_IRQ_RAW_BASE &&
        off < MAILBOX_IRQ_RAW_BASE + TI_MAILBOX_NUM_USERS_MAX * MAILBOX_IRQ_STRIDE) {
        user = (off - MAILBOX_IRQ_RAW_BASE) / MAILBOX_IRQ_STRIDE;
        if (user >= s->num_users) {
            TI_MAILBOX_TRACE_READ(0);
        }
        hw_raw = ti_mailbox_hw_raw_status(s);
        raw = (hw_raw & ~s->users[user].raw_clear_mask) |
            s->users[user].raw_set;
        if ((off - MAILBOX_IRQ_RAW_BASE) % MAILBOX_IRQ_STRIDE == 0) {
            TI_MAILBOX_TRACE_READ(raw);
        }
        if ((off - MAILBOX_IRQ_RAW_BASE) % MAILBOX_IRQ_STRIDE == 4) {
            TI_MAILBOX_TRACE_READ(raw & s->users[user].irq_enable);
        }
        if ((off - MAILBOX_IRQ_RAW_BASE) % MAILBOX_IRQ_STRIDE == 8) {
            TI_MAILBOX_TRACE_READ(s->users[user].irq_enable);
        }
        if ((off - MAILBOX_IRQ_RAW_BASE) % MAILBOX_IRQ_STRIDE == 0x0C) {
            TI_MAILBOX_TRACE_READ(s->users[user].irq_enable);
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: invalid read offset 0x%"HWADDR_PRIx"\n",
                  TYPE_TI_MAILBOX, off);
    TI_MAILBOX_TRACE_READ(0);

#undef TI_MAILBOX_TRACE_READ
}

static void ti_mailbox_write(void *opaque, hwaddr off, uint64_t val,
                             unsigned size)
{
    TIMailboxState *s = opaque;
    int mb;
    int user;
    int rel;

    (void)size;

    {
        char irq_bits[64];
        char trace_cpu_str[64];
        char trace_mbox_str[24];
        char trace_user_str[24];
        char trace_bits_str[96];
        int trace_mbox;
        int trace_user;
        const char *trace_reg = ti_mailbox_reg_name(off, &trace_mbox,
                                                    &trace_user);
        const char *trace_bits = "-";
        const char *trace_bits_strp = "";

        if (!strcmp(trace_reg, "MAILBOX_IRQ_STATUS_RAW_j") ||
            !strcmp(trace_reg, "MAILBOX_IRQ_STATUS_CLR_j") ||
            !strcmp(trace_reg, "MAILBOX_IRQ_ENABLE_SET_j") ||
            !strcmp(trace_reg, "MAILBOX_IRQ_ENABLE_CLR_j")) {
            ti_mailbox_irq_bits_to_str((uint32_t)val, irq_bits,
                                       sizeof(irq_bits));
            trace_bits = irq_bits;
            snprintf(trace_bits_str, sizeof(trace_bits_str),
                     " irq_bits=%s", trace_bits);
            trace_bits_strp = trace_bits_str;
        }

        ti_mailbox_fmt_cpu(trace_cpu_str, sizeof(trace_cpu_str));
        ti_mailbox_fmt_opt_int(trace_mbox_str, sizeof(trace_mbox_str),
                               "mbox", trace_mbox);
        ti_mailbox_fmt_opt_int(trace_user_str, sizeof(trace_user_str),
                               "user", trace_user);
        trace_ti_mailbox_write((uint64_t)off, (uint32_t)val, size,
                               trace_reg, s->mailbox_id, trace_cpu_str,
                               trace_mbox_str, trace_user_str,
                               trace_bits_strp);
    }

    switch (off) {
    case MAILBOX_SYSCONFIG:
        if (val & 0x1) {
            for (int i = 0; i < TI_MAILBOX_NUM_MBOX; i++) {
                fifo32_reset(&s->mbox[i]);
            }
            for (int i = 0; i < s->num_users; i++) {
                TIMailboxUser *u = &s->users[i];

                u->irq_enable = 0;
                u->raw_set = 0;
                u->raw_clear_mask = 0;
            }
            ti_mailbox_update_irqs(s);
        }
        return;
    case MAILBOX_IRQ_EOI:
        return;
    default:
        break;
    }

    if (off >= MAILBOX_MESSAGE_BASE &&
        off < MAILBOX_MESSAGE_BASE + TI_MAILBOX_NUM_MBOX * 4) {
        mb = (off - MAILBOX_MESSAGE_BASE) / 4;
        if (!ti_mailbox_fifo_push(&s->mbox[mb], s->fifo_depth, (uint32_t)val)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: mailbox %d full\n",
                          TYPE_TI_MAILBOX, mb);
            return;
        }
        ti_mailbox_update_irqs(s);
        return;
    }

    if (off >= MAILBOX_IRQ_RAW_BASE &&
        off < MAILBOX_IRQ_RAW_BASE + TI_MAILBOX_NUM_USERS_MAX * MAILBOX_IRQ_STRIDE) {
        user = (off - MAILBOX_IRQ_RAW_BASE) / MAILBOX_IRQ_STRIDE;
        if (user >= s->num_users) {
            return;
        }
        rel = (off - MAILBOX_IRQ_RAW_BASE) % MAILBOX_IRQ_STRIDE;
        uint32_t mask = (uint32_t)val;

        if (rel == 0) {
            if (mask == 0) {
                return;
            }
            s->users[user].raw_set |= mask;
            ti_mailbox_arm_pulse(s, s->users[user].raw_pulse_timer);
            ti_mailbox_update_irqs(s);
            return;
        }
        if (rel == 4) {
            if (mask == 0) {
                return;
            }
            s->users[user].raw_set &= ~mask;
            s->users[user].raw_clear_mask |= mask;
            ti_mailbox_arm_pulse(s, s->users[user].clr_pulse_timer);
            ti_mailbox_update_irqs(s);
            return;
        }
        if (rel == 8) {
            s->users[user].irq_enable |= mask;
            ti_mailbox_update_irqs(s);
            return;
        }
        if (rel == 0x0C) {
            s->users[user].irq_enable &= ~mask;
            ti_mailbox_update_irqs(s);
            return;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: invalid write offset 0x%"HWADDR_PRIx"\n",
                  TYPE_TI_MAILBOX, off);
}

static const MemoryRegionOps ti_mailbox_ops = {
    .read = ti_mailbox_read,
    .write = ti_mailbox_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ti_mailbox_reset(DeviceState *dev)
{
    TIMailboxState *s = TI_MAILBOX(dev);

    for (int i = 0; i < TI_MAILBOX_NUM_MBOX; i++) {
        fifo32_reset(&s->mbox[i]);
    }
    for (int i = 0; i < s->num_users; i++) {
        TIMailboxUser *u = &s->users[i];

        u->irq_enable = 0;
        u->raw_set = 0;
        u->raw_clear_mask = 0;
        u->irq_level = false;
        if (u->raw_pulse_timer) {
            timer_del(u->raw_pulse_timer);
        }
        if (u->clr_pulse_timer) {
            timer_del(u->clr_pulse_timer);
        }
    }

    ti_mailbox_update_irqs(s);
}

static void ti_mailbox_realize(DeviceState *dev, Error **errp)
{
    TIMailboxState *s = TI_MAILBOX(dev);

    if (s->num_users == 0 || s->num_users > TI_MAILBOX_NUM_USERS_MAX) {
        error_setg(errp, "num-users must be between 1 and %u",
                   TI_MAILBOX_NUM_USERS_MAX);
        return;
    }

    if (s->fifo_depth == 0 || s->fifo_depth > TI_MAILBOX_FIFO_DEPTH_MAX) {
        error_setg(errp, "fifo-depth must be between 1 and %u",
                   TI_MAILBOX_FIFO_DEPTH_MAX);
        return;
    }

    for (int i = 0; i < TI_MAILBOX_NUM_MBOX; i++) {
        fifo32_create(&s->mbox[i], s->fifo_depth);
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &ti_mailbox_ops, s,
                          TYPE_TI_MAILBOX, TI_MAILBOX_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    for (int i = 0; i < s->num_users; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->users[i].irq);
    }

    for (int i = 0; i < s->num_users; i++) {
        TIMailboxUser *u = &s->users[i];

        u->raw_pulse = g_new0(TIMailboxPulse, 1);
        u->raw_pulse->s = s;
        u->raw_pulse->user = i;
        u->raw_pulse->clear_mask = false;
        u->raw_pulse_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          ti_mailbox_pulse_expire,
                                          u->raw_pulse);

        u->clr_pulse = g_new0(TIMailboxPulse, 1);
        u->clr_pulse->s = s;
        u->clr_pulse->user = i;
        u->clr_pulse->clear_mask = true;
        u->clr_pulse_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          ti_mailbox_pulse_expire,
                                          u->clr_pulse);
    }
}

static void ti_mailbox_finalize(Object *obj)
{
    TIMailboxState *s = TI_MAILBOX(obj);

    for (int i = 0; i < TI_MAILBOX_NUM_MBOX; i++) {
        fifo32_destroy(&s->mbox[i]);
    }
    for (int i = 0; i < s->num_users; i++) {
        TIMailboxUser *u = &s->users[i];

        if (u->raw_pulse_timer) {
            timer_free(u->raw_pulse_timer);
        }
        if (u->clr_pulse_timer) {
            timer_free(u->clr_pulse_timer);
        }
        g_free(u->raw_pulse);
        g_free(u->clr_pulse);
    }
}

static const Property ti_mailbox_properties[] = {
    DEFINE_PROP_UINT8("num-users", TIMailboxState, num_users,
                      TI_MAILBOX_NUM_USERS_DEFAULT),
    DEFINE_PROP_UINT8("fifo-depth", TIMailboxState, fifo_depth,
                      TI_MAILBOX_FIFO_DEPTH_DEFAULT),
    DEFINE_PROP_UINT8("mailbox-id", TIMailboxState, mailbox_id, 0),
};

static void ti_mailbox_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ti_mailbox_reset);
    dc->realize = ti_mailbox_realize;
    dc->user_creatable = false;
    device_class_set_props(dc, ti_mailbox_properties);
}

static const TypeInfo ti_mailbox_types[] = {{
    .name = TYPE_TI_MAILBOX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIMailboxState),
    .class_init = ti_mailbox_class_init,
    .instance_finalize = ti_mailbox_finalize,
}};

DEFINE_TYPES(ti_mailbox_types)
