/****************************************************************************
 * apps/include/ok8mp_can_isotp.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_INCLUDE_OK8MP_CAN_ISOTP_H
#define __APPS_INCLUDE_OK8MP_CAN_ISOTP_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Classic-CAN ISO-TP normal addressing.  The implementation supports
 * single and multi-frame request/response payloads, 12-bit First Frame
 * lengths, Flow Control (CTS/WAIT/OVERFLOW), block size and STmin.
 */

#define OK8MP_CAN_ISOTP_MAX_PAYLOAD 128

int ok8mp_can_isotp_request(uint32_t request_id, uint32_t response_id,
                            bool extended,
                            FAR const uint8_t *request,
                            size_t request_length,
                            FAR uint8_t *response,
                            FAR size_t *response_length,
                            uint32_t timeout_ms);

#endif /* __APPS_INCLUDE_OK8MP_CAN_ISOTP_H */
