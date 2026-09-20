/****************************************************************************
 * apps/examples/ok8mp_can_diagnostic/ok8mp_can_diagnostic_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * One-shot project-defined CAN diagnostic for the OK8MP M7 demonstration.
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
 * Pre-processor Definitions
 ****************************************************************************/

/* This is intentionally a small project test protocol, not an ISO-TP/UDS
 * implementation:
 *
 *   M7  -> ECU, ID 0x700, DLC 3: A5 01 <token>
 *   ECU -> M7,  ID 0x708, DLC 4: 5A 01 <token> 01
 *
 * 0xa5/0x5a identify the two directions, 0x01 is the health-probe service,
 * and the final response byte says that the simulated ECU is healthy.
 */

#define CAN_DIAG_REQUEST_MAGIC  0xa5
#define CAN_DIAG_RESPONSE_MAGIC 0x5a
#define CAN_DIAG_SERVICE_HEALTH 0x01
#define CAN_DIAG_ECU_NORMAL     0x01
#define CAN_DIAG_TOKEN          0x5a
/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t can_diag_now_ms(void)
{
  return (uint32_t)TICK2MSEC(clock_systime_ticks());
}

static bool can_diag_is_standard(FAR const struct can_msg_s *msg)
{
#ifdef CONFIG_CAN_EXTID
  if (msg->cm_hdr.ch_extid)
    {
      return false;
    }
#endif

  return true;
}

static void can_diag_print_result(bool ok, FAR const char *state,
                                  uint32_t elapsed_ms,
                                  FAR const struct can_msg_s *response)
{
  if (ok)
    {
      printf("can_diagnostic: bus_normal response=0x%03lx latency=%lu ms\n",
             (unsigned long)response->cm_hdr.ch_id,
             (unsigned long)elapsed_ms);
    }
  else if (strcmp(state, "timeout") == 0)
    {
      printf("can_diagnostic: ecu_no_response timeout=%lu ms\n",
             (unsigned long)elapsed_ms);
    }
  else
    {
      printf("can_diagnostic: response_abnormal response=0x%03lx\n",
             response == NULL ? 0ul :
             (unsigned long)response->cm_hdr.ch_id);
    }

  printf("{\"ok\":%s,\"state\":\"%s\","
         "\"request_id\":\"0x%03x\",\"response_id\":\"0x%03x\","
         "\"latency_ms\":%lu}\n",
         ok ? "true" : "false", state,
         CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_REQUEST_ID,
         CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_RESPONSE_ID,
         (unsigned long)elapsed_ms);
}

static int can_diag_run(void)
{
  ok8mp_can_subscription_t subscription;
  struct can_msg_s request = {0};
  struct can_msg_s response = {0};
  uint32_t elapsed;
  uint32_t remaining;
  uint32_t start;
  int result;
  int ret = EXIT_FAILURE;

  result = ok8mp_can_service_subscribe(
             CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_RESPONSE_ID,
             0x7ff, false, &subscription);
  if (result < 0)
    {
      printf("can_diagnostic: subscribe failed: %d\n", -result);
      return EXIT_FAILURE;
    }

  request.cm_hdr.ch_id = CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_REQUEST_ID;
  request.cm_hdr.ch_rtr = false;
  request.cm_hdr.ch_dlc = can_bytes2dlc(3);
#ifdef CONFIG_CAN_EXTID
  request.cm_hdr.ch_extid = false;
#endif
  request.cm_data[0] = CAN_DIAG_REQUEST_MAGIC;
  request.cm_data[1] = CAN_DIAG_SERVICE_HEALTH;
  request.cm_data[2] = CAN_DIAG_TOKEN;

  result = ok8mp_can_service_send(&request, 200);
  if (result < 0)
    {
      printf("can_diagnostic: request send failed: %d\n", -result);
      can_diag_print_result(false, "timeout", 0, NULL);
      goto out_unsubscribe;
    }

  printf("can_diagnostic: request id=0x%03x data=A5 01 %02X\n",
         CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_REQUEST_ID,
         CAN_DIAG_TOKEN);
  start = can_diag_now_ms();

  while (can_diag_now_ms() - start <
         CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_TIMEOUT_MS)
    {
      elapsed = can_diag_now_ms() - start;
      remaining = CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_TIMEOUT_MS -
                  elapsed;
      result = ok8mp_can_service_receive(subscription, &response,
                                          remaining);
      if (result < 0)
        {
          if (result == -ETIMEDOUT || result == -EAGAIN)
            {
              break;
            }

          printf("can_diagnostic: response receive failed: %d\n",
                 -result);
          goto out_unsubscribe;
        }

      if (!can_diag_is_standard(&response) ||
          response.cm_hdr.ch_id !=
          CONFIG_EXAMPLES_OK8MP_CAN_DIAGNOSTIC_RESPONSE_ID)
        {
          continue;
        }

      if (can_dlc2bytes(response.cm_hdr.ch_dlc) != 4 ||
          response.cm_data[0] != CAN_DIAG_RESPONSE_MAGIC ||
          response.cm_data[1] != CAN_DIAG_SERVICE_HEALTH ||
          response.cm_data[2] != CAN_DIAG_TOKEN ||
          response.cm_data[3] != CAN_DIAG_ECU_NORMAL)
        {
          can_diag_print_result(false, "response_error",
                                can_diag_now_ms() - start, &response);
          goto out_unsubscribe;
        }

      can_diag_print_result(true, "healthy", can_diag_now_ms() - start,
                            &response);
      ret = EXIT_SUCCESS;
      goto out_unsubscribe;
    }

  can_diag_print_result(false, "timeout", can_diag_now_ms() - start, NULL);

out_unsubscribe:
  ok8mp_can_service_unsubscribe(subscription);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  if (argc != 1)
    {
      printf("Usage: %s\n", argv[0]);
      return EXIT_FAILURE;
    }

  return can_diag_run();
}
