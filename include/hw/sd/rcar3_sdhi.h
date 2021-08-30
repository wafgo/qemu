/*
 * Renesas RCar Gen3 SD Host Controller emulation
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

#ifndef HW_SD_RCAR3_SDHI_H
#define HW_SD_RCAR3_SDHI_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"


/** Renesas RCAR3 SD Host Controller */
#define TYPE_RCAR_SDHI "rcar3-sdhi"


/** @} */

/**
 * Object model macros
 * @{
 */

OBJECT_DECLARE_SIMPLE_TYPE(RCar3SDHIState, RCAR_SDHI)

/** @} */
struct rcar3_sdhi_regs;

/**
 * Rensesas SDHI Controller object instance state.
 */
struct RCar3SDHIState {
  /*< private >*/
  SysBusDevice busdev;
  /*< public >*/

  /** Secure Digital (SD) bus, which connects to SD card (if present) */
  SDBus sdbus;

  /** Maps I/O registers in physical memory */
  MemoryRegion iomem;

  /** Interrupt output signals to notify CPU */
  qemu_irq irq_sdi_all;
  qemu_irq irq_sdi_other;

  /** Memory region where DMA transfers are done */
  MemoryRegion *dma_mr;

  /** Address space used internally for DMA transfers */
  AddressSpace dma_as;

  /** Number of bytes left in current DMA transfer */
  uint32_t transfer_cnt;
  struct rcar3_sdhi_regs *regs;
  /**
   * @name Hardware Registers
   * @{
   */

    
  /** @} */

};

#endif /*HW_SD_RCAR3_SDHI_H*/
