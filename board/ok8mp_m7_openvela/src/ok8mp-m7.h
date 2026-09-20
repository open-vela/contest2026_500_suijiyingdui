/****************************************************************************
 * boards/arm/mx8mp/ok8mp-m7/src/ok8mp-m7.h
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

#ifndef __BOARDS_ARM_MX8MP_OK8MP_M7_SRC_OK8MP_M7_H
#define __BOARDS_ARM_MX8MP_OK8MP_M7_SRC_OK8MP_M7_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "mx8mp_gpio.h"
#include "mx8mp_iomuxc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Board LEDs */

/* OK8MP board LEDs reported by Linux as gpio-136/led1 and gpio-137/led2.
 * They map to GPIO5_IO08 and GPIO5_IO09 on i.MX8MP.
 */

#define GPIO_LED_1         (GPIO_OUTPUT | GPIO_PORT5 | GPIO_PIN8)
#define GPIO_LED_2         (GPIO_OUTPUT | GPIO_PORT5 | GPIO_PIN9)
#define GPIO_LED_3         (GPIO_OUTPUT | GPIO_PORT1 | GPIO_PIN5)
#define GPIO_LED_4         (GPIO_OUTPUT | GPIO_PORT1 | GPIO_PIN6)

#define IOMUX_LED_1         IOMUXC_ECSPI1_MISO_GPIO5_IO08, 0, GPIO_PAD_CTRL
#define IOMUX_LED_2         IOMUXC_ECSPI1_SS0_GPIO5_IO09, 0, GPIO_PAD_CTRL
#define IOMUX_LED_3         IOMUXC_GPIO1_IO05_GPIO1_IO05, 0, GPIO_PAD_CTRL
#define IOMUX_LED_4         IOMUXC_GPIO1_IO06_GPIO1_IO06, 0, GPIO_PAD_CTRL

/* Board buttons */

#define BUTTON_1_GPIO       (GPIO_INTERRUPT | GPIO_INTBOTH_EDGES | GPIO_PORT1 | GPIO_PIN7)
#define BUTTON_1_IRQ        MX8MP_IRQ_SOFT_GPIO1_7
#define BUTTON_1_IOMUX      IOMUXC_GPIO1_IO07_GPIO1_IO07, 0, GPIO_PAD_CTRL

/* SPIs */

#define IOMUX_SPI1_CLK      IOMUXC_ECSPI1_SCLK_ECSPI1_SCLK, 0, SPI_PAD_CTRL
#define IOMUX_SPI1_MOSI     IOMUXC_ECSPI1_MISO_ECSPI1_MISO, 0, SPI_PAD_CTRL
#define IOMUX_SPI1_MISO     IOMUXC_ECSPI1_MOSI_ECSPI1_MOSI, 0, SPI_PAD_CTRL
#define IOMUX_SPI1_CS       IOMUXC_ECSPI1_SS0_ECSPI1_SS0,   0, SPI_PAD_CTRL

/* ENET1/FEC management interface and the OK8MP PHY reset pin. */

#define IOMUX_FEC_MDC       IOMUXC_SAI1_RXD2_ENET1_MDC,  0, 0x04
#define IOMUX_FEC_MDIO      IOMUXC_SAI1_RXD3_ENET1_MDIO, 0, 0x04
#define IOMUX_FEC_PHY_RESET IOMUXC_SPDIF_RX_GPIO5_IO04,  0, 0x19
#define GPIO_FEC_PHY_RESET  (GPIO_OUTPUT | GPIO_PORT5 | GPIO_PIN4)

/* RGMII input pads use DSE(3) | FSEL | HYS = 0x96.  Output pads use
 * DSE(3) | FSEL = 0x16, matching the NXP MIMX8MP SDK ENET example.
 */

#define IOMUX_FEC_RGMII_RD0    IOMUXC_SAI1_RXD4_ENET1_RGMII_RD0, 0, 0x96
#define IOMUX_FEC_RGMII_RD1    IOMUXC_SAI1_RXD5_ENET1_RGMII_RD1, 0, 0x96
#define IOMUX_FEC_RGMII_RD2    IOMUXC_SAI1_RXD6_ENET1_RGMII_RD2, 0, 0x96
#define IOMUX_FEC_RGMII_RD3    IOMUXC_SAI1_RXD7_ENET1_RGMII_RD3, 0, 0x96
#define IOMUX_FEC_RGMII_RX_CTL IOMUXC_SAI1_TXFS_ENET1_RGMII_RX_CTL, 0, 0x96
#define IOMUX_FEC_RGMII_RXC    IOMUXC_SAI1_TXC_ENET1_RGMII_RXC, 0, 0x96
#define IOMUX_FEC_RGMII_TD0    IOMUXC_SAI1_TXD0_ENET1_RGMII_TD0, 0, 0x16
#define IOMUX_FEC_RGMII_TD1    IOMUXC_SAI1_TXD1_ENET1_RGMII_TD1, 0, 0x16
#define IOMUX_FEC_RGMII_TD2    IOMUXC_SAI1_TXD2_ENET1_RGMII_TD2, 0, 0x16
#define IOMUX_FEC_RGMII_TD3    IOMUXC_SAI1_TXD3_ENET1_RGMII_TD3, 0, 0x16
#define IOMUX_FEC_RGMII_TX_CTL IOMUXC_SAI1_TXD4_ENET1_RGMII_TX_CTL, 0, 0x16
#define IOMUX_FEC_RGMII_TXC    IOMUXC_SAI1_TXD5_ENET1_RGMII_TXC, 0, 0x16

/* Native CAN1 is routed to the isolated CAN1 transceiver on P29.
 *
 * The OK8MP board device tree routes CAN1 through SAI2_RXC (TX) and
 * SAI2_TXC (RX).  The 0x154 pad setting is the board-qualified setting
 * used by the Linux FlexCAN1 pinctrl group.  P29's isolated CAN1 channel
 * has no separately controlled standby GPIO in that board configuration.
 */

#define IOMUX_CAN1_TX          IOMUXC_SAI2_RXC_CAN1_TX, 0, 0x154
#define IOMUX_CAN1_RX          IOMUXC_SAI2_TXC_CAN1_RX, 0, 0x154

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mx8mp_bringup
 *
 * Description:
 *   Bring up board features
 *
 ****************************************************************************/

int mx8mp_bringup(void);

/****************************************************************************
 * Name: mx8mp_i2cdev_initialize
 *
 * Description:
 *   Called to configure all i2c
 *
 ****************************************************************************/

int mx8mp_i2cdev_initialize(void);

/****************************************************************************
 * Name: ok8mp_voice_i2c_initialize
 *
 * Description:
 *   Register the OK8MP-M7 board-level I2C voice sensor test device.
 *
 ****************************************************************************/

#ifdef CONFIG_OK8MP_M7_VOICE_I2C
int ok8mp_voice_i2c_prepare(void);
int ok8mp_voice_i2c_initialize(void);
#endif

#ifdef CONFIG_OK8MP_M7_FLEXCAN1
int ok8mp_flexcan_initialize(void);
#endif

/****************************************************************************
 * Name: mx8mp_spidev_initialize
 *
 * Description:
 *   Called to configure all spi
 *
 ****************************************************************************/

int mx8mp_spidev_initialize(void);

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_ARM_MX8MP_OK8MP_M7_SRC_OK8MP_M7_H */
