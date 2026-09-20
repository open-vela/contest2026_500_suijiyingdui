/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tools/tool_ok8mp_voice.h"
#include "agent_compat.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/sensors/ok8mp_voice.h>
#include <sensor/voice_command.h>
#include <uORB/uORB.h>

#include "cJSON.h"

#define OK8MP_VOICE_TOOL_RETRIES    3
#define OK8MP_VOICE_TOOL_RETRY_US   2000

static int ok8mp_voice_open(char *output, size_t output_size, int flags)
{
    int fd = open(OK8MP_VOICE_DEVPATH, flags);

    if (fd < 0) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"open %s\",\"errno\":%d}",
                 OK8MP_VOICE_DEVPATH, errno);
    }

    return fd;
}

static int json_get_byte(cJSON *root, const char *name, uint8_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);

    if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||
        item->valuedouble > 255) {
        return ERROR;
    }

    *value = (uint8_t)item->valueint;
    return OK;
}

static bool ok8mp_voice_retryable_errno(int errcode)
{
    /* MX8MP I2C maps arbitration lost to EACCES. */

    return errcode == EACCES || errcode == EBUSY || errcode == EAGAIN ||
           errcode == ETIMEDOUT;
}

static int ok8mp_voice_ioctl_retry(int fd, int cmd, unsigned long arg,
                                   const char *operation,
                                   char *output, size_t output_size)
{
    int attempt;
    int errcode = EIO;

    for (attempt = 1; attempt <= OK8MP_VOICE_TOOL_RETRIES; attempt++) {
        if (ioctl(fd, cmd, arg) >= 0) {
            return OK;
        }

        errcode = errno;
        if (!ok8mp_voice_retryable_errno(errcode) ||
            attempt == OK8MP_VOICE_TOOL_RETRIES) {
            break;
        }

        usleep(OK8MP_VOICE_TOOL_RETRY_US * attempt);
    }

    snprintf(output, output_size,
             "{\"ok\":false,\"error\":\"%s\",\"errno\":%d,"
             "\"attempts\":%d}",
             operation, errcode, attempt);
    return ERROR;
}

int tool_ok8mp_voice_status_execute(const char *input_json,
                                    char *output, size_t output_size)
{
    uint8_t version = 0xff;
    uint8_t busy = 0xff;
    uint8_t count = 0xff;
    uint8_t result = 0xff;
    int fd;

    (void)input_json;
    fd = ok8mp_voice_open(output, output_size, O_RDONLY);
    if (fd < 0) {
        return ERROR;
    }

    if (ok8mp_voice_ioctl_retry(fd, OK8MP_VOICEIOC_GET_VERSION,
                                (unsigned long)&version, "get version",
                                output, output_size) < 0 ||
        ok8mp_voice_ioctl_retry(fd, OK8MP_VOICEIOC_GET_BUSY,
                                (unsigned long)&busy, "get busy",
                                output, output_size) < 0 ||
        ok8mp_voice_ioctl_retry(fd, OK8MP_VOICEIOC_GET_COUNT,
                                (unsigned long)&count, "get word count",
                                output, output_size) < 0 ||
        ok8mp_voice_ioctl_retry(fd, OK8MP_VOICEIOC_GET_RESULT,
                                (unsigned long)&result, "get result",
                                output, output_size) < 0) {
        close(fd);
        return ERROR;
    }

    close(fd);
    snprintf(output, output_size,
             "{\"ok\":true,\"version\":%u,\"busy\":%u,"
             "\"word_count\":%u,\"command_id\":%u}",
             version, busy, count, result);
    return OK;
}

int tool_ok8mp_voice_rgb_execute(const char *input_json,
                                 char *output, size_t output_size)
{
    uint8_t rgb[3];
    cJSON *root;
    int fd;

    root = cJSON_Parse(input_json ? input_json : "{}");
    if (root == NULL || json_get_byte(root, "red", &rgb[0]) < 0 ||
        json_get_byte(root, "green", &rgb[1]) < 0 ||
        json_get_byte(root, "blue", &rgb[2]) < 0) {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"red/green/blue must be 0..255\"}");
        return ERROR;
    }

    cJSON_Delete(root);
    fd = ok8mp_voice_open(output, output_size, O_RDWR);
    if (fd < 0) {
        return ERROR;
    }

    if (ok8mp_voice_ioctl_retry(fd, OK8MP_VOICEIOC_SET_RGB,
                                (unsigned long)rgb, "set RGB",
                                output, output_size) < 0) {
        close(fd);
        return ERROR;
    }

    close(fd);
    snprintf(output, output_size,
             "{\"ok\":true,\"red\":%u,\"green\":%u,\"blue\":%u}",
             rgb[0], rgb[1], rgb[2]);
    return OK;
}

int tool_ok8mp_voice_buzzer_execute(const char *input_json,
                                    char *output, size_t output_size)
{
    uint8_t enabled;
    cJSON *root;
    cJSON *item;
    int fd;

    root = cJSON_Parse(input_json ? input_json : "{}");
    item = root ? cJSON_GetObjectItemCaseSensitive(root, "enabled") : NULL;
    if (cJSON_IsBool(item)) {
        enabled = cJSON_IsTrue(item) ? 1 : 0;
    } else if (cJSON_IsNumber(item) &&
               (item->valueint == 0 || item->valueint == 1)) {
        enabled = (uint8_t)item->valueint;
    } else {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"enabled must be boolean\"}");
        return ERROR;
    }

    cJSON_Delete(root);
    fd = ok8mp_voice_open(output, output_size, O_RDWR);
    if (fd < 0) {
        return ERROR;
    }

    if (ok8mp_voice_ioctl_retry(fd, OK8MP_VOICEIOC_SET_BUZZER,
                                (unsigned long)enabled, "set buzzer",
                                output, output_size) < 0) {
        close(fd);
        return ERROR;
    }

    close(fd);
    snprintf(output, output_size,
             "{\"ok\":true,\"enabled\":%s}",
             enabled ? "true" : "false");
    return OK;
}

int tool_ok8mp_wait_voice_execute(const char *input_json,
                                  char *output, size_t output_size)
{
    struct sensor_voice_command sample;
    struct pollfd pfd;
    cJSON *root;
    cJSON *item;
    int timeout_ms = 10000;
    int fd;
    int ret;

    root = cJSON_Parse(input_json ? input_json : "{}");
    item = root ? cJSON_GetObjectItemCaseSensitive(root, "timeout_ms") : NULL;
    if (cJSON_IsNumber(item)) {
        timeout_ms = item->valueint;
    }
    cJSON_Delete(root);

    if (timeout_ms < 1 || timeout_ms > 60000) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"timeout_ms range is 1..60000\"}");
        return ERROR;
    }

    fd = orb_subscribe(ORB_ID(sensor_voice_command));
    if (fd < 0) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"voice topic unavailable\","
                 "\"hint\":\"run voice_uorb start\"}");
        return ERROR;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) {
        orb_close(fd);
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"%s\"}",
                 ret == 0 ? "timeout" : "poll failed");
        return ERROR;
    }

    ret = orb_copy(ORB_ID(sensor_voice_command), fd, &sample);
    orb_close(fd);
    if (ret < 0) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"uORB copy\",\"errno\":%d}",
                 errno);
        return ERROR;
    }

    snprintf(output, output_size,
             "{\"ok\":true,\"timestamp\":%llu,\"command_id\":%u,"
             "\"valid\":%u,\"busy\":%u,\"word_count\":%u}",
             (unsigned long long)sample.timestamp, sample.command_id,
             sample.valid, sample.busy, sample.word_count);
    return OK;
}
