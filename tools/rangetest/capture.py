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

A position walk (R10) is one capture spanning many sweeps, so the default is to
run until you stop it: press Ctrl-C at the end of the walk and the trace is
written. `--sweeps N` stops on its own after N sweeps, for an unattended bench run.

Rows are appended to `--out` as they arrive, not held until exit. Nobody is
watching the laptop during a walk, and a trace that only exists in memory is one
unplugged USB cable away from a repeated afternoon.

Does NOT reset the board. Point it at a running initiator, or reset the board
yourself and start this first - a sweep already in progress is joined mid-way and
the partial rows are still valid, they are just fewer.
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial required: ~/.platformio/penv/bin/python -m pip install pyserial")


# A settings-dump line: a bare lower-case key, '=', a value. Deliberately strict -
# a looser "contains = and no comma" test swallowed an ESP-IDF log line
# (`i2cInit(): ... sda=17 scl=18`) into the trace's configuration block.
SETTING_RE = re.compile(r"^[a-z][a-z0-9_]*=\S*$")


class Trace:
    """The output file, opened at the CSV header and appended to per row.

    Deliberately not buffered until exit. The walk's failure modes - a killed
    terminal, a sleeping laptop, a tugged cable - all land between the first row
    and the last one, which is exactly the window a deferred write loses.
    """

    def __init__(self, path, note):
        self.path = path
        self.note = note
        self.f = None
        self.header = None
        self.rows = 0

    def open(self, header, meta):
        self.header = header
        self.f = open(self.path, "w")
        self.f.write(f"# LRAN range test trace, captured "
                     f"{time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        if self.note:
            self.f.write(f"# note: {self.note}\n")
        self.f.write("# schema: docs/rangetest/data/README.md\n#\n")
        for m in meta:
            self.f.write(f"# {m}\n")
        self.f.write("#\n")
        self.f.write(header + "\n")
        self.f.flush()

    def comment(self, text):
        if self.f:
            self.f.write(f"# {text}\n")
            self.f.flush()

    def row(self, line):
        self.f.write(line + "\n")
        self.f.flush()
        self.rows += 1

    def close(self, reason):
        if self.f:
            self.f.write(f"# capture ended: {reason} - {self.rows} rows\n")
            self.f.close()
            self.f = None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sweeps", type=int, default=0,
                    help="stop after this many 'sweep complete' markers; "
                         "0 (the default) means run until Ctrl-C, which is what a "
                         "position walk wants")
    ap.add_argument("--idle-timeout", type=float, default=1800.0,
                    help="give up after this many seconds with NO serial data at "
                         "all. Idle, not wall-clock: the initiator is silent for the "
                         "whole gap between positions while you walk, and a "
                         "wall-clock deadline ends the capture mid-walk")
    ap.add_argument("--note", default="",
                    help="free text recorded in the file header - antenna height, "
                         "bearing, weather, whatever R10 asks for")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    trace = Trace(args.out, args.note)
    meta = []
    sweeps = 0
    pending = []          # data rows seen before this boot's header, see below
    idle_deadline = time.time() + args.idle_timeout
    buf = b""
    reason = "sweep count reached"
    stop = False

    print(f"capturing from {args.port} -> {args.out}", flush=True)
    if args.sweeps == 0:
        print("  running until Ctrl-C", flush=True)

    try:
        while not stop:
            if args.sweeps and sweeps >= args.sweeps:
                break
            if time.time() >= idle_deadline:
                reason = f"no serial data for {args.idle_timeout:.0f}s"
                print(f"\n{reason}", file=sys.stderr)
                break

            chunk = ser.read(4096)
            if not chunk:
                continue
            idle_deadline = time.time() + args.idle_timeout
            buf += chunk

            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").rstrip("\r").strip()
                if not line:
                    continue

                if line.startswith("--- settings"):
                    # A fresh boot's settings dump starts here. Reset only the
                    # settings, not the rows: the dump is printed BEFORE the CSV
                    # header, so clearing it at the header (as the first fix did)
                    # threw away the very block this tool exists to retain and
                    # produced a trace with no configuration in it.
                    meta = []
                elif line.startswith("position,tp_index,"):
                    # The header is printed once per boot, so any DATA ROW seen
                    # before it belongs to a previous run still sitting in the
                    # serial buffer. The first capture picked up a stray tp_index=8
                    # row that way and wrote 25 rows for a 24-point plan.
                    pending = []
                    if trace.f is None:
                        trace.open(line, meta)
                    elif line != trace.header:
                        # Schema changed under us. Appending would produce a file
                        # whose columns mean two different things - refuse.
                        reason = "CSV schema changed mid-capture"
                        print(f"\n{reason} - stopping", file=sys.stderr)
                        buf, stop = b"", True
                        break
                    else:
                        # A mid-walk reboot. The rows already written stay: they are
                        # real measurements of real positions. Mark the seam, because
                        # the responder owns `position` and a reboot there restarts
                        # its numbering.
                        trace.comment("board rebooted here - position numbering "
                                      "may restart, see docs/rangetest/data/README.md")
                        print("\n  board rebooted - trace continues", flush=True)
                elif line.startswith("#"):
                    if "sweep complete" in line:
                        sweeps += 1
                        print(f"\n  sweep {sweeps} complete "
                              f"({trace.rows} rows) - {line.lstrip('# ')}", flush=True)
                elif SETTING_RE.match(line):
                    meta.append(line)          # settings dump: key=value
                elif line[0].isdigit() and line.count(",") > 20:
                    if trace.f is None:
                        pending.append(line)   # pre-header, discarded at the header
                        continue
                    trace.row(line)
                    print(f"\r  {trace.rows} rows", end="", flush=True)
    except KeyboardInterrupt:
        reason = "stopped by operator"
        print("\nstopped by operator", flush=True)
    finally:
        ser.close()

    print()
    if trace.f is None and trace.rows == 0:
        print("no CSV header seen - was the board reset after this script started?",
              file=sys.stderr)
        return 1
    rows = trace.rows
    trace.close(reason)
    if rows == 0:
        print("no data rows captured", file=sys.stderr)
        return 1

    print(f"wrote {rows} rows over {sweeps} sweep(s) to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
