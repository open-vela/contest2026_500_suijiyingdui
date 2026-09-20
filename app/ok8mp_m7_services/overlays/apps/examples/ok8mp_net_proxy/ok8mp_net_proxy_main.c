/****************************************************************************
 * apps/examples/ok8mp_net_proxy/ok8mp_net_proxy_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_EXAMPLES_OK8MP_NET_PROXY_TIMEOUT_MS
#  define CONFIG_EXAMPLES_OK8MP_NET_PROXY_TIMEOUT_MS 10000
#endif

#define NET_PROXY_REQ_PREFIX  "@@NETREQ "
#define NET_PROXY_RESP_PREFIX "@@NETRESP "
#define OK8MP_FEC_DIAG_REV    4

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s ping [timeout_ms]\n", progname);
  printf("  %s http <url> [timeout_ms]\n", progname);
  printf("  %s llm <prompt...>\n", progname);
  printf("  %s raw <json_payload> [timeout_ms]\n", progname);
  printf("  %s diag\n", progname);
}

static unsigned long make_request_id(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    {
      return 1;
    }

  return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void json_put_escaped(FAR const char *src)
{
  FAR const unsigned char *p = (FAR const unsigned char *)src;

  while (*p != '\0')
    {
      switch (*p)
        {
          case '\\':
            printf("\\\\");
            break;

          case '"':
            printf("\\\"");
            break;

          case '\n':
            printf("\\n");
            break;

          case '\r':
            printf("\\r");
            break;

          case '\t':
            printf("\\t");
            break;

          default:
            if (*p < 0x20)
              {
                printf("\\u%04x", *p);
              }
            else
              {
                putchar(*p);
              }
            break;
        }

      p++;
    }
}

static void print_request_header(unsigned long id, FAR const char *op)
{
  printf(NET_PROXY_REQ_PREFIX "{\"id\":%lu,\"op\":\"%s\"", id, op);
}

static int wait_response(int timeout_ms)
{
  struct pollfd pfd;
  char line[768];
  int ret;

  pfd.fd = STDIN_FILENO;
  pfd.events = POLLIN;
  pfd.revents = 0;

  printf("net_proxy: waiting response, timeout=%d ms\n", timeout_ms);
  fflush(stdout);

  ret = poll(&pfd, 1, timeout_ms);
  if (ret < 0)
    {
      printf("net_proxy: poll failed: %d\n", errno);
      return EXIT_FAILURE;
    }

  if (ret == 0)
    {
      printf("net_proxy: response timeout\n");
      return EXIT_FAILURE;
    }

  if (fgets(line, sizeof(line), stdin) == NULL)
    {
      printf("net_proxy: failed to read response\n");
      return EXIT_FAILURE;
    }

  if (strncmp(line, NET_PROXY_RESP_PREFIX,
              strlen(NET_PROXY_RESP_PREFIX)) == 0)
    {
      printf("net_proxy: response %s", line + strlen(NET_PROXY_RESP_PREFIX));
    }
  else
    {
      printf("net_proxy: unexpected line %s", line);
    }

  return EXIT_SUCCESS;
}

static int emit_ping(unsigned long id)
{
  print_request_header(id, "ping");
  printf("}\n");
  fflush(stdout);
  return OK;
}

static int emit_http(unsigned long id, FAR const char *url)
{
  print_request_header(id, "http_get");
  printf(",\"url\":\"");
  json_put_escaped(url);
  printf("\"}\n");
  fflush(stdout);
  return OK;
}

static int emit_llm(unsigned long id, int argc, FAR char *argv[],
                    int first_prompt_arg)
{
  int i;

  print_request_header(id, "llm_chat");
  printf(",\"prompt\":\"");

  for (i = first_prompt_arg; i < argc; i++)
    {
      if (i > first_prompt_arg)
        {
          putchar(' ');
        }

      json_put_escaped(argv[i]);
    }

  printf("\"}\n");
  fflush(stdout);
  return OK;
}

static int emit_raw(FAR const char *json)
{
  printf(NET_PROXY_REQ_PREFIX "%s\n", json);
  fflush(stdout);
  return OK;
}

static void print_ipv4(uint32_t network_addr)
{
  uint32_t host_addr = ntohl(network_addr);

  printf("%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32,
         (host_addr >> 24) & 0xff,
         (host_addr >> 16) & 0xff,
         (host_addr >> 8) & 0xff,
         host_addr & 0xff);
}

static int show_fec_diag(void)
{
  struct ok8mp_fec_net_diag_s diag;
  int ret;

  ret = ok8mp_fec_net_diag(&diag);
  if (ret < 0)
    {
      printf("net_proxy: fec diag failed: %d\n", -ret);
      return EXIT_FAILURE;
    }

  printf("fec: registered=%d ifup=%d local_target=%d flags=0x%08" PRIx32 "\n",
         diag.registered, diag.ifup, diag.local_target, diag.flags);
  printf("fec: diag_rev=%d\n", OK8MP_FEC_DIAG_REV);
  printf("fec: ip=");
  print_ipv4(diag.ipaddr);
  printf(" mask=");
  print_ipv4(diag.netmask);
  printf(" gateway=");
  print_ipv4(diag.draddr);
  printf("\n");
  printf("fec: poll=%" PRIu32 " txpoll=%" PRIu32
         " tx=%" PRIu32 " arp=%" PRIu32 " ipv4=%" PRIu32
         " busy=%" PRIu32 " errors=%" PRIu32 "\n",
         diag.poll_cycles, diag.txpoll_calls, diag.tx_attempts,
         diag.tx_arp, diag.tx_ipv4, diag.tx_busy, diag.tx_errors);
  printf("fec: txbd0=0x%04" PRIx32 "/%" PRIu32
         " txbd1=0x%04" PRIx32 "/%" PRIu32
         " eir=0x%08" PRIx32 " tdar=0x%08" PRIx32 "\n",
         diag.tx0_control, diag.tx0_length,
         diag.tx1_control, diag.tx1_length, diag.eir, diag.tdar);
  printf("fec: ecr=0x%08" PRIx32 " rdsr=0x%08" PRIx32
         " tdsr=0x%08" PRIx32 "\n",
         diag.ecr, diag.rdsr, diag.tdsr);
  printf("fec: tx0hdr=");
  for (unsigned int i = 0; i < sizeof(diag.tx0_header); i++)
    {
      printf("%02x%s", diag.tx0_header[i],
             i + 1 == sizeof(diag.tx0_header) ? "\n" : ":");
    }
  return EXIT_SUCCESS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  unsigned long id = make_request_id();
  int timeout_ms = CONFIG_EXAMPLES_OK8MP_NET_PROXY_TIMEOUT_MS;
  int ret;

  if (argc < 2)
    {
      usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "diag") == 0)
    {
      return show_fec_diag();
    }
  else if (strcmp(argv[1], "ping") == 0)
    {
      if (argc > 2)
        {
          timeout_ms = strtol(argv[2], NULL, 0);
        }

      ret = emit_ping(id);
    }
  else if (strcmp(argv[1], "http") == 0)
    {
      if (argc < 3)
        {
          usage(argv[0]);
          return EXIT_FAILURE;
        }

      if (argc > 3)
        {
          timeout_ms = strtol(argv[3], NULL, 0);
        }

      ret = emit_http(id, argv[2]);
    }
  else if (strcmp(argv[1], "llm") == 0)
    {
      if (argc < 3)
        {
          usage(argv[0]);
          return EXIT_FAILURE;
        }

      ret = emit_llm(id, argc, argv, 2);
    }
  else if (strcmp(argv[1], "raw") == 0)
    {
      if (argc < 3)
        {
          usage(argv[0]);
          return EXIT_FAILURE;
        }

      if (argc > 3)
        {
          timeout_ms = strtol(argv[3], NULL, 0);
        }

      ret = emit_raw(argv[2]);
    }
  else
    {
      usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  return wait_response(timeout_ms);
}
