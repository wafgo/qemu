#ifndef HW_CHAR_LINFLEXD_H
#define HW_CHAR_LINFLEXD_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_LINFLEXD "nxp.linflexd"
#define LINFLEXD(obj) OBJECT_CHECK(LinFlexDState, (obj), TYPE_LINFLEXD)

// Register offsets
#define LINFLEXD_LINCR1    0x00
#define LINFLEXD_LINIER    0x04
#define LINFLEXD_LINSR     0x08
#define LINFLEXD_LINESR    0x0C
#define LINFLEXD_UARTCR    0x10
#define LINFLEXD_UARTSR    0x14
#define LINFLEXD_LINTCSR   0x18
#define LINFLEXD_LINOCR    0x1C
#define LINFLEXD_LINTOCR   0x20
#define LINFLEXD_LINFBRR   0x24
#define LINFLEXD_LINIBRR   0x28
#define LINFLEXD_LINCFR    0x2C
#define LINFLEXD_LINCR2    0x30
#define LINFLEXD_BIDR      0x34
#define LINFLEXD_BDRL      0x38
#define LINFLEXD_BDRM      0x3C
#define LINFLEXD_IFER      0x40
#define LINFLEXD_IFMI      0x44
#define LINFLEXD_IFMR      0x48
#define LINFLEXD_GCR       0x4C
#define LINFLEXD_UARTPTO   0x50
#define LINFLEXD_UARTCTO   0x54
#define LINFLEXD_DMATXE    0x58
#define LINFLEXD_DMARXE    0x5C

// Number of 32-bit registers
#define LINFLEXD_NUM_REGS  24

enum linflexd_mode {
    LINFLEXD_SLEEP = 0,
    LINFLEXD_INIT,
    LINFLEXD_NORMAL,
};

// LinFlexD state structure
typedef struct LinFlexDState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    QEMUTimer ageing_timer;
    enum linflexd_mode mode;
    qemu_irq irq;
    CharBackend chr;
    uint32_t regs[LINFLEXD_NUM_REGS];
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

} LinFlexDState;


#endif /* HW_CHAR_LINFLEXD_H */
