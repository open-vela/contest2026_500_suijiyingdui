/****************************************************************************
 * apps/include/ok8mp_can_diag.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_INCLUDE_OK8MP_CAN_DIAG_H
#define __APPS_INCLUDE_OK8MP_CAN_DIAG_H

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#define OK8MP_CAN_DIAG_MAX_DTCS 8
#define OK8MP_CAN_DIAG_VIN_LENGTH 17

enum ok8mp_can_ecu_state_e
{
  OK8MP_CAN_ECU_NORMAL = 0,
  OK8MP_CAN_ECU_ENGINE_OVER_TEMPERATURE = 1,
  OK8MP_CAN_ECU_BATTERY_VOLTAGE_LOW = 2
};

struct ok8mp_can_vehicle_status_s
{
  enum ok8mp_can_ecu_state_e state;
  uint8_t coolant_c;
  uint16_t battery_mv;
  uint8_t active_dtc;
  uint8_t stored_dtc;
  uint16_t sequence;
};

struct ok8mp_can_dtc_s
{
  uint32_t code;
  uint8_t status;
};

/* UDS (ISO 14229) over ISO-TP physical addressing 0x7e0/0x7e8. */

int ok8mp_can_uds_session(uint8_t session);
int ok8mp_can_uds_tester_present(void);
int ok8mp_can_uds_read_status(FAR struct ok8mp_can_vehicle_status_s *status);
int ok8mp_can_uds_set_state(enum ok8mp_can_ecu_state_e state);
int ok8mp_can_uds_read_dtcs(FAR struct ok8mp_can_dtc_s *dtcs,
                            FAR size_t *count);
int ok8mp_can_uds_clear_dtcs(void);
int ok8mp_can_uds_read_vin(FAR char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1]);
int ok8mp_can_uds_echo(FAR const uint8_t *payload, size_t payload_length,
                       FAR uint8_t *response,
                       FAR size_t *response_length);

/* SAE J1979 OBD-II services over the same physical ISO-TP channel. */

int ok8mp_can_obd_supported_pids(uint8_t base_pid,
                                 FAR uint32_t *supported);
int ok8mp_can_obd_read_coolant(FAR int16_t *coolant_c);
int ok8mp_can_obd_read_voltage(FAR uint16_t *battery_mv);
int ok8mp_can_obd_read_dtcs(FAR struct ok8mp_can_dtc_s *dtcs,
                            FAR size_t *count);
int ok8mp_can_obd_clear_dtcs(void);
int ok8mp_can_obd_read_vin(FAR char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1]);

#endif /* __APPS_INCLUDE_OK8MP_CAN_DIAG_H */
