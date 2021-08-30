/*
 * Renesas System DMA Controller emulation
 *
 * Copyright (c) 2021 Continental Automotive GmbH
 *
 * Author:
 *   Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RENESAS_SYSDMAC_H
#define RENESAS_SYSDMAC_H

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"

#define TYPE_RENESAS_SYSDMAC    "renesas.sysdmac"

OBJECT_DECLARE_SIMPLE_TYPE(RenesasSysDmacState, RENESAS_SYSDMAC)

struct RenesasSysDmacState {
    SysBusDevice parent;
    MemoryRegion iomem;
    /* qemu_irq irq[SIFIVE_PDMA_IRQS]; */

    /* struct sifive_pdma_chan chan[SIFIVE_PDMA_CHANS]; */
};



#endif /* RENESAS_SYSDMAC_H */
