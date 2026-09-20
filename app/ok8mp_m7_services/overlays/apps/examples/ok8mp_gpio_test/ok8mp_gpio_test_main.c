/****************************************************************************
 * apps/examples/ok8mp_gpio_test/ok8mp_gpio_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <nuttx/board.h>
#include <arch/board/board.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void usage(const char *progname)
{
  printf("Usage: %s [loop_count] [interval_ms]\n", progname);
  printf("  loop_count  default 8\n");
  printf("  interval_ms default 250\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  unsigned int count = 8;
  unsigned int interval_ms = 250;
  uint32_t nleds;
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

  nleds = board_userled_initialize();
  printf("gpio_test: initialized %lu user LED GPIO(s)\n",
         (unsigned long)nleds);

  if (nleds == 0)
    {
      printf("gpio_test: no board LEDs are configured\n");
      return EXIT_FAILURE;
    }

  for (i = 0; i < count; i++)
    {
      int led = i % nleds;

      printf("gpio_test: LED%d ON\n", led);
      board_userled(led, true);
      usleep(interval_ms * 1000);

      printf("gpio_test: LED%d OFF\n", led);
      board_userled(led, false);
      usleep(interval_ms * 1000);
    }

  board_userled_all(0);
  printf("gpio_test: done\n");
  return EXIT_SUCCESS;
}
