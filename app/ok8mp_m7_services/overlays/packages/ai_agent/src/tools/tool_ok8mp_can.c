/* SPDX-License-Identifier: Apache-2.0 */

#include "tools/tool_ok8mp_can.h"

#include "agent_compat.h"
#include "cJSON.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/can/can.h>
#include <nuttx/clock.h>

#include <ok8mp_can_diag.h>
#include <ok8mp_can_service.h>

/* Project-defined active CAN health probe.  This stays deliberately
 * separate from UDS/OBD-II: it proves that a normal CAN request/response
 * can traverse the physical bus before an Agent attempts a diagnostic
 * transaction.
 *
 * M7 -> ECU: 0x700, A5 01 5A
 * ECU -> M7: 0x708, 5A 01 5A 01
 */

#define CAN_TOOL_HEALTH_REQUEST_ID     0x700
#define CAN_TOOL_HEALTH_RESPONSE_ID    0x708
#define CAN_TOOL_HEALTH_TIMEOUT_MS     1000
#define CAN_TOOL_HEALTH_REQUEST_MAGIC  0xa5
#define CAN_TOOL_HEALTH_RESPONSE_MAGIC 0x5a
#define CAN_TOOL_HEALTH_SERVICE        0x01
#define CAN_TOOL_HEALTH_TOKEN          0x5a
#define CAN_TOOL_HEALTH_ECU_NORMAL     0x01

static uint32_t can_tool_now_ms(void)
{
    return (uint32_t)TICK2MSEC(clock_systime_ticks());
}

static const char *can_fault_name(uint8_t state)
{
    switch (state) {
    case OK8MP_CAN_ERROR_PASSIVE:
        return "error_passive";
    case OK8MP_CAN_BUS_OFF:
        return "bus_off";
    case OK8MP_CAN_ERROR_ACTIVE:
    default:
        return "error_active";
    }
}

static const char *can_state_name(enum ok8mp_can_ecu_state_e state)
{
    switch (state) {
    case OK8MP_CAN_ECU_ENGINE_OVER_TEMPERATURE:
        return "engine_over_temperature";
    case OK8MP_CAN_ECU_BATTERY_VOLTAGE_LOW:
        return "battery_voltage_low";
    default:
        return "normal";
    }
}

static const char *can_dtc_name(uint32_t code)
{
    if (code == 0x021700 || code == 0x0217)
        return "P0217";
    if (code == 0x056200 || code == 0x0562)
        return "P0562";
    return "UNKNOWN";
}

static int can_tool_error(char *output, size_t output_size,
                          const char *protocol, int ret)
{
    snprintf(output, output_size,
             "{\"ok\":false,\"protocol\":\"%s\",\"error\":%d,"
             "\"message\":\"CAN diagnostic transaction failed\"}",
             protocol, -ret);
    return ERROR;
}

static const char *can_json_string(const char *input_json, const char *name,
                                   cJSON **root)
{
    cJSON *item;

    *root = cJSON_Parse(input_json ? input_json : "{}");
    item = *root ? cJSON_GetObjectItemCaseSensitive(*root, name) : NULL;
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int can_format_dtcs(char *output, size_t output_size,
                           const char *protocol,
                           const struct ok8mp_can_dtc_s *dtcs, size_t count)
{
    size_t used;
    size_t i;
    int n;

    n = snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"%s\",\"count\":%u,"
                 "\"dtcs\":[", protocol, (unsigned int)count);
    if (n < 0 || (size_t)n >= output_size)
        return ERROR;
    used = n;

    for (i = 0; i < count; i++) {
        n = snprintf(output + used, output_size - used,
                     "%s{\"code\":\"%s\",\"raw\":\"%06lx\","
                     "\"status\":%u}", i == 0 ? "" : ",",
                     can_dtc_name(dtcs[i].code),
                     (unsigned long)dtcs[i].code, dtcs[i].status);
        if (n < 0 || (size_t)n >= output_size - used)
            return ERROR;
        used += n;
    }

    if (output_size - used < 3)
        return ERROR;
    memcpy(output + used, "]}", 3);
    return OK;
}

int tool_ok8mp_can_health_execute(const char *input_json,
                                  char *output, size_t output_size)
{
    struct ok8mp_can_service_stats_s stats;
    bool healthy;
    int ret;

    (void)input_json;
    ret = ok8mp_can_service_get_stats(&stats);
    if (ret < 0) {
        return can_tool_error(output, output_size, "can", ret);
    }

    healthy = stats.running &&
              stats.controller.fault_state == OK8MP_CAN_ERROR_ACTIVE;
    snprintf(output, output_size,
             "{\"ok\":%s,\"running\":%s,\"state\":\"%s\","
             "\"tx_error\":%u,\"rx_error\":%u,\"rx_frames\":%lu,"
             "\"tx_submitted\":%lu,\"tx_timeouts\":%lu,"
             "\"rx_dropped\":%lu,\"bus_off_count\":%lu,"
             "\"recoveries\":%lu,\"last_error\":%d}",
             healthy ? "true" : "false", stats.running ? "true" : "false",
             can_fault_name(stats.controller.fault_state),
             stats.controller.tx_error_counter, stats.controller.rx_error_counter,
             (unsigned long)stats.rx_frames,
             (unsigned long)stats.tx_submitted,
             (unsigned long)stats.tx_timeouts,
             (unsigned long)stats.rx_dropped,
             (unsigned long)stats.controller.bus_off_count,
             (unsigned long)stats.controller.recoveries, stats.last_error);
    return healthy ? OK : ERROR;
}

int tool_ok8mp_can_read_status_execute(const char *input_json,
                                       char *output, size_t output_size)
{
    return tool_ok8mp_can_status_execute(input_json, output, output_size);
}

int tool_ok8mp_can_read_dtc_execute(const char *input_json,
                                    char *output, size_t output_size)
{
    (void)input_json;
    return tool_ok8mp_can_dtc_execute("{\"action\":\"read\"}", output,
                                      output_size);
}

int tool_ok8mp_can_diagnostic_execute(const char *input_json,
                                      char *output, size_t output_size)
{
    ok8mp_can_subscription_t subscription;
    struct can_msg_s request = {0};
    struct can_msg_s response = {0};
    uint32_t start;
    uint32_t elapsed;
    int ret;

    (void)input_json;
    ret = ok8mp_can_service_subscribe(CAN_TOOL_HEALTH_RESPONSE_ID, 0x7ff,
                                      false, &subscription);
    if (ret < 0) {
        return can_tool_error(output, output_size, "can", ret);
    }

    request.cm_hdr.ch_id = CAN_TOOL_HEALTH_REQUEST_ID;
    request.cm_hdr.ch_rtr = false;
    request.cm_hdr.ch_dlc = can_bytes2dlc(3);
#ifdef CONFIG_CAN_EXTID
    request.cm_hdr.ch_extid = false;
#endif
    request.cm_data[0] = CAN_TOOL_HEALTH_REQUEST_MAGIC;
    request.cm_data[1] = CAN_TOOL_HEALTH_SERVICE;
    request.cm_data[2] = CAN_TOOL_HEALTH_TOKEN;

    ret = ok8mp_can_service_send(&request, 200);
    if (ret < 0) {
        ok8mp_can_service_unsubscribe(subscription);
        return can_tool_error(output, output_size, "can", ret);
    }

    start = can_tool_now_ms();
    do {
        elapsed = can_tool_now_ms() - start;
        if (elapsed >= CAN_TOOL_HEALTH_TIMEOUT_MS) {
            break;
        }

        ret = ok8mp_can_service_receive(subscription, &response,
                                        CAN_TOOL_HEALTH_TIMEOUT_MS - elapsed);
        if (ret < 0) {
            break;
        }

#ifdef CONFIG_CAN_EXTID
        if (response.cm_hdr.ch_extid) {
            continue;
        }
#endif
        if (response.cm_hdr.ch_id == CAN_TOOL_HEALTH_RESPONSE_ID &&
            can_dlc2bytes(response.cm_hdr.ch_dlc) == 4 &&
            response.cm_data[0] == CAN_TOOL_HEALTH_RESPONSE_MAGIC &&
            response.cm_data[1] == CAN_TOOL_HEALTH_SERVICE &&
            response.cm_data[2] == CAN_TOOL_HEALTH_TOKEN &&
            response.cm_data[3] == CAN_TOOL_HEALTH_ECU_NORMAL) {
            elapsed = can_tool_now_ms() - start;
            snprintf(output, output_size,
                     "{\"ok\":true,\"state\":\"healthy\","
                     "\"request_id\":\"0x700\",\"response_id\":\"0x708\","
                     "\"latency_ms\":%lu}", (unsigned long)elapsed);
            ok8mp_can_service_unsubscribe(subscription);
            return OK;
        }

        elapsed = can_tool_now_ms() - start;
        snprintf(output, output_size,
                 "{\"ok\":false,\"state\":\"response_error\","
                 "\"request_id\":\"0x700\",\"response_id\":\"0x708\","
                 "\"latency_ms\":%lu}", (unsigned long)elapsed);
        ok8mp_can_service_unsubscribe(subscription);
        return ERROR;
    } while (elapsed < CAN_TOOL_HEALTH_TIMEOUT_MS);

    elapsed = can_tool_now_ms() - start;
    ok8mp_can_service_unsubscribe(subscription);
    snprintf(output, output_size,
             "{\"ok\":false,\"state\":\"timeout\","
             "\"request_id\":\"0x700\",\"response_id\":\"0x708\","
             "\"timeout_ms\":%u,\"latency_ms\":%lu,\"error\":%d}",
             CAN_TOOL_HEALTH_TIMEOUT_MS, (unsigned long)elapsed,
             ret < 0 ? -ret : ETIMEDOUT);
    return ERROR;
}

int tool_ok8mp_can_status_execute(const char *input_json,
                                  char *output, size_t output_size)
{
    struct ok8mp_can_vehicle_status_s status;
    int ret;

    (void)input_json;
    ret = ok8mp_can_uds_read_status(&status);
    if (ret < 0)
        return can_tool_error(output, output_size, "uds", ret);

    snprintf(output, output_size,
             "{\"ok\":true,\"protocol\":\"uds\",\"did\":\"F100\","
             "\"state\":\"%s\",\"coolant_c\":%u,\"battery_mv\":%u,"
             "\"active_dtc\":%u,\"stored_dtc\":%u,\"sequence\":%u}",
             can_state_name(status.state), status.coolant_c,
             status.battery_mv, status.active_dtc, status.stored_dtc,
             status.sequence);
    return OK;
}

int tool_ok8mp_can_set_state_execute(const char *input_json,
                                     char *output, size_t output_size)
{
    enum ok8mp_can_ecu_state_e state;
    const char *value;
    cJSON *root;
    int ret;

    value = can_json_string(input_json, "state", &root);
    if (value && strcmp(value, "normal") == 0)
        state = OK8MP_CAN_ECU_NORMAL;
    else if (value && strcmp(value, "overtemp") == 0)
        state = OK8MP_CAN_ECU_ENGINE_OVER_TEMPERATURE;
    else if (value && strcmp(value, "low_voltage") == 0)
        state = OK8MP_CAN_ECU_BATTERY_VOLTAGE_LOW;
    else {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"invalid state\"}");
        return ERROR;
    }
    cJSON_Delete(root);

    ret = ok8mp_can_uds_set_state(state);
    if (ret < 0)
        return can_tool_error(output, output_size, "uds", ret);

    snprintf(output, output_size,
             "{\"ok\":true,\"protocol\":\"uds\",\"service\":\"2E\","
             "\"state\":\"%s\"}", can_state_name(state));
    return OK;
}

int tool_ok8mp_can_dtc_execute(const char *input_json,
                               char *output, size_t output_size)
{
    struct ok8mp_can_dtc_s dtcs[OK8MP_CAN_DIAG_MAX_DTCS];
    size_t count = OK8MP_CAN_DIAG_MAX_DTCS;
    const char *action;
    cJSON *root;
    int ret;

    action = can_json_string(input_json, "action", &root);
    if (!action) {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"action is required\"}");
        return ERROR;
    }

    if (strcmp(action, "clear") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_uds_clear_dtcs();
        if (ret < 0)
            return can_tool_error(output, output_size, "uds", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"uds\","
                 "\"service\":\"14\",\"cleared\":true}");
        return OK;
    }

    if (strcmp(action, "read") != 0) {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"invalid action\"}");
        return ERROR;
    }
    cJSON_Delete(root);

    ret = ok8mp_can_uds_read_dtcs(dtcs, &count);
    if (ret < 0)
        return can_tool_error(output, output_size, "uds", ret);
    return can_format_dtcs(output, output_size, "uds", dtcs, count);
}

int tool_ok8mp_can_obd_execute(const char *input_json,
                               char *output, size_t output_size)
{
    struct ok8mp_can_dtc_s dtcs[OK8MP_CAN_DIAG_MAX_DTCS];
    size_t count = OK8MP_CAN_DIAG_MAX_DTCS;
    char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1];
    const char *query;
    uint32_t mask;
    uint16_t voltage;
    int16_t coolant;
    cJSON *root;
    int ret;

    query = can_json_string(input_json, "query", &root);
    if (!query) {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"query is required\"}");
        return ERROR;
    }

    if (strcmp(query, "supported_pids") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_obd_supported_pids(0x00, &mask);
        if (ret < 0)
            return can_tool_error(output, output_size, "obd2", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"obd2\","
                 "\"mode\":\"01\",\"base_pid\":\"00\","
                 "\"supported_mask\":\"%08lx\"}", (unsigned long)mask);
        return OK;
    }

    if (strcmp(query, "coolant") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_obd_read_coolant(&coolant);
        if (ret < 0)
            return can_tool_error(output, output_size, "obd2", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"obd2\","
                 "\"mode\":\"01\",\"pid\":\"05\","
                 "\"coolant_c\":%d}", coolant);
        return OK;
    }

    if (strcmp(query, "battery_voltage") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_obd_read_voltage(&voltage);
        if (ret < 0)
            return can_tool_error(output, output_size, "obd2", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"obd2\","
                 "\"mode\":\"01\",\"pid\":\"42\","
                 "\"battery_mv\":%u}", voltage);
        return OK;
    }

    if (strcmp(query, "dtc") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_obd_read_dtcs(dtcs, &count);
        if (ret < 0)
            return can_tool_error(output, output_size, "obd2", ret);
        return can_format_dtcs(output, output_size, "obd2", dtcs, count);
    }

    if (strcmp(query, "clear_dtc") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_obd_clear_dtcs();
        if (ret < 0)
            return can_tool_error(output, output_size, "obd2", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"obd2\","
                 "\"mode\":\"04\",\"cleared\":true}");
        return OK;
    }

    if (strcmp(query, "vin") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_obd_read_vin(vin);
        if (ret < 0)
            return can_tool_error(output, output_size, "obd2", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"obd2\","
                 "\"mode\":\"09\",\"pid\":\"02\","
                 "\"vin\":\"%s\"}", vin);
        return OK;
    }

    cJSON_Delete(root);
    snprintf(output, output_size,
             "{\"ok\":false,\"error\":\"invalid OBD-II query\"}");
    return ERROR;
}

int tool_ok8mp_can_uds_execute(const char *input_json,
                               char *output, size_t output_size)
{
    char vin[OK8MP_CAN_DIAG_VIN_LENGTH + 1];
    const char *operation;
    cJSON *root;
    int ret;

    operation = can_json_string(input_json, "operation", &root);
    if (!operation) {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"operation is required\"}");
        return ERROR;
    }

    if (strcmp(operation, "read_vin") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_uds_read_vin(vin);
        if (ret < 0)
            return can_tool_error(output, output_size, "uds", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"uds\","
                 "\"service\":\"22\",\"did\":\"F190\","
                 "\"vin\":\"%s\"}", vin);
        return OK;
    }

    if (strcmp(operation, "tester_present") == 0) {
        cJSON_Delete(root);
        ret = ok8mp_can_uds_tester_present();
        if (ret < 0)
            return can_tool_error(output, output_size, "uds", ret);
        snprintf(output, output_size,
                 "{\"ok\":true,\"protocol\":\"uds\","
                 "\"service\":\"3E\",\"tester_present\":true}");
        return OK;
    }

    cJSON_Delete(root);
    snprintf(output, output_size,
             "{\"ok\":false,\"error\":\"invalid UDS operation\"}");
    return ERROR;
}
