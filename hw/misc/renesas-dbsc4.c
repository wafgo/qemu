/*
 * Renesas DRAM Bus State Controller Emulation
 *
 * Copyright (C) 2021 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include "hw/misc/renesas-dbsc4.h"

#define DBSC_REG_SPACE (20 * 1024)
#define DBSC_PHY_ADDR_OFFSET 0x624
#define DBSC_PHY_DATA_OFFSET 0x628

#define OFFSET_TO_REG_INDEX(x) (x/4)
#define DBSC_PHY_ADDR_INDEX OFFSET_TO_REG_INDEX(DBSC_PHY_ADDR_OFFSET)

#define DDR_PHY_SLICE_OFS 0x800
#define DDR_PHY_ADR_V_OFS 0xa00
#define DDR_PHY_ADR_I_OFS 0xa80
#define DDR_PHY_ADR_G_OFS 0xb80
#define DDR_PI_OFS        0x200

//#define DEBUG_DBSC
struct dbsc4_phy_reg_class_entry {
  const char *name;
  uint32_t offset_start;
  uint32_t size;
  uint32_t (*get_reg_address)(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice);
};

#ifdef DEBUG_DBSC
static uint32_t dbsc4_get_phy_slice_ofs_addr(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice);
static uint32_t dbsc4_get_phy_adr_v_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice);
static uint32_t dbsc4_get_phy_adr_i_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice);
static uint32_t dbsc4_get_phy_adr_g_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice);
static uint32_t dbsc4_get_pi_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice);

static struct dbsc4_phy_reg_class_entry phy_reg_classifier[] = {
  {.name = "PI OFS", .offset_start = 0x200, .size = 0x600, .get_reg_address = dbsc4_get_pi_ofs},
  {.name = "SLICE OFS", .offset_start = 0x800, .size = 0x200, .get_reg_address = dbsc4_get_phy_slice_ofs_addr},
  {.name = "V OFS", .offset_start = 0xa00, .size = 0x80, .get_reg_address = dbsc4_get_phy_adr_v_ofs},
  {.name = "I OFS", .offset_start = 0xa80, .size = 0x100, .get_reg_address = dbsc4_get_phy_adr_i_ofs},
  {.name = "G OFS", .offset_start = 0xb80, .size = 0x200, .get_reg_address = dbsc4_get_phy_adr_g_ofs},
};
#endif

struct dbsc4_reg_access_hooks;

static void dbsc4_dbpdrga_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);
static void dbsc4_dbpdrga_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);

static void dbsc4_dbpdrgd_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);
static void dbsc4_dbpdrgd_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);

static void dbsc4_pll_lock_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);

static void dbsc4_dfistat_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);

static void dbsc4_dbpdstat_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);

static void dbsc4_dbpdcnt2_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);
static void dbsc4_dbmrrdr_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);

static void dbsc4_pyh_bc0_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);

static void dbsc4_pyh_dqs_slave_delay_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);
static void dbsc4_pyh_pi_version_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am);
static void dbsc4_pyh_pi_version_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am);
static void dbsc4_pyh_pi_int_status_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am);

static void dbsc4_pyh_pi_23c_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am);

static void dbsc4_pyh_pi_8b2_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am);
  
struct dbsc4_reg_access_hooks {
  hwaddr offset;
  void (*pre_read)(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);
  void (*post_write)(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am);
};

static struct dbsc4_reg_access_hooks reg_access_hooks[] = {
                 {.offset = DBSC_PHY_ADDR_OFFSET,
                  .pre_read = dbsc4_dbpdrga_pre_read,
                  .post_write = dbsc4_dbpdrga_post_write},
                 {.offset = DBSC_PHY_DATA_OFFSET,
                  .pre_read = dbsc4_dbpdrgd_pre_read,
                  .post_write = dbsc4_dbpdrgd_post_write},
                 {.offset = 0x4054, .pre_read = dbsc4_pll_lock_read},
		 {.offset = 0x4154, .pre_read = dbsc4_pll_lock_read},
		 {.offset = 0x4254, .pre_read = dbsc4_pll_lock_read},
		 {.offset = 0x4354, .pre_read = dbsc4_pll_lock_read},
		 {.offset = 0x600, .pre_read = dbsc4_dfistat_read},
		 {.offset = 0x640, .pre_read = dbsc4_dfistat_read},
		 {.offset = 0x680, .pre_read = dbsc4_dfistat_read},
		 {.offset = 0x6c0, .pre_read = dbsc4_dfistat_read},
		 {.offset = 0x630, .pre_read = dbsc4_dbpdstat_read},
		 {.offset = 0x618, .post_write = dbsc4_dbpdcnt2_post_write},
		 {.offset = 0x1800, .pre_read = dbsc4_dbmrrdr_pre_read},
};

static struct dbsc4_reg_access_hooks phy_reg_access_hooks[] = {
  {.offset = 0xbc0, .pre_read = dbsc4_pyh_bc0_pre_read},
  {.offset = 0x840, .pre_read = dbsc4_pyh_dqs_slave_delay_pre_read},
  {.offset = 0x8c0, .pre_read = dbsc4_pyh_dqs_slave_delay_pre_read},
  {.offset = 0x940, .pre_read = dbsc4_pyh_dqs_slave_delay_pre_read},
  {.offset = 0x9c0, .pre_read = dbsc4_pyh_dqs_slave_delay_pre_read},
  {.offset = 0x200, .pre_read = dbsc4_pyh_pi_version_pre_read, .post_write = dbsc4_pyh_pi_version_post_write},
  {.offset = 0x2cd, .pre_read = dbsc4_pyh_pi_int_status_pre_read},
  {.offset = 0x23c, .pre_read = dbsc4_pyh_pi_23c_pre_read},
  {.offset = 0x832, .pre_read = dbsc4_pyh_pi_8b2_pre_read},
  {.offset = 0x832, .pre_read = dbsc4_pyh_pi_8b2_pre_read},
  {.offset = 0x8b2, .pre_read = dbsc4_pyh_pi_8b2_pre_read},
  {.offset = 0x932, .pre_read = dbsc4_pyh_pi_8b2_pre_read},
  {.offset = 0x9b2, .pre_read = dbsc4_pyh_pi_8b2_pre_read},
};


static void dbsc4_pyh_pi_8b2_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am)
{
  s->phy_regs[am->offset] |= (50 << 24) | 25;
}

static void dbsc4_pyh_pi_23c_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am)
{
  s->phy_regs[am->offset] |= (1 << 24);
}

static void dbsc4_dbpdcnt2_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  uint32_t reg_idx = OFFSET_TO_REG_INDEX(am->offset);
  if (s->reg[reg_idx] == 0x0CF20000) {
    s->dfi_started = false;
  }
}

static void dbsc4_dbpdstat_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  
   if (s->dfi_started == true) {
    s->reg[OFFSET_TO_REG_INDEX(am->offset)] |= (1 << 0);
  } else {
    s->reg[OFFSET_TO_REG_INDEX(am->offset)] &= ~(1 << 0);
  }
}

static void dbsc4_pyh_pi_int_status_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am)
{
  s->phy_regs[am->offset] |= (1 << 0);
}

static void dbsc4_pyh_pi_version_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am)
{
  if (s->phy_regs[am->offset] & 0x1) {
    s->dfi_started = true;
  } else {
    s->dfi_started = false;
  }
}

static void dbsc4_pyh_pi_version_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks *am)
{
  s->phy_regs[am->offset] |= 0x2040 << 16;
}

static void dbsc4_pyh_dqs_slave_delay_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  s->phy_regs[am->offset] = ((31) | (31 << 6));
}

#ifdef DEBUG_DBSC
static uint32_t dbsc4_get_phy_slice_ofs_addr(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice)
{
  uint32_t offs = reg_offset - 0x800;
  if (slice != NULL)
    *slice = (offs / 0x80);
  
  return (offs % 0x80);
}

static uint32_t dbsc4_get_phy_adr_v_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice)
{
  return reg_offset - 0xa00;
}

static uint32_t dbsc4_get_phy_adr_i_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice)
{
  return reg_offset - 0xa80;
}

static uint32_t dbsc4_get_phy_adr_g_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice)
{
  return reg_offset - 0xb80;
}

static uint32_t dbsc4_get_pi_ofs(RenesasDBSC4State *s, uint32_t reg_offset, uint32_t *slice)
{
  return reg_offset - 0x200;
}
#endif
static void dbsc4_pyh_bc0_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  uint32_t phy_reg_num = s->reg[DBSC_PHY_ADDR_INDEX];
  s->phy_regs[phy_reg_num] |= 0x800000;  
}

static void dbsc4_dfistat_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  uint32_t reg_idx = OFFSET_TO_REG_INDEX(am->offset);
  s->reg[reg_idx] = 0x1;
}

static void dbsc4_pll_lock_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  uint32_t reg_idx = OFFSET_TO_REG_INDEX(am->offset);
  s->reg[reg_idx] = 0x1f;
}

#ifdef DEBUG_DBSC
static const char *dbsc4_get_phy_register_class(RenesasDBSC4State *s, uint32_t phy_reg_num, uint32_t *n, uint32_t *slice)
{
  for (int i = 0; i < ARRAY_SIZE(phy_reg_classifier); ++i) {
    struct dbsc4_phy_reg_class_entry *pre = &phy_reg_classifier[i];
    if ((phy_reg_num >= pre->offset_start) && (phy_reg_num < (pre->offset_start + pre->size))) {
      if (n != NULL)
	*n = pre->get_reg_address(s, phy_reg_num, slice);
      return pre->name;
    }
  }
  
  return "";
}
#endif

static void dbsc4_dbpdrgd_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  uint32_t phy_reg_num = s->reg[DBSC_PHY_ADDR_INDEX];
  if (phy_reg_num > ARRAY_SIZE(s->phy_regs))
    qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid phy register number 0x%04x\n", __func__,
                  (uint32_t)phy_reg_num);
  else {
    for (int i = 0; i < ARRAY_SIZE(phy_reg_access_hooks); ++i) {
      struct dbsc4_reg_access_hooks *ah = &phy_reg_access_hooks[i];
      if (ah->offset == phy_reg_num && ah->pre_read != NULL) {
	ah->pre_read(s, ah);
      }
    }

    s->reg[OFFSET_TO_REG_INDEX(am->offset)] = s->phy_regs[phy_reg_num];
  }
}

static void dbsc4_dbmrrdr_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  uint8_t mr_reg_to_read = (s->reg[OFFSET_TO_REG_INDEX(0x208)] >> 8) & 0xff;
  uint8_t mr_regs[] = {0,0,0,0,0,0xff,0x3,0x0,0x8};
  uint32_t reg_idx = OFFSET_TO_REG_INDEX(am->offset);
  if (mr_reg_to_read >= ARRAY_SIZE(mr_regs))
    s->reg[reg_idx] = 0xff;
  else
    s->reg[reg_idx] = mr_regs[mr_reg_to_read];
}

static void dbsc4_dbpdrga_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
    uint32_t reg_idx = OFFSET_TO_REG_INDEX(am->offset);
    if (s->reg[reg_idx] & (1 << 15)) {
      s->reg[reg_idx] &= ~(1 << 15);
    }
    /* ATF does something unexplainable during DRAM initialization, workaround this with the following code*/
    if (s->reg[reg_idx] & (1 << 14)) {
      s->reg[reg_idx] |= (1 << 15);
    }
      
}

static void dbsc4_dbpdrgd_post_write(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  if (s->reg[DBSC_PHY_ADDR_INDEX] & (1 << 15))
    s->reg[DBSC_PHY_ADDR_INDEX] &= ~(1 << 15);

  s->phy_regs[s->reg[DBSC_PHY_ADDR_INDEX]] = s->reg[OFFSET_TO_REG_INDEX(am->offset)];
  for (int i = 0; i < ARRAY_SIZE(phy_reg_access_hooks); ++i) {
      struct dbsc4_reg_access_hooks *ah = &phy_reg_access_hooks[i];
      if (ah->offset == s->reg[DBSC_PHY_ADDR_INDEX] && ah->post_write != NULL) {
        ah->post_write(s, ah);
      }
  }
  s->phy_reg_written = true;
}

static void dbsc4_dbpdrga_pre_read(RenesasDBSC4State *s, struct dbsc4_reg_access_hooks* am)
{
  uint32_t reg_idx = OFFSET_TO_REG_INDEX(am->offset);
  if (s->phy_reg_written) {
    s->reg[reg_idx] |= (1 << 15);
    s->phy_reg_written = false;
  }
}

static uint64_t renesas_dbsc4_read(void *opaque, hwaddr offset, unsigned size)
{
  RenesasDBSC4State *s = (RenesasDBSC4State *)opaque;
  unsigned int reg_idx = OFFSET_TO_REG_INDEX(offset);

  uint32_t val = 0;
  
  if (reg_idx >= s->num_reg)
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%04x\n", __func__,
                  (uint32_t)offset);
  else {
    for (int i = 0; i < ARRAY_SIZE(reg_access_hooks); ++i) {
      struct dbsc4_reg_access_hooks *acc = &reg_access_hooks[i];
      if (acc->offset == offset && acc->pre_read != NULL)
	acc->pre_read(s, acc);
    }
    val = s->reg[reg_idx];
  }
    
  return val;
}

static void renesas_dbsc4_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
  RenesasDBSC4State *s = (RenesasDBSC4State *)opaque;
  unsigned int reg_idx = OFFSET_TO_REG_INDEX(offset);
  if (reg_idx >= s->num_reg) 
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%04x\n", __func__, (uint32_t)offset);
  else {
    s->reg[reg_idx] = (uint32_t)val;
    for (int i = 0; i < ARRAY_SIZE(reg_access_hooks); ++i) {
      struct dbsc4_reg_access_hooks *acc = &reg_access_hooks[i];
      if (acc->offset == offset && acc->post_write != NULL)
	acc->post_write(s, acc);
    }
  }
}

static const MemoryRegionOps renesas_dbsc4_ops = {
    .read = renesas_dbsc4_read,
    .write = renesas_dbsc4_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    }
};

static void renesas_dbsc4_reset(DeviceState *dev)
{
    RenesasDBSC4State *s = RENESAS_DBSC4(dev);
    memset(s->reg, 0, s->num_reg * 4);
}

static void renesas_dbsc4_init(Object *obj)
{
    RenesasDBSC4State *s = RENESAS_DBSC4(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    
    s->num_reg = DBSC_REG_SPACE;
    s->reg = g_new0(uint32_t, DBSC_REG_SPACE);
 
    memory_region_init_io(&s->iomem, obj, &renesas_dbsc4_ops, s, TYPE_RENESAS_DBSC4, DBSC_REG_SPACE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void renesas_dbsc4_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = renesas_dbsc4_reset;
}

static const TypeInfo renesas_dbsc4_info = {
    .name          = TYPE_RENESAS_DBSC4,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasDBSC4State),
    .instance_init = renesas_dbsc4_init,
    .class_init    = renesas_dbsc4_class_init,
};

static void renesas_dbsc4_register(void)
{
    type_register_static(&renesas_dbsc4_info);
}

type_init(renesas_dbsc4_register)
