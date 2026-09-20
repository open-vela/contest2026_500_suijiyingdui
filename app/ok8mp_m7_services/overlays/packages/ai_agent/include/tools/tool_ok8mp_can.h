/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stddef.h>

/* Stable CAN Tool names exposed to the AI Agent. */

int tool_ok8mp_can_health_execute(const char *input_json,
                                  char *output, size_t output_size);
int tool_ok8mp_can_read_status_execute(const char *input_json,
                                       char *output, size_t output_size);
int tool_ok8mp_can_read_dtc_execute(const char *input_json,
                                    char *output, size_t output_size);
int tool_ok8mp_can_diagnostic_execute(const char *input_json,
                                      char *output, size_t output_size);

/* Backward-compatible Tool handlers retained for existing prompts. */

int tool_ok8mp_can_status_execute(const char *input_json,
                                  char *output, size_t output_size);
int tool_ok8mp_can_set_state_execute(const char *input_json,
                                     char *output, size_t output_size);
int tool_ok8mp_can_dtc_execute(const char *input_json,
                               char *output, size_t output_size);
int tool_ok8mp_can_obd_execute(const char *input_json,
                               char *output, size_t output_size);
int tool_ok8mp_can_uds_execute(const char *input_json,
                               char *output, size_t output_size);
