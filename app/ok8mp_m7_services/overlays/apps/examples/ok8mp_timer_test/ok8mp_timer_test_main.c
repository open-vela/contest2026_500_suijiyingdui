/****************************************************************************
 * apps/examples/ok8mp_timer_test/ok8mp_timer_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void usage(const char *progname)
{
  printf("Usage: %s [tick_count] [interval_ms]\n", progname);
  printf("  tick_count  default 10\n");
  printf("  interval_ms default 1000\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  unsigned int count = 10;
  unsigned int interval_ms = 1000;
  unsigned int i;

  if (argc > 1)
    {
      count = strtoul(argv[1], NULL, 0);
    }

  if (argc > 2)
    {
      interval_ms = strtoul(argv[2], NULL, 0);
    }

  if (argc > 3 || count == 0 || interval_ms == 0)
    {
      usage(argv[0]);
      return EXIT_FAILURE;
    }

  printf("timer_test: count=%u interval=%u ms\n", count, interval_ms);

  for (i = 0; i < count; i++)
    {
      struct timespec ts;

      clock_gettime(CLOCK_MONOTONIC, &ts);
      printf("timer_test: tick %u time=%ld.%09ld\n",
             i + 1, (long)ts.tv_sec, (long)ts.tv_nsec);

      usleep(interval_ms * 1000);
    }

  printf("timer_test: done\n");
  return EXIT_SUCCESS;
}
