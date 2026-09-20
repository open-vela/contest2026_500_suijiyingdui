/****************************************************************************
 * apps/examples/ok8mp_can_service/ok8mp_can_isotp.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ISO 15765-2 transport over the single-owner OK8MP CAN service.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/can/can.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>

#include <ok8mp_can_isotp.h>
#include <ok8mp_can_service.h>

#define ISOTP_PCI_TYPE_MASK       0xf0
#define ISOTP_PCI_SINGLE_FRAME    0x00
#define ISOTP_PCI_FIRST_FRAME     0x10
#define ISOTP_PCI_CONSECUTIVE     0x20
#define ISOTP_PCI_FLOW_CONTROL    0x30
#define ISOTP_FLOW_STATUS_MASK    0x0f
#define ISOTP_FLOW_CTS            0x00
#define ISOTP_FLOW_WAIT           0x01
#define ISOTP_FLOW_OVERFLOW       0x02
#define ISOTP_MAX_WAIT_FRAMES     3
#define ISOTP_CLASSIC_SF_DATA     7
#define ISOTP_CLASSIC_FF_DATA     6
#define ISOTP_CLASSIC_CF_DATA     7
#define ISOTP_FRAME_LENGTH        8

static mutex_t g_isotp_lock = NXMUTEX_INITIALIZER;

static uint32_t isotp_now_ms(void)
{
  return (uint32_t)TICK2MSEC(clock_systime_ticks());
}

static int isotp_remaining(uint32_t start, uint32_t timeout_ms,
                           FAR uint32_t *remaining)
{
  uint32_t elapsed = isotp_now_ms() - start;

  if (elapsed >= timeout_ms)
    {
      return -ETIMEDOUT;
    }

  *remaining = timeout_ms - elapsed;
  return OK;
}

static void isotp_init_frame(FAR struct can_msg_s *msg, uint32_t id,
                             bool extended)
{
  memset(msg, 0, sizeof(*msg));
  msg->cm_hdr.ch_id = id;
  msg->cm_hdr.ch_rtr = false;
  msg->cm_hdr.ch_dlc = can_bytes2dlc(ISOTP_FRAME_LENGTH);
#ifdef CONFIG_CAN_EXTID
  msg->cm_hdr.ch_extid = extended;
#else
  (void)extended;
#endif
}

static int isotp_send_frame(uint32_t id, bool extended,
                            FAR const uint8_t data[ISOTP_FRAME_LENGTH],
                            uint32_t start, uint32_t timeout_ms)
{
  struct can_msg_s msg;
  uint32_t remaining;
  int ret;

  ret = isotp_remaining(start, timeout_ms, &remaining);
  if (ret < 0)
    {
      return ret;
    }

  isotp_init_frame(&msg, id, extended);
  memcpy(msg.cm_data, data, ISOTP_FRAME_LENGTH);
  return ok8mp_can_service_send(&msg, remaining);
}

static int isotp_receive_frame(ok8mp_can_subscription_t subscription,
                               FAR struct can_msg_s *msg,
                               uint32_t start, uint32_t timeout_ms)
{
  uint32_t remaining;
  int ret;

  ret = isotp_remaining(start, timeout_ms, &remaining);
  if (ret < 0)
    {
      return ret;
    }

  return ok8mp_can_service_receive(subscription, msg, remaining);
}

static uint32_t isotp_stmin_us(uint8_t stmin)
{
  if (stmin <= 0x7f)
    {
      return (uint32_t)stmin * 1000;
    }

  if (stmin >= 0xf1 && stmin <= 0xf9)
    {
      return (uint32_t)(stmin - 0xf0) * 100;
    }

  return 0;
}

static int isotp_wait_flow_control(ok8mp_can_subscription_t subscription,
                                   FAR uint8_t *block_size,
                                   FAR uint8_t *stmin,
                                   uint32_t start, uint32_t timeout_ms)
{
  struct can_msg_s msg;
  unsigned int waits = 0;
  uint8_t flow_status;
  int ret;

  for (;;)
    {
      ret = isotp_receive_frame(subscription, &msg, start, timeout_ms);
      if (ret < 0)
        {
          return ret;
        }

      if (can_dlc2bytes(msg.cm_hdr.ch_dlc) < 3)
        {
          continue;
        }

      if ((msg.cm_data[0] & ISOTP_PCI_TYPE_MASK) !=
          ISOTP_PCI_FLOW_CONTROL)
        {
          continue;
        }

      flow_status = msg.cm_data[0] & ISOTP_FLOW_STATUS_MASK;
      if (flow_status == ISOTP_FLOW_CTS)
        {
          *block_size = msg.cm_data[1];
          *stmin = msg.cm_data[2];
          return OK;
        }

      if (flow_status == ISOTP_FLOW_OVERFLOW)
        {
          return -EMSGSIZE;
        }

      if (flow_status != ISOTP_FLOW_WAIT || ++waits > ISOTP_MAX_WAIT_FRAMES)
        {
          return -EPROTO;
        }
    }
}

static int isotp_send_payload(uint32_t id, bool extended,
                              ok8mp_can_subscription_t subscription,
                              FAR const uint8_t *payload, size_t length,
                              uint32_t start, uint32_t timeout_ms)
{
  uint8_t frame[ISOTP_FRAME_LENGTH] = {0};
  uint8_t block_size;
  uint8_t block_count = 0;
  uint8_t sequence = 1;
  uint8_t stmin;
  uint32_t delay_us;
  size_t copied;
  size_t chunk;
  int ret;

  if (length <= ISOTP_CLASSIC_SF_DATA)
    {
      frame[0] = (uint8_t)length;
      memcpy(&frame[1], payload, length);
      return isotp_send_frame(id, extended, frame, start, timeout_ms);
    }

  if (length > OK8MP_CAN_ISOTP_MAX_PAYLOAD || length > 0x0fff)
    {
      return -EMSGSIZE;
    }

  frame[0] = ISOTP_PCI_FIRST_FRAME | ((length >> 8) & 0x0f);
  frame[1] = length & 0xff;
  memcpy(&frame[2], payload, ISOTP_CLASSIC_FF_DATA);
  ret = isotp_send_frame(id, extended, frame, start, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  ret = isotp_wait_flow_control(subscription, &block_size, &stmin,
                                start, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  delay_us = isotp_stmin_us(stmin);
  copied = ISOTP_CLASSIC_FF_DATA;
  while (copied < length)
    {
      memset(frame, 0, sizeof(frame));
      frame[0] = ISOTP_PCI_CONSECUTIVE | (sequence & 0x0f);
      chunk = length - copied;
      if (chunk > ISOTP_CLASSIC_CF_DATA)
        {
          chunk = ISOTP_CLASSIC_CF_DATA;
        }

      memcpy(&frame[1], &payload[copied], chunk);
      if (delay_us != 0)
        {
          usleep(delay_us);
        }

      ret = isotp_send_frame(id, extended, frame, start, timeout_ms);
      if (ret < 0)
        {
          return ret;
        }

      copied += chunk;
      sequence = (sequence + 1) & 0x0f;
      block_count++;

      if (block_size != 0 && block_count >= block_size && copied < length)
        {
          ret = isotp_wait_flow_control(subscription, &block_size, &stmin,
                                        start, timeout_ms);
          if (ret < 0)
            {
              return ret;
            }

          delay_us = isotp_stmin_us(stmin);
          block_count = 0;
        }
    }

  return OK;
}

static int isotp_send_flow_control(uint32_t id, bool extended,
                                   uint8_t flow_status,
                                   uint32_t start, uint32_t timeout_ms)
{
  uint8_t frame[ISOTP_FRAME_LENGTH] = {0};

  frame[0] = ISOTP_PCI_FLOW_CONTROL |
             (flow_status & ISOTP_FLOW_STATUS_MASK);
  frame[1] = 0; /* Unlimited block size. */
  frame[2] = 5; /* Request 5 ms separation between CF frames. */
  return isotp_send_frame(id, extended, frame, start, timeout_ms);
}

static int isotp_receive_payload(uint32_t flow_control_id, bool extended,
                                 ok8mp_can_subscription_t subscription,
                                 FAR uint8_t *payload,
                                 FAR size_t *length,
                                 uint32_t start, uint32_t timeout_ms)
{
  struct can_msg_s msg;
  size_t capacity = *length;
  size_t total;
  size_t copied;
  size_t chunk;
  uint8_t expected_sequence = 1;
  uint8_t frame_type;
  int ret;

  ret = isotp_receive_frame(subscription, &msg, start, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  if (can_dlc2bytes(msg.cm_hdr.ch_dlc) == 0)
    {
      return -EPROTO;
    }

  frame_type = msg.cm_data[0] & ISOTP_PCI_TYPE_MASK;
  if (frame_type == ISOTP_PCI_SINGLE_FRAME)
    {
      total = msg.cm_data[0] & 0x0f;
      if (total > ISOTP_CLASSIC_SF_DATA ||
          can_dlc2bytes(msg.cm_hdr.ch_dlc) < total + 1)
        {
          return -EPROTO;
        }

      *length = total;
      if (total > capacity)
        {
          return -EMSGSIZE;
        }

      memcpy(payload, &msg.cm_data[1], total);
      return OK;
    }

  if (frame_type != ISOTP_PCI_FIRST_FRAME)
    {
      return -EPROTO;
    }

  if (can_dlc2bytes(msg.cm_hdr.ch_dlc) < ISOTP_FRAME_LENGTH)
    {
      return -EPROTO;
    }

  total = ((size_t)(msg.cm_data[0] & 0x0f) << 8) | msg.cm_data[1];
  *length = total;
  if (total <= ISOTP_CLASSIC_SF_DATA ||
      total > OK8MP_CAN_ISOTP_MAX_PAYLOAD || total > capacity)
    {
      (void)isotp_send_flow_control(flow_control_id, extended,
                                    ISOTP_FLOW_OVERFLOW,
                                    start, timeout_ms);
      return -EMSGSIZE;
    }

  memcpy(payload, &msg.cm_data[2], ISOTP_CLASSIC_FF_DATA);
  copied = ISOTP_CLASSIC_FF_DATA;
  ret = isotp_send_flow_control(flow_control_id, extended, ISOTP_FLOW_CTS,
                                start, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  while (copied < total)
    {
      ret = isotp_receive_frame(subscription, &msg, start, timeout_ms);
      if (ret < 0)
        {
          return ret;
        }

      if ((msg.cm_data[0] & ISOTP_PCI_TYPE_MASK) !=
          ISOTP_PCI_CONSECUTIVE ||
          (msg.cm_data[0] & 0x0f) != expected_sequence)
        {
          return -EPROTO;
        }

      chunk = total - copied;
      if (chunk > ISOTP_CLASSIC_CF_DATA)
        {
          chunk = ISOTP_CLASSIC_CF_DATA;
        }

      if (can_dlc2bytes(msg.cm_hdr.ch_dlc) < chunk + 1)
        {
          return -EPROTO;
        }

      memcpy(&payload[copied], &msg.cm_data[1], chunk);
      copied += chunk;
      expected_sequence = (expected_sequence + 1) & 0x0f;
    }

  return OK;
}

int ok8mp_can_isotp_request(uint32_t request_id, uint32_t response_id,
                            bool extended,
                            FAR const uint8_t *request,
                            size_t request_length,
                            FAR uint8_t *response,
                            FAR size_t *response_length,
                            uint32_t timeout_ms)
{
  ok8mp_can_subscription_t subscription;
  uint32_t mask;
  uint32_t start;
  int ret;

  if (request == NULL || request_length == 0 ||
      request_length > OK8MP_CAN_ISOTP_MAX_PAYLOAD ||
      response == NULL || response_length == NULL ||
      *response_length == 0 || timeout_ms == 0)
    {
      return -EINVAL;
    }

  mask = extended ? 0x1fffffff : 0x7ff;
  nxmutex_lock(&g_isotp_lock);
  ret = ok8mp_can_service_subscribe(response_id, mask, extended,
                                     &subscription);
  if (ret < 0)
    {
      nxmutex_unlock(&g_isotp_lock);
      return ret;
    }

  start = isotp_now_ms();
  ret = isotp_send_payload(request_id, extended, subscription,
                           request, request_length, start, timeout_ms);
  if (ret == OK)
    {
      ret = isotp_receive_payload(request_id, extended, subscription,
                                  response, response_length,
                                  start, timeout_ms);
    }

  ok8mp_can_service_unsubscribe(subscription);
  nxmutex_unlock(&g_isotp_lock);
  return ret;
}
