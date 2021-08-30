/*
 * Renesas RCar PRR Register Implemenation
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

#ifndef HW_MISC_RCAR_PRR_H
#define HW_MISC_RCAR_PRR_H

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qom/object_interfaces.h"
#include "qom/object.h"
#include "hw/sysbus.h"


#define RCAR_H3_PROD_ID 0x47
#define RCAR_M3W_PROD_ID 0x52
#define RCAR_V3M_PROD_ID 0x54
#define RCAR_V3H_PROD_ID 0x56
#define RCAR_D3_PROD_ID 0x58
#define RCAR_M3N_PROD_ID 0x55
#define RCAR_E3_PROD_ID 0x57

#define TYPE_RCAR_PRR   "rcar-prr"
OBJECT_DECLARE_SIMPLE_TYPE(RCarPrrRegisterState, RCAR_PRR)

struct RCarPrrRegisterState {
  /*< private >*/
  SysBusDevice parent_obj;
  /*< public >*/
  MemoryRegion iomem;
  uint8_t a57_num;
  uint8_t a53_num;
  uint8_t cr7_available;
  uint8_t product_id;
  uint8_t cut;
  uint32_t reg_val;
};

#endif /* HW_MISC_RCAR_PRR_H */
