#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 <holder>          # D31 open - see LRAN-Decision-Register
"""Capture a range-test sweep from the initiator into a committed CSV trace.

Reads the initiator's serial console, keeps the CSV header and data rows, and
discards everything else (`#` progress lines, boot banners, the settings dump).
The settings dump IS retained - as `#` comment lines at the top of the file - so a
trace carries the configuration that produced it, which is the whole reason R3
prints it.

Usage:
    python3 tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \\
        --out docs/rangetest/data/2026-08-31-bench.csv --sweeps 1

Does NOT reset the board. Point it at a running initiator, or reset the board
yourself and start this first - a sweep already in progress is joined mid-way and
the partial rows are still valid, they are just fewer.
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial required: ~/.platformio/penv/bin/python -m pip install pyserial")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sweeps", type=int, default=1,
                    help="stop after this many 'sweep complete' markers")
    ap.add_argument("--timeout", type=float, default=1800.0,
                    help="give up after this many seconds")
    ap.add_argument("--note", default="",
                    help="free text recorded in the file header - antenna height, "
                         "bearing, weather, whatever R10 asks for")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    rows, meta, header = [], [], None
    sweeps, deadline = 0, time.time() + args.timeout
    buf = b""

    print(f"capturing from {args.port} -> {args.out}", flush=True)
    while time.time() < deadline and sweeps < args.sweeps:
        chunk = ser.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r").strip()
            if not line:
                continue

            if line.startswith("position,tp_index,"):
                header = line
            elif line.startswith("#"):
                if "sweep complete" in line:
                    sweeps += 1
                    print(f"  sweep {sweeps}/{args.sweeps} complete", flush=True)
            elif "=" in line and "," not in line:
                meta.append(line)          # settings dump: key=value
            elif line[0].isdigit() and line.count(",") > 20:
                rows.append(line)
                print(f"\r  {len(rows)} rows", end="", flush=True)
    ser.close()
    print()

    if header is None:
        print("no CSV header seen - was the board reset after this script started?",
              file=sys.stderr)
        return 1
    if not rows:
        print("no data rows captured", file=sys.stderr)
        return 1

    with open(args.out, "w") as f:
        f.write(f"# LRAN range test trace, captured {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        if args.note:
            f.write(f"# note: {args.note}\n")
        f.write("# schema: docs/rangetest/data/README.md\n#\n")
        for m in meta:
            f.write(f"# {m}\n")
        f.write("#\n")
        f.write(header + "\n")
        for r in rows:
            f.write(r + "\n")

    print(f"wrote {len(rows)} rows to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
