#!/usr/bin/env python3
############################################################################
# tools/ok8mp_uart_cmocka.py
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

"""Run the openvela cmocka UART burst test through the OK8MP console."""

import argparse
import random
import string
import sys
import time

ALPHABET = string.ascii_letters + string.digits + ".,:;_+-="


def read_until_quiet(port, quiet_seconds=0.25, timeout_seconds=3.0):
    """Collect bytes until the serial line remains quiet."""

    output = bytearray()
    deadline = time.monotonic() + timeout_seconds
    quiet_deadline = time.monotonic() + quiet_seconds

    while time.monotonic() < deadline:
        waiting = port.in_waiting
        if waiting:
            output.extend(port.read(waiting))
            quiet_deadline = time.monotonic() + quiet_seconds
        elif time.monotonic() >= quiet_deadline:
            break
        else:
            time.sleep(0.01)

    return bytes(output)


def read_through(port, marker, timeout_seconds=3.0):
    """Read until marker appears, returning all bytes received."""

    output = bytearray()
    deadline = time.monotonic() + timeout_seconds

    while time.monotonic() < deadline:
        waiting = port.in_waiting
        if waiting:
            output.extend(port.read(waiting))
            if marker in output:
                return bytes(output)
        else:
            time.sleep(0.01)

    raise TimeoutError(f"timeout waiting for {marker!r}: {bytes(output)!r}")


def main():
    parser = argparse.ArgumentParser(
        description="Run cmocka_driver_uart case 2 on the OK8MP console"
    )
    parser.add_argument(
        "port",
        help="macOS serial device, for example /dev/cu.usbmodem58BE0307011",
    )
    parser.add_argument("-b", "--baudrate", type=int, default=115200)
    parser.add_argument("-t", "--turns", type=int, default=10)
    parser.add_argument("-l", "--max-length", type=int, default=100)
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print(
            "pyserial is required. Create a virtual environment, activate it, "
            "then run: python -m pip install pyserial"
        )
        return 2

    if args.turns < 1 or not 1 <= args.max_length <= 1000:
        parser.error("turns must be positive and max-length must be 1..1000")

    with serial.Serial(args.port, args.baudrate, timeout=0.05) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        port.write(b"cmocka_driver_uart -d /dev/ttyS0 -n 2\r\n")
        port.flush()

        startup = read_until_quiet(port, quiet_seconds=0.4, timeout_seconds=4.0)
        sys.stdout.buffer.write(startup)
        sys.stdout.flush()

        for turn in range(1, args.turns + 1):
            length = random.randint(1, args.max_length)
            payload = "".join(random.choices(ALPHABET, k=length)).encode()

            port.write(f"{length}#".encode())
            port.write(payload)
            port.flush()

            received = read_through(port, payload, timeout_seconds=4.0)
            if not received.endswith(payload):
                print(f"\nTURN[{turn}]: FAIL: unexpected echo {received!r}")
                port.write(b"fail#0#")
                return 1

            port.write(b"pass#")
            port.flush()
            print(f"TURN[{turn}]: PASS ({length} bytes)")

        port.write(b"0#")
        port.flush()
        result = read_through(port, b"nsh>", timeout_seconds=8.0)
        sys.stdout.buffer.write(result)
        sys.stdout.flush()

        if b"PASSED" not in result:
            print("UART CMocka result did not contain PASSED")
            return 1

    print("OK8MP UART RX/TX burst test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
