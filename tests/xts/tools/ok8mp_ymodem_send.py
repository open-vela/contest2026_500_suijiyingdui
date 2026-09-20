#!/usr/bin/env python3
############################################################################
# tools/ok8mp_ymodem_send.py
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

"""Reliably send files from a host to the OK8MP M7 NSH console.

The generic sbrb.py command discards the first CRC request emitted by NuttX
``rb`` and then performs a second handshake.  On the OK8MP console UART this
can leave ``rb`` and the host sender out of phase.  This helper keeps command
synchronization and YMODEM transfer in one serial session:

1. synchronize with a clean NSH prompt;
2. start ``rb`` on the target;
3. wait for (and consume) the first receiver ``C``;
4. immediately send the YMODEM filename packet.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import time
from pathlib import Path

import serial


SYNC_MARKER = b"__OK8MP_YMODEM_READY__"
CRC_REQUEST = b"C"


def load_sbrb(project_root: Path):
    source = project_root / "apps" / "system" / "ymodem" / "sbrb.py"
    spec = importlib.util.spec_from_file_location("ok8mp_sbrb", source)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {source}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_until(ser: serial.Serial, needle: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    received = bytearray()

    while time.monotonic() < deadline:
        chunk = ser.read(1)
        if chunk:
            received.extend(chunk)
            if needle in received:
                return bytes(received)

    raise TimeoutError(
        f"timeout waiting for {needle!r}; received={bytes(received)!r}"
    )


def synchronize_nsh(ser: serial.Serial) -> None:
    # Finish any partial line left by an interrupted binary transfer and ask
    # NSH for an unambiguous marker.  The whole operation is performed while
    # this process exclusively owns the serial device.

    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write(b"\x03\r\n")
    ser.flush()
    time.sleep(0.2)
    ser.reset_input_buffer()

    ser.write(b"echo " + SYNC_MARKER + b"\r\n")
    ser.flush()
    response = read_until(ser, b"nsh>", 5.0)
    if b"Unknown command" in response or SYNC_MARKER not in response:
        raise RuntimeError(f"NSH synchronization failed: {response!r}")


def start_receiver(ser: serial.Serial, destination: str) -> None:
    # The OK8MP console may report EAGAIN repeatedly while no byte is
    # available.  rb counts those returns as protocol failures, so its
    # default retry value (100) can be exhausted before the Python sender
    # emits the filename packet.  Keep rb alive long enough to remove that
    # host/target scheduling race.

    command = f"rb -r 20000 -f {destination}\r\n".encode()
    ser.reset_input_buffer()
    ser.write(command)
    ser.flush()

    response = read_until(ser, CRC_REQUEST, 10.0)
    if b"Unknown command" in response or b"Usage:" in response:
        raise RuntimeError(f"target rejected rb command: {response!r}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Send a file to the OK8MP M7 using synchronized YMODEM"
    )
    parser.add_argument("file", help="host file to send")
    parser.add_argument("-t", "--tty", required=True, help="serial device")
    parser.add_argument("-b", "--baudrate", type=int, default=115200)
    parser.add_argument("-s", "--sendto", default="/tmp", help="M7 directory")
    args = parser.parse_args()

    source = Path(args.file).expanduser().resolve()
    if not source.is_file():
        parser.error(f"file does not exist: {source}")

    project_root = Path(__file__).resolve().parents[1]
    sbrb = load_sbrb(project_root)

    try:
        ser = serial.Serial(
            args.tty,
            baudrate=args.baudrate,
            timeout=0.2,
            write_timeout=2.0,
            exclusive=True,
        )
    except (TypeError, ValueError):
        # Older pyserial versions may not expose the POSIX exclusive option.

        ser = serial.Serial(
            args.tty,
            baudrate=args.baudrate,
            timeout=0.2,
            write_timeout=2.0,
        )

    with ser:
        sbrb.fd_serial = ser
        sender = sbrb.ymodem(
            read=sbrb.ymodem_ser_read,
            write=sbrb.ymodem_ser_write,
            clear=sbrb.ymodem_ser_clear,
            packet128=True,
        )

        # start_receiver() consumes the receiver's first 'C'.  Once it
        # returns, sender.send() must emit the filename packet immediately.

        sender.send_handshake = lambda: True

        synchronize_nsh(ser)
        print("NSH synchronized")

        print(f"sending {source.name} ({os.path.getsize(source)} bytes)")
        start_receiver(ser, args.sendto)
        result = sender.send([str(source)])
        if result is not None and result < 0:
            raise RuntimeError(f"YMODEM transfer failed: {result}")

        ser.write(b"\n")
        ser.flush()

    print("OK8MP YMODEM send: PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, serial.SerialException, TimeoutError, RuntimeError) as exc:
        print(f"OK8MP YMODEM send: FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
