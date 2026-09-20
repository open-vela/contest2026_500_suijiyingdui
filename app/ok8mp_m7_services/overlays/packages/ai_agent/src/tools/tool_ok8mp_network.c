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

#include "tools/tool_ok8mp_network.h"

#include "agent_compat.h"
#include "infra/vela_tls.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define OK8MP_HEALTH_HOST       "example.com"
#define OK8MP_HEALTH_PORT       "443"
#define OK8MP_HEALTH_PATH       "/"
#define OK8MP_HEALTH_BODY_SIZE  1024

static int ok8mp_resolve_health_host(char *address, size_t address_size)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *item;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(OK8MP_HEALTH_HOST, OK8MP_HEALTH_PORT, &hints, &result);
    if (ret != 0) {
        return ret;
    }

    ret = EAI_NONAME;
    for (item = result; item != NULL; item = item->ai_next) {
        const void *raw = NULL;

        if (item->ai_family == AF_INET) {
            raw = &((const struct sockaddr_in *)item->ai_addr)->sin_addr;
        } else if (item->ai_family == AF_INET6) {
            raw = &((const struct sockaddr_in6 *)item->ai_addr)->sin6_addr;
        }

        if (raw != NULL && inet_ntop(item->ai_family, raw, address,
                                     address_size) != NULL) {
            ret = 0;
            break;
        }
    }

    freeaddrinfo(result);
    return ret;
}

int tool_ok8mp_network_health_execute(const char *input_json,
                                      char *output, size_t output_size)
{
    char address[INET6_ADDRSTRLEN] = "";
    char *body;
    size_t body_len = 0;
    int http_status;
    int ret;

    (void)input_json;

    ret = ok8mp_resolve_health_host(address, sizeof(address));
    if (ret != 0) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"host\":\"%s\",\"dns\":false,"
                 "\"tls_verified\":false,\"error\":\"DNS resolution\","
                 "\"code\":%d}",
                 OK8MP_HEALTH_HOST, ret);
        return ERROR;
    }

    body = malloc(OK8MP_HEALTH_BODY_SIZE);
    if (body == NULL) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"host\":\"%s\",\"dns\":true,"
                 "\"address\":\"%s\",\"tls_verified\":false,"
                 "\"error\":\"out of memory\"}",
                 OK8MP_HEALTH_HOST, address);
        return ERROR;
    }

    http_status = vela_https_request(OK8MP_HEALTH_HOST, OK8MP_HEALTH_PORT,
                                     "GET", OK8MP_HEALTH_PATH, NULL, NULL, 0,
                                     body, OK8MP_HEALTH_BODY_SIZE, &body_len);
    free(body);

    if (http_status < 0) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"host\":\"%s\",\"address\":\"%s\","
                 "\"dns\":true,\"tls_verified\":false,"
                 "\"error\":\"HTTPS request\",\"code\":%d}",
                 OK8MP_HEALTH_HOST, address, http_status);
        return ERROR;
    }

    if (http_status < 200 || http_status > 299) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"host\":\"%s\",\"address\":\"%s\","
                 "\"dns\":true,\"tls_verified\":true,"
                 "\"http_status\":%d,\"received\":%lu}",
                 OK8MP_HEALTH_HOST, address, http_status,
                 (unsigned long)body_len);
        return ERROR;
    }

    snprintf(output, output_size,
             "{\"ok\":true,\"host\":\"%s\",\"address\":\"%s\","
             "\"dns\":true,\"tls_verified\":true,"
             "\"http_status\":%d,\"received\":%lu}",
             OK8MP_HEALTH_HOST, address, http_status,
             (unsigned long)body_len);
    return OK;
}
