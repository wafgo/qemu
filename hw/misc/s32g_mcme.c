/*
 * S32 Mode Entry Module
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/s32g_mcme.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "target/arm/arm-powerctl.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#include <stdbool.h>
#include "exec/hwaddr.h"

#define DEBUG_S32G_MCME 0

#ifndef DEBUG_S32G_MCME
#define DEBUG_S32G_MCME 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_S32G_MCME) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_S32_MCME, \
                                             __func__, ##args); \
        } \
    } while (0)


static uint32_t* region_to_pt_regs(S32MCMEState *s, mcme_region_t region)
{
    switch (region) {
    case MCME_REGION_CONTROL:
        return s->ctrl_regs;
    case MCME_REGION_PARTITION0:
        return s->part0_regs;
    case MCME_REGION_PARTITION1:
        return s->part1_regs;
    case MCME_REGION_PARTITION2:
        return s->part2_regs;
    case MCME_REGION_PARTITION3:
        return s->part3_regs;
    default:
        return NULL;
    }
}

static mcme_region_t s32_mcme_region_type_from_offset(hwaddr offset)
{
    switch(offset) {
    case MC_ME_CTL_KEY_OFFSET ... MC_ME_MAIN_COREID_OFFSET:
        return MCME_REGION_CONTROL;
    case MC_ME_PRTN0_PCONF_OFFSET ... MC_ME_PRTN0_CORE3_ADDR_OFFSET:
        return MCME_REGION_PARTITION0;
    case MC_ME_PRTN1_PCONF_OFFSET ... MC_ME_PRTN1_CORE3_ADDR_OFFSET:
        return MCME_REGION_PARTITION1;
    case MC_ME_PRTN2_PCONF_OFFSET ... MC_ME_PRTN2_COFB0_CLKEN_OFFSET:
        return MCME_REGION_PARTITION2;
    case MC_ME_PRTN3_PCONF_OFFSET ... MC_ME_PRTN3_COFB0_CLKEN_OFFSET:
        return MCME_REGION_PARTITION3;
    default:
        return MCME_REGION_NO;
    }
}

static uint32_t mcme_offset2idx(hwaddr offset)
{
    mcme_region_t region = s32_mcme_region_type_from_offset(offset);
    switch (region) {
    case MCME_REGION_CONTROL:
        return offset / 4;
    case MCME_REGION_PARTITION0:
        return (offset - MC_ME_PRTN0_PCONF_OFFSET) / 4;
    case MCME_REGION_PARTITION1:
        return (offset - MC_ME_PRTN1_PCONF_OFFSET) / 4;
    case MCME_REGION_PARTITION2:
        return (offset - MC_ME_PRTN2_PCONF_OFFSET) / 4;
    case MCME_REGION_PARTITION3:
        return (offset - MC_ME_PRTN3_PCONF_OFFSET) / 4;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_MCME, __func__, offset);
        return 0;
    }
}

static const char *s32_mcme_region_name(mcme_region_t region)
{
    static char region_name[40];
    switch (region) {
    case MCME_REGION_CONTROL:
        return "MCME_REGION_CONTROL";
    case MCME_REGION_PARTITION0 ... MCME_REGION_PARTITION3:
        snprintf(region_name, sizeof(region_name), "MCME_REGION_PARTITION_%u", region - 1);
        return region_name;
    default:
        snprintf(region_name, sizeof(region_name), "%u ?", region);
        return region_name;
    }
}

static const VMStateDescription vmstate_s32_mcme = {
    .name = TYPE_S32_MCME,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ctrl_regs, S32MCMEState, MC_ME_CTRL_REGS),
        VMSTATE_UINT32_ARRAY(part0_regs, S32MCMEState, MC_ME_PART0_REGS),
        VMSTATE_UINT32_ARRAY(part1_regs, S32MCMEState, MC_ME_PART1_REGS),
        VMSTATE_UINT32_ARRAY(part2_regs, S32MCMEState, MC_ME_PART2_REGS),
        VMSTATE_UINT32_ARRAY(part3_regs, S32MCMEState, MC_ME_PART3_REGS),
        VMSTATE_END_OF_LIST()
    },
};

static uint64_t s32_mcme_read(void *opaque, hwaddr offset, unsigned size)
{
    S32MCMEState *s = (S32MCMEState *)opaque;
    mcme_region_t region = s32_mcme_region_type_from_offset(offset);
    uint32_t idx = mcme_offset2idx(offset);
    uint32_t *regs = region_to_pt_regs(s, region);
    if (offset > MC_ME_PRTN3_COFB0_CLKEN_OFFSET) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_MCME, __func__, offset);
    }
    
    DPRINTF("offset: 0x%" HWADDR_PRIx " region: %s => 0x%" PRIx32 " idx = 0x%i\n", offset, s32_mcme_region_name(s32_mcme_region_type_from_offset(offset)), regs[idx], idx);


    return regs[idx];
}

static void s32_mcme_handle_control_write(S32MCMEState *s, hwaddr offset, uint64_t value, unsigned size)
{
    int bit = 0;
    int core_id = 0;
    int pt_region = MCME_REGION_PARTITION0;
    switch (offset) {
    case MC_ME_CTL_KEY_OFFSET:
        if (s->ctrl_regs[offset] == 0x5AF0 && value == 0xA50F) {
            s->unlocked = true;
            for (pt_region = MCME_REGION_PARTITION0; pt_region < MCME_REGION_NO; ++pt_region) {
                uint32_t* regs = region_to_pt_regs(s, pt_region);
                for (bit = 0; bit < 32; ++bit) {
                    /* if IP should be updated, the update bit is set */
                    if (regs[MCME_PART_UPD_OFFSET_INDEX] & BIT(bit)) {
                        regs[MCME_PART_UPD_OFFSET_INDEX] &= ~BIT(bit);
                        if (regs[MCME_PART_CONF_OFFSET_INDEX] & BIT(bit))
                            regs[MCME_PART_STATUS_OFFSET_INDEX] |= BIT(bit);
                        else
                            regs[MCME_PART_STATUS_OFFSET_INDEX] &= ~BIT(bit);
                    }
                    /* do the same for the core specific registers */
                    for (core_id = 0; core_id < MC_ME_MAX_CORE_ID; ++core_id) {
                        int core_id_offset = MCME_PART_CONF_CORE0_PUPD_INDEX + (core_id * 8);
                        if (regs[core_id_offset] & BIT(bit)) {
                            regs[core_id_offset] &= ~BIT(bit);
                            if (regs[core_id_offset - 1] & BIT(bit))
                                regs[core_id_offset + 1] |= BIT(bit);
                            else
                                regs[core_id_offset + 1] &= ~BIT(bit);
                        }
                        regs[core_id_offset + 1] |= BIT(31);
                    }
                }
            }
        }
        s->ctrl_regs[offset] = value;
        break;
    default:
        s->ctrl_regs[offset] = value;
        break;
    }
}

static void s32_mcme_handle_partition_write(S32MCMEState *s, hwaddr offset, uint64_t value, unsigned size, uint32_t *regs)
{
    uint32_t idx = mcme_offset2idx(offset);
    DPRINTF("%s offset: 0x%" HWADDR_PRIx ", value: 0x%" PRIx64 " idx: 0x%x \n", __FUNCTION__, offset, value, idx);
    switch(offset) {
    case MC_ME_PRTN0_COFB0_CLKEN_OFFSET:
    {
        uint32_t status_idx = mcme_offset2idx(MC_ME_PRTN0_COFB0_STAT_OFFSET);
        regs[status_idx] = value;
    }
    break;
    case MC_ME_PRTN2_COFB0_CLKEN_OFFSET:
    {
        uint32_t status_idx = mcme_offset2idx(MC_ME_PRTN2_COFB0_STAT_OFFSET);
        regs[status_idx] = value;
    }
    break;
    case MC_ME_PRTN3_COFB0_CLKEN_OFFSET:
    {
        uint32_t status_idx = mcme_offset2idx(MC_ME_PRTN3_COFB0_STAT_OFFSET);
        regs[status_idx] = value;
    }
    break;
    }
    regs[idx] = value;
}

static void s32_mcme_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    S32MCMEState *s = (S32MCMEState *)opaque;
    unsigned long current_value = value;
    mcme_region_t region = s32_mcme_region_type_from_offset(offset);

    DPRINTF("offset: 0x%" HWADDR_PRIx " region: %s <= 0x%" PRIx32 "\n", offset, s32_mcme_region_name(s32_mcme_region_type_from_offset(offset)),
            (uint32_t)current_value);

    if (offset >= MC_ME_PRTN3_COFB0_CLKEN_OFFSET) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_MCME, __func__, offset);
        return;
    }

    switch (region) {
    case MCME_REGION_CONTROL:
        s32_mcme_handle_control_write(s, offset, value, size);
        break;
    case MCME_REGION_PARTITION0 ... MCME_REGION_PARTITION3:
        s32_mcme_handle_partition_write(s, offset, value, size, region_to_pt_regs(s, region));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_S32_MCME, __func__, offset);
        return;
    }
}

static const struct MemoryRegionOps s32_mcme_ops = {
    .read = s32_mcme_read,
    .write = s32_mcme_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void s32_mcme_reset(DeviceState *dev)
{
    S32MCMEState *s = S32_MCME(dev);

    s->unlocked = false;
    memset(s->ctrl_regs, 0, sizeof(s->ctrl_regs));
    s->ctrl_regs[mcme_offset2idx(MC_ME_CTL_KEY_OFFSET)] = 0x00005AF0;

    memset(s->part0_regs, 0, sizeof(s->part0_regs));
    s->part0_regs[mcme_offset2idx(MC_ME_PRTN0_PCONF_OFFSET)] = 0x00000001;
    s->part0_regs[mcme_offset2idx(MC_ME_PRTN0_STAT_OFFSET)] = 0x00000001;

    memset(s->part1_regs, 0, sizeof(s->part1_regs));
    s->part1_regs[mcme_offset2idx(MC_ME_PRTN1_PCONF_OFFSET)] = 0x00000004;
    s->part1_regs[mcme_offset2idx(MC_ME_PRTN1_STAT_OFFSET)] = 0x00000004;

    memset(s->part2_regs, 0, sizeof(s->part2_regs));
    s->part2_regs[mcme_offset2idx(MC_ME_PRTN2_PCONF_OFFSET)] = 0x00000004;
    s->part2_regs[mcme_offset2idx(MC_ME_PRTN2_STAT_OFFSET)] = 0x00000004;

    memset(s->part3_regs, 0, sizeof(s->part3_regs));
    s->part3_regs[mcme_offset2idx(MC_ME_PRTN3_PCONF_OFFSET)] = 0x00000004;
    s->part3_regs[mcme_offset2idx(MC_ME_PRTN3_STAT_OFFSET)] = 0x00000004;
}

static void s32_mcme_realize(DeviceState *dev, Error **errp)
{
    S32MCMEState *s = S32_MCME(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &s32_mcme_ops, s,
                          TYPE_S32_MCME, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

/* static Property mcme_properties[] = { */
/*     DEFINE_PROP_UINT32("num-application-cores", S32MSCMState, num_app_cores, 4), */
/*      DEFINE_PROP_END_OF_LIST(), */
/* }; */

static void s32_mcme_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32_mcme_realize;
    dc->reset = s32_mcme_reset;
    dc->vmsd = &vmstate_s32_mcme;
    dc->desc = "S32 Mode Entry Module";
    /* device_class_set_props(dc, mcme_properties); */
}

static const TypeInfo s32_mcme_info = {
    .name          = TYPE_S32_MCME,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32MCMEState),
    .class_init    = s32_mcme_class_init,
};

static void s32_mcme_register_types(void)
{
    type_register_static(&s32_mcme_info);
}

type_init(s32_mcme_register_types)

