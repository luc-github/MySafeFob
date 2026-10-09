#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""settime.py — sets and checks the device clock over the serial console
(ADR-006 serial channel, ROADMAP 8.0 P4).

Default: measures the device's offset against the PC clock, sets the clock
(`settime <epoch.fff>`, UTC, sent the instant it is computed), then measures
the offset again. --check only measures, so it also tells whether a time set
by another path (manual entry on the device, BLE, Wi-Fi) is right.

Usage:
  python tools/settime.py                  # auto-detect the port, check + set + check
  python tools/settime.py --port COM5
  python tools/settime.py --check          # measure only, change nothing
  python tools/settime.py --check --samples 10 --tolerance 2

The PC clock is the reference: make sure it is NTP-synced (Windows: Settings
> Time > Sync now). The offset is device minus PC, from the device's
`settime` status line "Epoch <s>.<ms>", taken at the middle of the
command's round trip (accuracy: a few tens of ms).

The port is opened with DTR/RTS low so the ESP32-S3 is NOT reset (a reset
would not lose the time, but would interrupt the session).
Needs pyserial (already in the ESP-IDF Python venv).
Exit code: 0 if the final offset is within --tolerance, 1 otherwise, 2 on
a connection or protocol error.
"""
import argparse
import re
import statistics
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is missing: run this with the ESP-IDF Python venv, or `pip install pyserial`.")
    sys.exit(2)

ESPRESSIF_VID = 0x303A
EPOCH_RE = re.compile(r"Epoch\s+(\d+)\.(\d{3})")
NOT_SET_RE = re.compile(r"Clock\s+not set")
SET_RE = re.compile(r"Clock set to .*")
ERROR_RE = re.compile(r"(Invalid time.*|Out of range.*|WARNING: .*)")


def find_port():
    ports = list(serial.tools.list_ports.comports())
    esp = [p for p in ports if p.vid == ESPRESSIF_VID]
    if len(esp) == 1:
        return esp[0].device
    candidates = esp or ports
    if not candidates:
        print("No serial port found. Plug the device, or pass --port.")
        sys.exit(2)
    print("Several serial ports, pass --port:")
    for p in candidates:
        print(f"  {p.device}  {p.description}")
    sys.exit(2)


def open_port(port, baud):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.05
    ser.dtr = False   # set before open(): no reset of the ESP32-S3
    ser.rts = False
    ser.open()
    return ser


def drain(ser, duration=0.3):
    end = time.monotonic() + duration
    while time.monotonic() < end:
        ser.read(4096)


def read_until(ser, patterns, timeout):
    """Reads lines until one matches a pattern; returns (match, arrival_time)."""
    buf = ""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        chunk = ser.read(4096)
        if not chunk:
            continue
        arrived = time.time()
        buf += chunk.decode("utf-8", errors="replace")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            for pat in patterns:
                m = pat.search(line)
                if m:
                    return m, arrived
    return None, None


def measure_once(ser):
    """Device minus PC clock in seconds, or None if the device clock is not set."""
    t0 = time.time()
    ser.write(b"settime\r\n")
    m, t1 = read_until(ser, [EPOCH_RE, NOT_SET_RE], timeout=3)
    if m is None:
        raise RuntimeError("no answer to `settime` (wrong port, or the console is busy?)")
    if m.re is NOT_SET_RE:
        return None, t1 - t0
    device = int(m.group(1)) + int(m.group(2)) / 1000.0
    drain(ser, 0.2)   # rest of the status output
    return device - (t0 + t1) / 2.0, t1 - t0


def measure(ser, samples):
    offsets, rtts = [], []
    for _ in range(samples):
        offset, rtt = measure_once(ser)
        if offset is None:
            return None, None
        offsets.append(offset)
        rtts.append(rtt)
    return statistics.median(offsets), statistics.median(rtts)


def report(label, offset, rtt):
    if offset is None:
        print(f"{label}: device clock not set")
    else:
        print(f"{label}: device {offset:+.3f} s vs PC (round trip {rtt * 1000:.0f} ms)")


def set_clock(ser):
    now = time.time()
    ser.write(f"settime {now:.3f}\r\n".encode())
    m, _ = read_until(ser, [SET_RE, ERROR_RE], timeout=5)   # RTC write takes 1-2 s
    if m is None:
        raise RuntimeError("no answer to `settime <epoch>`")
    print(f"Device: {m.group(0).strip()}")
    return m.re is SET_RE and not m.group(0).startswith("WARNING")


def main():
    parser = argparse.ArgumentParser(description="Set/check the device clock over serial (UTC).")
    parser.add_argument("--port", help="serial port (default: auto-detect the Espressif USB port)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--check", action="store_true", help="measure the offset only, do not set")
    parser.add_argument("--samples", type=int, default=5, help="measurements per check (median)")
    parser.add_argument("--tolerance", type=float, default=0.5,
                        help="max |offset| in seconds for exit code 0 (default 0.5)")
    args = parser.parse_args()

    port = args.port or find_port()
    try:
        ser = open_port(port, args.baud)
    except serial.SerialException as e:
        print(f"Cannot open {port}: {e}")
        return 2
    print(f"Port {port}, PC UTC {time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime())}")

    try:
        drain(ser)
        ser.write(b"\r\n")   # fresh prompt, drops any half-typed line
        drain(ser)

        offset, rtt = measure(ser, args.samples)
        report("Before" if not args.check else "Offset", offset, rtt)
        if not args.check:
            if not set_clock(ser):
                return 1
            drain(ser, 0.5)
            offset, rtt = measure(ser, args.samples)
            report("After ", offset, rtt)
    except RuntimeError as e:
        print(f"Error: {e}")
        return 2
    finally:
        ser.close()

    if offset is None or abs(offset) > args.tolerance:
        print(f"FAIL: offset outside +/-{args.tolerance} s")
        return 1
    print(f"OK: offset within +/-{args.tolerance} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
