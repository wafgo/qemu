/*
 * NXP LinFlexD Serial
 *
 * Copyright (c) 2024 Wadim Mueller <wadim.mueller@continental-corporation.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This is a `bare-bones' implementation of the S32 series LinFlexD ports, in UART Mode Only
 */

#ifndef S32_LINFLEXD_H
#define S32_LINFLEXD_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"
#include "qemu/fifo32.h"

#define TYPE_LINFLEXD_SERIAL "linflexd.serial"

/* All registers are 32-bit width */
#define LINCR1	0x0000	/* LIN control register				*/
#define LINIER	0x0004	/* LIN interrupt enable register		*/
#define LINSR	0x0008	/* LIN status register				*/
#define LINESR	0x000C	/* LIN error status register			*/
#define UARTCR	0x0010	/* UART mode control register			*/
#define UARTSR	0x0014	/* UART mode status register			*/
#define LINTCSR	0x0018	/* LIN timeout control status register		*/
#define LINOCR	0x001C	/* LIN output compare register			*/
#define LINTOCR	0x0020	/* LIN timeout control register			*/
#define LINFBRR	0x0024	/* LIN fractional baud rate register		*/
#define LINIBRR	0x0028	/* LIN integer baud rate register		*/
#define LINCFR	0x002C	/* LIN checksum field register			*/
#define LINCR2	0x0030	/* LIN control register 2			*/
#define BIDR	0x0034	/* Buffer identifier register			*/
#define BDRL	0x0038	/* Buffer data register least significant	*/
#define BDRM	0x003C	/* Buffer data register most significant	*/
#define IFER	0x0040	/* Identifier filter enable register		*/
#define IFMI	0x0044	/* Identifier filter match index		*/
#define IFMR	0x0048	/* Identifier filter mode register		*/
#define GCR	0x004C	/* Global control register			*/
#define UARTPTO	0x0050	/* UART preset timeout register			*/
#define UARTCTO	0x0054	/* UART current timeout register		*/
#define DMATXE	0x0058	/* DMA Tx enable register			*/
#define DMARXE	0x005C	/* DMA Rx enable register			*/

enum linflexd_mode {
    LINFLEXD_SLEEP = 0,
    LINFLEXD_INIT,
    LINFLEXD_NORMAL,
};

OBJECT_DECLARE_SIMPLE_TYPE(LINFLEXDSerialState, LINFLEXD_SERIAL)

struct LINFLEXDSerialState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    QEMUTimer ageing_timer;
    enum linflexd_mode mode;
    
    uint32_t lincr1;
    uint32_t linier;
    uint32_t linsr;
    uint32_t linesr;
    uint32_t uartcr;
    uint32_t uartsr;
    uint32_t lintcsr;
    uint32_t linocr;
    uint32_t lintocr;
    uint32_t linfbrr;
    uint32_t linibrr;
    uint32_t lincfr;
    uint32_t lincr2;
    uint32_t bidr;
    uint32_t bdrl;
    uint32_t bdrm;
    uint32_t ifer;
    uint32_t ifmi;
    uint32_t ifmr;
    uint32_t gcr;
    uint32_t uartpto;
    uint32_t uartcto;
    uint32_t dmatxe;
    uint32_t dmarxe;
    
    qemu_irq irq;
    CharBackend chr;
};

#endif
