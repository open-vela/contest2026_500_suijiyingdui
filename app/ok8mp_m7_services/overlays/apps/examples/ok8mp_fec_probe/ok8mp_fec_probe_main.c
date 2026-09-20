/****************************************************************************
 * apps/examples/ok8mp_fec_probe/ok8mp_fec_probe_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <arch/board/board.h>

#define YT8521_STATUS_LINK     (1u << 10)
#define YT8521_STATUS_DUPLEX   (1u << 13)
#define YT8521_STATUS_SPEED    (3u << 14)

static const char *fec_speed(uint16_t status)
{
  switch (status & YT8521_STATUS_SPEED)
    {
      case 0u << 14:
        return "10M";
      case 1u << 14:
        return "100M";
      case 2u << 14:
        return "1000M";
      default:
        return "unknown";
    }
}

static int fec_parse_ipv4(const char *text, uint8_t address[4])
{
  char *end;
  unsigned long value;
  unsigned int index;

  for (index = 0; index < 4; index++)
    {
      value = strtoul(text, &end, 10);
      if (end == text || value > 255)
        {
          return -1;
        }

      address[index] = (uint8_t)value;
      if (index == 3)
        {
          return *end == '\0' ? 0 : -1;
        }

      if (*end != '.')
        {
          return -1;
        }

      text = end + 1;
    }

  return -1;
}

int main(int argc, FAR char *argv[])
{
  struct ok8mp_fec_phy_s phy;
  struct ok8mp_fec_dma_s dma;
  bool send_frame;
  bool enable_irq;
  bool tx_only;
  int ret;

  if (argc == 2 && strcmp(argv[1], "rdc") == 0)
    {
      ret = ok8mp_fec_dma_rdc_status(&dma);
      if (ret < 0)
        {
          printf("fec_probe: RDC status failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("fec_probe: RDC domain=%lu M7=0x%08lx ENET1TX=0x%08lx "
             "ENET1RX=0x%08lx\n",
             (unsigned long)dma.rdc_domain,
             (unsigned long)dma.rdc_m7_mda,
             (unsigned long)dma.rdc_enet_tx_mda,
             (unsigned long)dma.rdc_enet_rx_mda);
      printf("fec_probe: RDC ENET1-PDAP=0x%08lx OCRAM-MRC2_0=0x%08lx\n",
             (unsigned long)dma.rdc_enet_pdap,
             (unsigned long)dma.rdc_ocram_mrc);
      return EXIT_SUCCESS;
    }

  if (argc >= 2 && strcmp(argv[1], "arp") == 0)
    {
      uint8_t source_ip[4];
      uint8_t target_ip[4];
      unsigned long watch_ms = 1000;
      unsigned long elapsed = 0;

      if ((argc != 4 && argc != 5) ||
          fec_parse_ipv4(argc > 2 ? argv[2] : "", source_ip) < 0 ||
          fec_parse_ipv4(argc > 3 ? argv[3] : "", target_ip) < 0)
        {
          printf("usage: fec_probe arp <source-ip> <target-ip> [watch_ms]\n");
          return EXIT_FAILURE;
        }

      if (argc == 5)
        {
          watch_ms = strtoul(argv[4], NULL, 0);
        }

      ret = ok8mp_fec_dma_initialize(&dma);
      if (ret < 0)
        {
          printf("fec_probe: ARP DMA initialize failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      ret = ok8mp_fec_dma_send_arp_request(&dma, source_ip, target_ip);
      printf("fec_probe: ARP request %u.%u.%u.%u -> %u.%u.%u.%u %s\n",
             source_ip[0], source_ip[1], source_ip[2], source_ip[3],
             target_ip[0], target_ip[1], target_ip[2], target_ip[3],
             ret == 0 ? "sent" : "failed");
      if (ret < 0)
        {
          return EXIT_FAILURE;
        }

      while (elapsed < watch_ms)
        {
          ok8mp_fec_dma_reclaim(&dma);
          usleep(10000);
          elapsed += 10;
        }

      ok8mp_fec_dma_status(&dma);
      printf("fec_probe: ARP replies=%lu last=%u.%u.%u.%u "
             "mac=%02x:%02x:%02x:%02x:%02x:%02x RX=%lu/%lu EIR=0x%08lx\n",
             (unsigned long)dma.arp_replies,
             dma.arp_sender_ip[0], dma.arp_sender_ip[1],
             dma.arp_sender_ip[2], dma.arp_sender_ip[3],
             dma.arp_sender_mac[0], dma.arp_sender_mac[1],
             dma.arp_sender_mac[2], dma.arp_sender_mac[3],
             dma.arp_sender_mac[4], dma.arp_sender_mac[5],
             (unsigned long)dma.rx_frames,
             (unsigned long)dma.rx_bytes,
             (unsigned long)dma.eir);
      if (dma.last_frame_length != 0)
        {
          unsigned int index;
          unsigned int length = dma.last_frame_length;

          if (length > sizeof(dma.last_frame))
            {
              length = sizeof(dma.last_frame);
            }

          printf("fec_probe: last RX (%lu bytes):",
                 (unsigned long)dma.last_frame_length);
          for (index = 0; index < length; index++)
            {
              printf(" %02x", dma.last_frame[index]);
            }

          printf("\n");
        }

      return dma.arp_replies == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc >= 2 && strcmp(argv[1], "ping") == 0)
    {
      uint8_t source_ip[4];
      uint8_t target_ip[4];
      unsigned long watch_ms = 1000;
      unsigned long elapsed = 0;

      if ((argc != 4 && argc != 5) ||
          fec_parse_ipv4(argc > 2 ? argv[2] : "", source_ip) < 0 ||
          fec_parse_ipv4(argc > 3 ? argv[3] : "", target_ip) < 0)
        {
          printf("usage: fec_probe ping <source-ip> <target-ip> [watch_ms]\\n");
          return EXIT_FAILURE;
        }

      if (argc == 5)
        {
          watch_ms = strtoul(argv[4], NULL, 0);
        }

      ret = ok8mp_fec_dma_initialize(&dma);
      if (ret < 0)
        {
          printf("fec_probe: ping DMA initialize failed: %d\\n", ret);
          return EXIT_FAILURE;
        }

      ret = ok8mp_fec_dma_send_arp_request(&dma, source_ip, target_ip);
      if (ret < 0)
        {
          printf("fec_probe: ARP request send failed: %d\\n", ret);
          return EXIT_FAILURE;
        }

      while (elapsed < 1000 && dma.arp_replies == 0)
        {
          ret = ok8mp_fec_dma_reclaim(&dma);
          if (ret < 0)
            {
              printf("fec_probe: ARP RX reclaim failed: %d\\n", ret);
              return EXIT_FAILURE;
            }

          usleep(10000);
          elapsed += 10;
        }

      if (dma.arp_replies == 0 ||
          memcmp(dma.arp_sender_ip, target_ip, sizeof(target_ip)) != 0)
        {
          printf("fec_probe: no ARP reply from %u.%u.%u.%u\\n",
                 target_ip[0], target_ip[1], target_ip[2], target_ip[3]);
          return EXIT_FAILURE;
        }

      printf("fec_probe: ARP resolved %u.%u.%u.%u at "
             "%02x:%02x:%02x:%02x:%02x:%02x\\n",
             target_ip[0], target_ip[1], target_ip[2], target_ip[3],
             dma.arp_sender_mac[0], dma.arp_sender_mac[1],
             dma.arp_sender_mac[2], dma.arp_sender_mac[3],
             dma.arp_sender_mac[4], dma.arp_sender_mac[5]);

      dma.icmp_replies = 0;
      printf("fec_probe: preparing ICMP echo frame\n");
      ret = ok8mp_fec_dma_send_icmp_echo(&dma, dma.arp_sender_mac,
                                         source_ip, target_ip);
      printf("fec_probe: ICMP TX submit returned %d, BD=0x%04lx\n", ret,
             (unsigned long)dma.tx_control);
      if (ret < 0)
        {
          printf("fec_probe: ICMP echo send failed: %d\\n", ret);
          return EXIT_FAILURE;
        }

      printf("fec_probe: ICMP echo request sent\\n");
      elapsed = 0;
      while (elapsed < watch_ms && dma.icmp_replies == 0)
        {
          ret = ok8mp_fec_dma_reclaim(&dma);
          if (ret < 0)
            {
              printf("fec_probe: ICMP RX reclaim failed: %d\\n", ret);
              return EXIT_FAILURE;
            }

          usleep(10000);
          elapsed += 10;
        }

      printf("fec_probe: ICMP replies=%lu last=%u.%u.%u.%u\\n",
             (unsigned long)dma.icmp_replies,
             dma.icmp_sender_ip[0], dma.icmp_sender_ip[1],
             dma.icmp_sender_ip[2], dma.icmp_sender_ip[3]);
      return dma.icmp_replies == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc >= 2 &&
      (strcmp(argv[1], "dma") == 0 || strcmp(argv[1], "frame") == 0 ||
       strcmp(argv[1], "tx") == 0 || strcmp(argv[1], "irq") == 0))
    {
      unsigned long watch_ms = 1000;
      unsigned long elapsed = 0;

      tx_only = strcmp(argv[1], "tx") == 0;
      send_frame = strcmp(argv[1], "frame") == 0 || tx_only;
      enable_irq = strcmp(argv[1], "irq") == 0;

      if (argc == 3)
        {
          watch_ms = strtoul(argv[2], NULL, 0);
        }
      else if (argc > 3)
        {
          printf("usage: fec_probe [rdc|dma|tx|frame|irq [watch_ms]]\n");
          return EXIT_FAILURE;
        }

      ret = tx_only ? ok8mp_fec_dma_initialize_tx(&dma) :
                      ok8mp_fec_dma_initialize(&dma);
      if (ret < 0)
        {
          printf("fec_probe: DMA initialize failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("fec_probe: DMA %s: 4 RX x 1536, 2 TX x 1536\n",
             tx_only ? "TX-only armed" : "RX/TX armed");
      printf("fec_probe: RDSR=0x%08lx TDSR=0x%08lx\n",
             (unsigned long)dma.rx_ring, (unsigned long)dma.tx_ring);
      printf("fec_probe: RXBUF=0x%08lx TXBUF=0x%08lx EIMR=0x%08lx\n",
             (unsigned long)dma.rx_buffer, (unsigned long)dma.tx_buffer,
             (unsigned long)dma.eimr);
      printf("fec_probe: RDC domain=%lu M7=0x%08lx TX=0x%08lx RX=0x%08lx "
             "OCRAM=0x%08lx\n",
             (unsigned long)dma.rdc_domain,
             (unsigned long)dma.rdc_m7_mda,
             (unsigned long)dma.rdc_enet_tx_mda,
             (unsigned long)dma.rdc_enet_rx_mda,
             (unsigned long)dma.rdc_ocram_mrc);
      printf("fec_probe: IRQ count=%lu last=%lu ERROR_AXI=%lu\n",
             (unsigned long)dma.irq_count,
             (unsigned long)dma.last_irq,
             (unsigned long)dma.error_axi_count);
      printf("fec_probe: MAC=f2:10:26:ab:52:2d DMA polling mode\n");

      if (enable_irq)
        {
          ret = ok8mp_fec_dma_enable_interrupts(&dma);
          if (ret < 0)
            {
              printf("fec_probe: IRQ enable failed: %d\n", ret);
              return EXIT_FAILURE;
            }

          printf("fec_probe: RX/TX interrupts enabled\n");
        }

      if (send_frame)
        {
          ret = ok8mp_fec_dma_send_test(&dma);
          printf("fec_probe: TX test frame (%u bytes) %s, BD=0x%04lx\n",
                 (unsigned int)dma.tx_length,
                 ret == 0 ? "complete" : "timeout",
                 (unsigned long)dma.tx_control);
          if (ret < 0)
            {
              return EXIT_FAILURE;
            }
        }

      if (tx_only)
        {
          usleep(watch_ms * 1000);
          ok8mp_fec_dma_status(&dma);
          printf("fec_probe: TX-only after %lums EIR=0x%08lx IRQ=%lu "
                 "last=%lu ERROR_AXI=%lu\n",
                 watch_ms, (unsigned long)dma.eir,
                 (unsigned long)dma.irq_count,
                 (unsigned long)dma.last_irq,
                 (unsigned long)dma.error_axi_count);
          return EXIT_SUCCESS;
        }

      while (elapsed < watch_ms)
        {
          ret = ok8mp_fec_dma_reclaim(&dma);
          if (ret < 0)
            {
              printf("fec_probe: RX reclaim failed: %d\n", ret);
              return EXIT_FAILURE;
            }

          usleep(10000);
          elapsed += 10;
        }

      ok8mp_fec_dma_status(&dma);
      printf("fec_probe: after %lums RX=%lu frames/%lu bytes EIR=0x%08lx "
             "IRQ-events=0x%08lx IRQ=%lu last=%lu ERROR_AXI=%lu\n",
             watch_ms, (unsigned long)dma.rx_frames,
             (unsigned long)dma.rx_bytes, (unsigned long)dma.eir,
             (unsigned long)dma.events,
             (unsigned long)dma.irq_count,
             (unsigned long)dma.last_irq,
             (unsigned long)dma.error_axi_count);
      return EXIT_SUCCESS;
    }

  ret = ok8mp_fec_phy_probe(&phy);
  if (ret < 0)
    {
      printf("fec_probe: MDIO transfer failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("fec_probe: PHY addr=1\n");
  printf("fec_probe: id1=0x%04x id2=0x%04x\n", phy.id1, phy.id2);
  printf("fec_probe: bmsr=0x%04x yt8521_status=0x%04x\n",
         phy.bmsr, phy.status);
  printf("fec_probe: link=%s speed=%s duplex=%s\n",
         (phy.status & YT8521_STATUS_LINK) != 0 ? "up" : "down",
         fec_speed(phy.status),
         (phy.status & YT8521_STATUS_DUPLEX) != 0 ? "full" : "half");
  return EXIT_SUCCESS;
}
