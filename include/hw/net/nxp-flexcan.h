#ifndef FLEXCAN_H
#define FLEXCAN_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "net/can_emu.h"
#include "hw/register.h"

struct flexcan_msg_cs {
    unsigned id_ext: 18;
    unsigned id_std: 11;
    unsigned prio: 3;
    unsigned timestamp: 16;
    unsigned dlc: 4;
    unsigned rtr: 1;
    unsigned ide: 1;
    unsigned srr: 1;
    unsigned res1: 1;
    unsigned code: 4;
    unsigned res0: 1;
    unsigned esi: 1;
    unsigned brs: 1;
    unsigned edl: 1;
} QEMU_PACKED;

typedef union flexcan_message_64 {
    char data[72];
    struct msg64{
        struct flexcan_msg_cs cs;
        char data[64];
    } msg;
} QEMU_PACKED flexcan_message_64_t;

typedef union flexcan_message_32 {
    char data[40];
    struct msg32{
        struct flexcan_msg_cs cs;
        char data[32];
    } msg;
} QEMU_PACKED flexcan_message_32_t;

typedef union flexcan_message_16 {
    char data[24];
    struct msg16{
        struct flexcan_msg_cs cs;
        char data[16];
    } msg;
} QEMU_PACKED flexcan_message_16_t;

typedef union flexcan_message_8 {
    char data[16];
    struct msg8{
        struct flexcan_msg_cs cs;
        char data[8];
    } msg;
} QEMU_PACKED flexcan_message_8_t;

#define FLEXCAN_RAM_BLOCK_ONE_SIZE 2048
#define FLEXCAN_FD_MB_NUM 28
#define FLEXCAN_FIFO_DEPTH 128

#define MB_BUSY_BIT BIT(0)

#define MB_RX_INACTIVE 0x0
#define MB_RX_EMPTY 0x4
#define MB_RX_FULL 0x2
#define MB_RX_OVERRUN 0x6
#define MB_RX_RANSWER 0xA

#define MB_TX_INACTIVE 0x8
#define MB_TX_ABORT 0x9
#define MB_TX_DATA_FRAME 0xC
#define MB_TX_REMOTE 0xC // distignuished from DATA FRAME through MB RTR Bit
#define MB_TX_TANSWER 0xE

#define TYPE_FLEXCAN "flexcan"
OBJECT_DECLARE_SIMPLE_TYPE(FlexCanState, FLEXCAN)

struct FlexCanState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    CanBusClientState       bus_client;
    CanBusState             *canfdbus;
    bool freeze_mode;
    bool low_power_mode;
    bool fd_en;
    bool rx_fifo_en;
    bool enh_rx_fifo_en;
    char can_msg_area[FLEXCAN_RAM_BLOCK_ONE_SIZE];
    flexcan_message_64_t canfd_msg_area[FLEXCAN_FD_MB_NUM];
    uint64_t ext_clk_hz;
    qemu_irq irq_bus_off;
    qemu_irq irq_err;
    qemu_irq irq_msg_lower;
    qemu_irq irq_msg_upper;
    uint32_t mcr;       // Module Configuration Register
    uint32_t ctrl1;     // Control 1 Register
    uint32_t timer;     // Free Running Timer
    uint32_t rxmgmask; // Rx Mailboxes Global Mask Register
    uint32_t rx14mask; // Rx 14 Mask Register
    uint32_t rx15mask; // Rx 15 Mask Register
    uint32_t ecr;       // Error Counter
    uint32_t esr1;      // Error and Status 1 Register
    uint32_t imask4;    // Interrupt Masks 4 Register
    uint32_t imask3;    // Interrupt Masks 3 Register
    uint32_t imask2;    // Interrupt Masks 2 Register
    uint32_t imask1;    // Interrupt Masks 1 Register
    uint32_t iflag4;    // Interrupt Flags 4 Register
    uint32_t iflag3;    // Interrupt Flags 3 Register
    uint32_t iflag2;    // Interrupt Flags 2 Register
    uint32_t iflag1;    // Interrupt Flags 1 Register
    uint32_t ctrl2;     // Control 2 Register
    uint32_t esr2;      // Error and Status 2 Register
    uint32_t crcr;      // CRC Register
    uint32_t rxfgmask; // Rx FIFO Global Mask Register
    uint32_t rxfir;     // Rx FIFO Information Register
    uint32_t cbt;       // CAN Bit Timing Register
    uint32_t mecr;      // Memory Error Control Register
    uint32_t erriar;    // Error Injection Address Register
    uint32_t erridpr;   // Error Injection Data Pattern Register
    uint32_t errippr;   // Error Injection Parity Pattern Register
    uint32_t rerrar;    // Error Report Address Register
    uint32_t rerrdr;    // Error Report Data Register
    uint32_t rerrsynr;  // Error Report Syndrome Register
    uint32_t errsr;     // Error Status Register
    uint32_t eprs;      // Enhanced CAN Bit Timing Prescalers Register
    uint32_t encbt;     // Enhanced Nominal CAN Bit Timing Register
    uint32_t edcbt;     // Enhanced Data Phase CAN Bit Timing Register
    uint32_t etdc;      // Enhanced Transceiver Delay Compensation Register
    uint32_t fdctrl;    // CAN FD Control Register
    uint32_t fdcbt;     // CAN FD Bit Timing Register
    uint32_t fdcrc;     // CAN FD CRC Register
    uint32_t erfcr;     // Enhanced Rx FIFO Control Register
    uint32_t erfier;    // Enhanced Rx FIFO Interrupt Enable Register
    uint32_t erfsr;     // Enhanced Rx FIFO Status Register
    uint32_t hr_time_stamp[FLEXCAN_FIFO_DEPTH]; // High Resolution Timestamp Registers
    uint32_t erffel[FLEXCAN_FIFO_DEPTH]; // Enhanced Rx FIFO Filter Element Registers
    uint32_t rximr[FLEXCAN_FIFO_DEPTH]; // Enhanced Rx FIFO Filter Element Registers
};

#endif /* FLEXCAN_H */
