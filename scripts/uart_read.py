#!/usr/bin/env python3
"""
uart_read.py - read the ZedBoard output from the USB-UART (port J14, 115200 8N1).

Usage:
  python3 scripts/uart_read.py [--port /dev/ttyACM0] [--seconds 10] [--out file.txt]

Stops after --seconds seconds or when the --until string is read (if given).
The benchmarks print their summary again every 2 seconds, so the reader can
be started after the program (e.g. after moving the cable from J17 to J14).
"""
import argparse
import glob
import sys
import time

import serial


def guess_port():
    # The ZedBoard USB-UART bridge (Cypress) usually shows up as ttyACM*
    ports = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return ports[0] if ports else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--until", default=None, help="string that ends the capture")
    ap.add_argument("--out", default=None, help="also save to this file")
    a = ap.parse_args()

    port = a.port or guess_port()
    if port is None:
        sys.exit("No serial port found: is the cable on the UART port (J14) and the board powered on?")

    out = open(a.out, "w") if a.out else None
    print(f"[uart_read] listening on {port} @ {a.baud} per {a.seconds}s", file=sys.stderr)
    with serial.Serial(port, a.baud, timeout=0.2) as s:
        s.reset_input_buffer()
        t_end = time.time() + a.seconds
        buf = ""
        while time.time() < t_end:
            chunk = s.read(4096).decode(errors="replace")
            if not chunk:
                continue
            sys.stdout.write(chunk)
            sys.stdout.flush()
            if out:
                out.write(chunk)
            buf += chunk
            if a.until and a.until in buf:
                break
    if out:
        out.close()


if __name__ == "__main__":
    main()
