/****************************************************************************
 * apps/include/ok8mp_can_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_INCLUDE_OK8MP_CAN_SERVICE_H
#define __APPS_INCLUDE_OK8MP_CAN_SERVICE_H

#include <nuttx/config.h>
#include <nuttx/can/can.h>
#include <nuttx/can/ok8mp_flexcan.h>

#include <stdbool.h>
#include <stdint.h>

typedef int ok8mp_can_subscription_t;

struct ok8mp_can_service_stats_s
{
  struct ok8mp_can_status_s controller;
  uint32_t rx_frames;
  uint32_t tx_submitted;
  uint32_t tx_timeouts;
  uint32_t rx_dropped;
  uint32_t read_errors;
  uint32_t auto_recoveries;
  int last_error;
  bool running;
};

int ok8mp_can_service_start(void);
int ok8mp_can_service_stop(uint32_t timeout_ms);
bool ok8mp_can_service_is_running(void);

int ok8mp_can_service_subscribe(uint32_t id, uint32_t mask,
                                bool extended,
                                FAR ok8mp_can_subscription_t *subscription);
int ok8mp_can_service_unsubscribe(ok8mp_can_subscription_t subscription);
int ok8mp_can_service_receive(ok8mp_can_subscription_t subscription,
                              FAR struct can_msg_s *msg,
                              uint32_t timeout_ms);
int ok8mp_can_service_send(FAR const struct can_msg_s *msg,
                           uint32_t timeout_ms);
int ok8mp_can_service_get_stats(FAR struct ok8mp_can_service_stats_s *stats);
int ok8mp_can_service_recover(void);

#endif /* __APPS_INCLUDE_OK8MP_CAN_SERVICE_H */
