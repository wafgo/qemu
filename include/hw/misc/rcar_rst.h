/*
 * Renesas RCar Mode Monitor Register emulation
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

#ifndef HW_MISC_RCAR_RST_H
#define HW_MISC_RCAR_RST_H

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qom/object_interfaces.h"
#include "qom/object.h"
#include "hw/sysbus.h"


#define TYPE_RCAR_RST   "rcar-rst"
OBJECT_DECLARE_SIMPLE_TYPE(RCarRstRegisterState, RCAR_RST)

struct RCarRstRegisterState {
  /*< private >*/
  SysBusDevice parent_obj;
  /*< public >*/
  MemoryRegion iomem;
  uint32_t mode_mr;
  uint32_t regs[0x1000/4];
};

#endif /* HW_MISC_RCAR_RST_H */
