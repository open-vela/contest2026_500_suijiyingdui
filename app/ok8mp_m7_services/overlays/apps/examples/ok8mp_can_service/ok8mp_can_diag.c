/****************************************************************************
 * apps/examples/ok8mp_can_service/ok8mp_can_diag.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ok8mp_can_diag.h>
#include <ok8mp_can_isotp.h>

#ifndef CONFIG_EXAMPLES_OK8MP_CAN_ECU_REQUEST_ID
#  define CONFIG_EXAMPLES_OK8MP_CAN_ECU_REQUEST_ID 0x7e0
#endif

#ifndef CONFIG_EXAMPLES_OK8MP_CAN_ECU_RESPONSE_ID
#  define CONFIG_EXAMPLES_OK8MP_CAN_ECU_RESPONSE_ID 0x7e8
#endif

#ifndef CONFIG_EXAMPLES_OK8MP_CAN_ECU_TIMEOUT_MS
#  define CONFIG_EXAMPLES_OK8MP_CAN_ECU_TIMEOUT_MS 2000
#endif

#define UDS_NEGATIVE_RESPONSE          0x7f
#define UDS_DIAGNOSTIC_SESSION         0x10
#define UDS_CLEAR_DIAGNOSTIC_INFO      0x14
#define UDS_READ_DTC_INFO              0x19
#define UDS_READ_DATA_BY_ID            0x22
#define UDS_WRITE_DATA_BY_ID           0x2e
#define UDS_ROUTINE_CONTROL            0x31
#define UDS_TESTER_PRESENT             0x3e
#define UDS_POSITIVE_OFFSET            0x40
#define UDS_DID_VEHICLE_STATUS         0xf100
#define UDS_DID_VIN                    0xf190
#define UDS_DTC_STATUS_MASK_ALL        0xff
#define UDS_SESSION_EXTENDED           0x03
#define UDS_ROUTINE_ECHO               0xff00

#define OBD_MODE_CURRENT_DATA          0x01
#define OBD_MODE_STORED_DTC            0x03
#define OBD_MODE_CLEAR_DTC             0x04
#define OBD_MODE_VEHICLE_INFO          0x09
#define OBD_POSITIVE_OFFSET            0x40
#define OBD_PID_COOLANT                0x05
#define OBD_PID_MODULE_VOLTAGE         0x42
#define OBD_INFO_VIN                   0x02

static int can_diag_request(FAR const uint8_t *request,
                            size_t request_length,
                            FAR uint8_t *response,
                            FAR size_t *response_length)
{
  int ret;

  ret = ok8mp_can_isotp_request(
          CONFIG_EXAMPLES_OK8MP_CAN_ECU_REQUEST_ID,
          CONFIG_EXAMPLES_OK8MP_CAN_ECU_RESPONSE_ID,
          false, request, request_length, response, response_length,
          CONFIG_EXAMPLES_OK8MP_CAN_ECU_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }

  if (*response_length >= 3 && response[0] == UDS_NEGATIVE_RESPONSE)
    {
      return -EPROTO;
    }

  return OK;
}

static int can_diag_expect(FAR const uint8_t *request,
                           size_t request_length,
                           FAR uint8_t *response,
                           FAR size_t *response_length,
                           uint8_t positive_service,
                           size_t minimum_length)
{
  int ret = can_diag_request(request, request_length, response,
                             response_length);
  if (ret < 0)
    {
      return ret;
    }

  if (*response_length < minimum_length ||
      response[0] != positive_service)
    {
      return -EPROTO;
    }

  return OK;
}

int ok8mp_can_uds_session(uint8_t session)
{
  uint8_t request[] = {UDS_DIAGNOSTIC_SESSION, session};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        UDS_DIAGNOSTIC_SESSION + UDS_POSITIVE_OFFSET, 2);
  if (ret < 0)
    {
      return ret;
    }

  return response[1] == session ? OK : -EPROTO;
}

int ok8mp_can_uds_tester_present(void)
{
  uint8_t request[] = {UDS_TESTER_PRESENT, 0x00};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);

  return can_diag_expect(request, sizeof(request), response, &length,
                         UDS_TESTER_PRESENT + UDS_POSITIVE_OFFSET, 2);
}

int ok8mp_can_uds_read_status(FAR struct ok8mp_can_vehicle_status_s *status)
{
  uint8_t request[] = {UDS_READ_DATA_BY_ID,
                       UDS_DID_VEHICLE_STATUS >> 8,
                       UDS_DID_VEHICLE_STATUS & 0xff};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  uint8_t checksum;
  size_t length = sizeof(response);
  int ret;

  if (status == NULL)
    {
      return -EINVAL;
    }

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        UDS_READ_DATA_BY_ID + UDS_POSITIVE_OFFSET, 12);
  if (ret < 0)
    {
      return ret;
    }

  if (length != 12 || response[1] != request[1] ||
      response[2] != request[2] || response[3] > OK8MP_CAN_ECU_BATTERY_VOLTAGE_LOW)
    {
      return -EPROTO;
    }

  checksum = response[3] ^ response[4] ^ response[5] ^ response[6] ^
             response[7] ^ response[8] ^ response[9] ^ response[10];
  if (checksum != response[11])
    {
      return -EBADMSG;
    }

  status->state = (enum ok8mp_can_ecu_state_e)response[3];
  status->coolant_c = response[4];
  status->battery_mv = ((uint16_t)response[5] << 8) | response[6];
  status->active_dtc = response[7];
  status->stored_dtc = response[8];
  status->sequence = ((uint16_t)response[9] << 8) | response[10];
  return OK;
}

int ok8mp_can_uds_set_state(enum ok8mp_can_ecu_state_e state)
{
  uint8_t request[] = {UDS_WRITE_DATA_BY_ID,
                       UDS_DID_VEHICLE_STATUS >> 8,
                       UDS_DID_VEHICLE_STATUS & 0xff,
                       (uint8_t)state};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  if (state < OK8MP_CAN_ECU_NORMAL ||
      state > OK8MP_CAN_ECU_BATTERY_VOLTAGE_LOW)
    {
      return -EINVAL;
    }

  ret = ok8mp_can_uds_session(UDS_SESSION_EXTENDED);
  if (ret < 0)
    {
      return ret;
    }

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        UDS_WRITE_DATA_BY_ID + UDS_POSITIVE_OFFSET, 4);
  if (ret < 0)
    {
      return ret;
    }

  return length == 4 && response[1] == request[1] &&
         response[2] == request[2] && response[3] == request[3] ?
         OK : -EPROTO;
}

int ok8mp_can_uds_read_dtcs(FAR struct ok8mp_can_dtc_s *dtcs,
                            FAR size_t *count)
{
  uint8_t request[] = {UDS_READ_DTC_INFO, 0x02,
                       UDS_DTC_STATUS_MASK_ALL};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t capacity;
  size_t records;
  size_t length = sizeof(response);
  size_t i;
  int ret;

  if (dtcs == NULL || count == NULL)
    {
      return -EINVAL;
    }

  capacity = *count;
  ret = can_diag_expect(request, sizeof(request), response, &length,
                        UDS_READ_DTC_INFO + UDS_POSITIVE_OFFSET, 3);
  if (ret < 0)
    {
      return ret;
    }

  if (response[1] != 0x02 || (length - 3) % 4 != 0)
    {
      return -EPROTO;
    }

  records = (length - 3) / 4;
  if (records > capacity)
    {
      return -ENOSPC;
    }

  for (i = 0; i < records; i++)
    {
      size_t offset = 3 + i * 4;
      dtcs[i].code = ((uint32_t)response[offset] << 16) |
                     ((uint32_t)response[offset + 1] << 8) |
                     response[offset + 2];
      dtcs[i].status = response[offset + 3];
    }

  *count = records;
  return OK;
}

int ok8mp_can_uds_clear_dtcs(void)
{
  uint8_t request[] = {UDS_CLEAR_DIAGNOSTIC_INFO, 0xff, 0xff, 0xff};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        UDS_CLEAR_DIAGNOSTIC_INFO + UDS_POSITIVE_OFFSET, 1);
  return ret < 0 ? ret : (length == 1 ? OK : -EPROTO);
}

int ok8mp_can_uds_read_vin(FAR char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1])
{
  uint8_t request[] = {UDS_READ_DATA_BY_ID, UDS_DID_VIN >> 8,
                       UDS_DID_VIN & 0xff};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  if (vin == NULL)
    {
      return -EINVAL;
    }

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        UDS_READ_DATA_BY_ID + UDS_POSITIVE_OFFSET,
                        3 + OK8MP_CAN_DIAG_VIN_LENGTH);
  if (ret < 0)
    {
      return ret;
    }

  if (length != 3 + OK8MP_CAN_DIAG_VIN_LENGTH ||
      response[1] != request[1] || response[2] != request[2])
    {
      return -EPROTO;
    }

  memcpy(vin, &response[3], OK8MP_CAN_DIAG_VIN_LENGTH);
  vin[OK8MP_CAN_DIAG_VIN_LENGTH] = '\0';
  return OK;
}

int ok8mp_can_uds_echo(FAR const uint8_t *payload, size_t payload_length,
                       FAR uint8_t *response,
                       FAR size_t *response_length)
{
  uint8_t request[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t received;
  int ret;

  if (payload == NULL || response == NULL || response_length == NULL ||
      payload_length > sizeof(request) - 4)
    {
      return -EINVAL;
    }

  request[0] = UDS_ROUTINE_CONTROL;
  request[1] = 0x01;
  request[2] = UDS_ROUTINE_ECHO >> 8;
  request[3] = UDS_ROUTINE_ECHO & 0xff;
  memcpy(&request[4], payload, payload_length);

  received = *response_length;
  ret = can_diag_expect(request, payload_length + 4, response, &received,
                        UDS_ROUTINE_CONTROL + UDS_POSITIVE_OFFSET,
                        payload_length + 4);
  if (ret < 0)
    {
      return ret;
    }

  if (received != payload_length + 4 || response[1] != 0x01 ||
      response[2] != request[2] || response[3] != request[3] ||
      memcmp(&response[4], payload, payload_length) != 0)
    {
      return -EPROTO;
    }

  *response_length = received;
  return OK;
}

int ok8mp_can_obd_supported_pids(uint8_t base_pid,
                                 FAR uint32_t *supported)
{
  uint8_t request[] = {OBD_MODE_CURRENT_DATA, base_pid};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  if (supported == NULL)
    {
      return -EINVAL;
    }

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        OBD_MODE_CURRENT_DATA + OBD_POSITIVE_OFFSET, 6);
  if (ret < 0)
    {
      return ret;
    }

  if (length != 6 || response[1] != base_pid)
    {
      return -EPROTO;
    }

  *supported = ((uint32_t)response[2] << 24) |
               ((uint32_t)response[3] << 16) |
               ((uint32_t)response[4] << 8) | response[5];
  return OK;
}

int ok8mp_can_obd_read_coolant(FAR int16_t *coolant_c)
{
  uint8_t request[] = {OBD_MODE_CURRENT_DATA, OBD_PID_COOLANT};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  if (coolant_c == NULL)
    {
      return -EINVAL;
    }

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        OBD_MODE_CURRENT_DATA + OBD_POSITIVE_OFFSET, 3);
  if (ret < 0)
    {
      return ret;
    }

  if (length != 3 || response[1] != OBD_PID_COOLANT)
    {
      return -EPROTO;
    }

  *coolant_c = (int16_t)response[2] - 40;
  return OK;
}

int ok8mp_can_obd_read_voltage(FAR uint16_t *battery_mv)
{
  uint8_t request[] = {OBD_MODE_CURRENT_DATA, OBD_PID_MODULE_VOLTAGE};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  if (battery_mv == NULL)
    {
      return -EINVAL;
    }

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        OBD_MODE_CURRENT_DATA + OBD_POSITIVE_OFFSET, 4);
  if (ret < 0)
    {
      return ret;
    }

  if (length != 4 || response[1] != OBD_PID_MODULE_VOLTAGE)
    {
      return -EPROTO;
    }

  *battery_mv = ((uint16_t)response[2] << 8) | response[3];
  return OK;
}

int ok8mp_can_obd_read_dtcs(FAR struct ok8mp_can_dtc_s *dtcs,
                            FAR size_t *count)
{
  uint8_t request[] = {OBD_MODE_STORED_DTC};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t capacity;
  size_t records;
  size_t length = sizeof(response);
  size_t i;
  int ret;

  if (dtcs == NULL || count == NULL)
    {
      return -EINVAL;
    }

  capacity = *count;
  ret = can_diag_expect(request, sizeof(request), response, &length,
                        OBD_MODE_STORED_DTC + OBD_POSITIVE_OFFSET, 1);
  if (ret < 0)
    {
      return ret;
    }

  if ((length - 1) % 2 != 0)
    {
      return -EPROTO;
    }

  records = (length - 1) / 2;
  if (records > capacity)
    {
      return -ENOSPC;
    }

  for (i = 0; i < records; i++)
    {
      uint16_t raw = ((uint16_t)response[1 + i * 2] << 8) |
                     response[2 + i * 2];
      dtcs[i].code = raw;
      dtcs[i].status = 0;
    }

  *count = records;
  return OK;
}

int ok8mp_can_obd_clear_dtcs(void)
{
  uint8_t request[] = {OBD_MODE_CLEAR_DTC};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        OBD_MODE_CLEAR_DTC + OBD_POSITIVE_OFFSET, 1);
  return ret < 0 ? ret : (length == 1 ? OK : -EPROTO);
}

int ok8mp_can_obd_read_vin(FAR char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1])
{
  uint8_t request[] = {OBD_MODE_VEHICLE_INFO, OBD_INFO_VIN};
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t length = sizeof(response);
  int ret;

  if (vin == NULL)
    {
      return -EINVAL;
    }

  ret = can_diag_expect(request, sizeof(request), response, &length,
                        OBD_MODE_VEHICLE_INFO + OBD_POSITIVE_OFFSET,
                        3 + OK8MP_CAN_DIAG_VIN_LENGTH);
  if (ret < 0)
    {
      return ret;
    }

  if (length != 3 + OK8MP_CAN_DIAG_VIN_LENGTH ||
      response[1] != OBD_INFO_VIN || response[2] != 0x01)
    {
      return -EPROTO;
    }

  memcpy(vin, &response[3], OK8MP_CAN_DIAG_VIN_LENGTH);
  vin[OK8MP_CAN_DIAG_VIN_LENGTH] = '\0';
  return OK;
}
