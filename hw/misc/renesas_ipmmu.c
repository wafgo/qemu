/*
 * Copyright (c) 2021 Wadim Mueller <wadim.mueller@mail.de>
 *
 *  renesas ipmmu (iommu) emulation
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/misc/renesas_ipmmu.h"
#include "hw/qdev-properties.h"


#define OFFSET_TO_REG_IDX(_os_) (_os_/4)
#define MK_IPMMU_REG_ENTRY(_nm_, _os_, _ca_, _ma_) {.name = _nm_, .offset = _os_, .cache_access = _ca_, .main_access = _ma_}
#define MK_IPMMU_CO_ENTRY(_nm_, _os_) MK_IPMMU_REG_ENTRY(_nm_, _os_, true, false)
#define MK_IPMMU_MO_ENTRY(_nm_, _os_) MK_IPMMU_REG_ENTRY(_nm_, _os_, false, true)
#define MK_IPMMU_COMO_ENTRY(_nm_, _os_) MK_IPMMU_REG_ENTRY(_nm_, _os_, true, true)

static struct RenesasIPMMURegister {
    hwaddr offset;
    const char *name;
    bool cache_access;
    bool main_access;
} ipmmu_regs[] = {
    MK_IPMMU_COMO_ENTRY("IMCTR0", 0x0),
    MK_IPMMU_COMO_ENTRY("IMCTR1", 0x40),
    MK_IPMMU_COMO_ENTRY("IMCTR2", 0x80),
    MK_IPMMU_COMO_ENTRY("IMCTR3", 0xC0),
    MK_IPMMU_COMO_ENTRY("IMCTR4", 0x100),
    MK_IPMMU_COMO_ENTRY("IMCTR5", 0x140),
    MK_IPMMU_COMO_ENTRY("IMCTR6", 0x180),
    MK_IPMMU_COMO_ENTRY("IMCTR7", 0x1C0),
    MK_IPMMU_MO_ENTRY("IMTTBCR0", 0x8),
    MK_IPMMU_MO_ENTRY("IMTTBCR1", 0x48),
    MK_IPMMU_MO_ENTRY("IMTTBCR2", 0x88),
    MK_IPMMU_MO_ENTRY("IMTTBCR3", 0xC8),
    MK_IPMMU_MO_ENTRY("IMTTBCR4", 0x108),
    MK_IPMMU_MO_ENTRY("IMTTBCR5", 0x148),
    MK_IPMMU_MO_ENTRY("IMTTBCR6", 0x188),
    MK_IPMMU_MO_ENTRY("IMTTBCR7", 0x1C8),
    MK_IPMMU_MO_ENTRY("IMTTLBR00", 0x10),
    MK_IPMMU_MO_ENTRY("IMTTLBR01", 0x50),
    MK_IPMMU_MO_ENTRY("IMTTLBR02", 0x90),
    MK_IPMMU_MO_ENTRY("IMTTLBR03", 0xD0),
    MK_IPMMU_MO_ENTRY("IMTTLBR04", 0x110),
    MK_IPMMU_MO_ENTRY("IMTTLBR05", 0x150),
    MK_IPMMU_MO_ENTRY("IMTTLBR06", 0x190),
    MK_IPMMU_MO_ENTRY("IMTTLBR07", 0x1D0),
    MK_IPMMU_MO_ENTRY("IMTTUBR00", 0x14),
    MK_IPMMU_MO_ENTRY("IMTTUBR01", 0x54),
    MK_IPMMU_MO_ENTRY("IMTTUBR02", 0x94),
    MK_IPMMU_MO_ENTRY("IMTTUBR03", 0xD4),
    MK_IPMMU_MO_ENTRY("IMTTUBR04", 0x114),
    MK_IPMMU_MO_ENTRY("IMTTUBR05", 0x154),
    MK_IPMMU_MO_ENTRY("IMTTUBR06", 0x194),
    MK_IPMMU_MO_ENTRY("IMTTUBR07", 0x1D4),
    MK_IPMMU_MO_ENTRY("IMTTLBR10", 0x18),
    MK_IPMMU_MO_ENTRY("IMTTLBR11", 0x58),
    MK_IPMMU_MO_ENTRY("IMTTLBR12", 0x98),
    MK_IPMMU_MO_ENTRY("IMTTLBR13", 0xd8),
    MK_IPMMU_MO_ENTRY("IMTTLBR14", 0x118),
    MK_IPMMU_MO_ENTRY("IMTTLBR15", 0x158),
    MK_IPMMU_MO_ENTRY("IMTTLBR16", 0x198),
    MK_IPMMU_MO_ENTRY("IMTTLBR17", 0x1d8),
    MK_IPMMU_MO_ENTRY("IMTTUBR10", 0x1c),
    MK_IPMMU_MO_ENTRY("IMTTUBR11", 0x5c),
    MK_IPMMU_MO_ENTRY("IMTTUBR12", 0x9c),
    MK_IPMMU_MO_ENTRY("IMTTUBR13", 0xdc),
    MK_IPMMU_MO_ENTRY("IMTTUBR14", 0x11c),
    MK_IPMMU_MO_ENTRY("IMTTUBR15", 0x15c),
    MK_IPMMU_MO_ENTRY("IMTTUBR16", 0x19c),
    MK_IPMMU_MO_ENTRY("IMTTUBR17", 0x1dc),
    MK_IPMMU_MO_ENTRY("IMSTR0", 0x20),
    MK_IPMMU_MO_ENTRY("IMSTR1", 0x60),
    MK_IPMMU_MO_ENTRY("IMSTR2", 0xA0),
    MK_IPMMU_MO_ENTRY("IMSTR3", 0xE0),
    MK_IPMMU_MO_ENTRY("IMSTR4", 0x120),
    MK_IPMMU_MO_ENTRY("IMSTR5", 0x160),
    MK_IPMMU_MO_ENTRY("IMSTR6", 0x1A0),
    MK_IPMMU_MO_ENTRY("IMSTR7", 0x1E0),
    MK_IPMMU_MO_ENTRY("IMMAIR00", 0x28),
    MK_IPMMU_MO_ENTRY("IMMAIR01", 0x68),
    MK_IPMMU_MO_ENTRY("IMMAIR02", 0xA8),
    MK_IPMMU_MO_ENTRY("IMMAIR03", 0xE8),
    MK_IPMMU_MO_ENTRY("IMMAIR04", 0x128),
    MK_IPMMU_MO_ENTRY("IMMAIR05", 0x168),
    MK_IPMMU_MO_ENTRY("IMMAIR06", 0x1A8),
    MK_IPMMU_MO_ENTRY("IMMAIR07", 0x1E8),
    MK_IPMMU_MO_ENTRY("IMMAIR10", 0x2c),
    MK_IPMMU_MO_ENTRY("IMMAIR11", 0x6c),
    MK_IPMMU_MO_ENTRY("IMMAIR12", 0xac),
    MK_IPMMU_MO_ENTRY("IMMAIR13", 0xec),
    MK_IPMMU_MO_ENTRY("IMMAIR14", 0x12c),
    MK_IPMMU_MO_ENTRY("IMMAIR15", 0x16c),
    MK_IPMMU_MO_ENTRY("IMMAIR16", 0x1ac),
    MK_IPMMU_MO_ENTRY("IMMAIR17", 0x1ec),
    MK_IPMMU_MO_ENTRY("IMELAR0", 0x30),
    MK_IPMMU_MO_ENTRY("IMELAR1", 0x70),
    MK_IPMMU_MO_ENTRY("IMELAR2", 0xB0),
    MK_IPMMU_MO_ENTRY("IMELAR3", 0xF0),
    MK_IPMMU_MO_ENTRY("IMELAR4", 0x130),
    MK_IPMMU_MO_ENTRY("IMELAR5", 0x170),
    MK_IPMMU_MO_ENTRY("IMELAR6", 0x1B0),
    MK_IPMMU_MO_ENTRY("IMELAR7", 0x1F0),
    MK_IPMMU_MO_ENTRY("IMEUAR0", 0x34),
    MK_IPMMU_MO_ENTRY("IMEUAR1", 0x74),
    MK_IPMMU_MO_ENTRY("IMEUAR2", 0xB4),
    MK_IPMMU_MO_ENTRY("IMEUAR3", 0xF4),
    MK_IPMMU_MO_ENTRY("IMEUAR4", 0x134),
    MK_IPMMU_MO_ENTRY("IMEUAR5", 0x174),
    MK_IPMMU_MO_ENTRY("IMEUAR6", 0x1B4),
    MK_IPMMU_MO_ENTRY("IMEUAR7", 0x1F4),
    MK_IPMMU_CO_ENTRY("IMPCTR", 0x200),
    MK_IPMMU_CO_ENTRY("IMPSTR", 0x208),
    MK_IPMMU_CO_ENTRY("IMPEAR", 0x20c),
    MK_IPMMU_CO_ENTRY("IMPMBA00", 0x280),
    MK_IPMMU_CO_ENTRY("IMPMBA01", 0x284),
    MK_IPMMU_CO_ENTRY("IMPMBA02", 0x288),
    MK_IPMMU_CO_ENTRY("IMPMBA03", 0x28c),
    MK_IPMMU_CO_ENTRY("IMPMBA04", 0x290),
    MK_IPMMU_CO_ENTRY("IMPMBA05", 0x294),
    MK_IPMMU_CO_ENTRY("IMPMBA06", 0x298),
    MK_IPMMU_CO_ENTRY("IMPMBA07", 0x29c),
    MK_IPMMU_CO_ENTRY("IMPMBA08", 0x2a0),
    MK_IPMMU_CO_ENTRY("IMPMBA09", 0x2a4),
    MK_IPMMU_CO_ENTRY("IMPMBA10", 0x2a8),
    MK_IPMMU_CO_ENTRY("IMPMBA11", 0x2ac),
    MK_IPMMU_CO_ENTRY("IMPMBA12", 0x2b0),
    MK_IPMMU_CO_ENTRY("IMPMBA13", 0x2B4),
    MK_IPMMU_CO_ENTRY("IMPMBA14", 0x2B8),
    MK_IPMMU_CO_ENTRY("IMPMBA15", 0x2bc),
    MK_IPMMU_CO_ENTRY("IMPMBD00", 0x2C0),
    MK_IPMMU_CO_ENTRY("IMPMBD01", 0x2C4),
    MK_IPMMU_CO_ENTRY("IMPMBD02", 0x2C8),
    MK_IPMMU_CO_ENTRY("IMPMBD03", 0x2CC),
    MK_IPMMU_CO_ENTRY("IMPMBD04", 0x2D0),
    MK_IPMMU_CO_ENTRY("IMPMBD05", 0x2D4),
    MK_IPMMU_CO_ENTRY("IMPMBD06", 0x2D8),
    MK_IPMMU_CO_ENTRY("IMPMBD07", 0x2DC),
    MK_IPMMU_CO_ENTRY("IMPMBD08", 0x2E0),
    MK_IPMMU_CO_ENTRY("IMPMBD09", 0x2E4),
    MK_IPMMU_CO_ENTRY("IMPMBD10", 0x2E8),
    MK_IPMMU_CO_ENTRY("IMPMBD11", 0x2EC),
    MK_IPMMU_CO_ENTRY("IMPMBD12", 0x2F0),
    MK_IPMMU_CO_ENTRY("IMPMBD13", 0x2F4),
    MK_IPMMU_CO_ENTRY("IMPMBD14", 0x2F8),
    MK_IPMMU_CO_ENTRY("IMPMBD15", 0x2FC),
    MK_IPMMU_CO_ENTRY("IMUCTR0", 0x300),
    MK_IPMMU_CO_ENTRY("IMUCTR1", 0x310),
    MK_IPMMU_CO_ENTRY("IMUCTR2", 0x320),
    MK_IPMMU_CO_ENTRY("IMUCTR3", 0x330),
    MK_IPMMU_CO_ENTRY("IMUCTR4", 0x340),
    MK_IPMMU_CO_ENTRY("IMUCTR5", 0x350),
    MK_IPMMU_CO_ENTRY("IMUCTR6", 0x360),
    MK_IPMMU_CO_ENTRY("IMUCTR7", 0x370),
    MK_IPMMU_CO_ENTRY("IMUCTR8", 0x380),
    MK_IPMMU_CO_ENTRY("IMUCTR9", 0x390),
    MK_IPMMU_CO_ENTRY("IMUCTR10", 0x3a0),
    MK_IPMMU_CO_ENTRY("IMUCTR11", 0x3b0),
    MK_IPMMU_CO_ENTRY("IMUCTR12", 0x3c0),
    MK_IPMMU_CO_ENTRY("IMUCTR13", 0x3d0),
    MK_IPMMU_CO_ENTRY("IMUCTR14", 0x3e0),
    MK_IPMMU_CO_ENTRY("IMUCTR15", 0x3f0),
    MK_IPMMU_CO_ENTRY("IMUCTR16", 0x400),
    MK_IPMMU_CO_ENTRY("IMUCTR17", 0x410),
    MK_IPMMU_CO_ENTRY("IMUCTR18", 0x420),
    MK_IPMMU_CO_ENTRY("IMUCTR19", 0x430),
    MK_IPMMU_CO_ENTRY("IMUCTR20", 0x440),
    MK_IPMMU_CO_ENTRY("IMUCTR21", 0x450),
    MK_IPMMU_CO_ENTRY("IMUCTR22", 0x460),
    MK_IPMMU_CO_ENTRY("IMUCTR23", 0x470),
    MK_IPMMU_CO_ENTRY("IMUCTR24", 0x480),
    MK_IPMMU_CO_ENTRY("IMUCTR25", 0x490),
    MK_IPMMU_CO_ENTRY("IMUCTR26", 0x4a0),
    MK_IPMMU_CO_ENTRY("IMUCTR27", 0x4b0),
    MK_IPMMU_CO_ENTRY("IMUCTR28", 0x4c0),
    MK_IPMMU_CO_ENTRY("IMUCTR29", 0x4d0),
    MK_IPMMU_CO_ENTRY("IMUCTR30", 0x4e0),
    MK_IPMMU_CO_ENTRY("IMUCTR31", 0x4f0),
    MK_IPMMU_CO_ENTRY("IMUCTR32", 0x600),
    MK_IPMMU_CO_ENTRY("IMUCTR33", 0x610),
    MK_IPMMU_CO_ENTRY("IMUCTR34", 0x620),
    MK_IPMMU_CO_ENTRY("IMUCTR35", 0x630),
    MK_IPMMU_CO_ENTRY("IMUCTR36", 0x640),
    MK_IPMMU_CO_ENTRY("IMUCTR37", 0x650),
    MK_IPMMU_CO_ENTRY("IMUCTR38", 0x660),
    MK_IPMMU_CO_ENTRY("IMUCTR39", 0x670),
    MK_IPMMU_CO_ENTRY("IMUCTR40", 0x680),
    MK_IPMMU_CO_ENTRY("IMUCTR41", 0x690),
    MK_IPMMU_CO_ENTRY("IMUCTR42", 0x6a0),
    MK_IPMMU_CO_ENTRY("IMUCTR43", 0x6b0),
    MK_IPMMU_CO_ENTRY("IMUCTR44", 0x6c0),
    MK_IPMMU_CO_ENTRY("IMUCTR45", 0x6d0),
    MK_IPMMU_CO_ENTRY("IMUCTR46", 0x6e0),
    MK_IPMMU_CO_ENTRY("IMUCTR47", 0x6f0),
    MK_IPMMU_CO_ENTRY("IMUASID0", 0x308),
    MK_IPMMU_CO_ENTRY("IMUASID1", 0x318),
    MK_IPMMU_CO_ENTRY("IMUASID2", 0x328),
    MK_IPMMU_CO_ENTRY("IMUASID3", 0x338),
    MK_IPMMU_CO_ENTRY("IMUASID4", 0x348),
    MK_IPMMU_CO_ENTRY("IMUASID5", 0x358),
    MK_IPMMU_CO_ENTRY("IMUASID6", 0x368),
    MK_IPMMU_CO_ENTRY("IMUASID7", 0x378),
    MK_IPMMU_CO_ENTRY("IMUASID8", 0x388),
    MK_IPMMU_CO_ENTRY("IMUASID9", 0x398),
    MK_IPMMU_CO_ENTRY("IMUASID10", 0x3A8),
    MK_IPMMU_CO_ENTRY("IMUASID11", 0x3B8),
    MK_IPMMU_CO_ENTRY("IMUASID12", 0x3C8),
    MK_IPMMU_CO_ENTRY("IMUASID13", 0x3D8),
    MK_IPMMU_CO_ENTRY("IMUASID14", 0x3E8),
    MK_IPMMU_CO_ENTRY("IMUASID15", 0x3F8),
    MK_IPMMU_CO_ENTRY("IMUASID16", 0x408),
    MK_IPMMU_CO_ENTRY("IMUASID17", 0x418),
    MK_IPMMU_CO_ENTRY("IMUASID18", 0x428),
    MK_IPMMU_CO_ENTRY("IMUASID19", 0x438),
    MK_IPMMU_CO_ENTRY("IMUASID20", 0x448),
    MK_IPMMU_CO_ENTRY("IMUASID21", 0x458),
    MK_IPMMU_CO_ENTRY("IMUASID22", 0x468),
    MK_IPMMU_CO_ENTRY("IMUASID23", 0x478),
    MK_IPMMU_CO_ENTRY("IMUASID24", 0x488),
    MK_IPMMU_CO_ENTRY("IMUASID25", 0x498),
    MK_IPMMU_CO_ENTRY("IMUASID26", 0x4A8),
    MK_IPMMU_CO_ENTRY("IMUASID27", 0x4B8),
    MK_IPMMU_CO_ENTRY("IMUASID28", 0x4C8),
    MK_IPMMU_CO_ENTRY("IMUASID29", 0x4D8),
    MK_IPMMU_CO_ENTRY("IMUASID30", 0x4E8),
    MK_IPMMU_CO_ENTRY("IMUASID31", 0x4F8),
    MK_IPMMU_CO_ENTRY("IMUASID32", 0x608),
    MK_IPMMU_CO_ENTRY("IMUASID33", 0x618),
    MK_IPMMU_CO_ENTRY("IMUASID34", 0x628),
    MK_IPMMU_CO_ENTRY("IMUASID35", 0x638),
    MK_IPMMU_CO_ENTRY("IMUASID36", 0x648),
    MK_IPMMU_CO_ENTRY("IMUASID37", 0x658),
    MK_IPMMU_CO_ENTRY("IMUASID38", 0x668),
    MK_IPMMU_CO_ENTRY("IMUASID39", 0x678),
    MK_IPMMU_CO_ENTRY("IMUASID40", 0x688),
    MK_IPMMU_CO_ENTRY("IMUASID41", 0x698),
    MK_IPMMU_CO_ENTRY("IMUASID42", 0x6A8),
    MK_IPMMU_CO_ENTRY("IMUASID43", 0x6B8),
    MK_IPMMU_CO_ENTRY("IMUASID44", 0x6C8),
    MK_IPMMU_CO_ENTRY("IMUASID45", 0x6D8),
    MK_IPMMU_CO_ENTRY("IMUASID46", 0x6E8),
    MK_IPMMU_CO_ENTRY("IMUASID47", 0x6F8),
    MK_IPMMU_COMO_ENTRY("IMSCTLR", 0x500),
    MK_IPMMU_MO_ENTRY("IMSAUXCTLR", 0x504),
    MK_IPMMU_MO_ENTRY("IMSSTR", 0x540),
    MK_IPMMU_MO_ENTRY("IMRAM0ERRCTR", 0x560),
    MK_IPMMU_MO_ENTRY("IMRAM0ERRSTR", 0x564),
    MK_IPMMU_MO_ENTRY("IMRAM1ERRCTR", 0x568),
    MK_IPMMU_MO_ENTRY("IMRAM1ERRSTR", 0x56c),
    MK_IPMMU_MO_ENTRY("IMRAM2ERRSTR", 0x570),
    MK_IPMMU_MO_ENTRY("IMRAM3ERRSTR", 0x574),
    MK_IPMMU_MO_ENTRY("IMRAMECCCMPCTR", 0x578),
    MK_IPMMU_MO_ENTRY("IMRAMECCCMPSTR", 0x57c),
    MK_IPMMU_COMO_ENTRY("IMPFMCTR", 0x580),
    MK_IPMMU_COMO_ENTRY("IMPFMTOTAL", 0x590),
    MK_IPMMU_COMO_ENTRY("IMPFMHIT", 0x594),
    MK_IPMMU_MO_ENTRY("IMPFML3MISS", 0x598),
    MK_IPMMU_MO_ENTRY("IMPFML2MISS", 0x59c),
    MK_IPMMU_CO_ENTRY("IMPFMMISS", 0x598),
};

#define MR_TO_IPMMU_STATE(_mr_) container_of(_mr_, RenesasIPMMUState, iommu)

static struct RenesasIPMMURegister *renesas_ipmmu_reg_from_name(RenesasIPMMUState *s, const char *name)
{
    for (int i = 0;  i < ARRAY_SIZE(ipmmu_regs); ++i) {
        struct RenesasIPMMURegister *r = &ipmmu_regs[i];
        if (strcmp(r->name, name) == 0)
            return r;
    }
    return NULL;
}

static struct RenesasIPMMURegister *renesas_ipmmu_reg_from_offset(RenesasIPMMUState *s, hwaddr offset)
{
    for (int i = 0;  i < ARRAY_SIZE(ipmmu_regs); ++i) {
        struct RenesasIPMMURegister *r = &ipmmu_regs[i];
        if (offset == r->offset)
            return r;
    }
    return NULL;
}

static IOMMUTLBEntry renesas_ipmmu_translate(IOMMUMemoryRegion *mr, hwaddr addr, IOMMUAccessFlags flags, int iommu_idx)
{
    RenesasIPMMUState *s = MR_TO_IPMMU_STATE(mr);
    IOMMUTLBEntry ret = {0};
    static int i = 1;
    printf("%i (%s) -----------------------> IOMMU TRANSLATE 0x%"HWADDR_PRIx" (utlb: %i)-> flags: %i, mmu_idx: %i\n", i++, s->ipmmu_type, addr, (int)(addr >> ((sizeof(hwaddr) * 8) - s->num_hsb)), flags, iommu_idx);
    return ret;
}

static MemTxResult renesas_ipmmu_reg_read(void *opaque, hwaddr addr, uint64_t *pdata, unsigned size, MemTxAttrs attrs)
{
    RenesasIPMMUState *s = (RenesasIPMMUState *) opaque;
    if (strcmp(s->ipmmu_type, "IPMMU_DS0") == 0) {
        struct RenesasIPMMURegister *r = renesas_ipmmu_reg_from_offset(s, addr);
        printf("------------------>Read %s (Register %s)offset: 0x%" HWADDR_PRIx
               " size: 0x%x\n",
               s->ipmmu_type, r->name ,addr, size);
    }
    *pdata = s->reg[OFFSET_TO_REG_IDX(addr)];
    return MEMTX_OK;
}

static MemTxResult renesas_ipmmu_reg_write(void *opaque, hwaddr addr, uint64_t value,
                                           unsigned size, MemTxAttrs attrs)
{
    RenesasIPMMUState *s = (RenesasIPMMUState *) opaque;
    if (strcmp(s->ipmmu_type, "IPMMU_MM") == 0) {
        struct RenesasIPMMURegister *r = renesas_ipmmu_reg_from_offset(s, addr);
        printf("------------------>WRITE %s (Register %s)offset: 0x%" HWADDR_PRIx
               " value: 0x%lx\n",
               s->ipmmu_type, r == NULL ? "unknown" : r->name, addr, value);
    }
    struct RenesasIPMMURegister * r = renesas_ipmmu_reg_from_offset(s, addr);
    assert(r != NULL);
    if ((r->cache_access && !s->is_main) || (r->main_access && s->is_main))
        s->reg[OFFSET_TO_REG_IDX(addr)] = value & 0xffffffff;

    return MEMTX_OK;
}

static const MemoryRegionOps renesas_ipmmu_reg_ops = {
    .read_with_attrs = renesas_ipmmu_reg_read,
    .write_with_attrs = renesas_ipmmu_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void renesas_ipmmu_instance_init(Object *obj)
{
    
}

static void renesas_ipmmu_realize(DeviceState *dev, Error **errp)
{
    RenesasIPMMUState *s = RENESAS_IPMMU(dev);
    s->reg = g_new0(uint32_t, (0x1000/4));
    
    s->ctr_idx = OFFSET_TO_REG_IDX(renesas_ipmmu_reg_from_name(s, "IMCTR0")->offset);
    s->ttbcr_idx = OFFSET_TO_REG_IDX(renesas_ipmmu_reg_from_name(s, "IMTTBCR0")->offset);
    s->ttubr_idx[0] = OFFSET_TO_REG_IDX(renesas_ipmmu_reg_from_name(s, "IMTTUBR00")->offset);
    s->ttubr_idx[1] = OFFSET_TO_REG_IDX(renesas_ipmmu_reg_from_name(s, "IMTTUBR10")->offset);
    s->ttlbr_idx[0] = OFFSET_TO_REG_IDX(renesas_ipmmu_reg_from_name(s, "IMTTLBR00")->offset);
    s->ttlbr_idx[1] = OFFSET_TO_REG_IDX(renesas_ipmmu_reg_from_name(s, "IMTTLBR10")->offset);
    
    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_RENESAS_IPMMU_MEMORY_REGION, OBJECT(s),
                             "renesas-ipmmu", UINT64_MAX);
    address_space_init(&s->iommu_as, MEMORY_REGION(&s->iommu), "ipmmu-as");
    memory_region_init_io(&s->iomem, OBJECT(s), &renesas_ipmmu_reg_ops, s, "ripmmu", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), MEMORY_REGION(&s->iommu));
    
    
}

static const VMStateDescription renesas_ipmmu_vmstate = {
    .name = "renesas-ipmmu-vmstate",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static Property renesas_ipmmu_properties[] = {
    DEFINE_PROP_BOOL("is_main", RenesasIPMMUState, is_main, false),
    DEFINE_PROP_STRING("ipmmu_type", RenesasIPMMUState, ipmmu_type),
    DEFINE_PROP_UINT32("utlb_num", RenesasIPMMUState, num_utlb, 48),
    DEFINE_PROP_UINT8("hsb_num", RenesasIPMMUState, num_hsb, 8),
    DEFINE_PROP_LINK("main_ipmmu", RenesasIPMMUState, main,
                     TYPE_RENESAS_IPMMU, RenesasIPMMUState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void renesas_ipmmu_reset(DeviceState *dev)
{

}

static void renesas_ipmmu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = renesas_ipmmu_realize;
    dc->vmsd = &renesas_ipmmu_vmstate;
    dc->reset = renesas_ipmmu_reset;
    device_class_set_props(dc, renesas_ipmmu_properties);
}

static void renesas_ipmmu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);
    imrc->translate = renesas_ipmmu_translate;
}

static const TypeInfo renesas_ipmmu_info = {
    .name          = TYPE_RENESAS_IPMMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasIPMMUState),
    .instance_init = renesas_ipmmu_instance_init,
    .class_init    = renesas_ipmmu_class_init,
};

static const TypeInfo renesas_ipmmu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_RENESAS_IPMMU_MEMORY_REGION,
    .class_init = renesas_ipmmu_memory_region_class_init,
};

static void renesas_ipmmu_types(void)
{
    type_register(&renesas_ipmmu_info);
    type_register(&renesas_ipmmu_memory_region_info);
}

type_init(renesas_ipmmu_types)


