/****************************************************************************
 * apps/examples/ok8mp_can_ecu/ok8mp_can_ecu_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ok8mp_can_diag.h>
#include <ok8mp_can_isotp.h>

#define DTC_TEST_FAILED       (1u << 0)
#define DTC_CONFIRMED         (1u << 3)

static FAR const char *ecu_state_name(enum ok8mp_can_ecu_state_e state)
{
  switch (state)
    {
      case OK8MP_CAN_ECU_ENGINE_OVER_TEMPERATURE:
        return "engine_over_temperature";
      case OK8MP_CAN_ECU_BATTERY_VOLTAGE_LOW:
        return "battery_voltage_low";
      default:
        return "normal";
    }
}

static FAR const char *ecu_dtc_name(uint32_t code)
{
  if (code == 0x021700 || code == 0x0217)
    {
      return "P0217";
    }

  if (code == 0x056200 || code == 0x0562)
    {
      return "P0562";
    }

  return "UNKNOWN";
}

static int ecu_error(FAR const char *protocol, int ret)
{
  printf("can_ecu: %s transaction failed: %d\n", protocol, -ret);
  printf("{\"ok\":false,\"protocol\":\"%s\",\"error\":%d}\n",
         protocol, -ret);
  return EXIT_FAILURE;
}

static int ecu_status(void)
{
  struct ok8mp_can_vehicle_status_s status;
  int ret = ok8mp_can_uds_read_status(&status);

  if (ret < 0)
    {
      return ecu_error("uds", ret);
    }

  printf("can_ecu: UDS DID F100 state=%s coolant=%u C battery=%u mV "
         "active_dtc=%u stored_dtc=%u seq=%u\n",
         ecu_state_name(status.state), status.coolant_c, status.battery_mv,
         status.active_dtc, status.stored_dtc, status.sequence);
  printf("{\"ok\":true,\"protocol\":\"uds\",\"did\":\"F100\","
         "\"state\":\"%s\",\"coolant_c\":%u,\"battery_mv\":%u,"
         "\"active_dtc\":%u,\"stored_dtc\":%u,\"sequence\":%u}\n",
         ecu_state_name(status.state), status.coolant_c, status.battery_mv,
         status.active_dtc, status.stored_dtc, status.sequence);
  return EXIT_SUCCESS;
}

static int ecu_set_state(FAR const char *name)
{
  enum ok8mp_can_ecu_state_e state;
  int ret;

  if (strcmp(name, "normal") == 0)
    {
      state = OK8MP_CAN_ECU_NORMAL;
    }
  else if (strcmp(name, "overtemp") == 0)
    {
      state = OK8MP_CAN_ECU_ENGINE_OVER_TEMPERATURE;
    }
  else if (strcmp(name, "low_voltage") == 0)
    {
      state = OK8MP_CAN_ECU_BATTERY_VOLTAGE_LOW;
    }
  else
    {
      return -EINVAL;
    }

  ret = ok8mp_can_uds_set_state(state);
  if (ret < 0)
    {
      return ecu_error("uds", ret);
    }

  printf("can_ecu: UDS DID F100 state changed to %s\n",
         ecu_state_name(state));
  printf("{\"ok\":true,\"protocol\":\"uds\",\"service\":\"2E\","
         "\"state\":\"%s\"}\n", ecu_state_name(state));
  return EXIT_SUCCESS;
}

static int ecu_print_dtcs(bool obd)
{
  struct ok8mp_can_dtc_s dtcs[OK8MP_CAN_DIAG_MAX_DTCS];
  size_t count = OK8MP_CAN_DIAG_MAX_DTCS;
  size_t i;
  int ret;

  ret = obd ? ok8mp_can_obd_read_dtcs(dtcs, &count) :
              ok8mp_can_uds_read_dtcs(dtcs, &count);
  if (ret < 0)
    {
      return ecu_error(obd ? "obd2" : "uds", ret);
    }

  printf("{\"ok\":true,\"protocol\":\"%s\",\"dtcs\":[",
         obd ? "obd2" : "uds");
  for (i = 0; i < count; i++)
    {
      if (obd)
        {
          printf("%s{\"code\":\"%s\",\"raw\":\"%04lx\"}",
                 i == 0 ? "" : ",", ecu_dtc_name(dtcs[i].code),
                 (unsigned long)dtcs[i].code);
        }
      else
        {
          printf("%s{\"code\":\"%s\",\"raw\":\"%06lx\","
                 "\"status\":\"0x%02x\",\"active\":%s,"
                 "\"confirmed\":%s}",
                 i == 0 ? "" : ",", ecu_dtc_name(dtcs[i].code),
                 (unsigned long)dtcs[i].code, dtcs[i].status,
                 (dtcs[i].status & DTC_TEST_FAILED) != 0 ? "true" : "false",
                 (dtcs[i].status & DTC_CONFIRMED) != 0 ? "true" : "false");
        }
    }

  printf("]}\n");
  return EXIT_SUCCESS;
}

static int ecu_clear_dtcs(bool obd)
{
  int ret = obd ? ok8mp_can_obd_clear_dtcs() :
                  ok8mp_can_uds_clear_dtcs();

  if (ret < 0)
    {
      return ecu_error(obd ? "obd2" : "uds", ret);
    }

  printf("can_ecu: DTC history cleared through %s\n",
         obd ? "OBD-II Mode 04" : "UDS service 14");
  printf("{\"ok\":true,\"protocol\":\"%s\",\"cleared\":true}\n",
         obd ? "obd2" : "uds");
  return EXIT_SUCCESS;
}

static int ecu_isotp_test(void)
{
  uint8_t payload[28];
  uint8_t response[OK8MP_CAN_ISOTP_MAX_PAYLOAD];
  size_t response_length = sizeof(response);
  unsigned int i;
  int ret;

  for (i = 0; i < sizeof(payload); i++)
    {
      payload[i] = i + 1;
    }

  ret = ok8mp_can_uds_echo(payload, sizeof(payload), response,
                           &response_length);
  if (ret < 0)
    {
      return ecu_error("uds-isotp", ret);
    }

  printf("can_ecu: UDS RoutineControl ISO-TP multi-frame verified, "
         "request=32 response=%u bytes\n", (unsigned int)response_length);
  printf("{\"ok\":true,\"protocol\":\"uds-isotp\","
         "\"request_bytes\":32,\"response_bytes\":%u}\n",
         (unsigned int)response_length);
  return EXIT_SUCCESS;
}

static int ecu_uds(int argc, FAR char *argv[])
{
  char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1];
  uint8_t session;
  int ret;

  if (argc == 3 && strcmp(argv[2], "vin") == 0)
    {
      ret = ok8mp_can_uds_read_vin(vin);
      if (ret < 0)
        {
          return ecu_error("uds", ret);
        }

      printf("can_ecu: UDS DID F190 VIN=%s\n", vin);
      printf("{\"ok\":true,\"protocol\":\"uds\","
             "\"did\":\"F190\",\"vin\":\"%s\"}\n", vin);
      return EXIT_SUCCESS;
    }

  if (argc == 3 && strcmp(argv[2], "tester-present") == 0)
    {
      ret = ok8mp_can_uds_tester_present();
      if (ret < 0)
        {
          return ecu_error("uds", ret);
        }

      printf("{\"ok\":true,\"protocol\":\"uds\","
             "\"service\":\"3E\",\"tester_present\":true}\n");
      return EXIT_SUCCESS;
    }

  if (argc == 4 && strcmp(argv[2], "session") == 0)
    {
      if (strcmp(argv[3], "default") == 0)
        {
          session = 0x01;
        }
      else if (strcmp(argv[3], "extended") == 0)
        {
          session = 0x03;
        }
      else
        {
          return -EINVAL;
        }

      ret = ok8mp_can_uds_session(session);
      if (ret < 0)
        {
          return ecu_error("uds", ret);
        }

      printf("{\"ok\":true,\"protocol\":\"uds\","
             "\"service\":\"10\",\"session\":\"%s\"}\n",
             session == 0x03 ? "extended" : "default");
      return EXIT_SUCCESS;
    }

  return -EINVAL;
}

static int ecu_obd(int argc, FAR char *argv[])
{
  char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1];
  uint32_t mask00;
  uint32_t mask20;
  uint32_t mask40;
  uint16_t battery_mv;
  int16_t coolant_c;
  int ret;

  if (argc != 3)
    {
      return -EINVAL;
    }

  if (strcmp(argv[2], "supported") == 0)
    {
      ret = ok8mp_can_obd_supported_pids(0x00, &mask00);
      ret = ret < 0 ? ret : ok8mp_can_obd_supported_pids(0x20, &mask20);
      ret = ret < 0 ? ret : ok8mp_can_obd_supported_pids(0x40, &mask40);
      if (ret < 0)
        {
          return ecu_error("obd2", ret);
        }

      printf("{\"ok\":true,\"protocol\":\"obd2\","
             "\"mode\":\"01\",\"pid_masks\":{"
             "\"00\":\"%08lx\",\"20\":\"%08lx\","
             "\"40\":\"%08lx\"}}\n",
             (unsigned long)mask00, (unsigned long)mask20,
             (unsigned long)mask40);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[2], "coolant") == 0)
    {
      ret = ok8mp_can_obd_read_coolant(&coolant_c);
      if (ret < 0)
        {
          return ecu_error("obd2", ret);
        }

      printf("{\"ok\":true,\"protocol\":\"obd2\","
             "\"mode\":\"01\",\"pid\":\"05\","
             "\"coolant_c\":%d}\n", coolant_c);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[2], "voltage") == 0)
    {
      ret = ok8mp_can_obd_read_voltage(&battery_mv);
      if (ret < 0)
        {
          return ecu_error("obd2", ret);
        }

      printf("{\"ok\":true,\"protocol\":\"obd2\","
             "\"mode\":\"01\",\"pid\":\"42\","
             "\"battery_mv\":%u}\n", battery_mv);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[2], "dtc") == 0)
    {
      return ecu_print_dtcs(true);
    }

  if (strcmp(argv[2], "clear") == 0)
    {
      return ecu_clear_dtcs(true);
    }

  if (strcmp(argv[2], "vin") == 0)
    {
      ret = ok8mp_can_obd_read_vin(vin);
      if (ret < 0)
        {
          return ecu_error("obd2", ret);
        }

      printf("{\"ok\":true,\"protocol\":\"obd2\","
             "\"mode\":\"09\",\"pid\":\"02\","
             "\"vin\":\"%s\"}\n", vin);
      return EXIT_SUCCESS;
    }

  return -EINVAL;
}

int main(int argc, FAR char *argv[])
{
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      return ecu_status();
    }

  if (argc == 3 && strcmp(argv[1], "set") == 0)
    {
      ret = ecu_set_state(argv[2]);
      if (ret != -EINVAL)
        {
          return ret;
        }
    }

  if (argc == 2 && strcmp(argv[1], "dtc") == 0)
    {
      return ecu_print_dtcs(false);
    }

  if (argc == 3 && strcmp(argv[1], "dtc") == 0 &&
      strcmp(argv[2], "clear") == 0)
    {
      return ecu_clear_dtcs(false);
    }

  if (argc == 2 && strcmp(argv[1], "isotp-test") == 0)
    {
      return ecu_isotp_test();
    }

  if (argc >= 3 && strcmp(argv[1], "uds") == 0)
    {
      ret = ecu_uds(argc, argv);
      if (ret != -EINVAL)
        {
          return ret;
        }
    }

  if (argc >= 3 && strcmp(argv[1], "obd") == 0)
    {
      ret = ecu_obd(argc, argv);
      if (ret != -EINVAL)
        {
          return ret;
        }
    }

  printf("Usage: %s status|set normal|set overtemp|set low_voltage|"
         "dtc|dtc clear|isotp-test|uds vin|uds tester-present|"
         "uds session default|uds session extended|"
         "obd supported|obd coolant|obd voltage|obd dtc|obd clear|obd vin\n",
         argv[0]);
  return EXIT_FAILURE;
}
