/****************************************************************************
 * boards/arm/mx8mp/ok8mp-m7/src/mx8mp_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/board/board.h>
#include <nuttx/leds/userled.h>
#include <nuttx/input/buttons.h>
#include <debug.h>

#include <sched.h>
#include <stdio.h>
#include <unistd.h>

#include "ok8mp-m7.h"
#include "mx8mp_gpio.h"

#ifdef CONFIG_SENSORS_INA219
#  include "mx8mp_ina219.h"
#endif

#ifdef CONFIG_MX8MP_RPMSG
#  include <mx8mp_rptun.h>
#endif

#ifdef CONFIG_RPMSG_UART
#  include <nuttx/serial/uart_rpmsg.h>
#endif

#if defined(CONFIG_FS_PROCFS) || defined(CONFIG_FS_TMPFS)
#  include <nuttx/fs/fs.h>
#endif


/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_OK8MP_M7_HEARTBEAT
static int ok8mp_m7_heartbeat_main(int argc, FAR char *argv[])
{
  unsigned long tick = 0;

  while (true)
    {
      printf("[M7 heartbeat] tick=%lu\n", ++tick);
      fflush(stdout);
      sleep(CONFIG_OK8MP_M7_HEARTBEAT_INTERVAL);
    }

  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_RPMSG_UART
void rpmsg_serialinit(void)
{
  uart_rpmsg_init("netcore", "proxy", 4096, true);
}
#endif

/****************************************************************************
 * Name: mx8mp_bringup
 *
 * Description:
 *   Bring up board features
 *
 ****************************************************************************/

int mx8mp_bringup(void)
{
  int ret = OK;

#ifdef CONFIG_MX8MP_RPMSG
  mx8mp_rptun_init("imx8mp-shmem", "netcore");
#endif /* CONFIG_MX8MP_RPMSG */

#if defined(CONFIG_USERLED) && !defined(CONFIG_ARCH_LEDS)
#ifdef CONFIG_USERLED_LOWER
  /* Register the LED driver */

  ret = userled_lower_initialize("/dev/userleds");
  if (ret != OK)
    {
      syslog(LOG_ERR, "ERROR: userled_lower_initialize() failed: %d\n", ret);
      return ret;
    }
#else
  /* Enable USER LED support for some other purpose */

  board_userled_initialize();
#endif /* CONFIG_USERLED_LOWER */
#endif /* CONFIG_USERLED && !CONFIG_ARCH_LEDS */

#ifdef CONFIG_INPUT_BUTTONS
#ifdef CONFIG_INPUT_BUTTONS_LOWER
  /* Register the BUTTON driver */

  ret = btn_lower_initialize("/dev/buttons");
  if (ret != OK)
    {
      syslog(LOG_ERR, "ERROR: btn_lower_initialize() failed: %d\n", ret);
      return ret;
    }
#else
  /* Enable BUTTON support for some other purpose */

  board_button_initialize();
#endif /* CONFIG_INPUT_BUTTONS_LOWER */
#endif /* CONFIG_INPUT_BUTTONS */

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */

  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount procfs at /proc: %d\n", ret);
    }
#endif

#ifdef CONFIG_FS_TMPFS
  /* The official openvela AI Agent stores its runtime configuration,
   * sessions and Markdown Skills below /data/agent.  The M7 firmware
   * currently has no persistent block device, so provide a writable tmpfs
   * mount for bring-up and competition demonstrations.
   */

  ret = nx_mount(NULL, "/data", "tmpfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount tmpfs at /data: %d\n", ret);
    }
#endif

#ifdef CONFIG_MX8MP_I2C_DRIVER
#ifdef CONFIG_OK8MP_M7_VOICE_I2C
  /* The OK8MP voice module is physically connected to M7 I2C3.  Configure
   * its clock root, gate and pads before the generic I2C character driver
   * takes its first reference.  Do not rely on state left by U-Boot/Linux. */

  ret = ok8mp_voice_i2c_prepare();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ok8mp_voice_i2c_prepare() failed: %d\n",
             ret);
    }
#endif

  /* Initialize I2C buses */

  ret = mx8mp_i2cdev_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: mx8mp_i2cdev_initialize() failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_OK8MP_M7_VOICE_I2C
  /* Register the OK8MP-M7 I2C voice sensor test device */

  ret = ok8mp_voice_i2c_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ok8mp_voice_i2c_initialize() failed: %d\n",
             ret);
    }
#endif

#ifdef CONFIG_OK8MP_M7_FEC_NET
  /* Register ENET1/FEC before netinit/NSH configures eth0. */

  ret = ok8mp_fec_netinitialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ok8mp_fec_netinitialize() failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_OK8MP_M7_FLEXCAN1
  /* Register /dev/can0 during board bring-up.  The FlexCAN lower half only
   * configures clocks, pads and IRQ when an application opens the device. */

  ret = ok8mp_flexcan_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ok8mp_flexcan_initialize() failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_SENSORS_INA219
  /* Configure and initialize the INA219 sensor in I2C4 */

  ret = board_ina219_initialize(4);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: mx8mp_ina219_initialize() failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_MX8MP_SPI_DRIVER
  /* Initialize SPI buses */

  ret = mx8mp_spidev_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: spidev_initialize() failed: %d\n", ret);
    }
#endif


#ifdef CONFIG_OK8MP_M7_HEARTBEAT
  ret = task_create("m7_heartbeat",
                    CONFIG_OK8MP_M7_HEARTBEAT_PRIORITY,
                    CONFIG_OK8MP_M7_HEARTBEAT_STACKSIZE,
                    ok8mp_m7_heartbeat_main,
                    NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: m7_heartbeat task_create() failed: %d\n",
             ret);
    }
#endif

  return ret;
}
