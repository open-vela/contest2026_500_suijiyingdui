/****************************************************************************
 * apps/examples/ok8mp_can_monitor/ok8mp_can_monitor_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Persistent CAN health monitor for the OK8MP Cortex-M7 CAN demonstration.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/can/can.h>
#include <nuttx/clock.h>

#include <ok8mp_can_service.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum can_monitor_state_e
{
  CAN_MONITOR_STOPPED = 0,
  CAN_MONITOR_WAITING,
  CAN_MONITOR_HEALTHY,
  CAN_MONITOR_TIMEOUT,
  CAN_MONITOR_PERIOD_ERROR,
  CAN_MONITOR_COUNTER_ERROR,
  CAN_MONITOR_MALFORMED,
  CAN_MONITOR_IO_ERROR
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile bool g_running;
static volatile bool g_stop;
static volatile enum can_monitor_state_e g_state = CAN_MONITOR_STOPPED;
static volatile uint32_t g_started_ms;
static volatile uint32_t g_last_rx_ms;
static volatile uint32_t g_last_period_ms;
static volatile uint16_t g_last_counter;
static volatile bool g_have_timing;
static volatile bool g_have_counter;
static volatile bool g_startup_sync_pending;
static volatile unsigned long g_frame_count;
static volatile unsigned long g_ignored_count;
static volatile unsigned long g_period_errors;
static volatile unsigned long g_counter_errors;
static volatile unsigned long g_malformed_errors;
static volatile unsigned long g_io_errors;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t can_monitor_now_ms(void)
{
  return (uint32_t)TICK2MSEC(clock_systime_ticks());
}

static FAR const char *can_monitor_state_name(enum can_monitor_state_e state)
{
  switch (state)
    {
      case CAN_MONITOR_WAITING:
        return "waiting";

      case CAN_MONITOR_HEALTHY:
        return "healthy";

      case CAN_MONITOR_TIMEOUT:
        return "timeout";

      case CAN_MONITOR_PERIOD_ERROR:
        return "period_error";

      case CAN_MONITOR_COUNTER_ERROR:
        return "counter_error";

      case CAN_MONITOR_MALFORMED:
        return "malformed";

      case CAN_MONITOR_IO_ERROR:
        return "io_error";

      case CAN_MONITOR_STOPPED:
      default:
        return "stopped";
    }
}

/* The ECU transmits periodically, so only state changes should reach the
 * interactive console.  This keeps NSH usable during continuous monitoring.
 */

static bool can_monitor_set_state(enum can_monitor_state_e state)
{
  if (g_state == state)
    {
      return false;
    }

  g_state = state;
  return true;
}

static uint32_t can_monitor_age_ms(uint32_t now)
{
  if (g_frame_count == 0)
    {
      return now - g_started_ms;
    }

  return now - g_last_rx_ms;
}

static void can_monitor_check_timeout(uint32_t now)
{
  if (g_running &&
      can_monitor_age_ms(now) > CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_TIMEOUT_MS)
    {
      if (can_monitor_set_state(CAN_MONITOR_TIMEOUT))
        {
          /* The next received frame establishes a new baseline.  Its
           * interval includes the outage, so it must not be classified as
           * a second period or counter fault. */

          g_have_timing = false;
          g_have_counter = false;
          printf("can_monitor: state=timeout age=%lu ms\n",
                 (unsigned long)can_monitor_age_ms(now));
        }
    }
}

static bool can_monitor_expected_frame(FAR const struct can_msg_s *msg)
{
#ifdef CONFIG_CAN_EXTID
  if (msg->cm_hdr.ch_extid)
    {
      return false;
    }
#endif

  return msg->cm_hdr.ch_id ==
         CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_ID;
}

static bool can_monitor_period_valid(uint32_t period_ms)
{
  uint32_t expected = CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_PERIOD_MS;
  uint32_t tolerance = CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_PERIOD_TOLERANCE_MS;
  uint32_t lower = expected > tolerance ? expected - tolerance : 0;
  uint32_t upper = expected + tolerance;

  return period_ms >= lower && period_ms <= upper;
}

static bool can_monitor_period_too_short(uint32_t period_ms)
{
  uint32_t expected = CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_PERIOD_MS;
  uint32_t tolerance = CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_PERIOD_TOLERANCE_MS;
  uint32_t lower = expected > tolerance ? expected - tolerance : 0;

  return period_ms < lower;
}

static void can_monitor_process(FAR const struct can_msg_s *msg,
                                uint32_t now)
{
  uint16_t counter;
  uint16_t expected_counter = 0;
  bool period_ok = true;
  bool counter_ok = true;

  if (!can_monitor_expected_frame(msg))
    {
      g_ignored_count++;
      return;
    }

  if (msg->cm_hdr.ch_dlc != 8)
    {
      g_malformed_errors++;
      if (can_monitor_set_state(CAN_MONITOR_MALFORMED))
        {
          printf("can_monitor: state=malformed id=0x%03lx dlc=%u\n",
                 (unsigned long)msg->cm_hdr.ch_id, msg->cm_hdr.ch_dlc);
        }
      return;
    }

  counter = (uint16_t)msg->cm_data[1] |
            ((uint16_t)msg->cm_data[2] << 8);

  /* The generic character device can retain frames received before this
   * monitor was launched.  Synchronize until a plausible ECU period is
   * observed; timestamps from a queued burst do not represent the ECU's
   * actual transmission period. */

  if (g_startup_sync_pending)
    {
      if (!g_have_timing ||
          can_monitor_period_too_short(now - g_last_rx_ms))
        {
          g_last_counter = counter;
          g_have_timing = true;
          g_have_counter = true;
          g_last_rx_ms = now;
          g_last_period_ms = 0;
          g_frame_count++;
          return;
        }

      g_startup_sync_pending = false;
    }

  if (g_have_timing)
    {
      g_last_period_ms = now - g_last_rx_ms;
      period_ok = can_monitor_period_valid(g_last_period_ms);
      if (!period_ok)
        {
          g_period_errors++;
        }
    }

  if (g_have_counter)
    {
      expected_counter = (uint16_t)(g_last_counter + 1);
      if (counter != expected_counter)
        {
          g_counter_errors++;
          counter_ok = false;
        }
    }

  g_last_counter = counter;
  g_have_timing = true;
  g_have_counter = true;
  g_last_rx_ms = now;
  g_frame_count++;

  if (!period_ok)
    {
      if (can_monitor_set_state(CAN_MONITOR_PERIOD_ERROR))
        {
          printf("can_monitor: state=period_error id=0x%03lx period=%lu ms\n",
                 (unsigned long)msg->cm_hdr.ch_id,
                 (unsigned long)g_last_period_ms);
        }
    }
  else if (!counter_ok)
    {
      if (can_monitor_set_state(CAN_MONITOR_COUNTER_ERROR))
        {
          printf("can_monitor: state=counter_error id=0x%03lx got=%u expected=%u\n",
                 (unsigned long)msg->cm_hdr.ch_id, counter,
                 expected_counter);
        }
    }
  else
    {
      if (can_monitor_set_state(CAN_MONITOR_HEALTHY))
        {
          printf("can_monitor: state=healthy id=0x%03lx seq=%u period=%lu ms\n",
                 (unsigned long)msg->cm_hdr.ch_id, counter,
                 (unsigned long)g_last_period_ms);
        }
    }
}

static int can_monitor_daemon(int argc, FAR char *argv[])
{
  ok8mp_can_subscription_t subscription;
  struct can_msg_s msg;
  uint32_t now;
  int ret;

  (void)argc;
  (void)argv;

  ret = ok8mp_can_service_subscribe(
          CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_ID, 0x7ff, false,
          &subscription);
  if (ret < 0)
    {
      printf("can_monitor: subscribe failed: %d\n", -ret);
      g_state = CAN_MONITOR_IO_ERROR;
      g_running = false;
      return EXIT_FAILURE;
    }

  g_started_ms = can_monitor_now_ms();
  g_last_rx_ms = g_started_ms;
  g_last_period_ms = 0;
  g_last_counter = 0;
  g_have_timing = false;
  g_have_counter = false;
  g_startup_sync_pending = true;
  g_frame_count = 0;
  g_ignored_count = 0;
  g_period_errors = 0;
  g_counter_errors = 0;
  g_malformed_errors = 0;
  g_io_errors = 0;
  g_state = CAN_MONITOR_WAITING;

  printf("can_monitor: watching %s id=0x%03x every %d+-%d ms timeout=%d ms\n",
         CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_DEVPATH,
         CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_ID,
         CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_PERIOD_MS,
         CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_PERIOD_TOLERANCE_MS,
         CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_TIMEOUT_MS);

  while (!g_stop)
    {
      now = can_monitor_now_ms();
      can_monitor_check_timeout(now);

      ret = ok8mp_can_service_receive(
              subscription, &msg,
              CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_POLL_MS);
      if (ret == OK)
        {
          can_monitor_process(&msg, can_monitor_now_ms());
        }
      else if (ret != -ETIMEDOUT && ret != -EAGAIN)
        {
          g_io_errors++;
          if (can_monitor_set_state(CAN_MONITOR_IO_ERROR))
            {
              printf("can_monitor: state=io_error receive=%d\n", -ret);
            }
        }
    }

  ok8mp_can_service_unsubscribe(subscription);
  g_running = false;
  g_stop = false;
  g_state = CAN_MONITOR_STOPPED;
  printf("can_monitor: stopped\n");
  return EXIT_SUCCESS;
}

static void can_monitor_print_status(bool json)
{
  uint32_t now = can_monitor_now_ms();
  uint32_t age = can_monitor_age_ms(now);
  bool healthy = g_running && g_state == CAN_MONITOR_HEALTHY;

  if (json)
    {
      printf("{\"ok\":%s,\"running\":%s,\"state\":\"%s\","
             "\"expected_id\":\"0x%03x\",\"frames\":%lu,"
             "\"ignored\":%lu,\"last_period_ms\":%lu,"
             "\"age_ms\":%lu,\"counter\":%u,"
             "\"period_errors\":%lu,\"counter_errors\":%lu,"
             "\"malformed_errors\":%lu,\"io_errors\":%lu}\n",
             healthy ? "true" : "false", g_running ? "true" : "false",
             can_monitor_state_name(g_state),
             CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_ID, g_frame_count,
             g_ignored_count, (unsigned long)g_last_period_ms,
             (unsigned long)age, g_last_counter, g_period_errors,
             g_counter_errors, g_malformed_errors, g_io_errors);
    }
  else
    {
      printf("can_monitor: running=%u state=%s id=0x%03x frames=%lu "
             "ignored=%lu period=%lu ms age=%lu ms seq=%u "
             "errors(period=%lu counter=%lu malformed=%lu io=%lu)\n",
             g_running, can_monitor_state_name(g_state),
             CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_EXPECTED_ID, g_frame_count,
             g_ignored_count, (unsigned long)g_last_period_ms,
             (unsigned long)age, g_last_counter, g_period_errors,
             g_counter_errors, g_malformed_errors, g_io_errors);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int pid;
  int retry;

  if (argc != 2)
    {
      goto usage;
    }

  if (strcmp(argv[1], "start") == 0)
    {
      if (g_running)
        {
          printf("can_monitor: already running\n");
          return EXIT_SUCCESS;
        }

      g_stop = false;
      g_running = true;
      pid = task_create("can_monitor",
                        CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_PRIORITY,
                        CONFIG_EXAMPLES_OK8MP_CAN_MONITOR_STACKSIZE,
                        can_monitor_daemon, NULL);
      if (pid < 0)
        {
          g_running = false;
          printf("can_monitor: task_create failed: %d\n", errno);
          return EXIT_FAILURE;
        }

      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "stop") == 0)
    {
      if (!g_running)
        {
          printf("can_monitor: not running\n");
          return EXIT_SUCCESS;
        }

      g_stop = true;
      for (retry = 0; retry < 20 && g_running; retry++)
        {
          usleep(100000);
        }

      return g_running ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      can_monitor_print_status(false);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "health") == 0)
    {
      can_monitor_print_status(true);
      return EXIT_SUCCESS;
    }

usage:
  printf("Usage: %s start|stop|status|health\n", argv[0]);
  return EXIT_FAILURE;
}
