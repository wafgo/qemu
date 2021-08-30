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
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "sysemu/blockdev.h"
#include "sysemu/dma.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "hw/sd/rcar3_sdhi.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qom/object.h"
#include "qemu/bitops.h"

#define RCAR3_SDHI_VERSION 0xd

#define TYPE_RCAR3_SDHI_BUS "rcar3-sdhi-bus"
/* This is reusing the SDBus typedef from SD_BUS */
DECLARE_INSTANCE_CHECKER(SDBus, RCAR3_SDHI_BUS,
                         TYPE_RCAR3_SDHI_BUS)

struct rcar3_sdhi_regs {
  uint64_t cmd;
  uint64_t porsel;
  uint64_t arg;
  uint64_t arg1;
  uint64_t stop;
  uint64_t seccnt;
  uint64_t rsp10;
  uint64_t rsp1;
  uint64_t rsp32;
  uint64_t rsp3;
  uint64_t rsp54;
  uint64_t rsp5;
  uint64_t rsp76;
  uint64_t rsp7;
  uint64_t info1;
  uint64_t info2;
  uint64_t info1_mask;
  uint64_t info2_mask;
  uint64_t clk_ctrl;
  uint64_t size;
  uint64_t option;
  uint64_t err_sts1;
  uint64_t err_sts2;
  uint64_t mode;
  uint64_t sdio_info1;
  uint64_t sdio_info1_mask;
  uint64_t cc_ext_mode;
  uint64_t soft_rst;
  uint64_t version;
  uint64_t host_mode;
  uint64_t sdif_mode;
  uint64_t dm_cm_info1;
  uint64_t dm_cm_info1_mask;
  uint64_t dm_cm_info2;
  uint64_t dm_cm_info2_mask;
  uint64_t dm_cm_seq_regset;
  uint64_t dm_cm_seq_ctrl;
  uint64_t dm_cm_dtran_mode;
  uint64_t dm_cm_dtran_ctrl;
  uint64_t dm_cm_rst;
  uint64_t dm_cm_seq_stat;
  uint64_t dm_dtran_addr;
  uint64_t dm_seq_cmd;
  uint64_t dm_seq_arg;
  uint64_t dm_seq_size;
  uint64_t dm_seq_cnt;
  uint64_t dm_seq_rsp;
  uint64_t dm_seq_rsp_chk;
  uint64_t dm_seq_addr;
};

/* Rcar3 SD Host register offsets */
enum {
  /* Command Type Register */
    SD_CMD       = 0x00,  
    /* Port select register */
    SD_PORTSEL    = 0x08,  
    /* Command Argument Registers */
    SD_ARG       = 0x10,  
    SD_ARG1       = 0x18,  
    /* Data Stop Register */
    SD_STOP       = 0x20,  
    /* Block count register */
    SD_SECCNT     = 0x28,  
    /* SD Card Respons Registers*/
    SD_RSP10       = 0x30,  
    SD_RSP1       = 0x38,  
    SD_RSP32      = 0x40,  
    SD_RSP3      = 0x48,  
    SD_RSP54      = 0x50,  
    SD_RSP5      = 0x58,  
    SD_RSP76       = 0x60,  
    SD_RSP7       = 0x68,
    /* SD card interrupt flag register 1*/
    SD_INFO1       = 0x70,
    /* SD card interrupt flag register 2*/
    SD_INFO2       = 0x78,
    /* SD card interrupt flag register 1*/
    SD_INFO1_MASK = 0x80,
    /* SD card interrupt flag register 2*/
    SD_INFO2_MASK = 0x88,
    /* SD clock control register */
    SD_CLK_CTRL = 0x90,
    /* Transfer data length register */
    SD_SIZE = 0x98,
    /* SD card access control option register */
    SD_OPTION = 0xa0,
    /* SD error status register 1*/
    SD_ERR_STS1 = 0xb0,
    /* SD error status register 2*/
    SD_ERR_STS2 = 0xb8,
    /* SD Buffer read write register*/
    SD_BUF0 = 0xc0,
    /* SDIO mode control register */
    SDIO_MODE = 0xd0,
    /* SDIO interrupt flag register */
    SDIO_INFO1 = 0xd8,
    /* interrupt mask register */
    SDIO_INFO1_MASK = 0xe0,
    /* DMA mode enable register */
    CC_EXT_MODE = 0x0360, 
    /* Software reset register */
    SOFT_RST = 0x0380, 
    /* Version register */
    SDHI_VERSION = 0x0388, 
    /* Host interface mode setting register */
    HOST_MODE = 0x0390, 
    /* SD interface mode setting register */
    SDIF_MODE = 0x0398, 
    /* DMAC interrupt flag register 1 */
    DM_CM_INFO1 = 0x840,
    /* DM_CM_INFO1 interrupt mask register */
    DM_CM_INFO1_MASK = 0x0848, 
    /* DMAC interrupt flag register 2 */
    DM_CM_INFO2 = 0x850,
    /* DM_CM_INFO2 interrupt mask register */
    DM_CM_INFO2_MASK = 0x0858, 
    /* DMAC Sequencer Set Register */
    DM_CM_SEQ_REGSET = 0x0800, 
    /* DMAC Sequencer Control Register */
    DM_CM_SEQ_CTRL = 0x0810, 
    /* DMAC transfer mode register */
    DM_CM_DTRAN_MODE = 0x0820, 
    /* DMAC transfer control register */
    DM_CM_DTRAN_CTRL = 0x0828, 
    /* DMAC reset register */
    DM_CM_RST = 0x0830, 
    /* DMAC Status Register */
    DM_CM_SEQ_STAT = 0x0868, 
    /* DMAC transfer address register */
    DM_DTRAN_ADDR = 0x0880, 
    /* DMAC Sequencer Command Register */
    DM_SEQ_CMD = 0x08A0, 
    /* DMAC Sequencer Argument Register */
    DM_SEQ_ARG = 0x08A8, 
    /* DMAC Sequencer Size Set Register */
    DM_SEQ_SIZE = 0x08B0, 
    /* DMAC Sequencer Count Set register */
    DM_SEQ_SECCNT = 0x08B8, 
    /* DMAC Sequencer Response Register */
    DM_SEQ_RSP = 0x08C0, 
    /* DMAC Sequencer Response Check  register */
    DM_SEQ_RSP_CHK = 0x8C8,
    /* DMAC Sequencer Address Register */
    DM_SEQ_ADDR = 0x8D0
    
};

enum {
  SD_CMD_RST = 0x0,
  SD_PORTSEL_RST = BIT(8),
  SD_ARG_RST = 0x0,
  SD_ARG1_RST = 0x0,
  SD_STOP_RST = 0x0,
  SD_SECCNT_RST = 0x0,
  SD_RSP10_RST = 0x0, 
  SD_RSP1_RST = 0x0,
  SD_RSP32_RST = 0x0,
  SD_RSP3_RST = 0x0,
  SD_RSP54_RST = 0x0,
  SD_RSP5_RST = 0x0,
  SD_RSP76_RST = 0x0,
  SD_RSP7_RST = 0x0,
  SD_INFO1_RST = 0x0,
  SD_INFO2_RST = BIT(13),
  SD_INFO1_MASK_RST = (BIT(0) | BIT(2) | BIT(3) | BIT(4) | BIT(8) | BIT(9) | BIT(16)),
  SD_INFO2_MASK_RST = (BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(8) | BIT(9) | BIT(11) | BIT(15)),
  SD_CLK_CTRL_RST = BIT(5),
  SD_SIZE_RST = BIT(9),
  SD_OPTION_RST = (BIT(1) | BIT(2) | BIT(3) | BIT(5) | BIT(6) | BIT(7) | BIT(14)),
  SD_ERR_STS1_RST = BIT(13),
  SD_ERR_STS2_RST = 0x0,
  SDIO_MODE_RST = 0x0,
  SDIO_INFO1_RST = 0x0,
  SDIO_INFO1_MASK_RST = (BIT(0) | BIT(1) | BIT(2) | BIT(14) | BIT(15)),
  CC_EXT_MODE_RST = (BIT(4) | BIT(12)),
  SOFT_RST_RST = (BIT(0) | BIT(1) | BIT(2)),
  SDHI_VERSION_RST = (BIT(1) | BIT(14) | BIT(15) | (RCAR3_SDHI_VERSION << 8)),
  HOST_MODE_RST = 0x0,
  SDIF_MODE_RST = 0x0,
  DM_CM_INFO1_RST = 0x0,
  DM_CM_INFO1_MASK_RST = 0xffffffff,
  DM_CM_INFO2_RST = 0x0,
  DM_CM_INFO2_MASK_RST = 0xffffffff,
  DM_CM_SEQ_REGSET_RST = 0x0,
  DM_CM_SEQ_CTRL_RST = 0x0,
  DM_CM_DTRAN_MODE_RST = 0x0,
  DM_CM_DTRAN_CTRL_RST = 0x0,
  DM_CM_RST_RST = ~(BIT(8) | BIT(9) | BIT(0)),
  DM_CM_SEQ_STAT_RST = 0x0,
  DM_DTRAN_ADDR_RST = 0x0,
  DM_SEQ_CMD_RST = 0x0,
  DM_SEQ_ARG_RST = 0x0,
  DM_SEQ_SIZE_RST = 0x0,
  DM_SEQ_SECCNT_RST = 0x0,
  DM_SEQ_RSP_RST = 0x0,
  DM_SEQ_RSP_CHK_RST = 0x0,
  DM_SEQ_ADDR_RST = 0x0,
};

static Property rcar3_sdhi_properties[] = {
    DEFINE_PROP_LINK("dma-memory", RCar3SDHIState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};


static void rcar3_sdhi_realize(DeviceState *dev, Error **errp)
{
    RCar3SDHIState *s = RCAR_SDHI(dev);

    if (!s->dma_mr) {
        error_setg(errp, TYPE_RCAR_SDHI " 'dma-memory' link not set");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, "sdhost-dma");
}


static void rcar3_sdhi_reset(Object *obj, ResetType type)
{
  RCar3SDHIState *s = RCAR_SDHI(obj);
  struct rcar3_sdhi_regs *regs = s->regs;
  
  regs->cmd = SD_CMD_RST;
  regs->arg = SD_ARG_RST;
  regs->arg1 = SD_ARG1_RST;
  regs->cc_ext_mode = CC_EXT_MODE_RST;
  regs->clk_ctrl = SD_CLK_CTRL_RST;
  regs->dm_cm_dtran_ctrl = DM_CM_DTRAN_CTRL_RST;
  regs->dm_cm_dtran_mode = DM_CM_DTRAN_MODE_RST;
  regs->dm_cm_info1 = DM_CM_INFO1_RST;
  regs->dm_cm_info1_mask = DM_CM_INFO1_MASK_RST;
  regs->dm_cm_info2 = DM_CM_INFO2_RST;
  regs->dm_cm_info2_mask = DM_CM_INFO2_MASK_RST;
  regs->dm_cm_rst = DM_CM_RST_RST;
  regs->dm_cm_seq_ctrl = DM_CM_SEQ_CTRL_RST;
  regs->dm_cm_seq_regset = DM_CM_SEQ_REGSET_RST;
  regs->dm_cm_seq_stat = DM_CM_SEQ_STAT_RST;
  regs->dm_dtran_addr = DM_DTRAN_ADDR_RST;
  regs->dm_seq_addr = DM_SEQ_ADDR_RST;
  regs->dm_seq_arg = DM_SEQ_ARG_RST;
  regs->dm_seq_cmd = DM_SEQ_CMD_RST;
  regs->dm_seq_cnt = DM_SEQ_SECCNT_RST;
  regs->dm_seq_rsp = DM_SEQ_RSP_RST;
  regs->dm_seq_rsp_chk = DM_SEQ_RSP_CHK_RST;
  regs->dm_seq_size = DM_SEQ_SIZE_RST;
  regs->err_sts1 = SD_ERR_STS1_RST;
  regs->err_sts2 = SD_ERR_STS2_RST;
  regs->host_mode = HOST_MODE_RST;
  regs->info1 = SD_INFO1_RST;
  regs->info1_mask = SD_INFO1_MASK_RST;
  regs->info2 = SD_INFO2_RST;
  regs->info2_mask = SD_INFO2_MASK_RST;
  regs->mode = SDIF_MODE_RST;
  regs->option = SD_OPTION_RST;
  regs->porsel = SD_PORTSEL_RST;
  regs->rsp1 = SD_RSP1_RST;
  regs->rsp10 = SD_RSP10_RST;
  regs->rsp3 = SD_RSP3_RST;
  regs->rsp32 = SD_RSP32_RST;
  regs->rsp5 = SD_RSP5_RST;
  regs->rsp54 = SD_RSP54_RST;
  regs->rsp7 = SD_RSP7_RST;
  regs->rsp76 = SD_RSP76_RST;
  regs->sdif_mode = SDIF_MODE_RST;
  regs->sdio_info1 = SDIO_INFO1_RST;
  regs->sdio_info1_mask = SDIO_INFO1_MASK_RST;
  regs->seccnt = SD_SECCNT_RST;
  regs->size = SD_SIZE_RST;
  regs->soft_rst = SOFT_RST_RST;
  regs->stop = SD_STOP_RST;
  regs->version = SDHI_VERSION_RST;
}

static void rcar3_sdhi_class_init(ObjectClass *klass, void *data)
{
  ResettableClass *rc = RESETTABLE_CLASS(klass);
  DeviceClass *dc = DEVICE_CLASS(klass);
  
  rc->phases.enter = rcar3_sdhi_reset;
  dc->realize = rcar3_sdhi_realize;
  device_class_set_props(dc, rcar3_sdhi_properties);
}

static uint64_t rcar3_sdhi_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
  RCar3SDHIState *s = RCAR_SDHI(opaque);
  struct rcar3_sdhi_regs *regs = s->regs;
  uint64_t value = 0;
  uint64_t mask = ~0;
  uint8_t shift = 0;

  if (size != 8) {
    mask = (1ULL << (size << 3ULL)) - 1ULL;
    if (offset & 0x7) {
      offset &= ~(0x7);
      shift = (size << 3);
    }
  }

  switch(offset) {
  case SD_CMD:
    value = regs->cmd;
    break;
  case SD_PORTSEL:
    value = regs->porsel;
    break;
  case SD_ARG:
    value = regs->arg;
    break;
  case SD_ARG1:
    value = regs->arg1;
    break;
  case SD_STOP:
    value = regs->stop;
    break;
  case SD_SECCNT:
    value = regs->seccnt;
    break;
  case SD_RSP10:
    value = regs->rsp10;
    break;
  case SD_RSP1:
    value = regs->rsp1;
    break;
  case SD_RSP32:
    value = regs->rsp32;
    break;
  case SD_RSP3:
    value = regs->rsp32;
    break;
  case SD_RSP54:
    value = regs->rsp54;
    break;
  case SD_RSP5:
    value = regs->rsp5;
    break;
  case SD_RSP76:
    value = regs->rsp76;
    break;
  case SD_RSP7:
    value = regs->rsp7;
    break;
  case SD_INFO1:
    value = regs->info1;
    break;
  case SD_INFO2:
    value = regs->info2;
    break;
  case SD_INFO1_MASK:
    value = regs->info1_mask;
    break;
  case SD_INFO2_MASK:
    value = regs->info2_mask;
    break;
  case SD_CLK_CTRL:
    value = regs->clk_ctrl;
    break;
  case SD_SIZE:
    value = regs->size;
    break;
  case SD_OPTION:
    value = regs->option;
    break;
  case SD_ERR_STS1:
    value = regs->err_sts1;
    break;
  case SD_ERR_STS2:
    value = regs->err_sts2;
    break;
  case SD_BUF0:
    break;
  case SDIO_MODE:
    value = regs->mode;
    break;
  case SDIO_INFO1:
    value = regs->sdio_info1;
    break;
  case SDIO_INFO1_MASK:
    value = regs->sdio_info1_mask;
    break;
  case CC_EXT_MODE:
    value = regs->cc_ext_mode;
    break;
  case SOFT_RST:
    value = regs->soft_rst;
    break;
  case HOST_MODE:
    value = regs->host_mode;
    break;
  case SDIF_MODE:
    value = regs->sdif_mode;
    break;
  case DM_CM_INFO1:
    value = regs->dm_cm_info1;
    break;
  case DM_CM_INFO1_MASK:
    value = regs->dm_cm_info1_mask;
    break;
  case DM_CM_INFO2:
    value = regs->dm_cm_info2;
    break;
  case DM_CM_INFO2_MASK:
    value = regs->dm_cm_info2_mask;
    break;
  case DM_CM_SEQ_REGSET:
    value = regs->dm_cm_seq_regset;
    break;
  case DM_CM_SEQ_CTRL:
    value = regs->dm_cm_seq_ctrl;
    break;
  case DM_CM_DTRAN_MODE:
    value = regs->dm_cm_dtran_mode;
    break;
  case DM_CM_DTRAN_CTRL:
    value = regs->dm_cm_dtran_ctrl;
    break;
  case DM_CM_RST:
    value = regs->dm_cm_rst;
    break;
  case DM_CM_SEQ_STAT:
    value = regs->dm_cm_seq_stat;
    break;
  case DM_DTRAN_ADDR:
    value = regs->dm_dtran_addr;
    break;
  case DM_SEQ_CMD:
    value = regs->dm_seq_cmd;
    break;
  case DM_SEQ_ARG:
    value = regs->dm_seq_arg;
    break;
  case DM_SEQ_SIZE:
    value = regs->dm_seq_size;
    break;
  case DM_SEQ_SECCNT:
    value = regs->dm_seq_cnt;
    break;
  case DM_SEQ_RSP:
    value = regs->dm_seq_rsp;
    break;
  case DM_SEQ_RSP_CHK:
    value = regs->dm_seq_rsp_chk;
    break;
  case DM_SEQ_ADDR:
    value = regs->dm_seq_addr;
    break;
  default:
    break;
  }

  return (value >> shift) & mask;
}

static void rcar3_sdhi_start_dma_transfer(RCar3SDHIState *s)
{
    /* Using DMA? */
    if ((s->regs->cc_ext_mode & 0x2) && (s->regs->dm_cm_dtran_ctrl & 0x1)) {
        uint32_t buf_size = s->regs->size * ((s->regs->seccnt == 0) ? 1 : s->regs->seccnt);
        uint8_t *buf = g_new0(uint8_t, buf_size);

        switch((s->regs->dm_cm_dtran_mode >> 16) & 0x3) {
        case 0x0: //Write
            dma_memory_read(&s->dma_as, (s->regs->dm_dtran_addr), buf, buf_size);
            sdbus_write_data(&s->sdbus, buf, buf_size);
            break;
        case 0x1: //Read
            sdbus_read_data(&s->sdbus, buf, buf_size);
            dma_memory_write(&s->dma_as, (s->regs->dm_dtran_addr), buf, buf_size);
            break;
        default: //Invalid
            break;
        }
        g_free(buf);
    }
  
}

static void rcar3_sdhi_send_command(RCar3SDHIState *s)
{
    SDRequest request;
    uint8_t resp[16];
    int rlen;
    
    request.cmd = s->regs->cmd & 0x3f;
    request.arg = s->regs->arg;
    s->regs->info1 &= ~(1 << 0);
    rlen = sdbus_do_command(&s->sdbus, &request, resp);
    
    
    if (unlikely(rlen < 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Could not send command to sd card. Error %i", __func__, rlen);
        return;
    } else {
        if (rlen != 0) {
            uint32_t tresp = 0;
            uint8_t *prsp10 = (uint8_t *)&s->regs->rsp10;
            uint8_t *prsp32 = (uint8_t *)&s->regs->rsp32;
            uint8_t *prsp54 = (uint8_t *)&s->regs->rsp54;
            uint8_t *prsp76 = (uint8_t *)&s->regs->rsp76;
            memset(&s->regs->rsp10, 0, sizeof(s->regs->rsp10));
            memset(&s->regs->rsp54, 0, sizeof(s->regs->rsp54));
	
            switch (rlen) {
            case 4:
                tresp = ldl_be_p(&resp[0]);
                memcpy(&s->regs->rsp10, &tresp, sizeof(s->regs->rsp10));
                break;
            case 16:
                for (int i = 0; i < 7; i++) {
                    s->regs->rsp54 |= (uint64_t)resp[i] << ((56ULL - (( (uint64_t)i + 1ULL) * 8ULL)));
                }
                for (int i = 0; i < 8; i++) {
                    s->regs->rsp10 |= (uint64_t)resp[i + 7] << ((64ULL - (((uint64_t)i + 1ULL) * 8ULL)));
                }
                break;
            default:
                break;
            }
	
	
            memcpy(&s->regs->rsp1, &prsp10[2], 2);
            memcpy(&s->regs->rsp32, &prsp10[4], 4);
            memcpy(&s->regs->rsp3, &prsp32[2], 2);

            memcpy(&s->regs->rsp5, &prsp54[2], 2);
            memcpy(&s->regs->rsp76, &prsp54[4], 4);
            memcpy(&s->regs->rsp7, &prsp76[2], 2);
        }
      
        s->regs->info1 |= (1 << 0);
    }
}

static void rcar3_sdhi_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    RCar3SDHIState *s = RCAR_SDHI(opaque);
    struct rcar3_sdhi_regs *regs = s->regs;
    uint64_t mask = ~0;
    uint8_t shift = 0;
    hwaddr orig_offset = offset;

    if (size != 8) {
        mask = ~((1ULL << (size << 3ULL)) - 1ULL);
        if (offset & 0x7) {
            offset &= ~(0x7);
            mask = ~mask;
            shift = (size << 3);
        }
    }

    switch(offset) {
    case SD_CMD:
        regs->cmd &= mask;
        regs->cmd |= (value << shift);
        if (orig_offset == 0)
            rcar3_sdhi_send_command(s);
        break;
    case SD_PORTSEL:
        regs->porsel &= mask;
        regs->porsel |= (value << shift);
        break;
    case SD_ARG:
        regs->arg &= mask;
        regs->arg |= (value << shift);
        break;
    case SD_ARG1:
        regs->arg1 &= mask;
        regs->arg1 |= (value << shift);
        break;
    case SD_STOP:
        regs->stop &= mask;
        regs->stop |= (value << shift);
        break;
    case SD_SECCNT:
        regs->seccnt &= mask;
        regs->seccnt |= (value << shift);
        break;
    case SD_RSP10:
        regs->rsp10 &= mask;
        regs->rsp10 |= (value << shift);
        break;
    case SD_RSP1:
        regs->rsp1 &= mask;
        regs->rsp1 |= (value << shift);
        break;
    case SD_RSP32:
        regs->rsp32 &= mask;
        regs->rsp32 |= (value << shift);
        break;
    case SD_RSP3:
        regs->rsp32 &= mask;
        regs->rsp32 |= (value << shift);
        break;
    case SD_RSP54:
        regs->rsp54 &= mask;
        regs->rsp54 |= (value << shift);
        break;
    case SD_RSP5:
        regs->rsp5 &= mask;
        regs->rsp5 |= (value << shift);
        break;
    case SD_RSP76:
        regs->rsp76 &= mask;
        regs->rsp76 |= (value << shift);
        break;
    case SD_RSP7:
        regs->rsp7 &= mask;
        regs->rsp7 |= (value << shift);
        break;
    case SD_INFO1:
        regs->info1 &= mask;
        regs->info1 |= (value << shift);
        break;
    case SD_INFO2:
        regs->info2 &= mask;
        regs->info2 |= (value << shift);
        break;
    case SD_INFO1_MASK:
        regs->info1_mask &= mask;
        regs->info1_mask |= (value << shift);
        break;
    case SD_INFO2_MASK:
        regs->info2_mask &= mask;
        regs->info2_mask |= (value << shift);
        break;
    case SD_CLK_CTRL:
        regs->clk_ctrl &= mask;
        regs->clk_ctrl |= (value << shift);
        break;
    case SD_SIZE:
        regs->size &= mask;
        regs->size |= (value << shift);
        break;
    case SD_OPTION:
        regs->option &= mask;
        regs->option |= (value << shift);
        break;
    case SD_ERR_STS1:
        regs->err_sts1 &= mask;
        regs->err_sts1 |= (value << shift);
        break;
    case SD_ERR_STS2:
        regs->err_sts2 &= mask;
        regs->err_sts2 |= (value << shift);
        break;
    case SD_BUF0:
        break;
    case SDIO_MODE:
        regs->mode &= mask;
        regs->mode |= (value << shift);
        break;
    case SDIO_INFO1:
        regs->sdio_info1 &= mask;
        regs->sdio_info1 |= (value << shift);
        break;
    case SDIO_INFO1_MASK:
        regs->sdio_info1_mask &= mask;
        regs->sdio_info1_mask |= (value << shift);
        break;
    case CC_EXT_MODE:
        regs->cc_ext_mode &= mask;
        regs->cc_ext_mode |= (value << shift);
        break;
    case SOFT_RST:
        regs->soft_rst &= mask;
        regs->soft_rst |= (value << shift);
        break;
    case HOST_MODE:
        regs->host_mode &= mask;
        regs->host_mode |= (value << shift);
        break;
    case SDIF_MODE:
        regs->sdif_mode &= mask;
        regs->sdif_mode |= (value << shift);
        break;
    case DM_CM_INFO1:
        regs->dm_cm_info1 &= mask;
        regs->dm_cm_info1 |= (value << shift);
        break;
    case DM_CM_INFO1_MASK:
        regs->dm_cm_info1_mask &= mask;
        regs->dm_cm_info1_mask |= (value << shift);
        break;
    case DM_CM_INFO2:
        regs->dm_cm_info2 &= mask;
        regs->dm_cm_info2 |= (value << shift);
        break;
    case DM_CM_INFO2_MASK:
        regs->dm_cm_info2_mask &= mask;
        regs->dm_cm_info2_mask |= (value << shift);
        break;
    case DM_CM_SEQ_REGSET:
        regs->dm_cm_seq_regset &= mask;
        regs->dm_cm_seq_regset |= (value << shift);
        break;
    case DM_CM_SEQ_CTRL:
        regs->dm_cm_seq_ctrl &= mask;
        regs->dm_cm_seq_ctrl |= (value << shift);
        break;
    case DM_CM_DTRAN_MODE:
        regs->dm_cm_dtran_mode &= mask;
        regs->dm_cm_dtran_mode |= (value << shift);
        break;
    case DM_CM_DTRAN_CTRL:
        regs->dm_cm_dtran_ctrl &= mask;
        regs->dm_cm_dtran_ctrl |= (value << shift);
        if ((orig_offset % 8) == 0)
            rcar3_sdhi_start_dma_transfer(s);
        break;
    case DM_CM_RST:
        regs->dm_cm_rst &= mask;
        regs->dm_cm_rst |= (value << shift);
        break;
    case DM_CM_SEQ_STAT:
        regs->dm_cm_seq_stat &= mask;
        regs->dm_cm_seq_stat |= (value << shift);
        break;
    case DM_DTRAN_ADDR:
        regs->dm_dtran_addr &= mask;
        regs->dm_dtran_addr |= (value << shift);
        break;
    case DM_SEQ_CMD:
        regs->dm_seq_cmd &= mask;
        regs->dm_seq_cmd |= (value << shift);
        break;
    case DM_SEQ_ARG:
        regs->dm_seq_arg &= mask;
        regs->dm_seq_arg |= (value << shift);
        break;
    case DM_SEQ_SIZE:
        regs->dm_seq_size &= mask;
        regs->dm_seq_size |= (value << shift);
        break;
    case DM_SEQ_SECCNT:
        regs->dm_seq_cnt &= mask;
        regs->dm_seq_cnt |= (value << shift);
        break;
    case DM_SEQ_RSP:
        regs->dm_seq_rsp &= mask;
        regs->dm_seq_rsp |= (value << shift);
        break;
    case DM_SEQ_RSP_CHK:
        regs->dm_seq_rsp_chk &= mask;
        regs->dm_seq_rsp_chk |= (value << shift);
        break;
    case DM_SEQ_ADDR:
        regs->dm_seq_addr &= mask;
        regs->dm_seq_addr |= (value << shift);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid Register offset %lu\n", __FUNCTION__, offset);
        break;

    }
}

static const MemoryRegionOps rcar3_sdhi_ops = {
    .read = rcar3_sdhi_read,
    .write = rcar3_sdhi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl.min_access_size = 4,
};

static void rcar3_sdhi_init(Object *obj)
{
  RCar3SDHIState *s = RCAR_SDHI(obj);

  struct rcar3_sdhi_regs *regs = g_malloc(sizeof(*regs));
  if (regs == NULL) {
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unable to malloc %i bytes for rcar3 sdhi controller register structure\n", __FUNCTION__, (int)sizeof(*regs));
    return;
  }
  s->regs = regs;
  memset(s->regs, 0, sizeof(*regs));
  qbus_create_inplace(&s->sdbus, sizeof(s->sdbus), TYPE_RCAR3_SDHI_BUS, DEVICE(s), "sd-bus");

  memory_region_init_io(&s->iomem, obj, &rcar3_sdhi_ops, s, TYPE_RCAR_SDHI, 4 * KiB);
  sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
  sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_sdi_all);
}

static void rcar3_sdhi_set_inserted(DeviceState *dev, bool inserted)
{

}


static void rcar3_sdhi_bus_class_init(ObjectClass *klass, void *data)
{
    SDBusClass *sbc = SD_BUS_CLASS(klass);
    sbc->set_inserted = rcar3_sdhi_set_inserted;
}


static const TypeInfo rcar3_sdhi_types[] = {
    {
        .name = TYPE_RCAR_SDHI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RCar3SDHIState),
        .instance_init = rcar3_sdhi_init,
        .class_init = rcar3_sdhi_class_init,
    },
    {
        .name = TYPE_RCAR3_SDHI_BUS,
        .parent = TYPE_SD_BUS,
        .instance_size = sizeof(SDBus),
        .class_init = rcar3_sdhi_bus_class_init,
    }

};

DEFINE_TYPES(rcar3_sdhi_types)
