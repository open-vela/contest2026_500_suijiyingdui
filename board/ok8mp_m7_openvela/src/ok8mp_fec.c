/****************************************************************************
 * boards/arm/mx8mp/ok8mp-m7/src/ok8mp_fec.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OK8MP ENET1/FEC PHY diagnostics and polling NuttX Ethernet driver.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/irq.h>

#ifdef CONFIG_OK8MP_M7_FEC_NET
#  include <arpa/inet.h>
#  include <net/ethernet.h>
#  include <nuttx/net/net.h>
#  include <nuttx/net/netdev.h>
#endif

#include "arm_internal.h"
#include "mx8mp_ccm.h"
#include "mx8mp_gpio.h"
#include "mx8mp_iomuxc.h"
#include "hardware/mx8mp_ccm.h"
#include "hardware/mx8mp_memorymap.h"
#include "hardware/mx8mp_rdc.h"

#include <arch/board/board.h>

#include "ok8mp-m7.h"

#define FEC_EIR_OFFSET            0x004
#define FEC_EIMR_OFFSET           0x008
#define FEC_RDAR_OFFSET           0x010
#define FEC_TDAR_OFFSET           0x014
#define FEC_ECR_OFFSET            0x024
#define FEC_MMFR_OFFSET           0x040
#define FEC_MSCR_OFFSET           0x044
#define FEC_RCR_OFFSET            0x084
#define FEC_TCR_OFFSET            0x0c4
#define FEC_PALR_OFFSET           0x0e4
#define FEC_PAUR_OFFSET           0x0e8
#define FEC_TFWR_OFFSET           0x144
#define FEC_RDSR_OFFSET           0x180
#define FEC_TDSR_OFFSET           0x184
#define FEC_MRBR_OFFSET           0x188

#define FEC_EIR_MII               (1u << 23)
#define FEC_EIR_EBERR             (1u << 22)
#define FEC_EIR_RXF               (1u << 25)
#define FEC_EIR_TXF               (1u << 27)
#define FEC_RDAR_ACTIVE            (1u << 24)
#define FEC_TDAR_ACTIVE            (1u << 24)
#define FEC_ECR_RESET              (1u << 0)
#define FEC_ECR_ETHEREN            (1u << 1)
#define FEC_ECR_SPEED              (1u << 5)
#define FEC_ECR_DBSWP              (1u << 8)
#define FEC_RCR_MII_MODE           (1u << 2)
#define FEC_RCR_RGMII_EN           (1u << 6)
#define FEC_RCR_MAX_FL_SHIFT       16
#define FEC_TCR_FDEN               (1u << 2)
#define FEC_TFWR_STRFWD            (1u << 8)
#define FEC_MMFR_ST               (1u << 30)
#define FEC_MMFR_OP_WRITE         (1u << 28)
#define FEC_MMFR_OP_READ          (2u << 28)
#define FEC_MMFR_PA_SHIFT         23
#define FEC_MMFR_RA_SHIFT         18
#define FEC_MMFR_TA               (2u << 16)
#define FEC_MSCR_SPEED_SHIFT      1
#define FEC_MSCR_HOLDTIME_SHIFT   8

#define FEC_PHY_ADDR              1
#define FEC_PHY_BMCR              0
#define FEC_PHY_BMSR              1
#define FEC_PHY_ID1               2
#define FEC_PHY_ID2               3
#define FEC_PHY_ANAR              4
#define FEC_PHY_GBCR              9
#define FEC_PHY_YT8521_STATUS     0x11
#define FEC_PHY_BMCR_AUTONEG      (1u << 12)
#define FEC_PHY_BMCR_RESTART_AN   (1u << 9)
#define FEC_PHY_BMCR_ISOLATE      (1u << 10)
#define FEC_PHY_ANAR_ALL_10_100   0x01e1
#define FEC_PHY_GBCR_1000_FULL    0x0200
#define FEC_MDIO_TIMEOUT_US       100000
#define FEC_PHY_POWERUP_DELAY_US  100000
#define FEC_PHY_AUTONEG_DELAY_US  3000000
#define FEC_RESET_TIMEOUT_US       100000

#define FEC_RX_DESC_COUNT          4
#define FEC_TX_DESC_COUNT          2
#define FEC_BUFFER_SIZE             1536

#define FEC_BD_EMPTY                (1u << 15)
#define FEC_BD_READY                (1u << 15)
#define FEC_BD_WRAP                 (1u << 13)
#define FEC_BD_LAST                 (1u << 11)
#define FEC_BD_TXCRC                (1u << 10)

#define FEC_TX_TIMEOUT_US            100000
#define FEC_ETH_MIN_FRAME_SIZE       60
#define FEC_TEST_FRAME_SIZE          FEC_ETH_MIN_FRAME_SIZE
#define FEC_TEST_ETHERTYPE           0x88b5
#define FEC_ARP_FRAME_SIZE            60
#define FEC_ARP_HEADER_SIZE           42
#define FEC_ICMP_FRAME_SIZE            60
#define FEC_IPV4_HEADER_SIZE           20
#define FEC_ICMP_HEADER_SIZE           8
#define FEC_ICMP_PAYLOAD_SIZE          8

/* RDC setup for the FEC DMA masters and the system-visible OCRAM alias.
 * The M7 sees OCRAM at 0x20200000 while bus masters, including ENET1,
 * access the same RAM through 0x00900000.
 */

#define RDC_MDA_OFFSET                0x200
#define RDC_PDAP_OFFSET               0x400
#define RDC_MR_OFFSET                 0x800
#define RDC_MDA_M7                    1
#define RDC_MDA_ENET1_TX              22
#define RDC_MDA_ENET1_RX              23
#define RDC_PDAP_RDC                  29
#define RDC_PDAP_ENET1                94
#define RDC_MR_OCRAM0                 12
#define RDC_MDA_DID_MASK               0x3u
#define RDC_MDA_LOCK                   (1u << 31)
#define RDC_MRC_ENABLE                 (1u << 30)
#define RDC_MRC_LOCK                   (1u << 31)
#define RDC_MRC_ALL_DOMAINS_RW         0xffu

struct fec_bd_s
{
  uint16_t length;
  uint16_t control;
  uint32_t buffer;
};

static struct fec_bd_s g_fec_rx_desc[FEC_RX_DESC_COUNT]
  __attribute__((section(".fec_dma"), aligned(64)));
static struct fec_bd_s g_fec_tx_desc[FEC_TX_DESC_COUNT]
  __attribute__((section(".fec_dma"), aligned(64)));
static uint8_t g_fec_rx_buffer[FEC_RX_DESC_COUNT][FEC_BUFFER_SIZE]
  __attribute__((section(".fec_dma"), aligned(64)));
static uint8_t g_fec_tx_buffer[FEC_TX_DESC_COUNT][FEC_BUFFER_SIZE]
  __attribute__((section(".fec_dma"), aligned(64)));

static volatile uint32_t g_fec_dma_events;
static volatile uint32_t g_fec_irq_count;
static volatile uint32_t g_fec_last_irq;
static volatile uint32_t g_fec_error_axi_count;
static bool g_fec_irq_attached;

#ifdef CONFIG_OK8MP_M7_FEC_NET
#  define FEC_NET_POLL_USEC          10000
#  define FEC_NET_PRIORITY            100
#  define FEC_NET_STACKSIZE           3072

struct ok8mp_fec_net_s
{
  struct net_driver_s dev;
  volatile uint32_t poll_cycles;
  volatile uint32_t txpoll_calls;
  volatile uint32_t tx_attempts;
  volatile uint32_t tx_arp;
  volatile uint32_t tx_ipv4;
  volatile uint32_t tx_busy;
  volatile uint32_t tx_errors;
  bool ifup;
  bool registered;
  uint8_t rxhead;
  uint8_t txhead;
};

static struct ok8mp_fec_net_s g_fec_net;
#endif

/* The frame builder copies this address with word loads.  Keep the source
 * explicitly word-aligned because unaligned word loads trap on this M7.
 */

static const uint8_t g_fec_mac[6] __attribute__((aligned(4))) =
{
  0xf2, 0x10, 0x26, 0xab, 0x52, 0x2d
};

static uint32_t ok8mp_fec_dma_busaddr(uintptr_t local)
{
  DEBUGASSERT(local >= MX8M_M7_OCRAM);
  DEBUGASSERT(local < MX8M_M7_OCRAM + 32 * 1024);
  return (uint32_t)(local - (MX8M_M7_OCRAM - MX8M_OCRAM));
}

static uintptr_t ok8mp_rdc_mda_reg(unsigned int master)
{
  return MX8M_RDC + RDC_MDA_OFFSET + master * sizeof(uint32_t);
}

static uintptr_t ok8mp_rdc_pdap_reg(unsigned int periph)
{
  return MX8M_RDC + RDC_PDAP_OFFSET + periph * sizeof(uint32_t);
}

static uintptr_t ok8mp_rdc_mr_reg(unsigned int region, unsigned int reg)
{
  return MX8M_RDC + RDC_MR_OFFSET + region * 16 + reg;
}

static void ok8mp_fec_rdc_snapshot(struct ok8mp_fec_dma_s *state)
{
  state->rdc_domain = getreg32(RDC_STAT) & RDC_MDA_DID_MASK;
  state->rdc_m7_mda = getreg32(ok8mp_rdc_mda_reg(RDC_MDA_M7));
  state->rdc_enet_tx_mda = getreg32(ok8mp_rdc_mda_reg(RDC_MDA_ENET1_TX));
  state->rdc_enet_rx_mda = getreg32(ok8mp_rdc_mda_reg(RDC_MDA_ENET1_RX));
  state->rdc_enet_pdap = getreg32(ok8mp_rdc_pdap_reg(RDC_PDAP_ENET1));
  state->rdc_ocram_mrc = getreg32(ok8mp_rdc_mr_reg(RDC_MR_OCRAM0, 8));
}

static int ok8mp_fec_rdc_prepare(struct ok8mp_fec_dma_s *state)
{
  uint32_t domain;
  uint32_t policy;
  uint32_t value;

  ok8mp_fec_rdc_snapshot(state);
  domain = state->rdc_domain;

  /* Do not touch a locked or inaccessible RDC configuration.  Returning an
   * error here prevents the FEC DMA master from causing an asynchronous bus
   * fault after RDAR is set.
   */

  policy = (getreg32(ok8mp_rdc_pdap_reg(RDC_PDAP_RDC)) >> (domain * 2)) & 0x3u;
  if ((policy & 0x1u) == 0)
    {
      return -EACCES;
    }

  value = state->rdc_enet_tx_mda;
  if ((value & RDC_MDA_LOCK) != 0 &&
      (value & RDC_MDA_DID_MASK) != domain)
    {
      return -EACCES;
    }

  if ((value & RDC_MDA_LOCK) == 0)
    {
      putreg32((value & ~RDC_MDA_DID_MASK) | domain,
               ok8mp_rdc_mda_reg(RDC_MDA_ENET1_TX));
    }

  value = state->rdc_enet_rx_mda;
  if ((value & RDC_MDA_LOCK) != 0 &&
      (value & RDC_MDA_DID_MASK) != domain)
    {
      return -EACCES;
    }

  if ((value & RDC_MDA_LOCK) == 0)
    {
      putreg32((value & ~RDC_MDA_DID_MASK) | domain,
               ok8mp_rdc_mda_reg(RDC_MDA_ENET1_RX));
    }

  value = state->rdc_ocram_mrc;
  if ((value & RDC_MRC_LOCK) != 0)
    {
      return -EACCES;
    }

  /* Match the SDK RDC example: MRC2_0 protects the 128 KiB OCRAM range.
   * Its address fields use address bits [31:1] on i.MX8MP.
   */

  putreg32(MX8M_OCRAM >> 1, ok8mp_rdc_mr_reg(RDC_MR_OCRAM0, 0));
  putreg32((MX8M_OCRAM + MX8M_OCRAM_SIZE) >> 1,
           ok8mp_rdc_mr_reg(RDC_MR_OCRAM0, 4));
  putreg32(RDC_MRC_ENABLE | RDC_MRC_ALL_DOMAINS_RW,
           ok8mp_rdc_mr_reg(RDC_MR_OCRAM0, 8));
  __sync_synchronize();

  ok8mp_fec_rdc_snapshot(state);
  if ((state->rdc_enet_tx_mda & RDC_MDA_DID_MASK) != domain ||
      (state->rdc_enet_rx_mda & RDC_MDA_DID_MASK) != domain ||
      (state->rdc_ocram_mrc & RDC_MRC_ENABLE) == 0)
    {
      return -EACCES;
    }

  return OK;
}

static int ok8mp_fec_interrupt(int irq, void *context, void *arg)
{
  uint32_t events = getreg32(MX8M_ENET1 + FEC_EIR_OFFSET);

  putreg32(events, MX8M_ENET1 + FEC_EIR_OFFSET);
  g_fec_dma_events |= events;
  g_fec_last_irq = (uint32_t)irq;
  g_fec_irq_count++;
  return OK;
}

static int ok8mp_fec_error_axi_interrupt(int irq, void *context, void *arg)
{
  /* A bus error from a DMA master is routed through ERROR_AXI.  Quiesce the
   * MAC before returning so that an error can be reported by fec_probe
   * instead of entering NuttX's default unexpected-IRQ assertion path.
   */

  putreg32(0, MX8M_ENET1 + FEC_EIMR_OFFSET);
  putreg32(0, MX8M_ENET1 + FEC_ECR_OFFSET);
  g_fec_last_irq = (uint32_t)irq;
  g_fec_error_axi_count++;
  up_disable_irq(irq);
  return OK;
}

static int ok8mp_fec_attach_interrupts(void)
{
  int ret;

  if (g_fec_irq_attached)
    {
      return OK;
    }

  ret = irq_attach(MX8MP_IRQ_ENET1_0, ok8mp_fec_interrupt, NULL);
  if (ret >= 0)
    {
      ret = irq_attach(MX8MP_IRQ_ENET1_1, ok8mp_fec_interrupt, NULL);
    }

  if (ret >= 0)
    {
      ret = irq_attach(MX8MP_IRQ_ENET1_2, ok8mp_fec_interrupt, NULL);
    }

  if (ret >= 0)
    {
      ret = irq_attach(MX8MP_IRQ_ENET1_3, ok8mp_fec_interrupt, NULL);
    }

  if (ret >= 0)
    {
      ret = irq_attach(MX8MP_IRQ_ERROR_AXI,
                       ok8mp_fec_error_axi_interrupt, NULL);
    }

  if (ret < 0)
    {
      return ret;
    }

  up_enable_irq(MX8MP_IRQ_ENET1_0);
  up_enable_irq(MX8MP_IRQ_ENET1_1);
  up_enable_irq(MX8MP_IRQ_ENET1_2);
  up_enable_irq(MX8MP_IRQ_ENET1_3);
  up_enable_irq(MX8MP_IRQ_ERROR_AXI);
  g_fec_irq_attached = true;
  return OK;
}

static void ok8mp_fec_dma_snapshot(struct ok8mp_fec_dma_s *state)
{
  state->rx_ring = ok8mp_fec_dma_busaddr((uintptr_t)g_fec_rx_desc);
  state->tx_ring = ok8mp_fec_dma_busaddr((uintptr_t)g_fec_tx_desc);
  state->rx_buffer = ok8mp_fec_dma_busaddr((uintptr_t)g_fec_rx_buffer);
  state->tx_buffer = ok8mp_fec_dma_busaddr((uintptr_t)g_fec_tx_buffer);
  state->eimr = getreg32(MX8M_ENET1 + FEC_EIMR_OFFSET);
  state->eir = getreg32(MX8M_ENET1 + FEC_EIR_OFFSET);
  state->events = g_fec_dma_events;
  state->irq_count = g_fec_irq_count;
  state->last_irq = g_fec_last_irq;
  state->error_axi_count = g_fec_error_axi_count;
}

static void ok8mp_fec_build_test_frame(uint8_t *frame)
{
  static const char payload[] = "OK8MP-M7-FEC-DMA";
  volatile const uint8_t *mac = g_fec_mac;
  volatile const char *data = payload;
  unsigned int index;

  for (index = 0; index < 6; index++)
    {
      frame[index] = 0xff;
      /* Keep this as a byte load.  The linker may place the small constant
       * array at an odd address, while the M7 traps unaligned word loads.
       */

      frame[6 + index] = mac[index];
    }

  frame[12] = FEC_TEST_ETHERTYPE >> 8;
  frame[13] = FEC_TEST_ETHERTYPE & 0xff;

  for (index = 0; index < sizeof(payload) - 1; index++)
    {
      /* Ethernet data starts at byte 14, which is not word aligned. */

      frame[14 + index] = data[index];
    }

  for (index = 14 + sizeof(payload) - 1;
       index < FEC_TEST_FRAME_SIZE;
       index++)
    {
      frame[index] = 0;
    }
}

static void ok8mp_fec_build_arp_request(uint8_t *frame,
                                        const uint8_t source_ip[4],
                                        const uint8_t target_ip[4])
{
  volatile const uint8_t *mac = g_fec_mac;
  unsigned int index;

  for (index = 0; index < FEC_ARP_FRAME_SIZE; index++)
    {
      frame[index] = 0;
    }

  for (index = 0; index < 6; index++)
    {
      frame[index] = 0xff;
      frame[6 + index] = mac[index];
      frame[22 + index] = mac[index];
    }

  frame[12] = 0x08;
  frame[13] = 0x06;
  frame[14] = 0x00;
  frame[15] = 0x01;
  frame[16] = 0x08;
  frame[17] = 0x00;
  frame[18] = 6;
  frame[19] = 4;
  frame[20] = 0x00;
  frame[21] = 0x01;

  for (index = 0; index < 4; index++)
    {
      frame[28 + index] = source_ip[index];
      frame[38 + index] = target_ip[index];
    }
}

static uint16_t ok8mp_fec_checksum(const volatile uint8_t *data,
                                   uint16_t length)
{
  uint32_t sum = 0;
  uint16_t index;

  for (index = 0; index + 1 < length; index += 2)
    {
      sum += ((uint16_t)data[index] << 8) | data[index + 1];
    }

  if (index < length)
    {
      sum += (uint16_t)data[index] << 8;
    }

  while ((sum >> 16) != 0)
    {
      sum = (sum & 0xffff) + (sum >> 16);
    }

  return (uint16_t)~sum;
}

static void ok8mp_fec_build_icmp_echo(uint8_t *frame,
                                       const uint8_t target_mac[6],
                                       const uint8_t source_ip[4],
                                       const uint8_t target_ip[4])
{
  static const char payload[] = "OK8MPM7!";
  volatile uint8_t *packet = frame;
  volatile const uint8_t *mac = g_fec_mac;
  volatile const char *data = payload;
  uint16_t checksum;
  unsigned int index;

  for (index = 0; index < FEC_ICMP_FRAME_SIZE; index++)
    {
      packet[index] = 0;
    }

  for (index = 0; index < 6; index++)
    {
      packet[index] = target_mac[index];
      packet[6 + index] = mac[index];
    }

  packet[12] = 0x08;
  packet[13] = 0x00;
  packet[14] = 0x45;
  packet[15] = 0x00;
  packet[16] = 0x00;
  packet[17] = FEC_IPV4_HEADER_SIZE + FEC_ICMP_HEADER_SIZE +
               FEC_ICMP_PAYLOAD_SIZE;
  packet[18] = 0x4d;
  packet[19] = 0x37;
  packet[20] = 0x40;
  packet[21] = 0x00;
  packet[22] = 64;
  packet[23] = 1;
  packet[24] = 0;
  packet[25] = 0;

  for (index = 0; index < 4; index++)
    {
      packet[26 + index] = source_ip[index];
      packet[30 + index] = target_ip[index];
    }

  checksum = ok8mp_fec_checksum(&packet[14], FEC_IPV4_HEADER_SIZE);
  packet[24] = checksum >> 8;
  packet[25] = checksum & 0xff;

  packet[34] = 8;
  packet[35] = 0;
  packet[36] = 0;
  packet[37] = 0;
  packet[38] = 0x4d;
  packet[39] = 0x37;
  packet[40] = 0;
  packet[41] = 1;
  for (index = 0; index < FEC_ICMP_PAYLOAD_SIZE; index++)
    {
      packet[42 + index] = data[index];
    }

  checksum = ok8mp_fec_checksum(&packet[34],
                                 FEC_ICMP_HEADER_SIZE + FEC_ICMP_PAYLOAD_SIZE);
  packet[36] = checksum >> 8;
  packet[37] = checksum & 0xff;
}

static void ok8mp_fec_capture_arp_reply(struct ok8mp_fec_dma_s *state,
                                         const uint8_t *frame,
                                         uint16_t length)
{
  unsigned int index;

  if (length < FEC_ARP_HEADER_SIZE ||
      frame[12] != 0x08 || frame[13] != 0x06 ||
      frame[20] != 0x00 || frame[21] != 0x02)
    {
      return;
    }

  state->arp_replies++;
  for (index = 0; index < 6; index++)
    {
      state->arp_sender_mac[index] = frame[22 + index];
    }

  for (index = 0; index < 4; index++)
    {
      state->arp_sender_ip[index] = frame[28 + index];
    }
}

static void ok8mp_fec_capture_icmp_reply(struct ok8mp_fec_dma_s *state,
                                          const uint8_t *frame,
                                          uint16_t length)
{
  uint16_t ip_header_length;
  uint16_t icmp_offset;
  unsigned int index;

  if (length < 14 + FEC_IPV4_HEADER_SIZE + FEC_ICMP_HEADER_SIZE ||
      frame[12] != 0x08 || frame[13] != 0x00 ||
      (frame[14] & 0xf0) != 0x40 || frame[23] != 1)
    {
      return;
    }

  ip_header_length = (frame[14] & 0x0f) * 4;
  icmp_offset = 14 + ip_header_length;
  if (ip_header_length < FEC_IPV4_HEADER_SIZE ||
      length < icmp_offset + FEC_ICMP_HEADER_SIZE ||
      frame[icmp_offset] != 0 || frame[icmp_offset + 1] != 0)
    {
      return;
    }

  state->icmp_replies++;
  for (index = 0; index < 4; index++)
    {
      state->icmp_sender_ip[index] = frame[26 + index];
    }
}

static int ok8mp_fec_mdio_read(uint8_t reg, uint16_t *value)
{
  uint32_t mmfr;
  unsigned int elapsed;

  putreg32(FEC_EIR_MII, MX8M_ENET1 + FEC_EIR_OFFSET);

  mmfr = FEC_MMFR_ST | FEC_MMFR_OP_READ |
         (FEC_PHY_ADDR << FEC_MMFR_PA_SHIFT) |
         (reg << FEC_MMFR_RA_SHIFT) | FEC_MMFR_TA;
  putreg32(mmfr, MX8M_ENET1 + FEC_MMFR_OFFSET);

  for (elapsed = 0; elapsed < FEC_MDIO_TIMEOUT_US; elapsed += 10)
    {
      if ((getreg32(MX8M_ENET1 + FEC_EIR_OFFSET) & FEC_EIR_MII) != 0)
        {
          *value = getreg32(MX8M_ENET1 + FEC_MMFR_OFFSET) & 0xffff;
          putreg32(FEC_EIR_MII, MX8M_ENET1 + FEC_EIR_OFFSET);
          return OK;
        }

      usleep(10);
    }

  return -ETIMEDOUT;
}

static int ok8mp_fec_mdio_write(uint8_t reg, uint16_t value)
{
  uint32_t mmfr;
  unsigned int elapsed;

  putreg32(FEC_EIR_MII, MX8M_ENET1 + FEC_EIR_OFFSET);

  mmfr = FEC_MMFR_ST | FEC_MMFR_OP_WRITE |
         (FEC_PHY_ADDR << FEC_MMFR_PA_SHIFT) |
         (reg << FEC_MMFR_RA_SHIFT) | FEC_MMFR_TA | value;
  putreg32(mmfr, MX8M_ENET1 + FEC_MMFR_OFFSET);

  for (elapsed = 0; elapsed < FEC_MDIO_TIMEOUT_US; elapsed += 10)
    {
      if ((getreg32(MX8M_ENET1 + FEC_EIR_OFFSET) & FEC_EIR_MII) != 0)
        {
          putreg32(FEC_EIR_MII, MX8M_ENET1 + FEC_EIR_OFFSET);
          return OK;
        }

      usleep(10);
    }

  return -ETIMEDOUT;
}

static int ok8mp_fec_mdio_read_ready(uint8_t reg, uint16_t *value)
{
  int ret;
  unsigned int attempt;

  /* The first MDIO frame immediately after PHY reset can return 0xffff. */

  for (attempt = 0; attempt < 3; attempt++)
    {
      ret = ok8mp_fec_mdio_read(reg, value);
      if (ret < 0 || *value != 0xffff)
        {
          return ret;
        }

      usleep(10000);
    }

  return OK;
}

static int ok8mp_fec_restart_autoneg(void)
{
  uint16_t bmcr;
  int ret;

  ret = ok8mp_fec_mdio_read_ready(FEC_PHY_BMCR, &bmcr);
  if (ret >= 0)
    {
      ret = ok8mp_fec_mdio_write(FEC_PHY_ANAR, FEC_PHY_ANAR_ALL_10_100);
    }

  if (ret >= 0)
    {
      ret = ok8mp_fec_mdio_write(FEC_PHY_GBCR, FEC_PHY_GBCR_1000_FULL);
    }

  if (ret >= 0)
    {
      bmcr &= ~FEC_PHY_BMCR_ISOLATE;
      bmcr |= FEC_PHY_BMCR_AUTONEG | FEC_PHY_BMCR_RESTART_AN;
      ret = ok8mp_fec_mdio_write(FEC_PHY_BMCR, bmcr);
    }

  return ret;
}

static int ok8mp_fec_prepare(void)
{
  uint32_t ecr;
  unsigned int elapsed;

  /* Match the NXP SDK ENET1 clock setup: AXI=250 MHz, timer=100 MHz,
   * reference=125 MHz.  Only MDC/MDIO is used in this probe.
   */

  mx8mp_ccm_configure_clock(ENET_AXI_CLK_ROOT, SYSTEM_PLL2_DIV4_CLK, 1, 1);
  mx8mp_ccm_configure_clock(ENET_TIMER_CLK_ROOT, SYSTEM_PLL2_DIV10_CLK,
                            1, 1);
  mx8mp_ccm_configure_clock(ENET_REF_CLK_ROOT, SYSTEM_PLL2_DIV8_CLK, 1, 1);
  mx8mp_ccm_enable_clock(ENET_AXI_CLK_ROOT);
  mx8mp_ccm_enable_clock(ENET_TIMER_CLK_ROOT);
  mx8mp_ccm_enable_clock(ENET_REF_CLK_ROOT);
  mx8mp_ccm_gate_clock(CCM_ENET1_CLK_GATE, CLK_ALWAYS_NEEDED);
  mx8mp_ccm_gate_clock(CCM_SIM_ENET_CLK_GATE, CLK_ALWAYS_NEEDED);
  /* Reset FEC before programming MSCR. The reset clears the MDC divider. */

  /* Keep the reset sequence identical to the NXP SDK: RESET is an
   * in-place bit update, not a full ECR register value.  A full write of
   * 0x1 cleared implementation-defined control bits on this board.
   */

  ecr = getreg32(MX8M_ENET1 + FEC_ECR_OFFSET);
  putreg32(ecr | FEC_ECR_RESET, MX8M_ENET1 + FEC_ECR_OFFSET);
  for (elapsed = 0; elapsed < FEC_RESET_TIMEOUT_US; elapsed += 10)
    {
      if ((getreg32(MX8M_ENET1 + FEC_ECR_OFFSET) & FEC_ECR_RESET) == 0)
        {
          break;
        }

      usleep(10);
    }

  if (elapsed == FEC_RESET_TIMEOUT_US)
    {
      return -ETIMEDOUT;
    }

  mx8mp_iomuxc_config(IOMUX_FEC_MDC);
  mx8mp_iomuxc_config(IOMUX_FEC_MDIO);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_RD0);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_RD1);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_RD2);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_RD3);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_RX_CTL);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_RXC);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_TD0);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_TD1);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_TD2);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_TD3);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_TX_CTL);
  mx8mp_iomuxc_config(IOMUX_FEC_RGMII_TXC);
  mx8mp_iomuxc_config(IOMUX_FEC_PHY_RESET);
  mx8mp_gpio_config(GPIO_FEC_PHY_RESET);

  /* The board DTS describes GPIO5_IO04 as active-low PHY reset. */

  mx8mp_gpio_write(GPIO_FEC_PHY_RESET, false);
  usleep(10000);
  mx8mp_gpio_write(GPIO_FEC_PHY_RESET, true);
  usleep(FEC_PHY_POWERUP_DELAY_US);

  /* 250 MHz ENET clock -> 2.5 MHz MDC, with a two-cycle MDIO hold time. */

  putreg32((49u << FEC_MSCR_SPEED_SHIFT) |
           (2u << FEC_MSCR_HOLDTIME_SHIFT),
           MX8M_ENET1 + FEC_MSCR_OFFSET);

  return OK;
}

int ok8mp_fec_phy_probe(struct ok8mp_fec_phy_s *phy)
{
  int ret;

  if (phy == NULL)
    {
      return -EINVAL;
    }

  ret = ok8mp_fec_prepare();
  if (ret < 0)
    {
      return ret;
    }

  ret = ok8mp_fec_restart_autoneg();
  if (ret < 0)
    {
      return ret;
    }

  /* Allow the copper link to complete auto-negotiation before sampling. */

  usleep(FEC_PHY_AUTONEG_DELAY_US);

  ret = ok8mp_fec_mdio_read_ready(FEC_PHY_ID1, &phy->id1);
  if (ret >= 0)
    {
      ret = ok8mp_fec_mdio_read_ready(FEC_PHY_ID2, &phy->id2);
    }

  if (ret >= 0)
    {
      /* BMSR link state is latch-low, so it must be sampled twice. */

      ret = ok8mp_fec_mdio_read_ready(FEC_PHY_BMSR, &phy->bmsr);
    }

  if (ret >= 0)
    {
      ret = ok8mp_fec_mdio_read(FEC_PHY_BMSR, &phy->bmsr);
    }

  if (ret >= 0)
    {
      ret = ok8mp_fec_mdio_read(FEC_PHY_YT8521_STATUS, &phy->status);
    }

  return ret;
}

static int ok8mp_fec_dma_initialize_internal(struct ok8mp_fec_dma_s *state,
                                              bool start_rx,
                                              bool attach_irqs)
{
  uint32_t ecr;
  uint32_t rcr;
  unsigned int index;
  int ret;

  if (state == NULL)
    {
      return -EINVAL;
    }

  state->arp_replies = 0;
  state->icmp_replies = 0;
  for (index = 0; index < 6; index++)
    {
      state->arp_sender_mac[index] = 0;
    }

  for (index = 0; index < 4; index++)
    {
      state->arp_sender_ip[index] = 0;
      state->icmp_sender_ip[index] = 0;
    }

  state->last_frame_length = 0;
  for (index = 0; index < FEC_ARP_HEADER_SIZE; index++)
    {
      state->last_frame[index] = 0;
    }

  ret = ok8mp_fec_rdc_prepare(state);
  if (ret < 0)
    {
      return ret;
    }

  if (attach_irqs)
    {
      ret = ok8mp_fec_attach_interrupts();
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = ok8mp_fec_prepare();
  if (ret < 0)
    {
      return ret;
    }

  ret = ok8mp_fec_restart_autoneg();
  if (ret < 0)
    {
      return ret;
    }

  usleep(FEC_PHY_AUTONEG_DELAY_US);

  for (index = 0; index < FEC_RX_DESC_COUNT; index++)
    {
      g_fec_rx_desc[index].length = 0;
      g_fec_rx_desc[index].control = FEC_BD_EMPTY;
      g_fec_rx_desc[index].buffer =
        ok8mp_fec_dma_busaddr((uintptr_t)g_fec_rx_buffer[index]);
    }

  g_fec_rx_desc[FEC_RX_DESC_COUNT - 1].control |= FEC_BD_WRAP;

  for (index = 0; index < FEC_TX_DESC_COUNT; index++)
    {
      g_fec_tx_desc[index].length = 0;
      g_fec_tx_desc[index].control = FEC_BD_TXCRC;
      g_fec_tx_desc[index].buffer =
        ok8mp_fec_dma_busaddr((uintptr_t)g_fec_tx_buffer[index]);
    }

  g_fec_tx_desc[FEC_TX_DESC_COUNT - 1].control |= FEC_BD_WRAP;

  /* Configure 1000M RGMII before enabling the MAC.  DBSWP lets the M7 use
   * the SDK's native little-endian descriptor layout.
   */

  putreg32(0, MX8M_ENET1 + FEC_RCR_OFFSET);
  ecr = getreg32(MX8M_ENET1 + FEC_ECR_OFFSET);
  ecr &= ~FEC_ECR_ETHEREN;
  ecr |= FEC_ECR_SPEED | FEC_ECR_DBSWP;
  putreg32(ecr, MX8M_ENET1 + FEC_ECR_OFFSET);

  rcr = FEC_RCR_MII_MODE | FEC_RCR_RGMII_EN |
        (1518u << FEC_RCR_MAX_FL_SHIFT);
  putreg32(rcr, MX8M_ENET1 + FEC_RCR_OFFSET);
  putreg32(FEC_TCR_FDEN, MX8M_ENET1 + FEC_TCR_OFFSET);
  putreg32((g_fec_mac[0] << 24) | (g_fec_mac[1] << 16) |
           (g_fec_mac[2] << 8) | g_fec_mac[3],
           MX8M_ENET1 + FEC_PALR_OFFSET);
  putreg32((g_fec_mac[4] << 24) | (g_fec_mac[5] << 16),
           MX8M_ENET1 + FEC_PAUR_OFFSET);
  putreg32(ok8mp_fec_dma_busaddr((uintptr_t)g_fec_rx_desc),
           MX8M_ENET1 + FEC_RDSR_OFFSET);
  putreg32(ok8mp_fec_dma_busaddr((uintptr_t)g_fec_tx_desc),
           MX8M_ENET1 + FEC_TDSR_OFFSET);
  putreg32(FEC_BUFFER_SIZE, MX8M_ENET1 + FEC_MRBR_OFFSET);
  putreg32(FEC_TFWR_STRFWD, MX8M_ENET1 + FEC_TFWR_OFFSET);

  putreg32(0xffffffff, MX8M_ENET1 + FEC_EIR_OFFSET);
  g_fec_dma_events = 0;

  /* Start with polling only.  The board-specific M7 interrupt route is
   * validated separately after MAC/DMA traffic is known to be stable.
   */

  putreg32(0, MX8M_ENET1 + FEC_EIMR_OFFSET);
  putreg32(ecr | FEC_ECR_ETHEREN | FEC_ECR_DBSWP,
           MX8M_ENET1 + FEC_ECR_OFFSET);
  if (start_rx)
    {
      putreg32(FEC_RDAR_ACTIVE, MX8M_ENET1 + FEC_RDAR_OFFSET);
    }

  ok8mp_fec_dma_snapshot(state);
  return OK;
}

int ok8mp_fec_dma_initialize(struct ok8mp_fec_dma_s *state)
{
  return ok8mp_fec_dma_initialize_internal(state, true, true);
}

int ok8mp_fec_dma_initialize_tx(struct ok8mp_fec_dma_s *state)
{
  return ok8mp_fec_dma_initialize_internal(state, false, true);
}

int ok8mp_fec_dma_enable_interrupts(struct ok8mp_fec_dma_s *state)
{
  uint32_t interrupts = FEC_EIR_RXF | FEC_EIR_TXF | FEC_EIR_EBERR;

  if (state == NULL)
    {
      return -EINVAL;
    }

  g_fec_dma_events = 0;
  putreg32(0xffffffff, MX8M_ENET1 + FEC_EIR_OFFSET);
  putreg32(interrupts, MX8M_ENET1 + FEC_EIMR_OFFSET);
  ok8mp_fec_dma_snapshot(state);
  return OK;
}

int ok8mp_fec_dma_status(struct ok8mp_fec_dma_s *state)
{
  if (state == NULL)
    {
      return -EINVAL;
    }

  ok8mp_fec_dma_snapshot(state);
  ok8mp_fec_rdc_snapshot(state);
  return OK;
}

int ok8mp_fec_dma_rdc_status(struct ok8mp_fec_dma_s *state)
{
  if (state == NULL)
    {
      return -EINVAL;
    }

  ok8mp_fec_rdc_snapshot(state);
  return OK;
}

static int ok8mp_fec_dma_send_frame(struct ok8mp_fec_dma_s *state,
                                    uint16_t length)
{
  uint16_t control;
  unsigned int elapsed;

  if (state == NULL)
    {
      return -EINVAL;
    }

  control = g_fec_tx_desc[0].control;
  if ((control & FEC_BD_READY) != 0)
    {
      return -EBUSY;
    }

  g_fec_tx_desc[0].length = length;
  g_fec_tx_desc[0].control = (control & FEC_BD_WRAP) |
                            FEC_BD_LAST | FEC_BD_TXCRC;

  /* The OCRAM DMA section is non-cacheable.  The barrier only orders the
   * descriptor writes before handing ownership to the FEC.
   */

  __sync_synchronize();
  g_fec_tx_desc[0].control |= FEC_BD_READY;
  __sync_synchronize();
  /* The raw probe preserves the known-good zero-write doorbell sequence. */

  putreg32(0, MX8M_ENET1 + FEC_TDAR_OFFSET);

  for (elapsed = 0; elapsed < FEC_TX_TIMEOUT_US; elapsed += 100)
    {
      if ((g_fec_tx_desc[0].control & FEC_BD_READY) == 0)
        {
          ok8mp_fec_dma_snapshot(state);
          state->tx_length = length;
          state->tx_control = g_fec_tx_desc[0].control;
          return OK;
        }

      usleep(100);
    }

  ok8mp_fec_dma_snapshot(state);
  state->tx_length = length;
  state->tx_control = g_fec_tx_desc[0].control;
  return -ETIMEDOUT;
}

int ok8mp_fec_dma_send_test(struct ok8mp_fec_dma_s *state)
{
  if (state == NULL)
    {
      return -EINVAL;
    }

  ok8mp_fec_build_test_frame(g_fec_tx_buffer[0]);
  return ok8mp_fec_dma_send_frame(state, FEC_TEST_FRAME_SIZE);
}

int ok8mp_fec_dma_send_arp_request(struct ok8mp_fec_dma_s *state,
                                    const uint8_t source_ip[4],
                                    const uint8_t target_ip[4])
{
  if (state == NULL || source_ip == NULL || target_ip == NULL)
    {
      return -EINVAL;
    }

  ok8mp_fec_build_arp_request(g_fec_tx_buffer[0], source_ip, target_ip);
  return ok8mp_fec_dma_send_frame(state, FEC_ARP_FRAME_SIZE);
}

int ok8mp_fec_dma_send_icmp_echo(struct ok8mp_fec_dma_s *state,
                                  const uint8_t target_mac[6],
                                  const uint8_t source_ip[4],
                                  const uint8_t target_ip[4])
{
  if (state == NULL || target_mac == NULL || source_ip == NULL ||
      target_ip == NULL)
    {
      return -EINVAL;
    }

  ok8mp_fec_build_icmp_echo(g_fec_tx_buffer[0], target_mac,
                            source_ip, target_ip);
  return ok8mp_fec_dma_send_frame(state, FEC_ICMP_FRAME_SIZE);
}

int ok8mp_fec_dma_reclaim(struct ok8mp_fec_dma_s *state)
{
  uint32_t frames = 0;
  uint32_t bytes = 0;
  unsigned int index;
  unsigned int offset;

  if (state == NULL)
    {
      return -EINVAL;
    }

  for (index = 0; index < FEC_RX_DESC_COUNT; index++)
    {
      uint16_t control = g_fec_rx_desc[index].control;

      if ((control & FEC_BD_EMPTY) != 0)
        {
          continue;
        }

      frames++;
      bytes += g_fec_rx_desc[index].length;
      state->last_frame_length = g_fec_rx_desc[index].length;
      for (offset = 0;
           offset < FEC_ARP_HEADER_SIZE &&
           offset < g_fec_rx_desc[index].length;
           offset++)
        {
          state->last_frame[offset] = g_fec_rx_buffer[index][offset];
        }

      ok8mp_fec_capture_arp_reply(state, g_fec_rx_buffer[index],
                                  g_fec_rx_desc[index].length);
      ok8mp_fec_capture_icmp_reply(state, g_fec_rx_buffer[index],
                                   g_fec_rx_desc[index].length);
      g_fec_rx_desc[index].length = 0;
      __sync_synchronize();
      g_fec_rx_desc[index].control = (control & FEC_BD_WRAP) | FEC_BD_EMPTY;
    }

  if (frames != 0)
    {
      __sync_synchronize();
      putreg32(FEC_RDAR_ACTIVE, MX8M_ENET1 + FEC_RDAR_OFFSET);
    }

  ok8mp_fec_dma_snapshot(state);
  state->rx_frames += frames;
  state->rx_bytes += bytes;
  return (int)frames;
}

#ifdef CONFIG_OK8MP_M7_FEC_NET

/****************************************************************************
 * NuttX Ethernet driver
 *
 * The raw FEC validation uses the same descriptors.  The network driver
 * deliberately starts in polling mode: the hardware IRQ routing remains a
 * separate optimization once IPv4/TCP have been proven stable.
 ****************************************************************************/

#define FEC_NET_ETHBUF(dev) ((FAR struct eth_hdr_s *)(dev)->d_buf)

static int ok8mp_fec_net_transmit(struct ok8mp_fec_net_s *priv)
{
  struct net_driver_s *dev = &priv->dev;
  struct fec_bd_s *desc;
  uint16_t control;
  uint16_t length;
  uint16_t copy_length;
  uint8_t index;
  unsigned int offset;

  if (dev->d_len == 0)
    {
      return OK;
    }

  if (dev->d_len > FEC_BUFFER_SIZE)
    {
      priv->tx_errors++;
      NETDEV_TXERRORS(dev);
      dev->d_len = 0;
      return -EMSGSIZE;
    }

  index = priv->txhead;
  desc = &g_fec_tx_desc[index];
  control = desc->control;
  if ((control & FEC_BD_READY) != 0)
    {
      priv->tx_busy++;
      return -EBUSY;
    }

  copy_length = dev->d_len;
  length = copy_length;
  if (length < FEC_ETH_MIN_FRAME_SIZE)
    {
      /* The FEC test path which is known to transmit uses a 60-byte ARP
       * frame.  Pad ARP and other short Ethernet frames to the mandatory
       * minimum size excluding FCS before granting the descriptor to DMA.
       */

      length = FEC_ETH_MIN_FRAME_SIZE;
    }

  priv->tx_attempts++;
  if (FEC_NET_ETHBUF(dev)->type == HTONS(ETHTYPE_ARP))
    {
      priv->tx_arp++;
    }
  else if (FEC_NET_ETHBUF(dev)->type == HTONS(ETHTYPE_IP))
    {
      priv->tx_ipv4++;
    }

  for (offset = 0; offset < copy_length; offset++)
    {
      g_fec_tx_buffer[index][offset] = dev->d_buf[offset];
    }

  for (; offset < length; offset++)
    {
      g_fec_tx_buffer[index][offset] = 0;
    }

  desc->length = length;
  desc->control = (control & FEC_BD_WRAP) | FEC_BD_LAST | FEC_BD_TXCRC;
  __sync_synchronize();
  desc->control |= FEC_BD_READY;
  __sync_synchronize();

  /* The FEC may have been reset by an earlier software probe.  Restore the
   * two control bits required by the SDK immediately before ringing TDAR.
   */

  control = getreg32(MX8M_ENET1 + FEC_ECR_OFFSET);
  putreg32(control | FEC_ECR_ETHEREN | FEC_ECR_DBSWP,
           MX8M_ENET1 + FEC_ECR_OFFSET);
  __sync_synchronize();

  /* Reuse the doorbell sequence proven by the board-level raw ARP test.
   * On this OK8MP boot path, writing zero causes the FEC to rescan the TX
   * ring, while writing TDAR[ACTIVE] leaves the descriptor owned by M7.
   */

  putreg32(0, MX8M_ENET1 + FEC_TDAR_OFFSET);

  priv->txhead++;
  if (priv->txhead >= FEC_TX_DESC_COUNT)
    {
      priv->txhead = 0;
    }

  dev->d_buf = g_fec_tx_buffer[priv->txhead];
  dev->d_len = 0;
  NETDEV_TXPACKETS(dev);
  return OK;
}

static int ok8mp_fec_net_txpoll(struct net_driver_s *dev)
{
  struct ok8mp_fec_net_s *priv = dev->d_private;
  int ret;

  priv->txpoll_calls++;
  ret = ok8mp_fec_net_transmit(priv);
  if (ret < 0)
    {
      return ret;
    }

  /* Submit at most one frame per poll pass.  A received ARP request already
   * proved that a single descriptor is transmitted correctly; allowing
   * devif_poll() to fill both descriptors back-to-back leaves both READY on
   * this FEC.  The 10 ms worker will poll again after DMA releases the ring.
   */

  return -EBUSY;
}

static void ok8mp_fec_net_dispatch(struct ok8mp_fec_net_s *priv)
{
  struct net_driver_s *dev = &priv->dev;

  if (dev->d_len < sizeof(struct eth_hdr_s))
    {
      NETDEV_RXDROPPED(dev);
      dev->d_len = 0;
      return;
    }

  NETDEV_RXPACKETS(dev);

#ifdef CONFIG_NET_IPv4
  if (FEC_NET_ETHBUF(dev)->type == HTONS(ETHTYPE_IP))
    {
      NETDEV_RXIPV4(dev);
      ipv4_input(dev);
    }
  else
#endif
#ifdef CONFIG_NET_ARP
  if (FEC_NET_ETHBUF(dev)->type == HTONS(ETHTYPE_ARP))
    {
      NETDEV_RXARP(dev);
      arp_input(dev);
    }
  else
#endif
    {
      NETDEV_RXDROPPED(dev);
      dev->d_len = 0;
    }

  if (dev->d_len > 0)
    {
      (void)ok8mp_fec_net_transmit(priv);
    }
}

static void ok8mp_fec_net_receive(struct ok8mp_fec_net_s *priv)
{
  struct net_driver_s *dev = &priv->dev;
  struct fec_bd_s *desc;
  uint16_t control;
  uint16_t length;
  uint8_t index;

  for (;;)
    {
      index = priv->rxhead;
      desc = &g_fec_rx_desc[index];
      control = desc->control;
      if ((control & FEC_BD_EMPTY) != 0)
        {
          break;
        }

      length = desc->length;
      if (length > FEC_BUFFER_SIZE)
        {
          NETDEV_RXERRORS(dev);
          length = 0;
        }

      dev->d_buf = g_fec_rx_buffer[index];
      dev->d_len = length;
      if (length != 0)
        {
          ok8mp_fec_net_dispatch(priv);
        }

      desc->length = 0;
      __sync_synchronize();
      desc->control = (control & FEC_BD_WRAP) | FEC_BD_EMPTY;
      __sync_synchronize();
      putreg32(FEC_RDAR_ACTIVE, MX8M_ENET1 + FEC_RDAR_OFFSET);

      priv->rxhead++;
      if (priv->rxhead >= FEC_RX_DESC_COUNT)
        {
          priv->rxhead = 0;
        }
    }
}

static int ok8mp_fec_net_ifup(struct net_driver_s *dev)
{
  struct ok8mp_fec_net_s *priv = dev->d_private;
  struct ok8mp_fec_dma_s state;
  int ret;

  /* The NuttX net driver polls descriptors.  Do not enable the raw probe's
   * ERROR_AXI handler here: it deliberately quiesces the MAC on an event.
   */

  ret = ok8mp_fec_dma_initialize_internal(&state, true, false);
  if (ret < 0)
    {
      return ret;
    }

  priv->rxhead = 0;
  priv->txhead = 0;
  priv->ifup = true;

#ifdef CONFIG_NET_IPv4
  /* Keep a statically configured FEC netdev routable if it is registered
   * after netinit.  DHCP configurations intentionally leave these values
   * untouched so that the lease can populate them.
   */

#ifdef CONFIG_NETINIT_IPADDR
  dev->d_ipaddr  = HTONL(CONFIG_NETINIT_IPADDR);
#endif
#ifdef CONFIG_NETINIT_NETMASK
  dev->d_netmask = HTONL(CONFIG_NETINIT_NETMASK);
#endif
#ifdef CONFIG_NETINIT_DRIPADDR
  dev->d_draddr  = HTONL(CONFIG_NETINIT_DRIPADDR);
#endif
#endif

  dev->d_flags |= IFF_RUNNING;
  dev->d_buf = g_fec_tx_buffer[0];
  dev->d_len = 0;
  return OK;
}

static int ok8mp_fec_net_ifdown(struct net_driver_s *dev)
{
  struct ok8mp_fec_net_s *priv = dev->d_private;
  uint32_t ecr;

  priv->ifup = false;
  dev->d_flags &= ~IFF_RUNNING;
  putreg32(0, MX8M_ENET1 + FEC_EIMR_OFFSET);
  ecr = getreg32(MX8M_ENET1 + FEC_ECR_OFFSET);
  putreg32(ecr & ~FEC_ECR_ETHEREN, MX8M_ENET1 + FEC_ECR_OFFSET);
  dev->d_len = 0;
  return OK;
}

static int ok8mp_fec_net_txavail(struct net_driver_s *dev)
{
  /* The polling task calls devif_poll() every 10 ms.  Returning quickly
   * keeps this callback safe when it is invoked with the network locked.
   */

  return OK;
}

static int ok8mp_fec_net_worker(int argc, FAR char *argv[])
{
  struct ok8mp_fec_net_s *priv = &g_fec_net;

  for (;;)
    {
      net_lock();
      priv->poll_cycles++;
      if (priv->ifup)
        {
          ok8mp_fec_net_receive(priv);
          priv->dev.d_buf = g_fec_tx_buffer[priv->txhead];
          priv->dev.d_len = 0;
          (void)devif_poll(&priv->dev, ok8mp_fec_net_txpoll);
        }

      net_unlock();
      usleep(FEC_NET_POLL_USEC);
    }

  return OK;
}

int ok8mp_fec_netinitialize(void)
{
  struct net_driver_s *dev = &g_fec_net.dev;
  int ret;

  if (g_fec_net.registered)
    {
      return OK;
    }

  memset(&g_fec_net, 0, sizeof(g_fec_net));
  memcpy(dev->d_mac.ether.ether_addr_octet, g_fec_mac, sizeof(g_fec_mac));
  dev->d_pktsize = FEC_BUFFER_SIZE;
  dev->d_buf = g_fec_tx_buffer[0];
  dev->d_ifup = ok8mp_fec_net_ifup;
  dev->d_ifdown = ok8mp_fec_net_ifdown;
  dev->d_txavail = ok8mp_fec_net_txavail;
  dev->d_private = &g_fec_net;

  ret = netdev_register(dev, NET_LL_ETHERNET);
  if (ret < 0)
    {
      return ret;
    }

  g_fec_net.registered = true;
  ret = task_create("fec_poll", FEC_NET_PRIORITY, FEC_NET_STACKSIZE,
                    ok8mp_fec_net_worker, NULL);
  if (ret < 0)
    {
      g_fec_net.registered = false;
      return ret;
    }

  return OK;
}

int ok8mp_fec_net_diag(struct ok8mp_fec_net_diag_s *diag)
{
  struct net_driver_s *dev = &g_fec_net.dev;
  in_addr_t target;

  if (diag == NULL)
    {
      return -EINVAL;
    }

  memset(diag, 0, sizeof(*diag));
  diag->flags = dev->d_flags;
  diag->ipaddr = dev->d_ipaddr;
  diag->netmask = dev->d_netmask;
  diag->draddr = dev->d_draddr;
  diag->poll_cycles = g_fec_net.poll_cycles;
  diag->txpoll_calls = g_fec_net.txpoll_calls;
  diag->tx_attempts = g_fec_net.tx_attempts;
  diag->tx_arp = g_fec_net.tx_arp;
  diag->tx_ipv4 = g_fec_net.tx_ipv4;
  diag->tx_busy = g_fec_net.tx_busy;
  diag->tx_errors = g_fec_net.tx_errors;
  diag->tx0_control = g_fec_tx_desc[0].control;
  diag->tx1_control = g_fec_tx_desc[1].control;
  diag->tx0_length = g_fec_tx_desc[0].length;
  diag->tx1_length = g_fec_tx_desc[1].length;
  diag->eir = getreg32(MX8M_ENET1 + FEC_EIR_OFFSET);
  diag->tdar = getreg32(MX8M_ENET1 + FEC_TDAR_OFFSET);
  diag->ecr = getreg32(MX8M_ENET1 + FEC_ECR_OFFSET);
  diag->rdsr = getreg32(MX8M_ENET1 + FEC_RDSR_OFFSET);
  diag->tdsr = getreg32(MX8M_ENET1 + FEC_TDSR_OFFSET);
  memcpy(diag->tx0_header, g_fec_tx_buffer[0], sizeof(diag->tx0_header));
  diag->registered = g_fec_net.registered;
  diag->ifup = g_fec_net.ifup;

  target = HTONL(CONFIG_NETINIT_DRIPADDR);
  diag->local_target = IFF_IS_RUNNING(dev->d_flags) &&
                       (dev->d_ipaddr & dev->d_netmask) ==
                       (target & dev->d_netmask);
  return OK;
}

#endif /* CONFIG_OK8MP_M7_FEC_NET */
