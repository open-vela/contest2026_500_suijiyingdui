/****************************************************************************
 * boards/arm/mx8mp/ok8mp-m7/src/mx8mp_appinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "ok8mp-m7.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   This entry point is required when boardctl() support is enabled.  Board
 *   device bring-up is already handled by board_late_initialize().
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  return OK;
}
