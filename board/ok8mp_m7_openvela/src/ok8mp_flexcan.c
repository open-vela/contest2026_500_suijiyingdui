/****************************************************************************
 * boards/arm/mx8mp/ok8mp-m7/src/ok8mp_flexcan.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026
 *
 * Native classic-CAN support for the OK8MP M7 board's isolated CAN1 port.
 *
 * This lower-half is deliberately limited to classic CAN (up to eight data
 * bytes).  The first target is a 500 kbit/s bench bus with an ESP32 TWAI
 * node.  CAN FD can be added later without changing the /dev/can0 API.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_OK8MP_M7_FLEXCAN1

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/can/can.h>
#include <nuttx/can/ok8mp_flexcan.h>
#include <nuttx/irq.h>

#include <debug.h>

#include "arm_internal.h"
#include "mx8mp_ccm.h"
#include "mx8mp_gpio.h"
#include "mx8mp_iomuxc.h"
#include "hardware/mx8mp_ccm.h"

#include "ok8mp-m7.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* FlexCAN1 is at 0x308c0000.  The register layout and bit definitions are
 * shared by the i.MX8MP FlexCAN implementation and NXP's MIMX8MP M7 SDK.
 */

#define OK8MP_FLEXCAN1_BASE          0x308c0000u

#define FLEXCAN_MCR_OFFSET            0x0000
#define FLEXCAN_CTRL1_OFFSET          0x0004
#define FLEXCAN_RXMGMASK_OFFSET       0x0010
#define FLEXCAN_RX14MASK_OFFSET       0x0014
#define FLEXCAN_RX15MASK_OFFSET       0x0018
#define FLEXCAN_ECR_OFFSET             0x001c
#define FLEXCAN_ESR1_OFFSET           0x0020
#define FLEXCAN_IMASK1_OFFSET         0x0028
#define FLEXCAN_IFLAG1_OFFSET         0x0030
#define FLEXCAN_RXIMR_OFFSET(n)       (0x0880u + ((n) << 2))

#define FLEXCAN_MB_OFFSET(n)          (0x0080u + ((n) << 4))
#define FLEXCAN_MB_CS_OFFSET          0x00
#define FLEXCAN_MB_ID_OFFSET          0x04
#define FLEXCAN_MB_WORD0_OFFSET       0x08
#define FLEXCAN_MB_WORD1_OFFSET       0x0c

#define FLEXCAN_MCR_MAXMB_SHIFT       0
#define FLEXCAN_MCR_MAXMB_MASK        (0x7fu << FLEXCAN_MCR_MAXMB_SHIFT)
#define FLEXCAN_MCR_MAXMB(n)          (((uint32_t)(n) << FLEXCAN_MCR_MAXMB_SHIFT) & \
                                       FLEXCAN_MCR_MAXMB_MASK)
#define FLEXCAN_MCR_AEN               (1u << 12)
#define FLEXCAN_MCR_IRMQ              (1u << 16)
#define FLEXCAN_MCR_SRXDIS            (1u << 17)
#define FLEXCAN_MCR_WRNEN             (1u << 21)
#define FLEXCAN_MCR_FRZACK            (1u << 24)
#define FLEXCAN_MCR_SOFTRST           (1u << 25)
#define FLEXCAN_MCR_NOTRDY            (1u << 27)
#define FLEXCAN_MCR_HALT              (1u << 28)
#define FLEXCAN_MCR_RFEN              (1u << 29)
#define FLEXCAN_MCR_FRZ               (1u << 30)
#define FLEXCAN_MCR_MDIS              (1u << 31)
#define FLEXCAN_MCR_LPMACK            (1u << 20)

#define FLEXCAN_CTRL1_PROPSEG_SHIFT   0
#define FLEXCAN_CTRL1_PROPSEG(n)      ((uint32_t)(n) << FLEXCAN_CTRL1_PROPSEG_SHIFT)
#define FLEXCAN_CTRL1_RJW_SHIFT       22
#define FLEXCAN_CTRL1_RJW(n)          ((uint32_t)(n) << FLEXCAN_CTRL1_RJW_SHIFT)
#define FLEXCAN_CTRL1_PSEG2_SHIFT     16
#define FLEXCAN_CTRL1_PSEG2(n)        ((uint32_t)(n) << FLEXCAN_CTRL1_PSEG2_SHIFT)
#define FLEXCAN_CTRL1_PSEG1_SHIFT     19
#define FLEXCAN_CTRL1_PSEG1(n)        ((uint32_t)(n) << FLEXCAN_CTRL1_PSEG1_SHIFT)
#define FLEXCAN_CTRL1_PRESDIV_SHIFT   24
#define FLEXCAN_CTRL1_PRESDIV(n)      ((uint32_t)(n) << FLEXCAN_CTRL1_PRESDIV_SHIFT)
#define FLEXCAN_CTRL1_TIMING_MASK     ((7u << FLEXCAN_CTRL1_PROPSEG_SHIFT) | \
                                       (7u << FLEXCAN_CTRL1_PSEG2_SHIFT) | \
                                       (7u << FLEXCAN_CTRL1_PSEG1_SHIFT) | \
                                       (3u << FLEXCAN_CTRL1_RJW_SHIFT) | \
                                       (0xffu << FLEXCAN_CTRL1_PRESDIV_SHIFT))

#define FLEXCAN_MB_CS_DLC_SHIFT       16
#define FLEXCAN_MB_CS_DLC(n)          ((uint32_t)(n) << FLEXCAN_MB_CS_DLC_SHIFT)
#define FLEXCAN_MB_CS_RTR             (1u << 20)
#define FLEXCAN_MB_CS_IDE             (1u << 21)
#define FLEXCAN_MB_CS_CODE_SHIFT      24
#define FLEXCAN_MB_CS_CODE(n)         ((uint32_t)(n) << FLEXCAN_MB_CS_CODE_SHIFT)
#define FLEXCAN_MB_CS_CODE_MASK       (0x0fu << FLEXCAN_MB_CS_CODE_SHIFT)
#define FLEXCAN_MB_CS_CODE_RX_FULL    0x2u
#define FLEXCAN_MB_CS_CODE_RX_EMPTY   0x4u
#define FLEXCAN_MB_CS_CODE_RX_OVERRUN 0x6u
#define FLEXCAN_MB_CS_CODE_TX_INACTIVE 0x8u
#define FLEXCAN_MB_CS_CODE_TX_DATA    0xcu

#define FLEXCAN_MB_ID_STD_SHIFT       18
#define FLEXCAN_MB_ID_STD_MASK        0x7ffu
#define FLEXCAN_MB_ID_EXT_MASK        0x1fffffffu

#define FLEXCAN_RESERVED_MB           0u
#define FLEXCAN_FIRST_RX_MB           1u
#define FLEXCAN_RX_MB_COUNT           8u
#define FLEXCAN_TX_MB                 (FLEXCAN_FIRST_RX_MB + FLEXCAN_RX_MB_COUNT)
#define FLEXCAN_LAST_MB               FLEXCAN_TX_MB
#define FLEXCAN_RX_MB_MASK            (((1u << FLEXCAN_RX_MB_COUNT) - 1u) \
                                       << FLEXCAN_FIRST_RX_MB)
#define FLEXCAN_TX_MB_MASK            (1u << FLEXCAN_TX_MB)

#define FLEXCAN_WAIT_US               100000u

#define FLEXCAN_ECR_TXERRCNT_MASK     0x000000ffu
#define FLEXCAN_ECR_RXERRCNT_SHIFT    8
#define FLEXCAN_ECR_RXERRCNT_MASK     0x0000ff00u

#define FLEXCAN_ESR1_ERRINT           (1u << 1)
#define FLEXCAN_ESR1_BOFFINT          (1u << 2)
#define FLEXCAN_ESR1_FLTCONF_SHIFT    4
#define FLEXCAN_ESR1_FLTCONF_MASK     (3u << FLEXCAN_ESR1_FLTCONF_SHIFT)
#define FLEXCAN_ESR1_FLTCONF_ACTIVE   (0u << FLEXCAN_ESR1_FLTCONF_SHIFT)
#define FLEXCAN_ESR1_FLTCONF_PASSIVE  (1u << FLEXCAN_ESR1_FLTCONF_SHIFT)
#define FLEXCAN_ESR1_FLTCONF_BUSOFF   (2u << FLEXCAN_ESR1_FLTCONF_SHIFT)
#define FLEXCAN_ESR1_STFERR           (1u << 10)
#define FLEXCAN_ESR1_FRMERR           (1u << 11)
#define FLEXCAN_ESR1_CRCERR           (1u << 12)
#define FLEXCAN_ESR1_ACKERR           (1u << 13)
#define FLEXCAN_ESR1_BIT0ERR          (1u << 14)
#define FLEXCAN_ESR1_BIT1ERR          (1u << 15)
#define FLEXCAN_ESR1_RWRNINT          (1u << 16)
#define FLEXCAN_ESR1_TWRNINT          (1u << 17)
#define FLEXCAN_ESR1_ERROR_MASK       (FLEXCAN_ESR1_ERRINT | \
                                       FLEXCAN_ESR1_BOFFINT | \
                                       FLEXCAN_ESR1_STFERR | \
                                       FLEXCAN_ESR1_FRMERR | \
                                       FLEXCAN_ESR1_CRCERR | \
                                       FLEXCAN_ESR1_ACKERR | \
                                       FLEXCAN_ESR1_BIT0ERR | \
                                       FLEXCAN_ESR1_BIT1ERR | \
                                       FLEXCAN_ESR1_RWRNINT | \
                                       FLEXCAN_ESR1_TWRNINT)

/* The CAN1 root is configured to the 24 MHz oscillator.
 *
 * The FlexCAN segment fields are encoded as segment minus one.  PROPSEG=4,
 * PSEG1=2 and PSEG2=2 represent 5, 3 and 3 time quanta; with the one
 * sync-segment bit this totals 12 TQ:
 *
 *   24 MHz / ((PREDIV 3 + 1) * 12) = 500 kbit/s
 */

#if CONFIG_OK8MP_M7_FLEXCAN1_BITRATE != 500000
#  error "The initial OK8MP FlexCAN1 driver currently supports 500000 bit/s"
#endif

#define FLEXCAN_TIMING                (FLEXCAN_CTRL1_PRESDIV(3) | \
                                       FLEXCAN_CTRL1_RJW(1) | \
                                       FLEXCAN_CTRL1_PSEG1(2) | \
                                       FLEXCAN_CTRL1_PSEG2(2) | \
                                       FLEXCAN_CTRL1_PROPSEG(4))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ok8mp_flexcan_s
{
  uint32_t base;
  int mbirq;
  bool hw_configured;
  bool irq_attached;
  bool rxint_enabled;
  bool txint_enabled;
  uint8_t last_fault_state;
  uint32_t last_error_bits;
  struct ok8mp_can_status_s stats;
  struct can_dev_s dev;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void ok8mp_flexcan_reset(FAR struct can_dev_s *dev);
static int ok8mp_flexcan_setup(FAR struct can_dev_s *dev);
static void ok8mp_flexcan_shutdown(FAR struct can_dev_s *dev);
static void ok8mp_flexcan_rxint(FAR struct can_dev_s *dev, bool enable);
static void ok8mp_flexcan_txint(FAR struct can_dev_s *dev, bool enable);
static int ok8mp_flexcan_ioctl(FAR struct can_dev_s *dev, int cmd,
                               unsigned long arg);
static int ok8mp_flexcan_remoterequest(FAR struct can_dev_s *dev,
                                        uint16_t id);
static int ok8mp_flexcan_send(FAR struct can_dev_s *dev,
                               FAR struct can_msg_s *msg);
static bool ok8mp_flexcan_txready(FAR struct can_dev_s *dev);
static bool ok8mp_flexcan_txempty(FAR struct can_dev_s *dev);
static bool ok8mp_flexcan_cancel(FAR struct can_dev_s *dev,
                                 FAR struct can_msg_s *msg);
static int ok8mp_flexcan_recover(FAR struct ok8mp_flexcan_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct can_ops_s g_ok8mp_flexcan_ops =
{
  .co_reset         = ok8mp_flexcan_reset,
  .co_setup         = ok8mp_flexcan_setup,
  .co_shutdown      = ok8mp_flexcan_shutdown,
  .co_rxint         = ok8mp_flexcan_rxint,
  .co_txint         = ok8mp_flexcan_txint,
  .co_ioctl         = ok8mp_flexcan_ioctl,
  .co_remoterequest = ok8mp_flexcan_remoterequest,
  .co_send          = ok8mp_flexcan_send,
  .co_txready       = ok8mp_flexcan_txready,
  .co_txempty       = ok8mp_flexcan_txempty,
  .co_cancel        = ok8mp_flexcan_cancel,
};

static struct ok8mp_flexcan_s g_ok8mp_flexcan1 =
{
  .base  = OK8MP_FLEXCAN1_BASE,
  .mbirq = MX8MP_IRQ_CAN_FD1_0,
  .dev   =
  {
    .cd_ops  = &g_ok8mp_flexcan_ops,
    .cd_priv = &g_ok8mp_flexcan1,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t ok8mp_flexcan_getreg(FAR struct ok8mp_flexcan_s *priv,
                                             uint32_t offset)
{
  return getreg32(priv->base + offset);
}

static inline void ok8mp_flexcan_putreg(FAR struct ok8mp_flexcan_s *priv,
                                        uint32_t offset, uint32_t value)
{
  putreg32(value, priv->base + offset);
}

static bool ok8mp_flexcan_waitmcr(FAR struct ok8mp_flexcan_s *priv,
                                  uint32_t mask, bool set)
{
  uint32_t elapsed;

  for (elapsed = 0; elapsed < FLEXCAN_WAIT_US; elapsed += 10)
    {
      bool value = (ok8mp_flexcan_getreg(priv, FLEXCAN_MCR_OFFSET) & mask) != 0;
      if (value == set)
        {
          return true;
        }

      up_udelay(10);
    }

  return false;
}

/* Registering /dev/can0 must not touch CAN hardware during board boot.
 * Configure clocks and pads only when the upper half opens the device.
 */

static int ok8mp_flexcan_configure_hw(FAR struct ok8mp_flexcan_s *priv)
{
  int ret;

  if (priv->hw_configured)
    {
      return OK;
    }

  /* mx8mp_ccm_configure_clock() stores divisor minus one.  Passing one
   * selects divide-by-one; passing zero would underflow. */

  ret = mx8mp_ccm_configure_clock(CAN1_CLK_ROOT, OSC_24M_REF_CLK, 1, 1);
  if (ret < 0)
    {
      return ret;
    }

  mx8mp_ccm_enable_clock(CAN1_CLK_ROOT);
  mx8mp_ccm_gate_clock(CCM_CAN1_CLK_GATE, CLK_ALWAYS_NEEDED);
  mx8mp_iomuxc_config(IOMUX_CAN1_TX);
  mx8mp_iomuxc_config(IOMUX_CAN1_RX);

  priv->hw_configured = true;
  return OK;
}

static inline uint32_t ok8mp_flexcan_mb_offset(uint32_t mbi)
{
  return FLEXCAN_MB_OFFSET(mbi);
}

static void ok8mp_flexcan_enter_freeze(FAR struct ok8mp_flexcan_s *priv)
{
  uint32_t mcr = ok8mp_flexcan_getreg(priv, FLEXCAN_MCR_OFFSET);
  mcr |= FLEXCAN_MCR_FRZ | FLEXCAN_MCR_HALT;
  ok8mp_flexcan_putreg(priv, FLEXCAN_MCR_OFFSET, mcr);
}

static void ok8mp_flexcan_leave_freeze(FAR struct ok8mp_flexcan_s *priv)
{
  uint32_t mcr = ok8mp_flexcan_getreg(priv, FLEXCAN_MCR_OFFSET);
  mcr &= ~FLEXCAN_MCR_HALT;
  ok8mp_flexcan_putreg(priv, FLEXCAN_MCR_OFFSET, mcr);
}

static int ok8mp_flexcan_hw_reset(FAR struct ok8mp_flexcan_s *priv)
{
  uint32_t mcr;
  uint32_t ctrl1;
  uint32_t mbi;

  /* Leave module-disable state before requesting a software reset. */

  mcr = ok8mp_flexcan_getreg(priv, FLEXCAN_MCR_OFFSET);
  mcr &= ~FLEXCAN_MCR_MDIS;
  ok8mp_flexcan_putreg(priv, FLEXCAN_MCR_OFFSET, mcr);
  if (!ok8mp_flexcan_waitmcr(priv, FLEXCAN_MCR_LPMACK, false))
    {
      canerr("ERROR: CAN1 did not leave low-power mode\n");
      return -ETIMEDOUT;
    }

  mcr = ok8mp_flexcan_getreg(priv, FLEXCAN_MCR_OFFSET);
  ok8mp_flexcan_putreg(priv, FLEXCAN_MCR_OFFSET,
                        mcr | FLEXCAN_MCR_SOFTRST);
  if (!ok8mp_flexcan_waitmcr(priv, FLEXCAN_MCR_SOFTRST, false))
    {
      canerr("ERROR: CAN1 software reset timed out\n");
      return -ETIMEDOUT;
    }

  ok8mp_flexcan_enter_freeze(priv);
  if (!ok8mp_flexcan_waitmcr(priv, FLEXCAN_MCR_FRZACK, true))
    {
      canerr("ERROR: CAN1 did not enter freeze mode\n");
      return -ETIMEDOUT;
    }

  mcr = ok8mp_flexcan_getreg(priv, FLEXCAN_MCR_OFFSET);
  /* Keep AEN clear.  ERR005829 is handled by a reserved inactive MB0,
   * not by aborting an active TX mailbox. */

  mcr &= ~(FLEXCAN_MCR_MAXMB_MASK | FLEXCAN_MCR_RFEN |
           FLEXCAN_MCR_AEN);
  /* The one-shot NSH can example sends 0x001 then waits for an external
   * frame.  Do not let it consume its own test frame. */

  mcr |= FLEXCAN_MCR_IRMQ | FLEXCAN_MCR_SRXDIS | FLEXCAN_MCR_WRNEN |
         FLEXCAN_MCR_MAXMB(FLEXCAN_LAST_MB);
  ok8mp_flexcan_putreg(priv, FLEXCAN_MCR_OFFSET, mcr);

  ctrl1 = ok8mp_flexcan_getreg(priv, FLEXCAN_CTRL1_OFFSET);
  ctrl1 &= ~FLEXCAN_CTRL1_TIMING_MASK;
  ctrl1 |= FLEXCAN_TIMING;
  ok8mp_flexcan_putreg(priv, FLEXCAN_CTRL1_OFFSET, ctrl1);

  /* Use individual masks of zero so every standard or extended identifier
   * is received.  Health-monitor filtering belongs in the application,
   * not in the board driver.
   */

  for (mbi = 0; mbi <= FLEXCAN_LAST_MB; mbi++)
    {
      uint32_t offset = ok8mp_flexcan_mb_offset(mbi);
      ok8mp_flexcan_putreg(priv, FLEXCAN_RXIMR_OFFSET(mbi), 0);
      ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_ID_OFFSET, 0);
      ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_WORD0_OFFSET, 0);
      ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_WORD1_OFFSET, 0);
      ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_CS_OFFSET,
                            (mbi >= FLEXCAN_FIRST_RX_MB &&
                             mbi < FLEXCAN_FIRST_RX_MB + FLEXCAN_RX_MB_COUNT) ?
                            FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_RX_EMPTY) :
                            FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_TX_INACTIVE));
    }

  ok8mp_flexcan_putreg(priv, FLEXCAN_RXMGMASK_OFFSET, 0);
  ok8mp_flexcan_putreg(priv, FLEXCAN_RX14MASK_OFFSET, 0);
  ok8mp_flexcan_putreg(priv, FLEXCAN_RX15MASK_OFFSET, 0);
  ok8mp_flexcan_putreg(priv, FLEXCAN_IFLAG1_OFFSET,
                        FLEXCAN_RX_MB_MASK | FLEXCAN_TX_MB_MASK);
  ok8mp_flexcan_putreg(priv, FLEXCAN_IMASK1_OFFSET, 0);

  ok8mp_flexcan_leave_freeze(priv);
  if (!ok8mp_flexcan_waitmcr(priv, FLEXCAN_MCR_FRZACK, false))
    {
      canerr("ERROR: CAN1 did not leave freeze mode\n");
      return -ETIMEDOUT;
    }

  return OK;
}

static void ok8mp_flexcan_snapshot(FAR struct ok8mp_flexcan_s *priv,
                                   FAR struct ok8mp_can_status_s *status)
{
  uint32_t ecr;
  uint32_t esr1;
  uint32_t error_bits;
  uint32_t txcs;
  uint8_t fault_state;

  ecr = ok8mp_flexcan_getreg(priv, FLEXCAN_ECR_OFFSET);
  esr1 = ok8mp_flexcan_getreg(priv, FLEXCAN_ESR1_OFFSET);
  txcs = ok8mp_flexcan_getreg(priv,
                              ok8mp_flexcan_mb_offset(FLEXCAN_TX_MB) +
                              FLEXCAN_MB_CS_OFFSET);

  switch (esr1 & FLEXCAN_ESR1_FLTCONF_MASK)
    {
      case FLEXCAN_ESR1_FLTCONF_BUSOFF:
        fault_state = OK8MP_CAN_BUS_OFF;
        break;

      case FLEXCAN_ESR1_FLTCONF_PASSIVE:
        fault_state = OK8MP_CAN_ERROR_PASSIVE;
        break;

      case FLEXCAN_ESR1_FLTCONF_ACTIVE:
      default:
        fault_state = OK8MP_CAN_ERROR_ACTIVE;
        break;
    }

  error_bits = esr1 & FLEXCAN_ESR1_ERROR_MASK;
  if (error_bits != 0 && error_bits != priv->last_error_bits)
    {
      priv->stats.error_events++;
    }

  if ((error_bits & FLEXCAN_ESR1_ACKERR) != 0 &&
      (priv->last_error_bits & FLEXCAN_ESR1_ACKERR) == 0)
    {
      priv->stats.ack_errors++;
    }

  if (fault_state == OK8MP_CAN_BUS_OFF &&
      priv->last_fault_state != OK8MP_CAN_BUS_OFF)
    {
      priv->stats.bus_off_count++;
    }

  priv->last_error_bits = error_bits;
  priv->last_fault_state = fault_state;
  priv->stats.esr1 = esr1;
  priv->stats.ecr = ecr;
  priv->stats.iflag1 = ok8mp_flexcan_getreg(priv, FLEXCAN_IFLAG1_OFFSET);
  priv->stats.tx_mb_cs = txcs;
  priv->stats.tx_error_counter = ecr & FLEXCAN_ECR_TXERRCNT_MASK;
  priv->stats.rx_error_counter =
    (ecr & FLEXCAN_ECR_RXERRCNT_MASK) >> FLEXCAN_ECR_RXERRCNT_SHIFT;
  priv->stats.fault_state = fault_state;
  priv->stats.tx_pending =
    ((txcs & FLEXCAN_MB_CS_CODE_MASK) >> FLEXCAN_MB_CS_CODE_SHIFT) ==
    FLEXCAN_MB_CS_CODE_TX_DATA;

  memcpy(status, &priv->stats, sizeof(*status));
}

/* Abort a possibly wedged transmit mailbox and fully reinitialize the
 * controller.  The caller must flush the generic upper-half TX queue before
 * invoking this operation; can_service does so on every recovery path.
 */

static int ok8mp_flexcan_recover(FAR struct ok8mp_flexcan_s *priv)
{
  uint32_t imask = 0;
  uint32_t offset;
  int ret;

  ok8mp_flexcan_putreg(priv, FLEXCAN_IMASK1_OFFSET, 0);
  offset = ok8mp_flexcan_mb_offset(FLEXCAN_TX_MB);
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_CS_OFFSET,
                        FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_TX_INACTIVE));
  ok8mp_flexcan_putreg(priv, FLEXCAN_IFLAG1_OFFSET,
                        FLEXCAN_TX_MB_MASK);

  ret = ok8mp_flexcan_hw_reset(priv);
  if (ret < 0)
    {
      return ret;
    }

  /* Recovery is called only through an already-open /dev/can0 file.
   * Re-enable every RX mailbox unconditionally rather than relying on the
   * cached upper-half interrupt state.  In particular, a reset while the
   * service owns the fd must not leave the controller able to ACK a reply
   * but unable to route it into the NuttX receive queue.
   */

  imask |= FLEXCAN_RX_MB_MASK;
  priv->rxint_enabled = true;

  if (priv->txint_enabled)
    {
      imask |= FLEXCAN_TX_MB_MASK;
    }

  ok8mp_flexcan_putreg(priv, FLEXCAN_IMASK1_OFFSET, imask);
  priv->stats.recoveries++;
  priv->last_error_bits = 0;
  priv->last_fault_state = OK8MP_CAN_ERROR_ACTIVE;
  return OK;
}

static void ok8mp_flexcan_receive(FAR struct ok8mp_flexcan_s *priv,
                                   uint32_t mbi)
{
  struct can_hdr_s hdr;
  uint8_t data[8];
  uint32_t offset = ok8mp_flexcan_mb_offset(mbi);
  uint32_t cs;
  uint32_t id;
  uint32_t word0;
  uint32_t word1;
  uint32_t code;

  cs = ok8mp_flexcan_getreg(priv, offset + FLEXCAN_MB_CS_OFFSET);
  code = (cs & FLEXCAN_MB_CS_CODE_MASK) >> FLEXCAN_MB_CS_CODE_SHIFT;
  if (code != FLEXCAN_MB_CS_CODE_RX_FULL &&
      code != FLEXCAN_MB_CS_CODE_RX_OVERRUN)
    {
      ok8mp_flexcan_putreg(priv, FLEXCAN_IFLAG1_OFFSET, 1u << mbi);
      return;
    }

  if (code == FLEXCAN_MB_CS_CODE_RX_OVERRUN)
    {
      priv->stats.rx_overruns++;
    }

  id = ok8mp_flexcan_getreg(priv, offset + FLEXCAN_MB_ID_OFFSET);
  word0 = ok8mp_flexcan_getreg(priv, offset + FLEXCAN_MB_WORD0_OFFSET);
  word1 = ok8mp_flexcan_getreg(priv, offset + FLEXCAN_MB_WORD1_OFFSET);

  memset(&hdr, 0, sizeof(hdr));
  hdr.ch_dlc = (cs >> FLEXCAN_MB_CS_DLC_SHIFT) & 0x0f;
  if (hdr.ch_dlc > 8)
    {
      hdr.ch_dlc = 8;
    }

  hdr.ch_rtr = (cs & FLEXCAN_MB_CS_RTR) != 0;
#ifdef CONFIG_CAN_EXTID
  hdr.ch_extid = (cs & FLEXCAN_MB_CS_IDE) != 0;
  hdr.ch_id = hdr.ch_extid ? (id & FLEXCAN_MB_ID_EXT_MASK) :
                             ((id >> FLEXCAN_MB_ID_STD_SHIFT) &
                              FLEXCAN_MB_ID_STD_MASK);
#else
  hdr.ch_id = (id >> FLEXCAN_MB_ID_STD_SHIFT) & FLEXCAN_MB_ID_STD_MASK;
#endif

  data[0] = word0 >> 24;
  data[1] = word0 >> 16;
  data[2] = word0 >> 8;
  data[3] = word0;
  data[4] = word1 >> 24;
  data[5] = word1 >> 16;
  data[6] = word1 >> 8;
  data[7] = word1;

  /* Reading CS followed by TIMER unlocks the FlexCAN message buffer.  A
   * TIMER read is sufficient here; its value is intentionally unused. */

  (void)ok8mp_flexcan_getreg(priv, 0x0008);
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_CS_OFFSET,
                        FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_RX_EMPTY));
  ok8mp_flexcan_putreg(priv, FLEXCAN_IFLAG1_OFFSET, 1u << mbi);

  priv->stats.rx_frames++;
  can_receive(&priv->dev, &hdr, data);
}

static int ok8mp_flexcan_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct ok8mp_flexcan_s *priv = arg;
  uint32_t flags;
  uint32_t mbi;

  UNUSED(irq);
  UNUSED(context);

  flags = ok8mp_flexcan_getreg(priv, FLEXCAN_IFLAG1_OFFSET);
  for (mbi = FLEXCAN_FIRST_RX_MB;
       mbi < FLEXCAN_FIRST_RX_MB + FLEXCAN_RX_MB_COUNT;
       mbi++)
    {
      if ((flags & (1u << mbi)) != 0)
        {
          ok8mp_flexcan_receive(priv, mbi);
        }
    }

  if ((flags & FLEXCAN_TX_MB_MASK) != 0)
    {
      ok8mp_flexcan_putreg(priv, FLEXCAN_IFLAG1_OFFSET,
                            FLEXCAN_TX_MB_MASK);
      priv->stats.tx_frames++;
      can_txdone(&priv->dev);
    }

  return OK;
}

/****************************************************************************
 * CAN lower-half operations
 ****************************************************************************/

static void ok8mp_flexcan_reset(FAR struct can_dev_s *dev)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;

  ok8mp_flexcan_putreg(priv, FLEXCAN_IMASK1_OFFSET, 0);
  (void)ok8mp_flexcan_hw_reset(priv);
}

static int ok8mp_flexcan_setup(FAR struct can_dev_s *dev)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;
  int ret;

  ret = ok8mp_flexcan_configure_hw(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = ok8mp_flexcan_hw_reset(priv);
  if (ret < 0)
    {
      return ret;
    }

  priv->last_error_bits = 0;
  priv->last_fault_state = OK8MP_CAN_ERROR_ACTIVE;

  if (!priv->irq_attached)
    {
      ret = irq_attach(priv->mbirq, ok8mp_flexcan_interrupt, priv);
      if (ret < 0)
        {
          canerr("ERROR: failed to attach CAN1 IRQ %d: %d\n", priv->mbirq,
                 ret);
          return ret;
        }

      priv->irq_attached = true;
    }

  up_enable_irq(priv->mbirq);
  return OK;
}

static void ok8mp_flexcan_shutdown(FAR struct can_dev_s *dev)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;
  uint32_t mcr;

  ok8mp_flexcan_putreg(priv, FLEXCAN_IMASK1_OFFSET, 0);
  up_disable_irq(priv->mbirq);

  if (priv->irq_attached)
    {
      irq_detach(priv->mbirq);
      priv->irq_attached = false;
    }

  priv->rxint_enabled = false;
  priv->txint_enabled = false;

  mcr = ok8mp_flexcan_getreg(priv, FLEXCAN_MCR_OFFSET);
  ok8mp_flexcan_putreg(priv, FLEXCAN_MCR_OFFSET, mcr | FLEXCAN_MCR_MDIS);
}

static void ok8mp_flexcan_rxint(FAR struct can_dev_s *dev, bool enable)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;
  uint32_t imask = ok8mp_flexcan_getreg(priv, FLEXCAN_IMASK1_OFFSET);

  if (enable)
    {
      imask |= FLEXCAN_RX_MB_MASK;
    }
  else
    {
      imask &= ~FLEXCAN_RX_MB_MASK;
    }

  priv->rxint_enabled = enable;
  ok8mp_flexcan_putreg(priv, FLEXCAN_IMASK1_OFFSET, imask);
}

static void ok8mp_flexcan_txint(FAR struct can_dev_s *dev, bool enable)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;
  uint32_t imask = ok8mp_flexcan_getreg(priv, FLEXCAN_IMASK1_OFFSET);

  if (enable)
    {
      imask |= FLEXCAN_TX_MB_MASK;
    }
  else
    {
      imask &= ~FLEXCAN_TX_MB_MASK;
    }

  priv->txint_enabled = enable;
  ok8mp_flexcan_putreg(priv, FLEXCAN_IMASK1_OFFSET, imask);
}

static int ok8mp_flexcan_ioctl(FAR struct can_dev_s *dev, int cmd,
                                unsigned long arg)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;

  switch (cmd)
    {
      case CANIOC_GET_BITTIMING:
        {
          FAR struct canioc_bittiming_s *bt =
            (FAR struct canioc_bittiming_s *)((uintptr_t)arg);

          if (bt == NULL)
            {
              return -EINVAL;
            }

          bt->bt_baud = CONFIG_OK8MP_M7_FLEXCAN1_BITRATE;
          bt->bt_tseg1 = 8;
          bt->bt_tseg2 = 3;
          bt->bt_sjw = 2;
          return OK;
        }

      case CANIOC_GET_STATE:
        {
          FAR int *state = (FAR int *)((uintptr_t)arg);

          if (state == NULL)
            {
              return -EINVAL;
            }

          *state = CAN_STATE_START;
          return OK;
        }

      case CANIOC_IFLUSH:
      case CANIOC_OFLUSH:
      case CANIOC_IOFLUSH:
        return OK;

      case CANIOC_BUSOFF_RECOVERY:
      case OK8MP_CANIOC_RECOVER:
        return ok8mp_flexcan_recover(priv);

      case OK8MP_CANIOC_GET_STATUS:
        {
          FAR struct ok8mp_can_status_s *status =
            (FAR struct ok8mp_can_status_s *)((uintptr_t)arg);

          if (status == NULL)
            {
              return -EINVAL;
            }

          ok8mp_flexcan_snapshot(priv, status);
          return OK;
        }

      default:
        return -ENOTTY;
    }
}

static int ok8mp_flexcan_remoterequest(FAR struct can_dev_s *dev,
                                        uint16_t id)
{
  UNUSED(dev);
  UNUSED(id);
  return -ENOTTY;
}

static int ok8mp_flexcan_send(FAR struct can_dev_s *dev,
                               FAR struct can_msg_s *msg)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;
  uint32_t offset = ok8mp_flexcan_mb_offset(FLEXCAN_TX_MB);
  uint32_t cs;
  uint32_t id;
  uint32_t word0;
  uint32_t word1;

  if (msg == NULL || msg->cm_hdr.ch_dlc > 8)
    {
      return -EINVAL;
    }

  cs = ok8mp_flexcan_getreg(priv, offset + FLEXCAN_MB_CS_OFFSET);
  if (((cs & FLEXCAN_MB_CS_CODE_MASK) >> FLEXCAN_MB_CS_CODE_SHIFT) ==
      FLEXCAN_MB_CS_CODE_TX_DATA)
    {
      priv->stats.tx_busy++;
      return -EBUSY;
    }

#ifdef CONFIG_CAN_EXTID
  if (msg->cm_hdr.ch_extid)
    {
      id = msg->cm_hdr.ch_id & FLEXCAN_MB_ID_EXT_MASK;
      cs = FLEXCAN_MB_CS_IDE;
    }
  else
#endif
    {
      id = (msg->cm_hdr.ch_id & FLEXCAN_MB_ID_STD_MASK) <<
           FLEXCAN_MB_ID_STD_SHIFT;
      cs = 0;
    }

  if (msg->cm_hdr.ch_rtr)
    {
      cs |= FLEXCAN_MB_CS_RTR;
    }

  word0 = ((uint32_t)msg->cm_data[0] << 24) |
          ((uint32_t)msg->cm_data[1] << 16) |
          ((uint32_t)msg->cm_data[2] << 8) |
          msg->cm_data[3];
  word1 = ((uint32_t)msg->cm_data[4] << 24) |
          ((uint32_t)msg->cm_data[5] << 16) |
          ((uint32_t)msg->cm_data[6] << 8) |
          msg->cm_data[7];

  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_CS_OFFSET,
                        FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_TX_INACTIVE));
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_ID_OFFSET, id);
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_WORD0_OFFSET, word0);
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_WORD1_OFFSET, word1);
  ok8mp_flexcan_putreg(priv, FLEXCAN_IFLAG1_OFFSET, FLEXCAN_TX_MB_MASK);
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_CS_OFFSET,
                        cs | FLEXCAN_MB_CS_DLC(msg->cm_hdr.ch_dlc) |
                        FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_TX_DATA));

  /* ERR005829: leave MB0 reserved and write it inactive twice after each
   * transmission.  MB0 is never used for application RX or TX. */

  offset = ok8mp_flexcan_mb_offset(FLEXCAN_RESERVED_MB);
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_CS_OFFSET,
                        FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_TX_INACTIVE));
  ok8mp_flexcan_putreg(priv, offset + FLEXCAN_MB_CS_OFFSET,
                        FLEXCAN_MB_CS_CODE(FLEXCAN_MB_CS_CODE_TX_INACTIVE));

  /* Keep the short post-submit settling interval used by the previously
   * verified bench image.  The ESP32 ECU can respond immediately after it
   * acknowledges a request; allowing the FlexCAN mailbox/IRQ state to
   * settle here prevents that reply from racing the next upper-half action.
   * This is deliberately not a completion wait: an unacknowledged frame
   * must remain asynchronous so the normal error/recovery path can handle
   * it.
   */

  up_udelay(1000);

  priv->stats.tx_requests++;
  return OK;
}

static bool ok8mp_flexcan_txready(FAR struct can_dev_s *dev)
{
  FAR struct ok8mp_flexcan_s *priv = dev->cd_priv;
  uint32_t cs = ok8mp_flexcan_getreg(priv,
                                      ok8mp_flexcan_mb_offset(FLEXCAN_TX_MB) +
                                      FLEXCAN_MB_CS_OFFSET);
  return ((cs & FLEXCAN_MB_CS_CODE_MASK) >> FLEXCAN_MB_CS_CODE_SHIFT) !=
         FLEXCAN_MB_CS_CODE_TX_DATA;
}

static bool ok8mp_flexcan_txempty(FAR struct can_dev_s *dev)
{
  return ok8mp_flexcan_txready(dev);
}

static bool ok8mp_flexcan_cancel(FAR struct can_dev_s *dev,
                                 FAR struct can_msg_s *msg)
{
  UNUSED(dev);
  UNUSED(msg);
  return false;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ok8mp_flexcan_initialize(void)
{
  int ret = can_register("/dev/can0", &g_ok8mp_flexcan1.dev);
  if (ret < 0)
    {
      canerr("ERROR: failed to register /dev/can0: %d\n", ret);
    }

  return ret;
}

#endif /* CONFIG_OK8MP_M7_FLEXCAN1 */
