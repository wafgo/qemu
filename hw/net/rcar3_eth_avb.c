/*
 * Renesas RCar Gen3 AVB E-MAC emulation
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


#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "hw/irq.h"
#include "sysemu/dma.h"
#include "hw/net/rcar3_eth_avb.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include <zlib.h>
#include "qom/object.h"
#include "qemu/error-report.h"

struct RcarAvbRegister {
    const char *description;
    const char *name;
    const char *rw;
    uint32_t reset_value;
    hwaddr offset;
    void (*post_write)(RCarEthAvbState *s, struct RcarAvbRegister *reg, uint64_t value);
    void (*pre_read)(RCarEthAvbState *s, struct RcarAvbRegister *reg);
};

static void ccc_postwrite(RCarEthAvbState *s, struct RcarAvbRegister *reg, uint64_t value);

#define OFFSET_TO_REG_IDX(_os_) (_os_/4)
#define MK_AVB_REG_ENTRY_WITH_CBS(_desc_, _nm_, _rw_, _rv_, _os_,_pw_,_pr_) {.description = _desc_, .name = _nm_, .rw = _rw_, .reset_value = _rv_, .offset = _os_, .post_write = _pw_, .pre_read = _pr_}
#define MK_AVB_REG_ENTRY(_desc_, _nm_, _rw_, _rv_, _os_) {.description = _desc_, .name = _nm_, .rw = _rw_, .reset_value = _rv_, .offset = _os_}
#define MK_AVB_RW_INIT_ENTRY(_desc_, _nm_, _rv_, _os_) MK_AVB_REG_ENTRY(_desc_, _nm_, "RW", _rv_, _os_)
#define MK_AVB_RW_ENTRY(_desc_, _nm_, _os_) MK_AVB_REG_ENTRY(_desc_, _nm_, "RW", 0x00000000, _os_)
#define MK_AVB_RO_INIT_ENTRY(_desc_, _nm_, _val_, _os_) MK_AVB_REG_ENTRY(_desc_, _nm_, "RO", _val_, _os_)
#define MK_AVB_WO_ENTRY(_desc_, _nm_, _os_) MK_AVB_REG_ENTRY(_desc_, _nm_, "WO", 0x00000000, _os_)
#define MK_AVB_RW_ENTRY_WITH_POSTWRITE(_desc_, _nm_, _os_, _pw_) MK_AVB_REG_ENTRY_WITH_CBS(_desc_, _nm_, "RW", 0x00000000, _os_,_pw_,NULL)

struct RcarAvbRegister avb_regs[] = {
    MK_AVB_RW_ENTRY_WITH_POSTWRITE("AVB-DMAC mode register", "CCC", 0x0, ccc_postwrite),
    MK_AVB_RW_ENTRY("Descriptor base address table register", "DBAT", 0x4),
    MK_AVB_RW_INIT_ENTRY("Descriptor base address load request register", "DLR", 0x3fffff, 0x8),
    MK_AVB_RW_INIT_ENTRY("AVB-DMAC status register", "CSR", 0x1, 0xc),
    MK_AVB_RW_ENTRY("Current descriptor address register 0", "CDAR0", 0x10),
    MK_AVB_RW_ENTRY("Current descriptor address register 1", "CDAR1", 0x14),
    MK_AVB_RW_ENTRY("Current descriptor address register 2", "CDAR2", 0x18),
    MK_AVB_RW_ENTRY("Current descriptor address register 3", "CDAR3", 0x1c),
    MK_AVB_RW_ENTRY("Current descriptor address register 4", "CDAR4", 0x20),
    MK_AVB_RW_ENTRY("Current descriptor address register 5", "CDAR5", 0x24),
    MK_AVB_RW_ENTRY("Current descriptor address register 6", "CDAR6", 0x28),
    MK_AVB_RW_ENTRY("Current descriptor address register 7", "CDAR7", 0x2c),
    MK_AVB_RW_ENTRY("Current descriptor address register 8", "CDAR8", 0x30),
    MK_AVB_RW_ENTRY("Current descriptor address register 9", "CDAR9", 0x34),
    MK_AVB_RW_ENTRY("Current descriptor address register 10", "CDAR10", 0x38),
    MK_AVB_RW_ENTRY("Current descriptor address register 11", "CDAR11", 0x3c),
    MK_AVB_RW_ENTRY("Current descriptor address register 12", "CDAR12", 0x40),
    MK_AVB_RW_ENTRY("Current descriptor address register 13", "CDAR13", 0x44),
    MK_AVB_RW_ENTRY("Current descriptor address register 14", "CDAR14", 0x48),
    MK_AVB_RW_ENTRY("Current descriptor address register 15", "CDAR15", 0x4c),
    MK_AVB_RW_ENTRY("Current descriptor address register 16", "CDAR16", 0x50),
    MK_AVB_RW_ENTRY("Current descriptor address register 17", "CDAR17", 0x54),
    MK_AVB_RW_ENTRY("Current descriptor address register 18", "CDAR18", 0x58),
    MK_AVB_RW_ENTRY("Current descriptor address register 19", "CDAR19", 0x5c),
    MK_AVB_RW_ENTRY("Current descriptor address register 20", "CDAR20", 0x60),
    MK_AVB_RW_ENTRY("Current descriptor address register 21", "CDAR21", 0x64),
    MK_AVB_RW_ENTRY("Error status register ESR", "ESR", 0x88),
    MK_AVB_RW_ENTRY("AVB-DMAC Product Specific Register", "APSR", 0x8c),
    MK_AVB_RW_INIT_ENTRY("Receive configuration register", "RCR", 0x18000000, 0x90),
    MK_AVB_RW_ENTRY("Receive queue configuration register 0", "RQC0", 0x94),
    MK_AVB_RW_ENTRY("Receive queue configuration register 1", "RQC1", 0x98),
    MK_AVB_RW_ENTRY("Receive queue configuration register 2", "RQC2", 0x9c),
    MK_AVB_RW_ENTRY("Receive queue configuration register 3", "RQC3", 0xa0),
    MK_AVB_RW_ENTRY("Receive queue configuration register 4", "RQC4", 0xa4),
    MK_AVB_RW_INIT_ENTRY("Receive padding configuration register", "RPC", 0x100, 0xb0),
    MK_AVB_RW_INIT_ENTRY("Reception Truncation Configuration register", "RTC", 0xffc0ffc, 0xb4),
    MK_AVB_RW_ENTRY("Unread frame counter warning level register", "UFCW", 0xbc),
    MK_AVB_RW_ENTRY("Unread frame counter stop level register", "UFCS", 0xc0),
    MK_AVB_RW_ENTRY("Unread frame counter register 0", "UFCV0", 0xc4),
    MK_AVB_RW_ENTRY("Unread frame counter register 1", "UFCV1", 0xc8),
    MK_AVB_RW_ENTRY("Unread frame counter register 2", "UFCV2", 0xcc),
    MK_AVB_RW_ENTRY("Unread frame counter register 3", "UFCV3", 0xd0),
    MK_AVB_RW_ENTRY("Unread frame counter register 4", "UFCV4", 0xd4),
    MK_AVB_RW_ENTRY("Unread frame counter decrement register 0", "UFCD0", 0xe0),
    MK_AVB_RW_ENTRY("Unread frame counter decrement register 1", "UFCD1", 0xe4),
    MK_AVB_RW_ENTRY("Unread frame counter decrement register 2", "UFCD2", 0xe8),
    MK_AVB_RW_ENTRY("Unread frame counter decrement register 3", "UFCD3", 0xec),
    MK_AVB_RW_ENTRY("Unread frame counter decrement register 4", "UFCD4", 0xf0),
    MK_AVB_RW_ENTRY("Separation filter offset register", "SFO", 0xfc),
    MK_AVB_RW_ENTRY("Separation filter pattern register 0", "SFP0", 0x100),
    MK_AVB_RW_ENTRY("Separation filter pattern register 1", "SFP1", 0x104),
    MK_AVB_RW_ENTRY("Separation filter pattern register 2", "SFP2", 0x108),
    MK_AVB_RW_ENTRY("Separation filter pattern register 3", "SFP3", 0x10c),
    MK_AVB_RW_ENTRY("Separation filter pattern register 4", "SFP4", 0x110),
    MK_AVB_RW_ENTRY("Separation filter pattern register 5", "SFP5", 0x114),
    MK_AVB_RW_ENTRY("Separation filter pattern register 6", "SFP6", 0x118),
    MK_AVB_RW_ENTRY("Separation filter pattern register 7", "SFP7", 0x11c),
    MK_AVB_RW_ENTRY("Separation filter pattern register 8", "SFP8", 0x120),
    MK_AVB_RW_ENTRY("Separation filter pattern register 9", "SFP9", 0x124),
    MK_AVB_RW_ENTRY("Separation filter pattern register 10", "SF10", 0x128),
    MK_AVB_RW_ENTRY("Separation filter pattern register 11", "SFP11", 0x12c),
    MK_AVB_RW_ENTRY("Separation filter pattern register 12", "SFP12", 0x130),
    MK_AVB_RW_ENTRY("Separation filter pattern register 13", "SFP13", 0x134),
    MK_AVB_RW_ENTRY("Separation filter pattern register 14", "SFP14", 0x138),
    MK_AVB_RW_ENTRY("Separation filter pattern register 15", "SFP15", 0x13c),
    MK_AVB_RW_ENTRY("Separation filter pattern register 16", "SFP16", 0x140),
    MK_AVB_RW_ENTRY("Separation filter pattern register 17", "SFP17", 0x144),
    MK_AVB_RW_ENTRY("Separation filter pattern register 18", "SFP18", 0x148),
    MK_AVB_RW_ENTRY("Separation filter pattern register 19", "SFP19", 0x14c),
    MK_AVB_RW_ENTRY("Separation filter pattern register 20", "SFP20", 0x150),
    MK_AVB_RW_ENTRY("Separation filter pattern register 21", "SFP21", 0x154),
    MK_AVB_RW_ENTRY("Separation filter pattern register 22", "SFP22", 0x158),
    MK_AVB_RW_ENTRY("Separation filter pattern register 23", "SFP23", 0x15c),
    MK_AVB_RW_ENTRY("Separation filter pattern register 24", "SFP24", 0x160),
    MK_AVB_RW_ENTRY("Separation filter pattern register 25", "SFP25", 0x164),
    MK_AVB_RW_ENTRY("Separation filter pattern register 26", "SFP26", 0x168),
    MK_AVB_RW_ENTRY("Separation filter pattern register 27", "SFP27", 0x16c),
    MK_AVB_RW_ENTRY("Separation filter pattern register 28", "SFP28", 0x170),
    MK_AVB_RW_ENTRY("Separation filter pattern register 29", "SFP29", 0x174),
    MK_AVB_RW_ENTRY("Separation filter pattern register 30", "SFP30", 0x178),
    MK_AVB_RW_ENTRY("Separation filter pattern register 31", "SFP31", 0x17c),
    MK_AVB_RW_ENTRY("Separation Filter Value register 0", "SFV0", 0x1b8),
    MK_AVB_RW_ENTRY("Separation Filter Value register 1", "SFV1", 0x1bc),
    MK_AVB_RW_ENTRY("Separation Filter Mask register 0", "SFM0", 0x1c0),
    MK_AVB_RW_ENTRY("Separation Filter Mask register 1", "SFM1", 0x1c4),
    MK_AVB_RW_INIT_ENTRY("Separation Filter Load register", "SFL", 0x1f, 0x1c8),
    MK_AVB_RW_ENTRY("Payload CRC register", "PCRC", 0x1cc),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 0", "CIAR0", 0x200),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 1", "CIAR1", 0x204),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 2", "CIAR2", 0x208),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 3", "CIAR3", 0x20c),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 4", "CIAR4", 0x210),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 5", "CIAR5", 0x214),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 6", "CIAR6", 0x218),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 7", "CIAR7", 0x21c),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 8", "CIAR8", 0x220),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 9", "CIAR9", 0x224),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 10", "CIAR10", 0x228),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 11", "CIAR11", 0x22c),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 12", "CIAR12", 0x230),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 13", "CIAR13", 0x234),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 14", "CIAR14", 0x238),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 15", "CIAR15", 0x23c),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 16", "CIAR16", 0x240),
    MK_AVB_RW_ENTRY("Current Incremental Address Register 17", "CIAR17", 0x244),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 0", "LIAR0", 0x280),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 1", "LIAR1", 0x284),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 2", "LIAR2", 0x288),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 3", "LIAR3", 0x28c),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 4", "LIAR4", 0x290),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 5", "LIAR5", 0x294),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 6", "LIAR6", 0x298),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 7", "LIAR7", 0x29c),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 8", "LIAR8", 0x2a0),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 9", "LIAR9", 0x2a4),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 10", "LIAR10", 0x2a8),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 11", "LIAR11", 0x2ac),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 12", "LIAR12", 0x2b0),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 13", "LIAR13", 0x2b4),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 14", "LIAR14", 0x2b8),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 15", "LIAR15", 0x2bc),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 16", "LIAR16", 0x2c0),
    MK_AVB_RW_ENTRY("Last Incremental Address Register 17", "LIAR17", 0x2c4),
    MK_AVB_RW_INIT_ENTRY("Transmit configuration register", "TGC", 0x222200, 0x300),
    MK_AVB_RW_ENTRY("Transmit configuration control register", "TCCR", 0x304),
    MK_AVB_RW_ENTRY("Transmit status register", "TSR", 0x308),
    MK_AVB_RW_ENTRY("E-MAC status FIFO Access register", "MFA", 0x30c),
    MK_AVB_RW_ENTRY("Time stamp FIFO access register 0", "TFA0", 0x310),
    MK_AVB_RW_ENTRY("Time stamp FIFO access register 1", "TFA1", 0x314),
    MK_AVB_RW_ENTRY("Time stamp FIFO access register 2", "TFA2", 0x318),
    MK_AVB_RO_INIT_ENTRY("Version and Release Register", "VRR", 0xe300, 0x31c),
    MK_AVB_RW_INIT_ENTRY("CBS increment value register 0", "CIVR0", 0x1, 0x320),
    MK_AVB_RW_INIT_ENTRY("CBS increment value register 1", "CIVR1", 0x1, 0x324),
    MK_AVB_RW_INIT_ENTRY("CBS decrement value register 0", "CDVR0", 0xffffffff, 0x328),
    MK_AVB_RW_INIT_ENTRY("CBS decrement value register 1", "CDVR1", 0xffffffff, 0x32c),
    MK_AVB_RW_INIT_ENTRY("CBS upper limit register 0", "CUL0", 0x7fffffff, 0x330),
    MK_AVB_RW_INIT_ENTRY("CBS upper limit register 1", "CUL1", 0x7fffffff, 0x334),
    MK_AVB_RW_INIT_ENTRY("CBS lower limit register 0", "CLL0", 0x80000001, 0x338),
    MK_AVB_RW_INIT_ENTRY("CBS lower limit register 1", "CLL1", 0x80000001, 0x33c),
    MK_AVB_RW_ENTRY("Descriptor interrupt control register", "DIC", 0x350),
    MK_AVB_RW_ENTRY("Descriptor interrupt status register", "DIS", 0x354),
    MK_AVB_RW_ENTRY("Error interrupt control register", "EIC", 0x358),
    MK_AVB_RW_ENTRY("Error interrupt status register", "EIS", 0x35c),
    MK_AVB_RW_ENTRY("Receive interrupt control register 0", "RIC0", 0x360),
    MK_AVB_RW_ENTRY("Receive interrupt status register 0", "RIS0", 0x364),
    MK_AVB_RW_ENTRY("Receive interrupt control register 1", "RIC1", 0x368),
    MK_AVB_RW_ENTRY("Receive interrupt status register 1", "RIS1", 0x36c),
    MK_AVB_RW_ENTRY("Receive interrupt control register 2", "RIC2", 0x370),
    MK_AVB_RW_ENTRY("Receive interrupt status register 2", "RIS2", 0x374),
    MK_AVB_RW_ENTRY("Transmit interrupt control register", "TIC", 0x378),
    MK_AVB_RW_ENTRY("Transmit interrupt status register", "TIS", 0x37c),
    MK_AVB_RW_ENTRY("Interrupt summary status register", "ISS", 0x380),
    MK_AVB_RW_ENTRY("Common Interrupt Enable register", "CIE", 0x384),
    MK_AVB_RW_ENTRY("Receive interrupt control register 3", "RIC3", 0x388),
    MK_AVB_RW_ENTRY("Receive interrupt status register 3", "RIS3", 0x38c),
    MK_AVB_RW_INIT_ENTRY("gPTP configuration control register", "GCCR", 0x2c, 0x390),
    MK_AVB_RW_ENTRY("gPTP maximum transit time configuration register", "GMTT", 0x394),
    MK_AVB_RW_ENTRY("gPTP presentation time comparison register", "GPTC", 0x398),
    MK_AVB_RW_INIT_ENTRY("gPTP timer increment configuration register", "GTI", 0x1, 0x39c),
    MK_AVB_RW_ENTRY("gPTP timer offset register 0", "GTO0", 0x3a0),
    MK_AVB_RW_ENTRY("gPTP timer offset register 1", "GTO1", 0x3a4),
    MK_AVB_RW_ENTRY("gPTP timer offset register 2", "GTO2", 0x3a8),
    MK_AVB_RW_ENTRY("gPTP interrupt control register", "GIC", 0x3ac),
    MK_AVB_RW_ENTRY("gPTP interrupt status register", "GIS", 0x3b0),
    MK_AVB_RW_ENTRY("gPTP Captured Presentation Time register", "GCPT", 0x3b4),
    MK_AVB_RW_ENTRY("gPTP timer capture register 0", "GCT0", 0x3b8),
    MK_AVB_RW_ENTRY("gPTP timer capture register 1", "GCT1", 0x3bc),
    MK_AVB_RW_ENTRY("gPTP timer capture register 2", "GCT2", 0x3c0),
    MK_AVB_RW_ENTRY("gPTP Status Register", "GSR", 0x3c4),
    MK_AVB_RW_ENTRY("gPTP Interrupt Enable register", "GIE", 0x3cc),
    MK_AVB_RW_ENTRY("gPTP Interrupt Disable register", "GID", 0x3d0),
    MK_AVB_RW_ENTRY("gPTP Interrupt Line selection register", "GIL", 0x3d4),
    MK_AVB_RW_ENTRY("gPTP Avtp Capture Prescaler register", "GACP", 0x3dc),
    MK_AVB_RW_ENTRY("gPTP Presentation Time FIFO register 0", "GPTF0", 0x3e0),
    MK_AVB_RW_ENTRY("gPTP Presentation Time FIFO register 1", "GPTF1", 0x3e4),
    MK_AVB_RW_ENTRY("gPTP Presentation Time FIFO register 2", "GPTF2", 0x3e8),
    MK_AVB_RW_ENTRY("gPTP Presentation Time FIFO register 3", "GPTF3", 0x3ec),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register 0", "GCAT0", 0x400),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  1", "GCAT1", 0x404),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  2", "GCAT2", 0x408),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  3", "GCAT3", 0x40c),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  4", "GCAT4", 0x410),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  5", "GCAT5", 0x414),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  6", "GCAT6", 0x418),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  7", "GCAT7", 0x41c),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  8", "GCAT8", 0x420),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register  9", "GCAT9", 0x424),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register 10", "GCAT10", 0x428),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register 11", "GCAT11", 0x42c),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register 12", "GCAT12", 0x430),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register 13", "GCAT13", 0x434),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register 14", "GCAT14", 0x438),
    MK_AVB_RW_ENTRY("gPTP Captured Avtp Time register 15", "GCAT15", 0x43c),
    MK_AVB_RW_ENTRY("Descriptor Interrupt Line selection register", "DIL", 0x440),
    MK_AVB_RW_ENTRY("Error Interrupt Line selection register", "EIL", 0x444),
    MK_AVB_RW_ENTRY("Transmission Interrupt Line selection register", "TIL", 0x448),
    MK_AVB_RW_ENTRY("Descriptor Interrupt Enable register", "DIE", 0x450),
    MK_AVB_RW_ENTRY("Descriptor Interrupt Disable register", "DID", 0x454),
    MK_AVB_RW_ENTRY("Error Interrupt Enable register", "EIE", 0x458),
    MK_AVB_RW_ENTRY("Error Interrupt Disable register", "EID", 0x45c),
    MK_AVB_RW_ENTRY("Reception Interrupt Enable register 0", "RIE0", 0x460),
    MK_AVB_RW_ENTRY("Reception Interrupt Disable register 0", "RID0", 0x464),
    MK_AVB_RW_ENTRY("Reception Interrupt Enable register 1", "RIE1", 0x468),
    MK_AVB_RW_ENTRY("Reception Interrupt Disable register 1", "RID1", 0x46c),
    MK_AVB_RW_ENTRY("Reception Interrupt Enable register 2", "RIE2", 0x470),
    MK_AVB_RW_ENTRY("Reception Interrupt Disable register 2", "RID2", 0x474),
    MK_AVB_RW_ENTRY("Transmission Interrupt Enable register", "TIE", 0x478),
    MK_AVB_RW_ENTRY("Transmission Interrupt Disable register", "TID", 0x47c),
    MK_AVB_RW_ENTRY("Reception Interrupt Enable register 3", "RIE3", 0x488),
    MK_AVB_RW_ENTRY("Reception Interrupt Disable register 3", "RID3", 0x48c),
    MK_AVB_RW_ENTRY("E-MAC mode register", "ECMR", 0x500),
    MK_AVB_RW_ENTRY("Receive frame length register", "RFLR", 0x508),
    MK_AVB_RW_ENTRY("E-MAC status register", "ECSR", 0x510),
    MK_AVB_RW_ENTRY("E-MAC interrupt permission register", "ECSIPR", 0x518),
    MK_AVB_RW_ENTRY("PHY interface register", "PIR", 0x520),
    MK_AVB_RW_ENTRY("PHY Status register", "PSR", 0x528),
    MK_AVB_RW_ENTRY("PHY_INT Polarity register", "PIPR", 0x52c),
    MK_AVB_RW_ENTRY("Automatic PAUSE frame register", "APR", 0x554),
    MK_AVB_RW_ENTRY("Manual PAUSE frame register", "MPR", 0x558),
    MK_AVB_RW_ENTRY("PAUSE frame transmit counter", "PFTCR", 0x55c),
    MK_AVB_RW_ENTRY("PAUSE frame receive counter", "PFRCR", 0x560),
    MK_AVB_RW_ENTRY("Automatic PAUSE frame retransmit count register", "TPAUSER", 0x564),
    MK_AVB_RW_ENTRY("PAUSE frame transmit times counter", "PFTTCR", 0x568),
    MK_AVB_RW_ENTRY("E-MAC Mode Register 2", "GECMR", 0x5b0),
    MK_AVB_RW_ENTRY("E-MAC address high register", "MAHR", 0x5c0),
    MK_AVB_RW_ENTRY("E-MAC address low register", "MALR", 0x5c8),
    MK_AVB_RW_ENTRY("Transmit retry over counter register", "TROCR", 0x700),
    MK_AVB_RW_ENTRY("Transmit retry over counter register", "CEFCR", 0x740),
    MK_AVB_RW_ENTRY("Too-long frame receive counter register", "TLFRCR", 0x758),
    MK_AVB_RW_ENTRY("Residual-bit frame receive counter register", "RFCR", 0x760),
    MK_AVB_RW_ENTRY("Multicast address frame receive counter register", "MAFCR", 0x778),
};

/* Multicast address frame receive counter register MAFCR R/W 0xE6800778 00000000 */

static const VMStateDescription vmstate_rcar_eth_avb = {
    .name = "rcar_eth_avb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(tcr, RCarEthAvbState),
        VMSTATE_UINT16(rcr, RCarEthAvbState),
        VMSTATE_UINT16(cr, RCarEthAvbState),
        VMSTATE_UINT16(ctr, RCarEthAvbState),
        VMSTATE_UINT16(gpr, RCarEthAvbState),
        VMSTATE_UINT16(ptr, RCarEthAvbState),
        VMSTATE_UINT16(ercv, RCarEthAvbState),
        VMSTATE_INT32(bank, RCarEthAvbState),
        VMSTATE_INT32(packet_num, RCarEthAvbState),
        VMSTATE_INT32(tx_alloc, RCarEthAvbState),
        VMSTATE_INT32(allocated, RCarEthAvbState),
        VMSTATE_INT32(tx_fifo_len, RCarEthAvbState),
        VMSTATE_INT32_ARRAY(tx_fifo, RCarEthAvbState, NUM_PACKETS),
        VMSTATE_INT32(rx_fifo_len, RCarEthAvbState),
        VMSTATE_INT32_ARRAY(rx_fifo, RCarEthAvbState, NUM_PACKETS),
        VMSTATE_INT32(tx_fifo_done_len, RCarEthAvbState),
        VMSTATE_INT32_ARRAY(tx_fifo_done, RCarEthAvbState, NUM_PACKETS),
        VMSTATE_BUFFER_UNSAFE(data, RCarEthAvbState, 0, NUM_PACKETS * 2048),
        VMSTATE_UINT8(int_level, RCarEthAvbState),
        VMSTATE_UINT8(int_mask, RCarEthAvbState),
        VMSTATE_END_OF_LIST()
    }
};

static void ccc_postwrite(RCarEthAvbState *s, struct RcarAvbRegister *reg, uint64_t value)
{
    uint8_t new_csr_op_mode = (1 << (value & 0x3));
    s->reg[OFFSET_TO_REG_IDX(0xc)] = (s->reg[OFFSET_TO_REG_IDX(0xc)] & 0xfffffff0) | new_csr_op_mode;
}

/* Update interrupt status.  */
static void rcar_eth_avb_update(RCarEthAvbState *s)
{
    int level = 0;

    qemu_set_irq(s->irq, level);
}

static bool rcar_eth_avb_can_receive(RCarEthAvbState *s)
{
    return true;
}

static inline void rcar_eth_avb_flush_queued_packets(RCarEthAvbState *s)
{
    if (rcar_eth_avb_can_receive(s)) {
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
    }
}

/* Try to allocate a packet.  Returns 0x80 on failure.  */
/* static int rcar_eth_avb_allocate_packet(RCarEthAvbState *s) */
/* { */
/*     int i; */
/*     if (s->allocated == (1 << NUM_PACKETS) - 1) { */
/*         return 0x80; */
/*     } */

/*     for (i = 0; i < NUM_PACKETS; i++) { */
/*         if ((s->allocated & (1 << i)) == 0) */
/*             break; */
/*     } */
/*     s->allocated |= 1 << i; */
/*     return i; */
/* } */


/* Process a pending TX allocate.  */
/* static void rcar_eth_avb_tx_alloc(RCarEthAvbState *s) */
/* { */
/*     s->tx_alloc = rcar_eth_avb_allocate_packet(s); */
/*     if (s->tx_alloc == 0x80) */
/*         return; */
/*     rcar_eth_avb_update(s); */
/* } */

/* Remove and item from the RX FIFO.  */
/* static void rcar_eth_avb_pop_rx_fifo(RCarEthAvbState *s) */
/* { */
/*     rcar_eth_avb_flush_queued_packets(s); */
/*     rcar_eth_avb_update(s); */
/* } */

/* Remove an item from the TX completion FIFO.  */
/* static void rcar_eth_avb_pop_tx_fifo_done(RCarEthAvbState *s) */
/* { */
/*     int i; */

/*     if (s->tx_fifo_done_len == 0) */
/*         return; */
/*     s->tx_fifo_done_len--; */
/*     for (i = 0; i < s->tx_fifo_done_len; i++) */
/*         s->tx_fifo_done[i] = s->tx_fifo_done[i + 1]; */
/* } */

/* Release the memory allocated to a packet.  */
/* static void rcar_eth_avb_release_packet(RCarEthAvbState *s, int packet) */
/* { */
/*     s->allocated &= ~(1 << packet); */
/*     if (s->tx_alloc == 0x80) */
/*         rcar_eth_avb_tx_alloc(s); */
/*     rcar_eth_avb_flush_queued_packets(s); */
/* } */

/* Flush the TX FIFO.  */
/* static void rcar_eth_avb_do_tx(RCarEthAvbState *s) */
/* { */
/*     int i; */
/*     int len; */
/*     int control; */
/*     int packetnum; */
/*     uint8_t *p; */

/*     if (s->tx_fifo_len == 0) */
/*         return; */
/*     for (i = 0; i < s->tx_fifo_len; i++) { */
/*         packetnum = s->tx_fifo[i]; */
/*         p = &s->data[packetnum][0]; */
/*         /\* Set status word.  *\/ */
/*         *(p++) = 0x01; */
/*         *(p++) = 0x40; */
/*         len = *(p++); */
/*         len |= ((int)*(p++)) << 8; */
/*         len -= 6; */
/*         control = p[len + 1]; */
/*         if (control & 0x20) */
/*             len++; */
/*         else if (s->tx_fifo_done_len < NUM_PACKETS) */
/*             s->tx_fifo_done[s->tx_fifo_done_len++] = packetnum; */
/*         qemu_send_packet(qemu_get_queue(s->nic), p, len); */
/*     } */
/*     s->tx_fifo_len = 0; */
/*     rcar_eth_avb_update(s); */
/* } */

/* Add a packet to the TX FIFO.  */
/* static void rcar_eth_avb_queue_tx(RCarEthAvbState *s, int packet) */
/* { */
/*     if (s->tx_fifo_len == NUM_PACKETS) */
/*         return; */
/*     s->tx_fifo[s->tx_fifo_len++] = packet; */
/*     rcar_eth_avb_do_tx(s); */
/* } */

static void rcar_eth_avb_reset(DeviceState *dev)
{
    RCarEthAvbState *s = RCAR_ETH_AVB(dev);

    for (int i = 0; i < ARRAY_SIZE(avb_regs); ++i) {
        struct RcarAvbRegister *r = &avb_regs[i];
        s->reg[i] = r->reset_value;
    }
}

#define SET_LOW(name, val) s->name = (s->name & 0xff00) | val
#define SET_HIGH(name, val) s->name = (s->name & 0xff) | (val << 8)

/* static void rcar_eth_avb_writeb(void *opaque, hwaddr offset, */
/*                              uint32_t value) */
/* { */
/*     RCarEthAvbState *s = (RCarEthAvbState *)opaque; */


/*     qemu_log_mask(LOG_GUEST_ERROR, "rcar_eth_avb_write(bank:%d) Illegal register" */
/*                                    " 0x%" HWADDR_PRIx " = 0x%x\n", */
/*                   s->bank, offset, value); */
/* } */

/* static uint32_t rcar_eth_avb_readb(void *opaque, hwaddr offset) */
/* { */
/*     RCarEthAvbState *s = (RCarEthAvbState *)opaque; */

/*     offset = offset & 0xf; */
/*     qemu_log_mask(LOG_GUEST_ERROR, "rcar_eth_avb_read(bank:%d) Illegal register" */
/*                                    " 0x%" HWADDR_PRIx "\n", */
/*                   s->bank, offset); */
/*     return 0; */
/* } */

static struct RcarAvbRegister *rcar_get_register_from_offset(hwaddr addr)
{
    for (int i = 0; i < ARRAY_SIZE(avb_regs); ++i) {
        struct RcarAvbRegister *r = &avb_regs[i];
        if (r->offset == addr)
            return r;
    }
    return NULL;
}
static uint64_t rcar_eth_avb_readfn(void *opaque, hwaddr addr, unsigned size)
{
    RCarEthAvbState *s = (RCarEthAvbState *) opaque;
    /* static int count_c = 20; */
    struct RcarAvbRegister *r = rcar_get_register_from_offset(addr);
    if (r->pre_read)   {
        r->pre_read(s, r);
    }
    /* if (addr == 0xc && --count_c == 0) { */
    /*     abort(); */
    /* } */
    printf("-----------> (%s: %s)reading from offset 0x%"HWADDR_PRIx" with size %i. Value is 0x%x\n", r->name, r->description ,addr, size, s->reg[OFFSET_TO_REG_IDX(addr)]);
    return s->reg[OFFSET_TO_REG_IDX(addr)];
}

static void rcar_eth_avb_writefn(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    RCarEthAvbState *s = (RCarEthAvbState *) opaque;
    s->reg[OFFSET_TO_REG_IDX(addr)] = value & 0xffffffff;
    struct RcarAvbRegister *r = rcar_get_register_from_offset(addr);
    if (r->post_write) {
        r->post_write(s, r, value);
    }
    printf("-----------> (%s: %s) writing to  offset 0x%"HWADDR_PRIx" with size %i = 0x%lx\n",  r->name, r->description, addr, size, value);
    
}

static bool rcar_eth_avb_can_receive_nc(NetClientState *nc)
{
    RCarEthAvbState *s = qemu_get_nic_opaque(nc);
    printf("-----------> %s\n", __FUNCTION__);
    return rcar_eth_avb_can_receive(s);
}

static ssize_t rcar_eth_avb_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    RCarEthAvbState *s = qemu_get_nic_opaque(nc);
    printf("-----------> %s\n", __FUNCTION__);
    rcar_eth_avb_update(s);

    return size;
}

static const MemoryRegionOps rcar_eth_avb_mem_ops = {
    .read = rcar_eth_avb_readfn,
    .write = rcar_eth_avb_writefn,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static NetClientInfo net_rcar_eth_avb_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = rcar_eth_avb_can_receive_nc,
    .receive = rcar_eth_avb_receive,
};

static void rcar_eth_avb_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    RCarEthAvbState *s = RCAR_ETH_AVB(dev);
    s->reg = g_new0(uint32_t, ARRAY_SIZE(avb_regs));
    if (s->dma_mr == NULL) {
        error_report("%s: DMA Memory region not set\n", __FUNCTION__);
        abort();
    }
    address_space_init(&s->dma_as, s->dma_mr, "avb-dma-as");
    /* char buf[128]; */
    /* dma_memory_read(&s->dma_as, 0x0 | ((uint64_t)s->io_mmu_utlb << ((sizeof(hwaddr) * 8) - s->num_hsb)), buf, 128); */
    memory_region_init_io(&s->mmio, OBJECT(s), &rcar_eth_avb_mem_ops, s,
                          "rcar_eth_avb-mmio", 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_rcar_eth_avb_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static Property rcar_eth_avb_properties[] = {
    DEFINE_NIC_PROPERTIES(RCarEthAvbState, conf),
    DEFINE_PROP_LINK("dma-memory", RCarEthAvbState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_UINT8("utlb_idx", RCarEthAvbState, io_mmu_utlb, 0),
    DEFINE_PROP_UINT8("hsb_num", RCarEthAvbState, num_hsb, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static void rcar_eth_avb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rcar_eth_avb_realize;
    dc->reset = rcar_eth_avb_reset;
    dc->vmsd = &vmstate_rcar_eth_avb;
    device_class_set_props(dc, rcar_eth_avb_properties);
}

static const TypeInfo rcar_eth_avb_info = {
    .name          = TYPE_RCAR_ETH_AVB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RCarEthAvbState),
    .class_init    = rcar_eth_avb_class_init,
};

static void rcar_eth_avb_register_types(void)
{
    type_register_static(&rcar_eth_avb_info);
}

type_init(rcar_eth_avb_register_types)
