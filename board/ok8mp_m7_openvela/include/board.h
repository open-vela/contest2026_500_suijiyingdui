/****************************************************************************
 * boards/arm/mx8mp/ok8mp-m7/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __BOARDS_ARM_MX8MP_OK8MP_M7_INCLUDE_BOARD_H
#define __BOARDS_ARM_MX8MP_OK8MP_M7_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#  include <stdbool.h>

struct ok8mp_fec_phy_s
{
  uint16_t id1;
  uint16_t id2;
  uint16_t bmsr;
  uint16_t status;
};

struct ok8mp_fec_dma_s
{
  uint32_t rx_ring;
  uint32_t tx_ring;
  uint32_t rx_buffer;
  uint32_t tx_buffer;
  uint32_t eimr;
  uint32_t eir;
  uint32_t events;
  uint32_t rx_frames;
  uint32_t rx_bytes;
  uint32_t tx_length;
  uint32_t tx_control;
  uint32_t rdc_domain;
  uint32_t rdc_m7_mda;
  uint32_t rdc_enet_tx_mda;
  uint32_t rdc_enet_rx_mda;
  uint32_t rdc_enet_pdap;
  uint32_t rdc_ocram_mrc;
  uint32_t irq_count;
  uint32_t last_irq;
  uint32_t error_axi_count;
  uint32_t arp_replies;
  uint32_t icmp_replies;
  uint32_t last_frame_length;
  uint8_t arp_sender_mac[6];
  uint8_t arp_sender_ip[4];
  uint8_t icmp_sender_ip[4];
  uint8_t last_frame[42];
};

#  ifdef CONFIG_OK8MP_M7_FEC_PROBE
int ok8mp_fec_phy_probe(struct ok8mp_fec_phy_s *phy);
int ok8mp_fec_dma_initialize(struct ok8mp_fec_dma_s *state);
int ok8mp_fec_dma_initialize_tx(struct ok8mp_fec_dma_s *state);
int ok8mp_fec_dma_enable_interrupts(struct ok8mp_fec_dma_s *state);
int ok8mp_fec_dma_status(struct ok8mp_fec_dma_s *state);
int ok8mp_fec_dma_rdc_status(struct ok8mp_fec_dma_s *state);
int ok8mp_fec_dma_send_test(struct ok8mp_fec_dma_s *state);
int ok8mp_fec_dma_send_arp_request(struct ok8mp_fec_dma_s *state,
                                    const uint8_t source_ip[4],
                                    const uint8_t target_ip[4]);
int ok8mp_fec_dma_send_icmp_echo(struct ok8mp_fec_dma_s *state,
                                  const uint8_t target_mac[6],
                                  const uint8_t source_ip[4],
                                  const uint8_t target_ip[4]);
int ok8mp_fec_dma_reclaim(struct ok8mp_fec_dma_s *state);
#  endif

#  ifdef CONFIG_OK8MP_M7_FEC_NET
struct ok8mp_fec_net_diag_s
{
  uint32_t flags;
  uint32_t ipaddr;
  uint32_t netmask;
  uint32_t draddr;
  uint32_t poll_cycles;
  uint32_t txpoll_calls;
  uint32_t tx_attempts;
  uint32_t tx_arp;
  uint32_t tx_ipv4;
  uint32_t tx_busy;
  uint32_t tx_errors;
  uint32_t tx0_control;
  uint32_t tx1_control;
  uint32_t tx0_length;
  uint32_t tx1_length;
  uint32_t eir;
  uint32_t tdar;
  uint32_t ecr;
  uint32_t rdsr;
  uint32_t tdsr;
  uint8_t tx0_header[14];
  bool registered;
  bool ifup;
  bool local_target;
};

int ok8mp_fec_netinitialize(void);
int ok8mp_fec_net_diag(struct ok8mp_fec_net_diag_s *diag);
#  endif
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Console UART IOMUX configuration */

#define IOMUX_CONSOLE_UART_RX IOMUXC_UART4_RXD_UART4_RX, 0, UART_PAD_CTRL
#define IOMUX_CONSOLE_UART_TX IOMUXC_UART4_TXD_UART4_TX, 0, UART_PAD_CTRL

/* LED definitions **********************************************************/

/* LED index values for use with board_userled() */

#define BOARD_LED_1       0
#define BOARD_LED_2       1
#define BOARD_LED_3       2
#define BOARD_LED_4       3
#define BOARD_NLEDS       4

/* LED bits for use with board_userled_all() */

#define BOARD_LED_1_BIT   (1 << BOARD_LED_1)
#define BOARD_LED_2_BIT   (1 << BOARD_LED_2)
#define BOARD_LED_3_BIT   (1 << BOARD_LED_3)
#define BOARD_LED_4_BIT   (1 << BOARD_LED_4)

/* If CONFIG_ARCH_LEDs is defined, then NuttX will control the LED on board.
 * The following definitions describe how NuttX controls
 * the LEDs:
 *
 *   SYMBOL                Meaning                      LED
 *   -------------------  ----------------------------  --------------------
 */

#define LED_STARTED       0 /* NuttX has been started    None  */
#define LED_HEAPALLOCATE  1 /* Heap has been allocated   ON(1),  OFF(2) */
#define LED_IRQSENABLED   2 /* Interrupts enabled        OFF(1), ON(2)  */
#define LED_STACKCREATED  3 /* Idle stack created        ON(1), ON(2) */
#define LED_INIRQ         4 /* In an interrupt          (no change) */
#define LED_SIGNAL        5 /* In a signal handler      (no change) */
#define LED_ASSERTION     6 /* An assertion failed       ON(3) */
#define LED_PANIC         7 /* The system has crashed    FLASH(1,2) */
#define LED_IDLE          8 /* idle loop                 FLASH(4) */

/* Button definitions *******************************************************/

/* The optional test button is mapped to GPIO1_IO07.  It is not populated as
 * a dedicated user button on every OK8MP carrier revision, so configurations
 * leave it disabled unless the signal has been verified on the target board.
 */

#define BUTTON_1          0
#define NUM_BUTTONS       1

#define BUTTON_1_BIT      (1 << BUTTON_1)

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_ARM_MX8MP_OK8MP_M7_INCLUDE_BOARD_H */
