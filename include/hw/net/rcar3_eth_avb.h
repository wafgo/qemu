/*
* Renesas RCar Gen3 AVB E-MAC emulation
 *
 * Copyright (C) 2021 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
/* For crc32 */
#include <zlib.h>
#include "qom/object.h"

/* Number of 2k memory pages available.  */
#define NUM_PACKETS 4

#define TYPE_RCAR_ETH_AVB "rcar.eth.avb"
OBJECT_DECLARE_SIMPLE_TYPE(RCarEthAvbState, RCAR_ETH_AVB)

struct RCarEthAvbState {
    SysBusDevice parent_obj;

    NICState *nic;
    NICConf conf;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    uint16_t tcr;
    uint16_t rcr;
    uint16_t cr;
    uint16_t ctr;
    uint16_t gpr;
    uint16_t ptr;
    uint16_t ercv;
    qemu_irq irq;
    int bank;
    int packet_num;
    int tx_alloc;
    /* Bitmask of allocated packets.  */
    int allocated;
    int tx_fifo_len;
    int tx_fifo[NUM_PACKETS];
    int rx_fifo_len;
    int rx_fifo[NUM_PACKETS];
    int tx_fifo_done_len;
    int tx_fifo_done[NUM_PACKETS];
    /* Packet buffer memory.  */
    uint8_t data[NUM_PACKETS][2048];
    uint8_t int_level;
    uint8_t int_mask;
    uint8_t io_mmu_utlb;
    uint8_t num_hsb; /* number of highest significant bits in the address, which identify the micro tlb index if io_mmu is used*/
    MemoryRegion mmio;
    uint32_t *reg;
};
