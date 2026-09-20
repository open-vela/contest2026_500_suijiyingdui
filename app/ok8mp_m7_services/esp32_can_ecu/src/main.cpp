#include <Arduino.h>
#include "driver/twai.h"

/* ESP32-S3 simulated vehicle ECU.
 * GPIO4 -> transceiver TX/TXD, GPIO5 <- transceiver RX/RXD.
 * 500 kbit/s, Classic CAN, 11-bit identifiers.
 */

static constexpr gpio_num_t kCanTxPin = GPIO_NUM_4;
static constexpr gpio_num_t kCanRxPin = GPIO_NUM_5;
static constexpr uint32_t kStatusCanId = 0x180;
static constexpr uint32_t kStatusPeriodMs = 1000;
static constexpr uint32_t kDiagnosticRequestCanId = 0x700;
static constexpr uint32_t kDiagnosticResponseCanId = 0x708;
static constexpr uint32_t kIsoTpRequestCanId = 0x7e0;
static constexpr uint32_t kIsoTpResponseCanId = 0x7e8;
/* A real UDS server returns after a finite P2 processing time.  The small
 * delay also gives the M7 FlexCAN transmitter time to complete arbitration
 * before this simulated ECU starts its response. */
static constexpr uint32_t kUdsResponseDelayMs = 10;
static constexpr uint32_t kInjectedPeriodExtraMs = 1500;
static constexpr size_t kIsoTpMaxPayload = 128;
static constexpr uint32_t kDtcP0217 = 0x021700;
static constexpr uint32_t kDtcP0562 = 0x056200;
static constexpr uint8_t kDtcTestFailed = 1u << 0;
static constexpr uint8_t kDtcFailedThisCycle = 1u << 1;
static constexpr uint8_t kDtcPending = 1u << 2;
static constexpr uint8_t kDtcConfirmed = 1u << 3;
static constexpr char kVin[] = "OK8MP2026CAN00001";

enum class EcuState : uint8_t
{
  Normal = 0,
  EngineOverTemperature = 1,
  BatteryVoltageLow = 2,
};

struct DtcRecord
{
  uint32_t code;
  uint8_t status;
  uint16_t occurrences;
};

struct IsoTpReceiveContext
{
  bool active;
  size_t total;
  size_t received;
  uint8_t next_sequence;
  uint8_t payload[kIsoTpMaxPayload];
};

static uint32_t g_sequence;
static uint32_t g_last_send_ms;
static bool g_periodic_tx_enabled;
static bool g_inject_counter_jump;
static uint32_t g_inject_period_extra_ms;
static bool g_inject_bad_diagnostic_response;
static EcuState g_ecu_state = EcuState::Normal;
static uint8_t g_uds_session = 0x01;
static uint8_t g_coolant_c = 60;
static uint16_t g_battery_mv = 12600;
static DtcRecord g_dtcs[] =
{
  {kDtcP0217, 0, 0},
  {kDtcP0562, 0, 0},
};
static IsoTpReceiveContext g_isotp_rx = {};

static const char *ecu_state_name(EcuState state)
{
  switch (state)
    {
      case EcuState::EngineOverTemperature:
        return "engine_over_temperature";
      case EcuState::BatteryVoltageLow:
        return "battery_voltage_low";
      default:
        return "normal";
    }
}

static bool dtc_active(const DtcRecord &dtc)
{
  return (dtc.status & kDtcTestFailed) != 0;
}

static uint8_t active_dtc_count()
{
  uint8_t count = 0;
  for (const auto &dtc : g_dtcs)
    {
      count += dtc_active(dtc) ? 1 : 0;
    }
  return count;
}

static uint8_t stored_dtc_count()
{
  uint8_t count = 0;
  for (const auto &dtc : g_dtcs)
    {
      count += dtc.status != 0 ? 1 : 0;
    }
  return count;
}

static void set_dtc_active(DtcRecord &dtc, bool active)
{
  if (active)
    {
      if (!dtc_active(dtc))
        {
          dtc.occurrences++;
        }
      dtc.status |= kDtcTestFailed | kDtcFailedThisCycle |
                    kDtcPending | kDtcConfirmed;
    }
  else
    {
      /* Confirmed history remains until an explicit clear. */
      dtc.status &= ~(kDtcTestFailed | kDtcFailedThisCycle | kDtcPending);
    }
}

static void apply_ecu_state(EcuState state)
{
  g_ecu_state = state;
  set_dtc_active(g_dtcs[0], state == EcuState::EngineOverTemperature);
  set_dtc_active(g_dtcs[1], state == EcuState::BatteryVoltageLow);

  if (state == EcuState::EngineOverTemperature)
    {
      g_coolant_c = 110;
      g_battery_mv = 12600;
    }
  else if (state == EcuState::BatteryVoltageLow)
    {
      g_coolant_c = 60;
      g_battery_mv = 10500;
    }
  else
    {
      g_coolant_c = 60;
      g_battery_mv = 12600;
    }

  Serial.printf("ECU state=%s coolant=%uC battery=%umV active_dtc=%u stored_dtc=%u\n",
                ecu_state_name(g_ecu_state), g_coolant_c, g_battery_mv,
                active_dtc_count(), stored_dtc_count());
}

static void clear_dtcs()
{
  for (auto &dtc : g_dtcs)
    {
      dtc.status = 0;
      dtc.occurrences = 0;
    }

  /* A fault that is still physically present becomes active again. */
  apply_ecu_state(g_ecu_state);
}

static void print_twai_status(const char *where)
{
  twai_status_info_t status = {};
  esp_err_t result = twai_get_status_info(&status);
  if (result == ESP_OK)
    {
      Serial.printf("TWAI %s: state=%d txerr=%lu rxerr=%lu txq=%lu rxq=%lu\n",
                    where, static_cast<int>(status.state),
                    static_cast<unsigned long>(status.tx_error_counter),
                    static_cast<unsigned long>(status.rx_error_counter),
                    static_cast<unsigned long>(status.msgs_to_tx),
                    static_cast<unsigned long>(status.msgs_to_rx));
    }
}

static void print_frame(const twai_message_t &message)
{
  Serial.printf("RX id=0x%03lX dlc=%u data=",
                static_cast<unsigned long>(message.identifier),
                message.data_length_code);
  for (uint8_t i = 0; i < message.data_length_code; i++)
    {
      Serial.printf("%02X ", message.data[i]);
    }
  Serial.println();
}

static esp_err_t transmit_can(uint32_t id, const uint8_t data[8])
{
  twai_message_t message = {};
  message.identifier = id;
  message.data_length_code = 8;
  memcpy(message.data, data, 8);
  return twai_transmit(&message, pdMS_TO_TICKS(200));
}

static void send_status_frame()
{
  twai_message_t message = {};
  uint16_t battery_10mv = g_battery_mv / 10;
  uint8_t flags = 0x01;

  if (g_inject_counter_jump)
    {
      g_sequence += 2;
      g_inject_counter_jump = false;
      Serial.println("FAULT-INJECT: next 0x180 frame uses a jumped counter.");
    }

  if (g_ecu_state == EcuState::EngineOverTemperature)
    {
      flags |= 1u << 1;
    }
  else if (g_ecu_state == EcuState::BatteryVoltageLow)
    {
      flags |= 1u << 2;
    }
  if (active_dtc_count() != 0)
    {
      flags |= 1u << 7;
    }

  message.identifier = kStatusCanId;
  message.data_length_code = 8;
  message.data[0] = flags;
  message.data[1] = g_sequence & 0xff;
  message.data[2] = (g_sequence >> 8) & 0xff;
  message.data[3] = g_coolant_c;
  message.data[4] = battery_10mv & 0xff;
  message.data[5] = battery_10mv >> 8;
  message.data[6] = active_dtc_count();
  message.data[7] = message.data[0] ^ message.data[1] ^ message.data[2] ^
                    message.data[3] ^ message.data[4] ^ message.data[5] ^
                    message.data[6];

  esp_err_t result = twai_transmit(&message, pdMS_TO_TICKS(100));
  Serial.printf("TX id=0x180 seq=%lu state=%s temp=%uC battery=%umV dtc=%u result=%d\n",
                static_cast<unsigned long>(g_sequence++),
                ecu_state_name(g_ecu_state), g_coolant_c, g_battery_mv,
                active_dtc_count(), result);
  if (result != ESP_OK)
    {
      print_twai_status("tx-error");
    }
}

static void send_diagnostic_response(const twai_message_t &request)
{
  twai_message_t response = {};
  response.identifier = kDiagnosticResponseCanId;
  response.data_length_code = 4;
  response.data[0] = 0x5a;
  response.data[1] = 0x01;
  response.data[2] = request.data[2];
  response.data[3] = g_inject_bad_diagnostic_response ? 0x00 : 0x01;
  g_inject_bad_diagnostic_response = false;
  esp_err_t result = twai_transmit(&response, pdMS_TO_TICKS(100));
  Serial.printf("DIAG response id=0x708 data=5A 01 %02X %02X result=%d\n",
                request.data[2], response.data[3], result);
}

static uint32_t isotp_stmin_us(uint8_t stmin)
{
  if (stmin <= 0x7f)
    {
      return static_cast<uint32_t>(stmin) * 1000;
    }
  if (stmin >= 0xf1 && stmin <= 0xf9)
    {
      return static_cast<uint32_t>(stmin - 0xf0) * 100;
    }
  return 0;
}

static esp_err_t isotp_send_flow_control(uint8_t status)
{
  uint8_t frame[8] = {};
  frame[0] = 0x30 | (status & 0x0f);
  frame[2] = 5;
  return transmit_can(kIsoTpResponseCanId, frame);
}

static bool isotp_wait_flow_control(uint8_t &block_size, uint8_t &stmin)
{
  uint32_t started = millis();
  unsigned int waits = 0;
  twai_message_t frame = {};

  while (millis() - started < 1000)
    {
      if (twai_receive(&frame, pdMS_TO_TICKS(50)) != ESP_OK)
        {
          continue;
        }
      if (frame.identifier != kIsoTpRequestCanId || frame.extd || frame.rtr ||
          frame.data_length_code < 3 || (frame.data[0] & 0xf0) != 0x30)
        {
          continue;
        }

      uint8_t status = frame.data[0] & 0x0f;
      if (status == 0)
        {
          block_size = frame.data[1];
          stmin = frame.data[2];
          return true;
        }
      if (status != 1 || ++waits > 3)
        {
          return false;
        }
    }
  return false;
}

static bool isotp_send_payload(const uint8_t *payload, size_t length)
{
  uint8_t frame[8] = {};
  uint8_t block_size = 0;
  uint8_t block_count = 0;
  uint8_t sequence = 1;
  uint8_t stmin = 0;
  size_t copied;

  if (length == 0 || length > kIsoTpMaxPayload)
    {
      return false;
    }
  if (length <= 7)
    {
      frame[0] = length;
      memcpy(&frame[1], payload, length);
      return transmit_can(kIsoTpResponseCanId, frame) == ESP_OK;
    }

  frame[0] = 0x10 | ((length >> 8) & 0x0f);
  frame[1] = length & 0xff;
  memcpy(&frame[2], payload, 6);
  if (transmit_can(kIsoTpResponseCanId, frame) != ESP_OK ||
      !isotp_wait_flow_control(block_size, stmin))
    {
      return false;
    }

  copied = 6;
  while (copied < length)
    {
      size_t chunk = min(static_cast<size_t>(7), length - copied);
      memset(frame, 0, sizeof(frame));
      frame[0] = 0x20 | (sequence & 0x0f);
      memcpy(&frame[1], &payload[copied], chunk);
      uint32_t delay_us = isotp_stmin_us(stmin);
      if (delay_us != 0)
        {
          delayMicroseconds(delay_us);
        }
      if (transmit_can(kIsoTpResponseCanId, frame) != ESP_OK)
        {
          return false;
        }

      copied += chunk;
      sequence = (sequence + 1) & 0x0f;
      block_count++;
      if (block_size != 0 && block_count >= block_size && copied < length)
        {
          if (!isotp_wait_flow_control(block_size, stmin))
            {
              return false;
            }
          block_count = 0;
        }
    }
  return true;
}

static void isotp_send_negative(uint8_t service, uint8_t error)
{
  uint8_t response[] = {0x7f, service, error};
  isotp_send_payload(response, sizeof(response));
}

static void process_isotp_request(const uint8_t *request, size_t length)
{
  uint8_t response[kIsoTpMaxPayload] = {};
  size_t response_length = 0;

  if (length == 0)
    {
      return;
    }

  switch (request[0])
    {
      case 0x10: /* UDS DiagnosticSessionControl. */
        if (length != 2 || (request[1] != 0x01 && request[1] != 0x03))
          {
            isotp_send_negative(request[0], 0x12);
            return;
          }
        g_uds_session = request[1];
        response[0] = 0x50;
        response[1] = request[1];
        response[2] = 0x00; /* P2 server max: 50 ms. */
        response[3] = 0x32;
        response[4] = 0x01; /* P2* server max: 5000 ms / 10. */
        response[5] = 0xf4;
        response_length = 6;
        break;

      case 0x22: /* UDS ReadDataByIdentifier. */
        if (length != 3)
          {
            isotp_send_negative(request[0], 0x13);
            return;
          }
        response[0] = 0x62;
        response[1] = request[1];
        response[2] = request[2];
        if (request[1] == 0xf1 && request[2] == 0x00)
          {
            response[3] = static_cast<uint8_t>(g_ecu_state);
            response[4] = g_coolant_c;
            response[5] = g_battery_mv >> 8;
            response[6] = g_battery_mv & 0xff;
            response[7] = active_dtc_count();
            response[8] = stored_dtc_count();
            response[9] = (g_sequence >> 8) & 0xff;
            response[10] = g_sequence & 0xff;
            response[11] = response[3] ^ response[4] ^ response[5] ^
                           response[6] ^ response[7] ^ response[8] ^
                           response[9] ^ response[10];
            response_length = 12;
          }
        else if (request[1] == 0xf1 && request[2] == 0x90)
          {
            memcpy(&response[3], kVin, sizeof(kVin) - 1);
            response_length = 3 + sizeof(kVin) - 1;
          }
        else
          {
            isotp_send_negative(request[0], 0x31);
            return;
          }
        break;

      case 0x2e: /* UDS WriteDataByIdentifier F100. */
        if (length != 4 || request[1] != 0xf1 || request[2] != 0x00 ||
            request[3] > 2)
          {
            isotp_send_negative(request[0], 0x31);
            return;
          }
        if (g_uds_session != 0x03)
          {
            isotp_send_negative(request[0], 0x22);
            return;
          }
        apply_ecu_state(static_cast<EcuState>(request[3]));
        response[0] = 0x6e;
        response[1] = request[1];
        response[2] = request[2];
        response[3] = request[3];
        response_length = 4;
        break;

      case 0x19: /* UDS ReadDTCInformation: report by status mask. */
        if (length != 3 || request[1] != 0x02)
          {
            isotp_send_negative(request[0], 0x12);
            return;
          }
        response[0] = 0x59;
        response[1] = 0x02;
        response[2] = 0xff;
        response_length = 3;
        for (const auto &dtc : g_dtcs)
          {
            if ((dtc.status & request[2]) == 0)
              {
                continue;
              }

            response[response_length++] = (dtc.code >> 16) & 0xff;
            response[response_length++] = (dtc.code >> 8) & 0xff;
            response[response_length++] = dtc.code & 0xff;
            response[response_length++] = dtc.status;
          }
        break;

      case 0x14: /* UDS ClearDiagnosticInformation, all groups. */
        if (length != 4 || request[1] != 0xff || request[2] != 0xff ||
            request[3] != 0xff)
          {
            isotp_send_negative(request[0], 0x31);
            return;
          }
        clear_dtcs();
        response[0] = 0x54;
        response_length = 1;
        break;

      case 0x31: /* UDS RoutineControl FF00: deterministic ISO-TP echo. */
        if (length < 4 || request[1] != 0x01 || request[2] != 0xff ||
            request[3] != 0x00)
          {
            isotp_send_negative(request[0], 0x31);
            return;
          }
        memcpy(response, request, length);
        response[0] = 0x71;
        response_length = length;
        break;

      case 0x3e: /* UDS TesterPresent. */
        if (length != 2 || (request[1] & 0x7f) != 0)
          {
            isotp_send_negative(request[0], 0x12);
            return;
          }
        response[0] = 0x7e;
        response[1] = request[1] & 0x7f;
        response_length = 2;
        break;

      case 0x01: /* OBD-II Mode 01: current data. */
        if (length != 2)
          {
            isotp_send_negative(request[0], 0x13);
            return;
          }
        response[0] = 0x41;
        response[1] = request[1];
        if (request[1] == 0x00)
          {
            /* PID 01, 05 and the next supported-PID range (20). */
            response[2] = 0x88;
            response[3] = 0x00;
            response[4] = 0x00;
            response[5] = 0x01;
            response_length = 6;
          }
        else if (request[1] == 0x20)
          {
            /* The 0x40 supported-PID range is present. */
            response[2] = 0x00;
            response[3] = 0x00;
            response[4] = 0x00;
            response[5] = 0x01;
            response_length = 6;
          }
        else if (request[1] == 0x40)
          {
            /* PID 42: control-module voltage. */
            response[2] = 0x40;
            response[3] = 0x00;
            response[4] = 0x00;
            response[5] = 0x00;
            response_length = 6;
          }
        else if (request[1] == 0x01)
          {
            response[2] = (active_dtc_count() ? 0x80 : 0x00) |
                          (active_dtc_count() & 0x7f);
            response[3] = 0;
            response[4] = 0;
            response[5] = 0;
            response_length = 6;
          }
        else if (request[1] == 0x05)
          {
            response[2] = g_coolant_c + 40;
            response_length = 3;
          }
        else if (request[1] == 0x42)
          {
            response[2] = g_battery_mv >> 8;
            response[3] = g_battery_mv & 0xff;
            response_length = 4;
          }
        else
          {
            isotp_send_negative(request[0], 0x31);
            return;
          }
        break;

      case 0x03: /* OBD-II Mode 03: stored emissions DTCs. */
        if (length != 1)
          {
            isotp_send_negative(request[0], 0x13);
            return;
          }
        response[0] = 0x43;
        response_length = 1;
        for (const auto &dtc : g_dtcs)
          {
            if (dtc.status == 0)
              {
                continue;
              }
            uint16_t sae_code = (dtc.code >> 8) & 0xffff;
            response[response_length++] = sae_code >> 8;
            response[response_length++] = sae_code & 0xff;
          }
        break;

      case 0x04: /* OBD-II Mode 04: clear DTCs. */
        if (length != 1)
          {
            isotp_send_negative(request[0], 0x13);
            return;
          }
        clear_dtcs();
        response[0] = 0x44;
        response_length = 1;
        break;

      case 0x09: /* OBD-II Mode 09 PID 02: VIN. */
        if (length != 2 || request[1] != 0x02)
          {
            isotp_send_negative(request[0], 0x31);
            return;
          }
        response[0] = 0x49;
        response[1] = 0x02;
        response[2] = 0x01;
        memcpy(&response[3], kVin, sizeof(kVin) - 1);
        response_length = 3 + sizeof(kVin) - 1;
        break;

      default:
        isotp_send_negative(request[0], 0x11);
        return;
    }

  delay(kUdsResponseDelayMs);
  bool sent = isotp_send_payload(response, response_length);
  Serial.printf("ISOTP service=0x%02X request_len=%u response_len=%u result=%s\n",
                request[0], static_cast<unsigned int>(length),
                static_cast<unsigned int>(response_length),
                sent ? "ok" : "failed");
}

static void handle_isotp_frame(const twai_message_t &frame)
{
  uint8_t type = frame.data[0] & 0xf0;

  if (type == 0x00)
    {
      size_t length = frame.data[0] & 0x0f;
      if (length <= 7 && frame.data_length_code >= length + 1)
        {
          process_isotp_request(&frame.data[1], length);
        }
    }
  else if (type == 0x10)
    {
      size_t total = (static_cast<size_t>(frame.data[0] & 0x0f) << 8) |
                     frame.data[1];
      if (total <= 7 || total > kIsoTpMaxPayload)
        {
          isotp_send_flow_control(2);
          return;
        }

      g_isotp_rx.active = true;
      g_isotp_rx.total = total;
      g_isotp_rx.received = 6;
      g_isotp_rx.next_sequence = 1;
      memcpy(g_isotp_rx.payload, &frame.data[2], 6);
      isotp_send_flow_control(0);
    }
  else if (type == 0x20 && g_isotp_rx.active)
    {
      uint8_t sequence = frame.data[0] & 0x0f;
      if (sequence != g_isotp_rx.next_sequence)
        {
          Serial.printf("ISOTP sequence error: got=%u expected=%u\n",
                        sequence, g_isotp_rx.next_sequence);
          g_isotp_rx.active = false;
          return;
        }

      size_t chunk = min(static_cast<size_t>(7),
                         g_isotp_rx.total - g_isotp_rx.received);
      memcpy(&g_isotp_rx.payload[g_isotp_rx.received], &frame.data[1], chunk);
      g_isotp_rx.received += chunk;
      g_isotp_rx.next_sequence = (g_isotp_rx.next_sequence + 1) & 0x0f;
      if (g_isotp_rx.received == g_isotp_rx.total)
        {
          g_isotp_rx.active = false;
          process_isotp_request(g_isotp_rx.payload, g_isotp_rx.total);
        }
    }
}

static void handle_can_frame(const twai_message_t &received)
{
  print_frame(received);
  if (received.extd || received.rtr)
    {
      return;
    }
  if (received.identifier == kIsoTpRequestCanId)
    {
      handle_isotp_frame(received);
    }
  else if (received.identifier == kDiagnosticRequestCanId &&
           received.data_length_code == 3 &&
           received.data[0] == 0xa5 && received.data[1] == 0x01)
    {
      send_diagnostic_response(received);
    }
}

void setup()
{
  Serial.begin(115200);
  delay(1200);

  twai_general_config_t general =
    TWAI_GENERAL_CONFIG_DEFAULT(kCanTxPin, kCanRxPin, TWAI_MODE_NORMAL);
  twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  esp_err_t result = twai_driver_install(&general, &timing, &filter);
  if (result != ESP_OK)
    {
      Serial.printf("TWAI driver install failed: %d\n", result);
      return;
    }

  result = twai_start();
  if (result != ESP_OK)
    {
      Serial.printf("TWAI start failed: %d\n", result);
      return;
    }

  Serial.println("ESP32 CAN simulated ECU v5: UDS + OBD-II, 500 kbit/s, TX=GPIO4, RX=GPIO5");
  Serial.println("States: 1=normal, 2=engine over-temperature, 3=low battery voltage.");
  Serial.println("Commands: t=start, r=stop, c=clear DTC, j=counter jump, p=period delay, x=bad diagnostic.");
  Serial.println("ISO-TP: request 0x7E0, response 0x7E8, max payload 128 bytes.");
  Serial.println("UDS: 10/14/19/22/2E/31/3E; OBD-II: Mode 01/03/04/09.");
  Serial.println("DTCs: P0217=engine over-temperature, P0562=system voltage low.");
  apply_ecu_state(EcuState::Normal);
  print_twai_status("boot");
}

void loop()
{
  twai_message_t received = {};
  while (twai_receive(&received, 0) == ESP_OK)
    {
      handle_can_frame(received);
    }

  while (Serial.available() > 0)
    {
      int command = Serial.read();
      if (command == 't' || command == 'T')
        {
          g_periodic_tx_enabled = true;
          g_last_send_ms = 0;
          Serial.println("Periodic ECU status transmission enabled.");
        }
      else if (command == 'r' || command == 'R')
        {
          g_periodic_tx_enabled = false;
          Serial.println("Periodic TX stopped; controller remains in NORMAL ACK mode.");
        }
      else if (command == '1')
        {
          apply_ecu_state(EcuState::Normal);
        }
      else if (command == '2')
        {
          apply_ecu_state(EcuState::EngineOverTemperature);
        }
      else if (command == '3')
        {
          apply_ecu_state(EcuState::BatteryVoltageLow);
        }
      else if (command == 'c' || command == 'C')
        {
          clear_dtcs();
          Serial.println("DTC history cleared; present faults evaluated again.");
        }
      else if (command == 'j' || command == 'J')
        {
          g_inject_counter_jump = true;
          Serial.println("FAULT-INJECT armed: jump the next ECU sequence counter.");
        }
      else if (command == 'p' || command == 'P')
        {
          g_inject_period_extra_ms = kInjectedPeriodExtraMs;
          Serial.printf("FAULT-INJECT armed: delay one status frame by %lu ms.\n",
                        static_cast<unsigned long>(kInjectedPeriodExtraMs));
        }
      else if (command == 'x' || command == 'X')
        {
          g_inject_bad_diagnostic_response = true;
          Serial.println("FAULT-INJECT armed: corrupt next diagnostic reply.");
        }
    }

  if (g_periodic_tx_enabled &&
      millis() - g_last_send_ms >= kStatusPeriodMs + g_inject_period_extra_ms)
    {
      if (g_inject_period_extra_ms != 0)
        {
          Serial.printf("FAULT-INJECT: delayed status period by %lu ms.\n",
                        static_cast<unsigned long>(g_inject_period_extra_ms));
          g_inject_period_extra_ms = 0;
        }
      g_last_send_ms = millis();
      send_status_frame();
    }

  delay(5);
}
