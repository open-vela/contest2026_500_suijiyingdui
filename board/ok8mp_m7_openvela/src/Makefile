############################################################################
# boards/arm/mx8mp/ok8mp-m7/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(TOPDIR)/Make.defs

CSRCS = mx8mp_boot.c mx8mp_bringup.c mx8mp_appinit.c

ifeq ($(CONFIG_SENSORS_INA219),y)
  CSRCS += mx8mp_ina219.c
endif

ifeq ($(CONFIG_MX8MP_I2C_DRIVER),y)
  CSRCS += mx8mp_i2cdev.c
endif

ifeq ($(CONFIG_OK8MP_M7_VOICE_I2C),y)
  CSRCS += ok8mp_voice_i2c.c
endif

ifneq ($(filter y,$(CONFIG_OK8MP_M7_FEC_PROBE) $(CONFIG_OK8MP_M7_FEC_NET)),)
  CSRCS += ok8mp_fec.c
endif

ifeq ($(CONFIG_OK8MP_M7_FLEXCAN1),y)
  CSRCS += ok8mp_flexcan.c
endif

ifeq ($(CONFIG_SPI),y)
CSRCS += mx8mp_spi.c
endif

ifeq ($(CONFIG_MX8MP_SPI_DRIVER),y)
  CSRCS += mx8mp_spidev.c
endif

ifeq ($(CONFIG_ARCH_LEDS),y)
  CSRCS += mx8mp_autoleds.c
else
  CSRCS += mx8mp_userleds.c
endif

ifeq ($(CONFIG_ARCH_BUTTONS),y)
  CSRCS += mx8mp_buttons.c
endif


include $(TOPDIR)/boards/Board.mk
