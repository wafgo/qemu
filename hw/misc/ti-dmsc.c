/*
 * TI DMSC emulator (TISCI minimal service) as a QOM device
 *
 * This is intended to be used together with a TI SEC_PROXY model.
 * The SEC_PROXY remains a transport/queue/data-window emulation, while this
 * device implements the DMSC "firmware logic" (TISCI request/response).
 *
 * Copyright (c) 2026 Wadim Mueller <wadim.mueller@cmblu.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "hw/resettable.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "target/arm/arm-powerctl.h"
#include "qemu/main-loop.h"
#include "hw/misc/ti-dmsc.h"
#include "trace.h"
#include <stdio.h>

/*
 * The sec-proxy's per-thread slot is SEC_PROXY_MSG_MAX_WORDS words wide,
 * but ti_sec_proxy_push_msg() always writes starting at current_message[1]
 * (word 0 is reserved/skipped -- see hw/misc/ti-sec-proxy.c), so the real
 * usable capacity for a pushed payload is one word less than the nominal
 * slot size. TI_DMSC_MAX_WORDS is a DMSC-side request-size limit and is
 * unrelated to this transport capacity; don't conflate the two.
 */
#define TI_DMSC_SEC_PROXY_PAYLOAD_MAX \
    ((SEC_PROXY_MSG_MAX_WORDS - 1) * sizeof(uint32_t))

static const char *ti_dmsc_proc_name_from_id(uint32_t proc_id)
{
      switch (proc_id) {
      case SCICLIENT_PROCID_A53_CL0_C0:
          return "A53_CL0_C0";
      case SCICLIENT_PROCID_A53_CL0_C1:
          return "A53_CL0_C1";

      case SCICLIENT_PROCID_R5_CL0_C0:
          return "R5_CL0_C0";
      case SCICLIENT_PROCID_R5_CL0_C1:
          return "R5_CL0_C1";
      case SCICLIENT_PROCID_R5_CL1_C0:
          return "R5_CL1_C0";
      case SCICLIENT_PROCID_R5_CL1_C1:
          return "R5_CL1_C1";

      case SCICLIENT_PROCID_MCU_M4FSS0_C0:
          return "MCU_M4FSS0_C0";

      default:
          return "UNKNOWN_PROC";
      }
}

static const char *ti_dmsc_host_name_from_id(uint32_t host_id)
{
    switch (host_id) {
    case TISCI_HOST_ID_DMSC:
        return "DMSC";

    case TISCI_HOST_ID_MAIN_0_R5_0:
        return "MAIN_0_R5_0";
    case TISCI_HOST_ID_MAIN_0_R5_1:
        return "MAIN_0_R5_1";
    case TISCI_HOST_ID_MAIN_0_R5_2:
        return "MAIN_0_R5_2";
    case TISCI_HOST_ID_MAIN_0_R5_3:
        return "MAIN_0_R5_3";

    case TISCI_HOST_ID_A53_0:
        return "A53_0";
    case TISCI_HOST_ID_A53_1:
        return "A53_1";
    case TISCI_HOST_ID_A53_2:
        return "A53_2";
    case TISCI_HOST_ID_A53_3:
        return "A53_3";
    case TISCI_HOST_ID_A53_4:
        return "A53_4";

    case TISCI_HOST_ID_M4_0:
        return "M4_0";

    case TISCI_HOST_ID_MAIN_1_R5_0:
        return "MAIN_1_R5_0";
    case TISCI_HOST_ID_MAIN_1_R5_1:
        return "MAIN_1_R5_1";
    case TISCI_HOST_ID_MAIN_1_R5_2:
        return "MAIN_1_R5_2";
    case TISCI_HOST_ID_MAIN_1_R5_3:
        return "MAIN_1_R5_3";

    case TISCI_HOST_ID_ICSSG_0:
        return "ICSSG_0";
    case TISCI_HOST_ID_ICSSG_1:
        return "ICSSG_1";

    default:
        return "UNKNOWN_HOST";
    }
}

static const char *ti_dmsc_device_state_to_str(uint8_t state)
{
    switch (state) {
    case TISCI_MSG_VALUE_DEVICE_SW_STATE_AUTO_OFF:
        return "AUTO";
    case TISCI_MSG_VALUE_DEVICE_SW_STATE_RETENTION:
        return "RETENTION";
    case TISCI_MSG_VALUE_DEVICE_SW_STATE_ON:
        return "ON";
    default:
        return "UNKNOWN_STATE";
    }
}

static const char *ti_dmsc_device_name_from_id(uint32_t dev_id)
{
    switch (dev_id) {
    case TISCI_DEV_ADC0:
        return "ADC0";
    case TISCI_DEV_CMP_EVENT_INTROUTER0:
        return "CMP_EVENT_INTROUTER0";
    case TISCI_DEV_DBGSUSPENDROUTER0:
        return "DBGSUSPENDROUTER0";
    case TISCI_DEV_MAIN_GPIOMUX_INTROUTER0:
        return "MAIN_GPIOMUX_INTROUTER0";
    case TISCI_DEV_MCU_MCU_GPIOMUX_INTROUTER0:
        return "MCU_MCU_GPIOMUX_INTROUTER0";
    case TISCI_DEV_TIMESYNC_EVENT_INTROUTER0:
        return "TIMESYNC_EVENT_INTROUTER0";
    case TISCI_DEV_MCU_M4FSS0:
        return "MCU_M4FSS0";
    case TISCI_DEV_MCU_M4FSS0_CBASS_0:
        return "MCU_M4FSS0_CBASS_0";
    case TISCI_DEV_MCU_M4FSS0_CORE0:
        return "MCU_M4FSS0_CORE0";
    case TISCI_DEV_CPSW0:
        return "CPSW0";
    case TISCI_DEV_CPT2_AGGR0:
        return "CPT2_AGGR0";
    case TISCI_DEV_STM0:
        return "STM0";
    case TISCI_DEV_DCC0:
        return "DCC0";
    case TISCI_DEV_DCC1:
        return "DCC1";
    case TISCI_DEV_DCC2:
        return "DCC2";
    case TISCI_DEV_DCC3:
        return "DCC3";
    case TISCI_DEV_DCC4:
        return "DCC4";
    case TISCI_DEV_DCC5:
        return "DCC5";
    case TISCI_DEV_DMSC0:
        return "DMSC0";
    case TISCI_DEV_MCU_DCC0:
        return "MCU_DCC0";
    case TISCI_DEV_DEBUGSS_WRAP0:
        return "DEBUGSS_WRAP0";
    case TISCI_DEV_DMASS0:
        return "DMASS0";
    case TISCI_DEV_DMASS0_BCDMA_0:
        return "DMASS0_BCDMA_0";
    case TISCI_DEV_DMASS0_CBASS_0:
        return "DMASS0_CBASS_0";
    case TISCI_DEV_DMASS0_INTAGGR_0:
        return "DMASS0_INTAGGR_0";
    case TISCI_DEV_DMASS0_IPCSS_0:
        return "DMASS0_IPCSS_0";
    case TISCI_DEV_DMASS0_PKTDMA_0:
        return "DMASS0_PKTDMA_0";
    case TISCI_DEV_DMASS0_RINGACC_0:
        return "DMASS0_RINGACC_0";
    case TISCI_DEV_MCU_TIMER0:
        return "MCU_TIMER0";
    case TISCI_DEV_TIMER0:
        return "TIMER0";
    case TISCI_DEV_TIMER1:
        return "TIMER1";
    case TISCI_DEV_TIMER2:
        return "TIMER2";
    case TISCI_DEV_TIMER3:
        return "TIMER3";
    case TISCI_DEV_TIMER4:
        return "TIMER4";
    case TISCI_DEV_TIMER5:
        return "TIMER5";
    case TISCI_DEV_TIMER6:
        return "TIMER6";
    case TISCI_DEV_TIMER7:
        return "TIMER7";
    case TISCI_DEV_TIMER8:
        return "TIMER8";
    case TISCI_DEV_TIMER9:
        return "TIMER9";
    case TISCI_DEV_TIMER10:
        return "TIMER10";
    case TISCI_DEV_TIMER11:
        return "TIMER11";
    case TISCI_DEV_MCU_TIMER1:
        return "MCU_TIMER1";
    case TISCI_DEV_MCU_TIMER2:
        return "MCU_TIMER2";
    case TISCI_DEV_MCU_TIMER3:
        return "MCU_TIMER3";
    case TISCI_DEV_ECAP0:
        return "ECAP0";
    case TISCI_DEV_ECAP1:
        return "ECAP1";
    case TISCI_DEV_ECAP2:
        return "ECAP2";
    case TISCI_DEV_ELM0:
        return "ELM0";
    case TISCI_DEV_EMIF_DATA_0_VD:
        return "EMIF_DATA_0_VD";
    case TISCI_DEV_MMCSD0:
        return "MMCSD0";
    case TISCI_DEV_MMCSD1:
        return "MMCSD1";
    case TISCI_DEV_EQEP0:
        return "EQEP0";
    case TISCI_DEV_EQEP1:
        return "EQEP1";
    case TISCI_DEV_GTC0:
        return "GTC0";
    case TISCI_DEV_EQEP2:
        return "EQEP2";
    case TISCI_DEV_ESM0:
        return "ESM0";
    case TISCI_DEV_MCU_ESM0:
        return "MCU_ESM0";
    case TISCI_DEV_FSIRX0:
        return "FSIRX0";
    case TISCI_DEV_FSIRX1:
        return "FSIRX1";
    case TISCI_DEV_FSIRX2:
        return "FSIRX2";
    case TISCI_DEV_FSIRX3:
        return "FSIRX3";
    case TISCI_DEV_FSIRX4:
        return "FSIRX4";
    case TISCI_DEV_FSIRX5:
        return "FSIRX5";
    case TISCI_DEV_FSITX0:
        return "FSITX0";
    case TISCI_DEV_FSITX1:
        return "FSITX1";
    case TISCI_DEV_FSS0:
        return "FSS0";
    case TISCI_DEV_FSS0_FSAS_0:
        return "FSS0_FSAS_0";
    case TISCI_DEV_FSS0_OSPI_0:
        return "FSS0_OSPI_0";
    case TISCI_DEV_GICSS0:
        return "GICSS0";
    case TISCI_DEV_GPIO0:
        return "GPIO0";
    case TISCI_DEV_GPIO1:
        return "GPIO1";
    case TISCI_DEV_MCU_GPIO0:
        return "MCU_GPIO0";
    case TISCI_DEV_GPMC0:
        return "GPMC0";
    case TISCI_DEV_PRU_ICSSG0:
        return "PRU_ICSSG0";
    case TISCI_DEV_PRU_ICSSG1:
        return "PRU_ICSSG1";
    case TISCI_DEV_LED0:
        return "LED0";
    case TISCI_DEV_CPTS0:
        return "CPTS0";
    case TISCI_DEV_DDPA0:
        return "DDPA0";
    case TISCI_DEV_EPWM0:
        return "EPWM0";
    case TISCI_DEV_EPWM1:
        return "EPWM1";
    case TISCI_DEV_EPWM2:
        return "EPWM2";
    case TISCI_DEV_EPWM3:
        return "EPWM3";
    case TISCI_DEV_EPWM4:
        return "EPWM4";
    case TISCI_DEV_EPWM5:
        return "EPWM5";
    case TISCI_DEV_EPWM6:
        return "EPWM6";
    case TISCI_DEV_EPWM7:
        return "EPWM7";
    case TISCI_DEV_EPWM8:
        return "EPWM8";
    case TISCI_DEV_VTM0:
        return "VTM0";
    case TISCI_DEV_MAILBOX0:
        return "MAILBOX0";
    case TISCI_DEV_MAIN2MCU_VD:
        return "MAIN2MCU_VD";
    case TISCI_DEV_MCAN0:
        return "MCAN0";
    case TISCI_DEV_MCAN1:
        return "MCAN1";
    case TISCI_DEV_MCU_MCRC64_0:
        return "MCU_MCRC64_0";
    case TISCI_DEV_MCU2MAIN_VD:
        return "MCU2MAIN_VD";
    case TISCI_DEV_I2C0:
        return "I2C0";
    case TISCI_DEV_I2C1:
        return "I2C1";
    case TISCI_DEV_I2C2:
        return "I2C2";
    case TISCI_DEV_I2C3:
        return "I2C3";
    case TISCI_DEV_MCU_I2C0:
        return "MCU_I2C0";
    case TISCI_DEV_MCU_I2C1:
        return "MCU_I2C1";
    case TISCI_DEV_PCIE0:
        return "PCIE0";
    case TISCI_DEV_R5FSS0:
        return "R5FSS0";
    case TISCI_DEV_R5FSS1:
        return "R5FSS1";
    case TISCI_DEV_R5FSS0_CORE0:
        return "R5FSS0_CORE0";
    case TISCI_DEV_R5FSS0_CORE1:
        return "R5FSS0_CORE1";
    case TISCI_DEV_R5FSS1_CORE0:
        return "R5FSS1_CORE0";
    case TISCI_DEV_R5FSS1_CORE1:
        return "R5FSS1_CORE1";
    case TISCI_DEV_RTI0:
        return "RTI0";
    case TISCI_DEV_RTI1:
        return "RTI1";
    case TISCI_DEV_RTI8:
        return "RTI8";
    case TISCI_DEV_RTI9:
        return "RTI9";
    case TISCI_DEV_RTI10:
        return "RTI10";
    case TISCI_DEV_RTI11:
        return "RTI11";
    case TISCI_DEV_MCU_RTI0:
        return "MCU_RTI0";
    case TISCI_DEV_SA2_UL0:
        return "SA2_UL0";
    case TISCI_DEV_COMPUTE_CLUSTER0:
        return "COMPUTE_CLUSTER0";
    case TISCI_DEV_A53SS0_CORE_0:
        return "A53SS0_CORE_0";
    case TISCI_DEV_A53SS0_CORE_1:
        return "A53SS0_CORE_1";
    case TISCI_DEV_A53SS0:
        return "A53SS0";
    case TISCI_DEV_DDR16SS0:
        return "DDR16SS0";
    case TISCI_DEV_PSC0:
        return "PSC0";
    case TISCI_DEV_MCU_PSC0:
        return "MCU_PSC0";
    case TISCI_DEV_MCSPI0:
        return "MCSPI0";
    case TISCI_DEV_MCSPI1:
        return "MCSPI1";
    case TISCI_DEV_MCSPI2:
        return "MCSPI2";
    case TISCI_DEV_MCSPI3:
        return "MCSPI3";
    case TISCI_DEV_MCSPI4:
        return "MCSPI4";
    case TISCI_DEV_UART0:
        return "UART0";
    case TISCI_DEV_MCU_MCSPI0:
        return "MCU_MCSPI0";
    case TISCI_DEV_MCU_MCSPI1:
        return "MCU_MCSPI1";
    case TISCI_DEV_MCU_UART0:
        return "MCU_UART0";
    case TISCI_DEV_SPINLOCK0:
        return "SPINLOCK0";
    case TISCI_DEV_TIMERMGR0:
        return "TIMERMGR0";
    case TISCI_DEV_UART1:
        return "UART1";
    case TISCI_DEV_UART2:
        return "UART2";
    case TISCI_DEV_UART3:
        return "UART3";
    case TISCI_DEV_UART4:
        return "UART4";
    case TISCI_DEV_UART5:
        return "UART5";
    case TISCI_DEV_BOARD0:
        return "BOARD0";
    case TISCI_DEV_UART6:
        return "UART6";
    case TISCI_DEV_MCU_UART1:
        return "MCU_UART1";
    case TISCI_DEV_USB0:
        return "USB0";
    case TISCI_DEV_SERDES_10G0:
        return "SERDES_10G0";
    case TISCI_DEV_PBIST0:
        return "PBIST0";
    case TISCI_DEV_PBIST1:
        return "PBIST1";
    case TISCI_DEV_PBIST2:
        return "PBIST2";
    case TISCI_DEV_PBIST3:
        return "PBIST3";
    case TISCI_DEV_COMPUTE_CLUSTER0_PBIST_0:
        return "COMPUTE_CLUSTER0_PBIST_0";
    default:
        return "UNKNOWN";
    }
}

static void ti_dmsc_init_device_states(TIDmscState *s)
{
    for (size_t i = 0; i < TISCI_DEV_ID_MAX; i++) {
        s->dev_hw_state[i] = TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;
        s->dev_prog_state[i] = TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;
    }

    s->dev_hw_state[TISCI_DEV_MCU_M4FSS0_CORE0] =
        TISCI_MSG_VALUE_DEVICE_HW_STATE_OFF;
    s->dev_prog_state[TISCI_DEV_MCU_M4FSS0_CORE0] =
        TISCI_MSG_VALUE_DEVICE_HW_STATE_OFF;
    s->m4_running = false;
    memset(s->proc_bootvector, 0, sizeof(s->proc_bootvector));
}

static const char *ti_dmsc_message_name_from_id(uint16_t msg_id)
{
    switch (msg_id) {
    case TISCI_MSG_GET_DEVICE:
        return "GET_DEVICE";
    case TISCI_MSG_SET_DEVICE:
        return "SET_DEVICE";
    case TISCI_MSG_SET_DEVICE_RESETS:
        return "SET_DEVICE_RESETS";
    case TISCI_MSG_DEVICE_DROP_POWERUP_REF:
        return "DEVICE_DROP_POWERUP_REF";
    case TISCI_MSG_PREPARE_SLEEP:
        return "PREPARE_SLEEP";
    case TISCI_MSG_ENTER_SLEEP:
        return "ENTER_SLEEP";
    case TISCI_MSG_VERSION:
        return "VERSION";
    case TISCI_MSG_BOOT_NOTIFICATION:
        return "BOOT_NOTIFICATION";
    case TISCI_MSG_BOARD_CONFIG:
        return "BOARD_CONFIG";
    case TISCI_MSG_BOARD_CONFIG_RM:
        return "BOARD_CONFIG_RM";
    case TISCI_MSG_BOARD_CONFIG_SECURITY:
        return "BOARD_CONFIG_SECURITY";
    case TISCI_MSG_BOARD_CONFIG_PM:
        return "BOARD_CONFIG_PM";
    case TISCI_MSG_ENABLE_WDT:
        return "ENABLE_WDT";
    case TISCI_MSG_WAKE_RESET:
        return "WAKE_RESET";
    case TISCI_MSG_WAKE_REASON:
        return "WAKE_REASON";
    case TISCI_MSG_GOODBYE:
        return "GOODBYE";
    case TISCI_MSG_SYS_RESET:
        return "SYS_RESET";
    case TISCI_MSG_QUERY_MSMC:
        return "QUERY_MSMC";
    case TISCI_MSG_GET_TRACE_CONFIG:
        return "GET_TRACE_CONFIG";
    case TISCI_MSG_QUERY_FW_CAPS:
        return "QUERY_FW_CAPS";
    case TISCI_MSG_SET_CLOCK:
        return "SET_CLOCK";
    case TISCI_MSG_GET_CLOCK:
        return "GET_CLOCK";
    case TISCI_MSG_SET_CLOCK_PARENT:
        return "SET_CLOCK_PARENT";
    case TISCI_MSG_GET_CLOCK_PARENT:
        return "GET_CLOCK_PARENT";
    case TISCI_MSG_GET_NUM_CLOCK_PARENTS:
        return "GET_NUM_CLOCK_PARENTS";
    case TISCI_MSG_SET_FREQ:
        return "SET_FREQ";
    case TISCI_MSG_QUERY_FREQ:
        return "QUERY_FREQ";
    case TISCI_MSG_GET_FREQ:
        return "GET_FREQ";
    case TISCI_MSG_PROC_REQUEST:
        return "PROC_REQUEST";
    case TISCI_MSG_PROC_RELEASE:
        return "PROC_RELEASE";
    case TISCI_MSG_PROC_HANDOVER:
        return "PROC_HANDOVER";
    case TISCI_MSG_SET_CONFIG:
        return "SET_CONFIG";
    case TISCI_MSG_SET_CTRL:
        return "SET_CTRL";
    case TISCI_MSG_GET_STATUS:
        return "GET_STATUS";
    case TISCI_MSG_WAIT_PROC_BOOT_STATUS:
        return "WAIT_PROC_BOOT_STATUS";
    case TISCI_MSG_FWL_SET:
        return "FWL_SET";
    case TISCI_MSG_FWL_GET:
        return "FWL_GET";
    case TISCI_MSG_FWL_CHANGE_OWNER:
        return "FWL_CHANGE_OWNER";
    case TISCI_MSG_SA2UL_GET_DKEK:
        return "SA2UL_GET_DKEK";
    case TISCI_MSG_READ_SWREV:
        return "READ_SWREV";
    case TISCI_MSG_READ_KEYCNT_KEYREV:
        return "READ_KEYCNT_KEYREV";
    default:
        return "UNKNOWN";
    }
}

/*
 * Design notes
 * ------------
 * - This device has NO MMIO itself.
 * - It links to an existing ti-sec-proxy device via a QOM link property.
 * - It registers a per-thread callback at SEC_PROXY, so SEC_PROXY can notify
 *   DMSC when a message is committed (i.e. the last word in the data window was written).
 *
 */
static void ti_dmsc_handle_one(TIDmscClient *client,
                               uint16_t thread_id,
                               const uint32_t *words,
                               size_t nwords);

/*
 * Push a response into the client's tx thread. Secure clients (the R5 SPL,
 * running as TISCI host 35 after ROM handoff) additionally prepend a
 * 4-byte {u16 checksum; u16 reserved} header on top of the plain
 * TISciMsgHdr response -- mirroring the same prefix the SPL prepends to
 * its requests. The checksum is always zero and never verified by
 * u-boot's ti_sci.c, so we simply zero-fill it.
 *
 * All handlers push their response through this single choke point so the
 * secure framing only has to be handled in one place.
 *
 * It is also the single choke point for TISCI no-response semantics
 * (TI_SCI_FLAG_REQ_GENERIC_NORESPONSE): if the request currently being
 * dispatched did not carry TISCI_MSG_FLAG_AOP, the sender does not expect a
 * reply on this thread at all -- pushing one anyway strands a stale message
 * in the single-slot RX thread and can corrupt the pairing of the next
 * request/response. client->cur_req_wants_resp is set from the request's
 * hdr.flags in ti_dmsc_handle_one() before any handler runs, so every
 * handler (and the unknown-message NAK path) is covered by this one check
 * without having to thread the flag through each call site individually.
 * Callers only test the return value for "falsy means push failed", so a
 * suppressed response reports success (nbytes) rather than 0, to avoid
 * spurious "Failed to push ... response" error logs for an intentional
 * no-op.
 */
static size_t ti_dmsc_client_respond(TIDmscClient *client,
                                     const uint32_t *words, size_t nbytes)
{
    TIDmscState *s = client->dmsc;

    if (!client->cur_req_wants_resp) {
        return nbytes;
    }

    if (client->secure) {
        uint32_t buf[TI_DMSC_MAX_WORDS + 1] = { 0 };

        /*
         * Guard against the *real* sec-proxy transport capacity
         * (TI_DMSC_SEC_PROXY_PAYLOAD_MAX), not just against overflowing the
         * local `buf` scratch array. `buf` is deliberately one word larger
         * than TI_DMSC_MAX_WORDS, which would let this check pass for
         * payloads the sec-proxy slot can't actually hold, silently
         * corrupting adjacent TISecProxyThreadInfo fields in
         * ti_sec_proxy_push_msg()'s unchecked memcpy().
         */
        if (nbytes + sizeof(uint32_t) > TI_DMSC_SEC_PROXY_PAYLOAD_MAX) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ti-dmsc: secure response too large (%zu bytes), dropping\n",
                          nbytes);
            return 0;
        }

        memcpy((uint8_t *)buf + sizeof(uint32_t), words, nbytes);
        return ti_sec_proxy_push_msg(s->sec_proxy, client->tx_thread_id,
                                     buf, nbytes + sizeof(uint32_t));
    }

    if (nbytes > TI_DMSC_SEC_PROXY_PAYLOAD_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: response too large (%zu bytes), dropping\n",
                      nbytes);
        return 0;
    }

    return ti_sec_proxy_push_msg(s->sec_proxy, client->tx_thread_id,
                                 words, nbytes);
}

/*
 * Emit the unsolicited boot notification (TISCI_MSG_BOOT_NOTIFICATION,
 * 0x000A) that a real DMSC/sysfw sends once on the boot host's response
 * thread as soon as its firmware is up.
 *
 * On the combined-image (HS-FS) boot flow the ROM starts the sysfw straight
 * from the tiboot3 image, so the R5 SPL never loads it itself -- it goes
 * directly to rproc_start() -> k3_sysctrler_start(), which does a blocking
 * mbox_recv() for this message and hangs (ret = -110) if it never arrives.
 * We therefore (re-)queue it from ti_dmsc_reset_hold() so it is already
 * waiting in the response thread every time the SPL polls: once for the
 * initial cold reset (covering first boot), and again for every later
 * "system_reset" -- ROM-boot mode re-executes the R5 SPL from scratch on
 * reset, and it blocks in k3_sysctrler_start() again, but nothing else
 * would re-send this message (the sec-proxy's own reset intentionally
 * leaves thread slots untouched, see ti_sec_proxy_read_target()), so a
 * notification already consumed on a prior boot simply stays gone.
 *
 * Framing note: for the secure R5 client ti_dmsc_client_respond() prepends
 * the usual 4-byte {u16 checksum; u16 reserved} prefix, so a bare
 * TISciMsgHdr{type=0x000A} lands exactly where u-boot's
 * struct k3_sysctrler_boot_notification_msg expects its cmd_id (offset 4).
 *
 * Idempotency: ti_dmsc_client_respond() -> ti_sec_proxy_push_msg()
 * overwrites the thread's current_message unconditionally, so calling this
 * more than once before the client ever reads it (e.g. two resets in a
 * row) is harmless -- it just overwrites with the same content. The one
 * thing that does need explicit care is the thread's num_messages
 * counter: push_msg() only increments it and nothing decrements an
 * outbound thread's counter (see ti_sec_proxy_read_target()), so it is
 * reset to zero first to avoid unbounded growth across repeated resets.
 */
static void ti_dmsc_send_boot_notification(TIDmscClient *client)
{
    TISciMsgHdr notif = { 0 };

    notif.type = TISCI_MSG_BOOT_NOTIFICATION;
    notif.host = TISCI_HOST_ID_DMSC;
    notif.seq = 0;
    notif.flags = 0;

    ti_sec_proxy_reset_thread_count(client->dmsc->sec_proxy,
                                    client->tx_thread_id);

    /*
     * Unsolicited: not a reply to any client request, so the no-response
     * check in ti_dmsc_client_respond() does not apply here. Force
     * delivery regardless of whatever cur_req_wants_resp was last left at
     * by a previously dispatched message (e.g. a no-response SYS_RESET
     * right before this reset fires).
     */
    client->cur_req_wants_resp = true;

    if (!ti_dmsc_client_respond(client, (uint32_t *)&notif, sizeof(notif))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push BOOT_NOTIFICATION into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/* Bottom half: handle pending message outside MMIO context */
static void ti_dmsc_bh(void *opaque)
{
    TIDmscState *s = opaque;
    uint32_t local_words[TI_DMSC_MAX_WORDS];

    while (true) {
        TIDmscClient *client = NULL;
        size_t local_nwords = 0;
        uint16_t tid = 0;

        qemu_mutex_lock(&s->lock);
        for (uint32_t i = 0; i < s->num_clients; i++) {
            if (s->clients[i].pending) {
                client = &s->clients[i];
                break;
            }
        }
        if (!client) {
            qemu_mutex_unlock(&s->lock);
            return;
        }

        tid = client->rx_thread_id;
        local_nwords = client->pending_nwords;
        if (local_nwords > TI_DMSC_MAX_WORDS) {
            local_nwords = TI_DMSC_MAX_WORDS;
        }
        memcpy(local_words, client->pending_words,
               local_nwords * sizeof(uint32_t));
        client->pending = false;
        client->pending_nwords = 0;
        qemu_mutex_unlock(&s->lock);

        ti_dmsc_handle_one(client, tid, local_words, local_nwords);
    }
}

/*
 * SEC_PROXY callback. Called when a message is committed into an outbound thread.
 * We only act on the thread(s) we're interested in.
 */
static void ti_dmsc_sec_proxy_cb(void *opaque,
                                 uint16_t thread_id,
                                 const uint32_t *words,
                                 size_t nwords)
{
    TIDmscClient *client = opaque;
    TIDmscState *s = client->dmsc;

    if (thread_id != client->rx_thread_id) {
        return;
    }

    qemu_mutex_lock(&s->lock);
    client->pending = true;

    if (nwords > TI_DMSC_MAX_WORDS) {
        nwords = TI_DMSC_MAX_WORDS;
    }
    memcpy(client->pending_words, words, nwords * sizeof(uint32_t));
    client->pending_nwords = nwords;
    qemu_mutex_unlock(&s->lock);

    qemu_bh_schedule(s->bh);
}


/*
 * Core message handler
 * --------------------
 * Here you can implement real TISCI semantics progressively:
 * - parse header.type
 * - implement a handful of message types needed for your M4 boot path
 * - return ACK + payload, or NACK
 */
static void ti_dmsc_handle_one(TIDmscClient *client,
                               uint16_t thread_id,
                               const uint32_t *words,
                               size_t nwords)
{
    TIDmscState *s = client->dmsc;

    if (!s->sec_proxy) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: No sec-proxy linked, dropping message\n");
        return;
    }

    size_t hdr_words = sizeof(TISciMsgHdr) / sizeof(uint32_t);
    size_t min_words = hdr_words + (client->secure ? 1 : 0);

    if (nwords < min_words) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Short message (words=%zu), dropping\n",
                      nwords);
        return;
    }

    if (client->secure) {
        /*
         * Secure clients (R5 SPL) prefix every message with a 4-byte
         * {u16 checksum; u16 reserved} word ahead of the TISciMsgHdr.
         * Strip it before parsing/dispatching so the header and any
         * request-struct casts done by the handlers line up exactly like
         * they do for the non-secure M4/A53 clients.
         */
        words += 1;
        nwords -= 1;
    }

    TISciMsgHdr hdr = { 0 };
    memcpy(&hdr, words, MIN(sizeof(hdr), nwords * sizeof(uint32_t)));

    /*
     * TISCI no-response semantics: remember whether this request asked for
     * a response (TISCI_MSG_FLAG_AOP) before dispatching to a handler or
     * building the unknown-message NAK below -- ti_dmsc_client_respond()
     * checks this to decide whether to actually push anything. The handler
     * itself still runs unconditionally for its side effects (device state
     * updates, etc.); only the reply is suppressed.
     */
    client->cur_req_wants_resp = (hdr.flags & TISCI_MSG_FLAG_AOP) != 0;

    if (hdr.type < ARRAY_SIZE(s->msg_handler) && s->msg_handler[hdr.type]) {
        trace_dmsc_new_message_received(hdr.type, ti_dmsc_message_name_from_id(hdr.type),
               ti_dmsc_host_name_from_id(hdr.host), thread_id);
        s->msg_handler[hdr.type](client, &hdr, thread_id, words, nwords);
        return;
    } else {
        TISciMsgHdr resp = hdr;

        trace_dmsc_unsupported_message(ti_dmsc_message_name_from_id(hdr.type), hdr.type, ti_dmsc_host_name_from_id(hdr.host), thread_id);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: No handler for message type=0x%04x (%s), dropping\n",
                      hdr.type, ti_dmsc_message_name_from_id(hdr.type));

        /*
         * NAK unknown message types instead of silently dropping them: a
         * header-only response with no ACK flag unblocks the sender's rx
         * wait instead of leaving it stuck until a timeout.
         */
        resp.flags = 0;
        if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ti-dmsc: Failed to push NAK response into sec-proxy thread=%u\n",
                          client->tx_thread_id);
        }
    }
}

static void ti_dmsc_reset_hold(Object *obj, ResetType type)
{
    TIDmscState *s = TI_DMSC(obj);

    qemu_mutex_lock(&s->lock);
    ti_dmsc_init_device_states(s);
    for (uint32_t i = 0; i < s->num_clients; i++) {
        s->clients[i].pending = false;
        s->clients[i].pending_nwords = 0;
        memset(s->clients[i].pending_words, 0,
               sizeof(s->clients[i].pending_words));
    }
    /*
     * NOTE: s->msg_handler is intentionally *not* cleared here. It only
     * ever holds compile-time-constant function pointers assigned once in
     * ti_dmsc_realize() -- it carries no per-boot guest-visible state, so
     * there is nothing to reset. Clearing it here (as an earlier version
     * of this function did) would permanently disable every TISCI handler
     * the moment this device's reset is actually wired into the machine's
     * reset tree (see qemu_register_resettable() in ti_dmsc_realize()),
     * since the very first cold reset runs right after realize.
     */
    qemu_mutex_unlock(&s->lock);

    /*
     * Re-arm the unsolicited boot notification for every secure client.
     * See the big comment on ti_dmsc_send_boot_notification() for why this
     * has to happen on every reset, not just once at realize time.
     */
    for (uint32_t i = 0; i < s->num_clients; i++) {
        if (s->clients[i].secure) {
            ti_dmsc_send_boot_notification(&s->clients[i]);
        }
    }
}

static TISciMsgHdr ti_dmsc_set_resp_flags(TISciMsgHdr *req_hdr, int add_flags)
{
    TISciMsgHdr resp = *req_hdr;
    resp.flags = ((req_hdr->flags & TISCI_MSG_FLAG_AOP) ? TISCI_MSG_FLAG_ACK : 0) | add_flags;
    return resp;
}

static void ti_dmsc_handle_set_clock(TIDmscClient *client, TISciMsgHdr *hdr,
                                     uint16_t thread_id, const uint32_t *words,
                                     size_t nwords)
{
    struct TisciMsgSetClockReq *req = (struct TisciMsgSetClockReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);
    
    trace_dmsc_handle_set_clock(ti_dmsc_message_name_from_id(hdr->type), ti_dmsc_host_name_from_id(hdr->host), ti_dmsc_device_name_from_id(req->device), req->clk);
    
    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_CLOCK response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }


}

/*
 * TISCI_MSG_SET_CLOCK_PARENT (0x0102): bare-header ACK. u-boot's
 * ti_sci_cmd_clk_set_parent() (drivers/firmware/ti_sci.c) only checks the
 * generic ACK/NACK flag on the response, so no payload is needed. Without
 * this handler the message falls through to the unknown-type NAK path in
 * ti_dmsc_handle_one(), and clk_set_parent() -- called from rproc_init()
 * while probing the a53 rproc node's "gtc" clock -- fails, which panics the
 * R5 SPL in jump_to_image_no_args() before it ever reaches rproc_load().
 */
static void ti_dmsc_handle_set_clock_parent(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    struct TisciMsgSetClockParentReq *req =
        (struct TisciMsgSetClockParentReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_set_clock_parent(ti_dmsc_message_name_from_id(hdr->type),
                                       ti_dmsc_host_name_from_id(hdr->host),
                                       ti_dmsc_device_name_from_id(req->dev_id),
                                       req->clk_id, req->parent_id);

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_CLOCK_PARENT response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_set_freq(TIDmscClient *client, TISciMsgHdr *hdr,
                                    uint16_t thread_id, const uint32_t *words,
                                    size_t nwords)
{
        TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

        if (!ti_dmsc_client_respond(client,
                                   (uint32_t *)&resp, sizeof(resp))) {
                qemu_log_mask(LOG_GUEST_ERROR,
                          "ti-dmsc: Failed to push SET_FREQ response into sec-proxy thread=%u\n",
                          client->tx_thread_id);
        }

}

static void ti_dmsc_handle_query_freq(TIDmscClient *client, TISciMsgHdr *hdr,
                                     uint16_t thread_id,
                                     const uint32_t *words,
                                     size_t nwords)
{
    struct TisciMsgQueryFreqReq *req = (struct TisciMsgQueryFreqReq *)words;
    struct TisciMsgQueryFreqResp resp = { 0 };
    
    trace_dmsc_handle_query_freq(ti_dmsc_message_name_from_id(hdr->type), ti_dmsc_host_name_from_id(hdr->host), ti_dmsc_device_name_from_id(req->device), req->clk, req->clk32, req->target_freq_hz); 


    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.freq_hz = req->target_freq_hz;
    
    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push QUERY_FREQ response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }

}

static void ti_dmsc_handle_get_freq(TIDmscClient *client, TISciMsgHdr *hdr,
                                    uint16_t thread_id,
                                    const uint32_t *words,
                                    size_t nwords)
{
    struct TisciMsgGetFreqReq *req = (struct TisciMsgGetFreqReq *)words;
    struct TisciMsgQueryFreqResp resp = { 0 };

    trace_dmsc_handle_get_freq(ti_dmsc_message_name_from_id(hdr->type),
                               ti_dmsc_host_name_from_id(hdr->host),
                               ti_dmsc_device_name_from_id(req->device),
                               req->clk);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    /*
     * Generic "unit clock" rate: the SPL only sanity-checks that GET_FREQ
     * comes back non-zero (u-boot's clk_get_rate(clk_xin) et al). DDR
     * frequencies come from the devicetree, not from this query, and
     * sdhci divides down from whatever rate it is handed -- so a fixed
     * sane value is sufficient for bring-up.
     */
    resp.freq_hz = 200000000ULL;

    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_FREQ response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }

}

static void ti_dmsc_handle_get_clock_parents(TIDmscClient *client,
                                             TISciMsgHdr *hdr,
                                             uint16_t thread_id,
                                             const uint32_t *words,
                                             size_t nwords)
{
    struct TisciMsgGetNumClockParentsReq *req = (struct TisciMsgGetNumClockParentsReq *)words;
    struct TisciMsgGetNumClockParentsResp resp = { 0 };

    trace_dmsc_handle_get_clock_parents(ti_dmsc_message_name_from_id(hdr->type), ti_dmsc_host_name_from_id(hdr->host), ti_dmsc_device_name_from_id(req->device), req->clk, req->clk32);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    /*
     * u-boot's ti_sci_clk_set_parent() (drivers/clk/ti/clk-sci.c) rejects
     * *any* clk_set_parent() call with -EINVAL unless num_parents >= 2 --
     * it treats a single-parent clock as fixed and refuses to touch it.
     * DT nodes only carry assigned-clock-parents for clocks that genuinely
     * are software-selectable on real silicon (e.g. the a53 rproc node's
     * "gtc" clock, k3_clks 61 0 -> parent k3_clks 61 2), so a generic reply
     * of 1 breaks every such clock. This stub does not model per-clock
     * parent topology, so report the minimum value (2) that lets
     * clk_set_parent() proceed for whichever parent index the caller asks
     * for, instead of hanging the R5 SPL's rproc_init() with
     * "clock has no settable parents!".
     */
    resp.num_parents = 2;
    resp.num_parentint32_t = UINT_MAX;

    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_CLOCK_PARENTS response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }

}

static void ti_dmsc_handle_get_clock(TIDmscClient *client, TISciMsgHdr *hdr,
                                     uint16_t thread_id, const uint32_t *words,
                                     size_t nwords)
{
    struct TisciMsgGetClockReq *req = (struct TisciMsgGetClockReq *)words;
    struct TisciMsgGetClockResp resp = { 0 };
    
    trace_dmsc_handle_get_clock(ti_dmsc_message_name_from_id(hdr->type), ti_dmsc_host_name_from_id(hdr->host), ti_dmsc_device_name_from_id(req->device), req->clk, req->clk32);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.current_state = resp.programmed_state = TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;
    
    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_CLOCK response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
  
}

static void ti_dmsc_stop_proc(TIDmscClient *client, TISciMsgHdr *hdr,
                              uint16_t thread_id, const uint32_t *words,
                              size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TiSciMsgReqProcRelease *req = (struct TiSciMsgReqProcRelease *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);
    trace_dmsc_stop_proc(ti_dmsc_proc_name_from_id(req->processor_id), req->processor_id, ti_dmsc_host_name_from_id(hdr->host));

    if (req->processor_id == SCICLIENT_PROCID_MCU_M4FSS0_C0) {
        s->m4_running = false;
    }

    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push PROC_RELEASE response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }

    
}

static void ti_dmsc_start_proc(TIDmscClient *client,
                                      TISciMsgHdr *hdr,
                                      uint16_t thread_id,
                                      const uint32_t *words,
                                      size_t nwords)
{
    struct TiSciMsgReqProcRequest *req = (struct TiSciMsgReqProcRequest *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);
    
    trace_dmsc_start_proc(ti_dmsc_proc_name_from_id(req->processor_id), req->processor_id, ti_dmsc_host_name_from_id(hdr->host));
     
    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push PROC_REQUEST response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }


}

/*
 * TISCI_MSG_PROC_HANDOVER (0xc005): bare-header ACK. u-boot's
 * ti_sci_proc_release() (drivers/remoteproc/ti_sci_proc.h) calls this
 * instead of PROC_RELEASE whenever the rproc node has a valid
 * ti,sci-host-id (e.g. the a53 rproc node hands the A53 cluster's proc_id
 * over to TISCI_HOST_ID_A53_0). This is the last TISCI step in
 * k3_arm64_start() -> rproc_start(1), called right after the R5 SPL prints
 * "Starting ATF on ARM64 core...". Without a handler here it falls
 * through to the unknown-type NAK path and jump_to_image_no_args() panics
 * with "ATF failed to start on rproc (-19)". Like PROC_RELEASE/
 * PROC_REQUEST above, no host-ownership state is modeled -- just ACK.
 */
static void ti_dmsc_handover_proc(TIDmscClient *client, TISciMsgHdr *hdr,
                                  uint16_t thread_id, const uint32_t *words,
                                  size_t nwords)
{
    struct TiSciMsgReqProcHandover *req =
        (struct TiSciMsgReqProcHandover *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handover_proc(ti_dmsc_proc_name_from_id(req->processor_id),
                             req->processor_id,
                             ti_dmsc_host_name_from_id(req->host_id),
                             ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push PROC_HANDOVER response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_SYS_RESET (0x0005): a full-system reset request. A guest
 * reboot walks Linux/u-boot `reset` -> PSCI SYSTEM_RESET -> TF-A
 * -> ti_sci_msg_req_reboot, which sends this to the DMSC. TISCI
 * defines it as a no-response message (sent with flags==0), so the
 * generic no-handler path would silently drop it and the guest would
 * hang waiting for the reset to take effect. Model TIFS's behaviour by
 * asking QEMU to reset the whole machine; the k3-bootrom reset hook
 * then re-arms the R5 boot core and re-runs the chain, exactly like a
 * real warm reset. If the request unusually asked for an ACK,
 * ti_dmsc_client_respond() emits the bare-header response (its central
 * AOP gate suppresses it otherwise) before tearing the machine down.
 */
static void ti_dmsc_handle_sys_reset(TIDmscClient *client, TISciMsgHdr *hdr,
                                     uint16_t thread_id, const uint32_t *words,
                                     size_t nwords)
{
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_sys_reset(ti_dmsc_message_name_from_id(hdr->type),
                                ti_dmsc_host_name_from_id(hdr->host));

    ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp));
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

static void ti_dmsc_query_hw_caps(TIDmscClient *client,
                                      TISciMsgHdr *hdr,
                                      uint16_t thread_id,
                                      const uint32_t *words,
                                      size_t nwords)
{
    struct TiSciMsgQueryFwCapsResp resp = { 0 };
    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.fw_caps = MSG_FLAG_CAPS_GENERIC;
    trace_dmsc_get_fw_caps(ti_dmsc_message_name_from_id(hdr->type), ti_dmsc_host_name_from_id(hdr->host));
    
    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FW CAPABILITIES response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_get_version(TIDmscClient *client,
                                      TISciMsgHdr *hdr,
                                      uint16_t thread_id,
                                      const uint32_t *words,
                                      size_t nwords)
{
    struct TiSciMsgVersionResp resp = { 0 };
    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.firmware_revision = 0x000a;
    resp.abi_major = 4;
    resp.abi_minor = 0;
    snprintf(resp.firmware_description, sizeof(resp.firmware_description), "QEMU_TI_DMSC (Wadims DMSC)");
    trace_dmsc_get_version(ti_dmsc_message_name_from_id(hdr->type), ti_dmsc_host_name_from_id(hdr->host), resp.firmware_description);
    
    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push VERSION response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_get_device(TIDmscClient *client,
                                      TISciMsgHdr *hdr,
                                      uint16_t thread_id,
                                      const uint32_t *words,
                                      size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgGetDeviceReq *req = (struct TisciMsgGetDeviceReq *)words;
    struct TisciMsgGetDeviceResp resp = { 0 };
    
    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    if (req->id < TISCI_DEV_ID_MAX) {
        resp.current_state = s->dev_hw_state[req->id];
        resp.programmed_state = s->dev_prog_state[req->id];
    } else {
        resp.current_state = resp.programmed_state =
            TISCI_MSG_VALUE_DEVICE_HW_STATE_ON;
    }

    trace_dmsc_handle_get_device(ti_dmsc_message_name_from_id(hdr->type), ti_dmsc_host_name_from_id(hdr->host), ti_dmsc_device_name_from_id(req->id), resp.programmed_state, resp.current_state);

    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_DEVICE response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_get_status(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgProcGetStatusReq *req =
        (struct TisciMsgProcGetStatusReq *)words;
    struct TisciMsgProcGetStatusResp resp = { 0 };

    trace_dmsc_handle_get_status(ti_dmsc_message_name_from_id(hdr->type),
                                 ti_dmsc_host_name_from_id(hdr->host),
                                 ti_dmsc_proc_name_from_id(req->processor_id),
                                 req->processor_id);

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.processor_id = req->processor_id;
    resp.bootvector_lo = 0;
    resp.bootvector_hi = 0;
    resp.config_flags_1 = 0;
    resp.control_flags_1 = 0;
    resp.status_flags_1 = 0;

    if (req->processor_id == SCICLIENT_PROCID_A53_CL0_C0 ||
        req->processor_id == SCICLIENT_PROCID_A53_CL0_C1) {
        uint64_t bv = s->proc_bootvector[req->processor_id -
                                         SCICLIENT_PROCID_A53_CL0_C0];

        resp.bootvector_lo = (uint32_t)bv;
        resp.bootvector_hi = (uint32_t)(bv >> 32);
    }

    if (req->processor_id == SCICLIENT_PROCID_MCU_M4FSS0_C0) {
        resp.status_flags_1 |= TISCI_MSG_VAL_PROC_BOOT_STATUS_FLAG_M4F_WFI;
    }

    trace_dmsc_get_status_resp(ti_dmsc_proc_name_from_id(req->processor_id),
                               req->processor_id,
                               resp.status_flags_1,
                               s->m4_running);

    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push GET_STATUS response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_WAIT_PROC_BOOT_STATUS (0xc401): the R5 shutdown path polls this
 * to check whether a given core reached a particular WFE/WFI status before
 * tearing it down (u-boot's ti_sci_proc_wait_boot_status()). We do not
 * model per-core wait/poll semantics, so this is a side-effect-free no-op
 * handler -- it exists purely so the message is routed here instead of
 * falling through to the "unknown message type" NAK path, which would log
 * a misleading warning for a message the SPL legitimately sends. Whether a
 * reply is actually pushed is governed centrally by
 * ti_dmsc_client_respond()'s AOP check: u-boot's shutdown path sends this
 * with hdr.flags = 0 (TI_SCI_FLAG_REQ_GENERIC_NORESPONSE), so in practice
 * no response goes out; an AOP-flagged caller still gets a bare ACK.
 */
static void ti_dmsc_handle_wait_proc_boot_status(TIDmscClient *client,
                                                 TISciMsgHdr *hdr,
                                                 uint16_t thread_id,
                                                 const uint32_t *words,
                                                 size_t nwords)
{
    struct TisciMsgReqWaitProcBootStatus *req =
        (struct TisciMsgReqWaitProcBootStatus *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_wait_proc_boot_status(
        ti_dmsc_message_name_from_id(hdr->type),
        ti_dmsc_host_name_from_id(hdr->host),
        ti_dmsc_proc_name_from_id(req->processor_id),
        req->processor_id);

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push WAIT_PROC_BOOT_STATUS response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_set_device_state(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgSetDeviceReq *req = (struct TisciMsgSetDeviceReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_set_device_state(ti_dmsc_message_name_from_id(hdr->type),
                                       ti_dmsc_host_name_from_id(hdr->host),
                                       ti_dmsc_device_name_from_id(req->id),
                                       ti_dmsc_device_state_to_str(req->state));

    if (req->id < TISCI_DEV_ID_MAX) 
        s->dev_hw_state[req->id] = s->dev_prog_state[req->id] = req->state;

    if (req->id == TISCI_DEV_MCU_M4FSS0_CORE0 &&
        req->state != TISCI_MSG_VALUE_DEVICE_SW_STATE_ON) {
        s->m4_running = false;
    }

    /*
     * A53 core power control: the R5 SPL's ATF handover
     * (u-boot's k3_r5's boot_core / release_resources_for_core_shutdown)
     * turns the A53 core device on after having programmed its boot
     * vector via TISCI_MSG_SET_CONFIG. Cold-start the matching vCPU at
     * that vector, in EL3/AArch64 -- exactly where a real core comes out
     * of reset to run BL31. arm_set_cpu_on() returning ALREADY_ON for a
     * core that is already running is harmless, so the return value is
     * intentionally not checked (mirrors the M4 handling in
     * ti_dmsc_handle_set_device_resets()).
     */
    if (req->id == TISCI_DEV_A53SS0_CORE_0 ||
        req->id == TISCI_DEV_A53SS0_CORE_1) {
        int core = req->id - TISCI_DEV_A53SS0_CORE_0;
        uint64_t cpuid = s->a53_cpu_id_base + core;

        if (req->state == TISCI_MSG_VALUE_DEVICE_SW_STATE_ON) {
            uint64_t entry = s->proc_bootvector[core];

            trace_dmsc_a53_start(core, entry);
            arm_set_cpu_on(cpuid, entry, 0, /* target_el */ 3,
                           /* target_aa64 */ true);
        } else if (req->state == TISCI_MSG_VALUE_DEVICE_SW_STATE_AUTO_OFF) {
            trace_dmsc_a53_stop(core);
            arm_set_cpu_off(cpuid);
        }
    }

    if (!ti_dmsc_client_respond(client,
                               (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_DEVICE response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_set_device_resets(TIDmscClient *client,
                                             TISciMsgHdr *hdr,
                                             uint16_t thread_id,
                                             const uint32_t *words,
                                             size_t nwords)
{
    TIDmscState *s = client->dmsc;
    struct TisciMsgSetDeviceResetsReq *req =
        (struct TisciMsgSetDeviceResetsReq *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_set_device_resets(ti_dmsc_message_name_from_id(hdr->type),
                                        ti_dmsc_host_name_from_id(hdr->host),
                                        ti_dmsc_device_name_from_id(req->id),
                                        req->resets);

    if (req->id == TISCI_DEV_MCU_M4FSS0_CORE0) {
        s->m4_running = !(req->resets);
        if (req->resets == 1) {
            arm_set_cpu_off(s->m4_cpu_id);
        } else {
            arm_set_cpu_on_and_reset(s->m4_cpu_id);
        }
    } else if (req->id == TISCI_DEV_MCU_M4FSS0_CORE0) {
        s->m4_running = false;
    }

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_DEVICE_RESETS response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * BOARD_CONFIG / BOARD_CONFIG_RM / BOARD_CONFIG_SECURITY / BOARD_CONFIG_PM:
 * the R5 SPL sends these during early bring-up to hand the DMSC its board
 * configuration blobs. A minimal emulation just needs to ACK them so the
 * SPL boot sequence keeps moving; no actual board-config state is modeled.
 */
static void ti_dmsc_handle_board_config(TIDmscClient *client,
                                        TISciMsgHdr *hdr,
                                        uint16_t thread_id,
                                        const uint32_t *words, size_t nwords)
{
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push BOARD_CONFIG response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_SET_CONFIG (0xc100): capture the boot vector for the A53
 * cores, then ACK with a bare header (the rproc-load path -- u-boot's
 * sciclient_procboot / k3_r5_load -- does not inspect any response
 * payload).
 *
 * The request mirrors u-boot's struct ti_sci_msg_req_set_proc_boot_config,
 * which is packed: hdr(8) + u8 processor_id + u32 bootvector_low +
 * u32 bootvector_high + u32 config_flags_set + u32 config_flags_clear.
 * bootvector_low therefore sits at byte offset 9 -- unaligned -- so it
 * must be extracted with ldl_le_p() byte loads, never via a struct cast.
 *
 * The A53 SPL later powers the core up through TISCI_MSG_SET_DEVICE on
 * TISCI_DEV_A53SS0_CORE_0/1; ti_dmsc_handle_set_device_state() starts the
 * vCPU at the vector stored here.
 */
static void ti_dmsc_handle_proc_set_config(TIDmscClient *client,
                                           TISciMsgHdr *hdr,
                                           uint16_t thread_id,
                                           const uint32_t *words, size_t nwords)
{
    TIDmscState *s = client->dmsc;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_proc_set_config(ti_dmsc_message_name_from_id(hdr->type),
                                      ti_dmsc_host_name_from_id(hdr->host));

    if (nwords * sizeof(uint32_t) >= sizeof(TISciMsgHdr) + 9) {
        /* packed payload right after the 8-byte header */
        const uint8_t *p = (const uint8_t *)words + sizeof(TISciMsgHdr);
        uint8_t proc_id = p[0];
        uint64_t bv = (uint64_t)(uint32_t)ldl_le_p(p + 1) |
                      ((uint64_t)(uint32_t)ldl_le_p(p + 5) << 32);

        if (proc_id == SCICLIENT_PROCID_A53_CL0_C0 ||
            proc_id == SCICLIENT_PROCID_A53_CL0_C1) {
            s->proc_bootvector[proc_id - SCICLIENT_PROCID_A53_CL0_C0] = bv;
            trace_dmsc_a53_bootvector(proc_id, bv);
        }
    }

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SET_CONFIG response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_FWL_SET (0x9000): bare-header ACK. OP-TEE's ti_sci_set_fwl_region()
 * (core/arch/arm/plat-k3/drivers/ti_sci.c) only checks the generic ACK/NACK
 * flag; a NAK here makes sa2ul_init() (driver_init) bail out with
 * "Could not set firewall region information" before it ever reaches
 * sa2ul_rng_init(), so this must ACK to let TRNG/HUK bring-up proceed.
 */
static void ti_dmsc_handle_fwl_set(TIDmscClient *client, TISciMsgHdr *hdr,
                                   uint16_t thread_id, const uint32_t *words,
                                   size_t nwords)
{
    struct TisciMsgReqFwlSetFirewallRegion *req =
        (struct TisciMsgReqFwlSetFirewallRegion *)words;
    TISciMsgHdr resp = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_fwl_set(ti_dmsc_message_name_from_id(hdr->type),
                              ti_dmsc_host_name_from_id(hdr->host),
                              req->fwl_id, req->region);

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FWL_SET response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_FWL_GET (0x9001): OP-TEE's ti_sci_get_fwl_region() propagates a
 * NAK straight up as "Could not get firewall region information" and aborts
 * sa2ul_init(), so this must ACK too. The response fields are not load-
 * bearing on the happy path -- ti_sci_get_fwl_region()'s only caller
 * (sa2ul_init()) immediately overwrites control/permissions with its own
 * values via a following FWL_SET -- so echoing the request's fwl_id/region
 * and zeroing the rest is sufficient (brief: "zeroed payload of the right
 * length is acceptable").
 */
static void ti_dmsc_handle_fwl_get(TIDmscClient *client, TISciMsgHdr *hdr,
                                   uint16_t thread_id, const uint32_t *words,
                                   size_t nwords)
{
    struct TisciMsgReqFwlGetFirewallRegion *req =
        (struct TisciMsgReqFwlGetFirewallRegion *)words;
    struct TisciMsgRespFwlGetFirewallRegion resp = { 0 };

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.fwl_id = req->fwl_id;
    resp.region = req->region;

    trace_dmsc_handle_fwl_get(ti_dmsc_message_name_from_id(hdr->type),
                              ti_dmsc_host_name_from_id(hdr->host),
                              req->fwl_id, req->region);

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FWL_GET response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_FWL_CHANGE_OWNER (0x9002): the most safety-critical ACK of this
 * group. OP-TEE's sa2ul_init() calls ti_sci_change_fwl_owner() twice: once
 * for the SA2UL region (a NAK there is explicitly tolerated -- "not fatal,
 * it just means we are on an HS device") and once for the TRNG region,
 * where a NAK is fatal ("Could not change TRNG firewall owner", immediate
 * return) and aborts before sa2ul_rng_init() ever runs. ACK unconditionally
 * so both call sites succeed.
 */
static void ti_dmsc_handle_fwl_change_owner(TIDmscClient *client,
                                            TISciMsgHdr *hdr,
                                            uint16_t thread_id,
                                            const uint32_t *words,
                                            size_t nwords)
{
    struct TisciMsgReqFwlChangeOwnerInfo *req =
        (struct TisciMsgReqFwlChangeOwnerInfo *)words;
    struct TisciMsgRespFwlChangeOwnerInfo resp = { 0 };

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);
    resp.fwl_id = req->fwl_id;
    resp.region = req->region;
    resp.owner_index = req->owner_index;

    trace_dmsc_handle_fwl_change_owner(ti_dmsc_message_name_from_id(hdr->type),
                                       ti_dmsc_host_name_from_id(hdr->host),
                                       req->fwl_id, req->region,
                                       req->owner_index);

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push FWL_CHANGE_OWNER response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_SA2UL_GET_DKEK (0x9029): OP-TEE's tee_otp_get_hw_unique_key()
 * (core/arch/arm/plat-k3/main.c) treats a NAK as fatal for HUK derivation
 * ("Could not get HUK", TEE_ERROR_SECURITY) -- this is the exact failure
 * seen in the pre-fix boot trace. A zeroed DKEK is not a real security
 * property, but it lets OP-TEE's boot proceed past the initcall instead of
 * failing it; no key material this emulator produces should ever be
 * treated as secret.
 */
static void ti_dmsc_handle_sa2ul_get_dkek(TIDmscClient *client,
                                          TISciMsgHdr *hdr,
                                          uint16_t thread_id,
                                          const uint32_t *words,
                                          size_t nwords)
{
    struct TisciMsgReqSa2ulGetDkek *req =
        (struct TisciMsgReqSa2ulGetDkek *)words;
    struct TisciMsgRespSa2ulGetDkek resp = { 0 };

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_sa2ul_get_dkek(ti_dmsc_message_name_from_id(hdr->type),
                                     ti_dmsc_host_name_from_id(hdr->host),
                                     req->sa2ul_instance);

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push SA2UL_GET_DKEK response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

/*
 * TISCI_MSG_READ_SWREV (0x9033) / TISCI_MSG_READ_KEYCNT_KEYREV (0x9034):
 * both are queried from OP-TEE's secure_boot_information()
 * (service_init_late), which only logs the values on success and does
 * nothing on failure -- a NAK is harmless here, but ACKing keeps the boot
 * log free of NAK-storm noise (brief rationale) and matches the sibling
 * 0x90xx handlers above.
 */
static void ti_dmsc_handle_read_swrev(TIDmscClient *client, TISciMsgHdr *hdr,
                                      uint16_t thread_id,
                                      const uint32_t *words, size_t nwords)
{
    struct TisciMsgRespReadSwrev resp = { 0 };

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_read_swrev(ti_dmsc_message_name_from_id(hdr->type),
                                 ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push READ_SWREV response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_handle_read_keycnt_keyrev(TIDmscClient *client,
                                              TISciMsgHdr *hdr,
                                              uint16_t thread_id,
                                              const uint32_t *words,
                                              size_t nwords)
{
    struct TisciMsgRespReadKeycntKeyrev resp = { 0 };

    resp.hdr = ti_dmsc_set_resp_flags(hdr, 0);

    trace_dmsc_handle_read_keycnt_keyrev(
        ti_dmsc_message_name_from_id(hdr->type),
        ti_dmsc_host_name_from_id(hdr->host));

    if (!ti_dmsc_client_respond(client, (uint32_t *)&resp, sizeof(resp))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: Failed to push READ_KEYCNT_KEYREV response into sec-proxy thread=%u\n",
                      client->tx_thread_id);
    }
}

static void ti_dmsc_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    TIDmscState *s = TI_DMSC(dev);

    if (!s->sec_proxy) {
        error_setg(errp, "ti-dmsc: 'sec-proxy' link not set");
        return;
    }

    s->msg_words = ti_sec_proxy_get_msg_words(s->sec_proxy);
    if (s->msg_words == 0) {
        /* Fallback if sec-proxy doesn't provide it yet */
        s->msg_words = TI_DMSC_MAX_WORDS;
    }

    if (s->msg_words > TI_DMSC_MAX_WORDS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ti-dmsc: msg_words=%u too large, clamping to %u\n",
                      s->msg_words, TI_DMSC_MAX_WORDS);
        s->msg_words = TI_DMSC_MAX_WORDS;
    }

    if (s->num_rx_threads || s->num_tx_threads) {
        if (s->num_rx_threads != s->num_tx_threads) {
            error_setg(errp,
                       "ti-dmsc: rx-threads and tx-threads must have the same length");
            return;
        }
        if (s->num_rx_threads == 0) {
            error_setg(errp, "ti-dmsc: rx-threads list is empty");
            return;
        }
        s->num_clients = s->num_rx_threads;
        s->clients = g_new0(TIDmscClient, s->num_clients);
        for (uint32_t i = 0; i < s->num_clients; i++) {
            s->clients[i].dmsc = s;
            s->clients[i].rx_thread_id = s->rx_thread_ids[i];
            s->clients[i].tx_thread_id = s->tx_thread_ids[i];
        }
    } else {
        s->num_clients = 1;
        s->clients = g_new0(TIDmscClient, s->num_clients);
        s->clients[0].dmsc = s;
        s->clients[0].rx_thread_id = s->rx_thread_id;
        s->clients[0].tx_thread_id = s->tx_thread_id;
    }

    for (uint32_t i = 0; i < s->num_clients; i++) {
        for (uint32_t j = 0; j < s->num_secure_rx_threads; j++) {
            if (s->clients[i].rx_thread_id == s->secure_rx_threads[j]) {
                s->clients[i].secure = true;
                break;
            }
        }
    }

    s->msg_handler[TISCI_MSG_PROC_RELEASE] = ti_dmsc_stop_proc;
    s->msg_handler[TISCI_MSG_PROC_REQUEST] = ti_dmsc_start_proc;
    s->msg_handler[TISCI_MSG_PROC_HANDOVER] = ti_dmsc_handover_proc;
    s->msg_handler[TISCI_MSG_SYS_RESET] = ti_dmsc_handle_sys_reset;
    s->msg_handler[TISCI_MSG_QUERY_FW_CAPS] = ti_dmsc_query_hw_caps;
    s->msg_handler[TISCI_MSG_VERSION] = ti_dmsc_get_version;
    s->msg_handler[TISCI_MSG_GET_DEVICE] = ti_dmsc_handle_get_device;
    s->msg_handler[TISCI_MSG_SET_DEVICE] = ti_dmsc_handle_set_device_state;
    s->msg_handler[TISCI_MSG_GET_STATUS] = ti_dmsc_handle_get_status;
    s->msg_handler[TISCI_MSG_WAIT_PROC_BOOT_STATUS] =
        ti_dmsc_handle_wait_proc_boot_status;
    s->msg_handler[TISCI_MSG_SET_DEVICE_RESETS] =
        ti_dmsc_handle_set_device_resets;
    s->msg_handler[TISCI_MSG_GET_CLOCK] = ti_dmsc_handle_get_clock;
    s->msg_handler[TISCI_MSG_SET_CLOCK] = ti_dmsc_handle_set_clock;
    s->msg_handler[TISCI_MSG_GET_NUM_CLOCK_PARENTS] = ti_dmsc_handle_get_clock_parents;
    s->msg_handler[TISCI_MSG_SET_CLOCK_PARENT] =
        ti_dmsc_handle_set_clock_parent;
    s->msg_handler[TISCI_MSG_QUERY_FREQ] = ti_dmsc_handle_query_freq;
    s->msg_handler[TISCI_MSG_GET_FREQ] = ti_dmsc_handle_get_freq;
    s->msg_handler[TISCI_MSG_SET_FREQ] = ti_dmsc_handle_set_freq;
    s->msg_handler[TISCI_MSG_SET_CONFIG] = ti_dmsc_handle_proc_set_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_RM] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_SECURITY] =
        ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_BOARD_CONFIG_PM] = ti_dmsc_handle_board_config;
    s->msg_handler[TISCI_MSG_FWL_SET] = ti_dmsc_handle_fwl_set;
    s->msg_handler[TISCI_MSG_FWL_GET] = ti_dmsc_handle_fwl_get;
    s->msg_handler[TISCI_MSG_FWL_CHANGE_OWNER] =
        ti_dmsc_handle_fwl_change_owner;
    s->msg_handler[TISCI_MSG_SA2UL_GET_DKEK] = ti_dmsc_handle_sa2ul_get_dkek;
    s->msg_handler[TISCI_MSG_READ_SWREV] = ti_dmsc_handle_read_swrev;
    s->msg_handler[TISCI_MSG_READ_KEYCNT_KEYREV] =
        ti_dmsc_handle_read_keycnt_keyrev;

    for (uint32_t i = 0; i < s->num_clients; i++) {
        ti_sec_proxy_register_msg_cb(s->sec_proxy, s->clients[i].rx_thread_id,
                                     ti_dmsc_sec_proxy_cb, &s->clients[i]);
    }

    ti_dmsc_init_device_states(s);

    /*
     * This device has no MMIO and is realized with bus=NULL (see the file
     * header: it's a pure QOM child of the SoC container, not attached to
     * any qdev bus), so nothing would otherwise reset it -- neither the
     * initial cold reset nor a later monitor/QMP "system_reset" walks a
     * bus that ti-dmsc is on. Register it with the global reset container
     * directly, mirroring hw/misc/vmcoreinfo.c's realize(). The boot
     * notification is (re-)queued from ti_dmsc_reset_hold() on every
     * reset, so this also naturally covers the very first boot (the
     * initial cold reset runs once, right after realize, before the guest
     * CPU starts).
     */
    qemu_register_resettable(OBJECT(dev));
}

static void ti_dmsc_init(Object *obj)
{
    TIDmscState *s = TI_DMSC(obj);

    qemu_mutex_init(&s->lock);
    s->bh = qemu_bh_new(ti_dmsc_bh, s);
    s->num_rx_threads = 0;
    s->rx_thread_ids = NULL;
    s->num_tx_threads = 0;
    s->tx_thread_ids = NULL;
    s->num_secure_rx_threads = 0;
    s->secure_rx_threads = NULL;
    s->num_clients = 0;
    s->clients = NULL;

    /*
     * Defaults: you can override these from your machine code (recommended),
     * or later turn them into properties if you prefer CLI configuration.
     *
     * For your M4 case on K3, these are commonly:
     * - RX:  M4_0_WRITE_THREAD
     * - TX:  M4_0_READ_RESPONSE_THREAD
     *
     * Use the enum values you already have in ti-sec-proxy (TISciThreadIds).
     */
    /* s->rx_thread_id = 17; /\* placeholder: M4_0_WRITE_THREAD in your list *\/ */
    /* s->tx_thread_id = 16; /\* placeholder: M4_0_READ_RESPONSE_THREAD *\/ */

    s->msg_words = TI_DMSC_MAX_WORDS;
    object_property_add_link(obj,
                             "sec-proxy",
                             TYPE_TI_SEC_PROXY,
                             (Object **)&s->sec_proxy,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static void ti_dmsc_finalize(Object *obj)
{
    TIDmscState *s = TI_DMSC(obj);

    if (s->bh) {
        qemu_bh_delete(s->bh);
        s->bh = NULL;
    }
    g_free(s->clients);
    s->clients = NULL;
    s->num_clients = 0;
    g_free(s->rx_thread_ids);
    s->rx_thread_ids = NULL;
    s->num_rx_threads = 0;
    g_free(s->tx_thread_ids);
    s->tx_thread_ids = NULL;
    s->num_tx_threads = 0;
    g_free(s->secure_rx_threads);
    s->secure_rx_threads = NULL;
    s->num_secure_rx_threads = 0;
    qemu_mutex_destroy(&s->lock);
}

static const Property ti_dmsc_props[] = {
    DEFINE_PROP_UINT16("rx-thread", TIDmscState, rx_thread_id, 17),
    DEFINE_PROP_UINT16("tx-thread", TIDmscState, tx_thread_id, 16),
    DEFINE_PROP_ARRAY("rx-threads", TIDmscState, num_rx_threads,
                      rx_thread_ids, qdev_prop_uint16, uint16_t),
    DEFINE_PROP_ARRAY("tx-threads", TIDmscState, num_tx_threads,
                      tx_thread_ids, qdev_prop_uint16, uint16_t),
    DEFINE_PROP_ARRAY("secure-rx-threads", TIDmscState,
                      num_secure_rx_threads, secure_rx_threads,
                      qdev_prop_uint16, uint16_t),
    DEFINE_PROP_UINT64("m4-cpu-id", TIDmscState, m4_cpu_id, 0),
    DEFINE_PROP_UINT64("a53-cpu-id-base", TIDmscState, a53_cpu_id_base, 0),
};

static void ti_dmsc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = ti_dmsc_realize;
    rc->phases.hold = ti_dmsc_reset_hold;
    device_class_set_props(dc, ti_dmsc_props);    
}

static const TypeInfo ti_dmsc_info = {
    .name          = TYPE_TI_DMSC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(TIDmscState),
    .instance_init = ti_dmsc_init,
    .instance_finalize = ti_dmsc_finalize,
    .class_init    = ti_dmsc_class_init,
};

static void ti_dmsc_types(void)
{
    type_register_static(&ti_dmsc_info);
}

type_init(ti_dmsc_types)
