#!/usr/bin/env python3
############################################################################
# tests/xts/tools/ok8mp_ymodem_receive.py
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

"""Receive one file from an OK8MP M7 NSH console over YMODEM.

The console UART is shared by NSH and the YMODEM ``sb`` command.  The generic
``sbrb.py -r`` path can send its first CRC request before ``sb`` has switched
the console to raw YMODEM mode.  NSH then consumes that request and both sides
wait forever.  This helper keeps command launch, CRC negotiation, and packet
reception in one exclusive serial session.  It sends a CRC request at a short
interval only until packet zero arrives, then completes a standard YMODEM
receive transaction.
"""

from __future__ import annotations

import argparse
import importlib.util
import sys
import time
from pathlib import Path, PurePath

import serial


SOH = b"\x01"
STX = b"\x02"
STC = b"\x03"
EOT = b"\x04"
ACK = b"\x06"
NAK = b"\x15"
CRC = b"C"
EEOT = 3
SYNC_MARKER = b"__OK8MP_YMODEM_READY__"


def load_sbrb(submission_root: Path):
    source = submission_root / "source" / "apps" / "system" / "ymodem" / "sbrb.py"
    spec = importlib.util.spec_from_file_location("ok8mp_sbrb", source)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {source}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PrefixedSerialReader:
    """Read a saved packet header before reading the remaining serial bytes."""

    def __init__(self, ser: serial.Serial, prefix: bytes, timeout: float):
        self._ser = ser
        self._prefix = bytearray(prefix)
        self._timeout = timeout

    def read(self, size: int) -> bytes:
        data = bytearray()
        deadline = time.monotonic() + self._timeout

        while len(data) < size:
            if self._prefix:
                take = min(size - len(data), len(self._prefix))
                data += self._prefix[:take]
                del self._prefix[:take]
                continue

            chunk = self._ser.read(size - len(data))
            if chunk:
                data += chunk
                continue

            if time.monotonic() >= deadline:
                break

        return bytes(data)


def read_until(ser: serial.Serial, needle: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    received = bytearray()

    while time.monotonic() < deadline:
        chunk = ser.read(1)
        if chunk:
            received += chunk
            if needle in received:
                return bytes(received)

    raise TimeoutError(f"timeout waiting for {needle!r}; received={bytes(received)!r}")


def synchronize_nsh(ser: serial.Serial) -> None:
    """Finish a partial console line and verify that NSH is idle."""

    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write(b"\x03\r\n")
    ser.flush()
    time.sleep(0.2)
    ser.reset_input_buffer()

    ser.write(b"echo " + SYNC_MARKER + b"\r\n")
    ser.flush()
    response = read_until(ser, b"nsh>", 5.0)
    if SYNC_MARKER not in response or b"Unknown command" in response:
        raise RuntimeError(f"NSH synchronization failed: {response!r}")


def start_target_sender(ser: serial.Serial, source: str, timeout: float) -> bytes:
    """Run ``sb`` and return its first YMODEM packet header.

    A CRC request sent before NSH finishes launching ``sb`` is discarded by
    the shell.  Probe at a short interval, but stop immediately once packet
    zero appears so no additional CRC byte enters the sender's ACK queue.
    """

    if "\r" in source or "\n" in source:
        raise ValueError("M7 source path must not contain a line break")

    ser.reset_input_buffer()
    ser.write(f"sb {source}\r\n".encode())
    ser.flush()

    deadline = time.monotonic() + timeout
    next_crc = time.monotonic() + 0.20
    console = bytearray()

    while time.monotonic() < deadline:
        chunk = ser.read(1)
        if chunk in (SOH, STX, STC):
            return chunk

        if chunk and len(console) < 2048:
            console += chunk

        now = time.monotonic()
        if now >= next_crc:
            ser.write(CRC)
            ser.flush()
            next_crc = now + 0.25

    text = console.decode("utf-8", errors="replace")
    raise TimeoutError(
        "M7 sb did not send YMODEM packet zero; console output was: " + repr(text)
    )


def receive_target_file(receiver, output_dir: Path) -> Path:
    """Receive one YMODEM file after its first packet header was observed."""

    receiver.init_pkt()
    ret = receiver.recv_packet()
    if ret < 0:
        raise RuntimeError(f"invalid YMODEM filename packet: {ret}")

    raw_name = receiver.data.split(b"\x00", 1)[0]
    raw_size = receiver.data.split(b"\x00", 2)[1]
    if not raw_name:
        raise RuntimeError("M7 sender returned an empty filename")

    try:
        remote_name = raw_name.decode("utf-8")
        file_size = int(raw_size.decode("ascii"))
    except (UnicodeDecodeError, ValueError) as exc:
        raise RuntimeError("invalid filename packet from M7") from exc

    filename = PurePath(remote_name).name
    if filename in ("", ".", ".."):
        raise RuntimeError(f"unsafe filename from M7: {remote_name!r}")

    output_dir.mkdir(parents=True, exist_ok=True)
    destination = output_dir / filename
    receiver.write(ACK)
    receiver.write(CRC)

    remaining = file_size
    retries = 0
    with destination.open("wb") as output:
        while remaining > 0:
            ret = receiver.recv_packet()
            if ret < 0:
                retries += 1
                if retries > 10:
                    raise RuntimeError(f"too many bad data packets: {ret}")
                receiver.write(NAK)
                continue

            retries = 0
            length = min(receiver.packetsize, remaining)
            output.write(receiver.data[:length])
            remaining -= length
            receiver.write(ACK)

    for _ in range(10):
        ret = receiver.recv_packet()
        if ret == -EEOT:
            receiver.write(ACK)
            receiver.write(CRC)
            break
        receiver.write(NAK)
    else:
        raise RuntimeError("did not receive YMODEM EOT from M7")

    # The YMODEM session ends with an empty packet-zero frame.
    receiver.init_pkt()
    ret = receiver.recv_packet()
    if ret < 0 or receiver.data.split(b"\x00", 1)[0]:
        raise RuntimeError("M7 did not send a valid final YMODEM packet")

    receiver.write(ACK)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Receive one M7 file using a synchronized YMODEM sb session"
    )
    parser.add_argument("-r", "--remote", required=True, help="absolute M7 file path")
    parser.add_argument(
        "-o", "--output", required=True, help="existing or new host output directory"
    )
    parser.add_argument("-t", "--tty", required=True, help="M7 serial device")
    parser.add_argument("-b", "--baudrate", type=int, default=115200)
    parser.add_argument(
        "--timeout", type=float, default=20.0, help="seconds allowed for sb startup"
    )
    args = parser.parse_args()

    submission_root = Path(__file__).resolve().parents[3]
    sbrb = load_sbrb(submission_root)

    try:
        ser = serial.Serial(
            args.tty,
            baudrate=args.baudrate,
            timeout=0.1,
            write_timeout=2.0,
            exclusive=True,
        )
    except (TypeError, ValueError):
        ser = serial.Serial(
            args.tty,
            baudrate=args.baudrate,
            timeout=0.1,
            write_timeout=2.0,
        )

    with ser:
        synchronize_nsh(ser)
        print("NSH synchronized")
        header = start_target_sender(ser, args.remote, args.timeout)
        print("M7 sb handshake received")

        reader = PrefixedSerialReader(ser, header, timeout=3.0)
        receiver = sbrb.ymodem(
            read=reader.read,
            write=lambda data: (ser.write(data), ser.flush()),
            clear=lambda: None,
            timeout=10,
        )
        destination = receive_target_file(receiver, Path(args.output).expanduser())

    print(f"OK8MP YMODEM receive: PASS ({destination})")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, serial.SerialException, TimeoutError, RuntimeError) as exc:
        print(f"OK8MP YMODEM receive: FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
