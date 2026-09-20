#!/usr/bin/env python3
############################################################################
# tools/run_sbrb_timeout.py
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

"""Run the bundled sbrb.py with a finite pyserial read timeout.

Some NSH console configurations do not echo the complete ``sb`` command.
Older sbrb.py revisions wait indefinitely for that exact echo before starting
the YMODEM receiver.  This wrapper leaves the protocol implementation
unchanged and only makes serial reads return after one second.
"""

from __future__ import annotations

import runpy
import sys
import time
from pathlib import Path

import serial


_serial_init = serial.Serial.__init__
_reset_input_buffer = serial.Serial.reset_input_buffer


def _serial_init_with_timeout(self, *args, **kwargs):
    kwargs["timeout"] = 1.0
    return _serial_init(self, *args, **kwargs)


def _reset_input_buffer_after_quiet_period(self):
    # The final CR/LF from the echoed NSH command can arrive just after
    # sbrb.py's immediate reset.  Let the console become quiet first so those
    # bytes cannot be mistaken for a YMODEM packet header.

    time.sleep(0.25)
    return _reset_input_buffer(self)


serial.Serial.__init__ = _serial_init_with_timeout
serial.Serial.reset_input_buffer = _reset_input_buffer_after_quiet_period

project_root = Path(__file__).resolve().parents[1]
sbrb = project_root / "apps" / "system" / "ymodem" / "sbrb.py"
sys.argv = [str(sbrb), *sys.argv[1:]]
runpy.run_path(str(sbrb), run_name="__main__")
