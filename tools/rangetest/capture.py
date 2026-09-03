#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 <holder>          # D31 open - see LRAN-Decision-Register
"""Capture a range-test sweep from the initiator into a committed CSV trace.

Reads the initiator's serial console, keeps the CSV header and data rows, and
discards everything else (`#` progress lines, boot banners, the settings dump).
The settings dump IS retained - as `#` comment lines at the top of the file - so a
trace carries the configuration that produced it, which is the whole reason R3
prints it.

Usage (PlatformIO's python has pyserial; a bare `python3` usually does not):
    ~/.platformio/penv/bin/python tools/rangetest/capture.py \\
        --port /dev/cu.usbserial-0001 --reset \\
        --out docs/rangetest/data/2026-08-31-bench.csv

A position walk (R10) is one capture spanning many sweeps, so the default is to
run until you stop it: press Ctrl-C at the end of the walk and the trace is
written. `--sweeps N` stops on its own after N sweeps, for an unattended bench run.

Rows are appended to `--out` as they arrive, not held until exit. Nobody is
watching the laptop during a walk, and a trace that only exists in memory is one
unplugged USB cable away from a repeated afternoon.

Pass --reset and it drives the board itself: reset, select the role, send any
console keys, then capture. That is ONE command for the whole job, and it is the
supported way - two processes on one serial port open without an exclusive lock on
macOS and then split the incoming bytes between them, quietly punching holes in the
trace.

Without --reset it only listens. Point it at a running initiator; a sweep already in
progress is joined mid-way and the partial rows are still valid, just fewer.
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    # Almost always the wrong interpreter rather than a missing package: PlatformIO
    # ships pyserial inside its own venv, and a bare `python3` on macOS is one of
    # several framework installs that has never seen it. Installing it again into
    # whichever python3 is first on PATH "works" here and breaks on the next machine,
    # so the message names the interpreter already known to have it.
    sys.exit(
        "pyserial not found in this interpreter.\n"
        "Run the script with PlatformIO's python, which already has it:\n"
        "    ~/.platformio/penv/bin/python tools/rangetest/capture.py ...\n"
        "Or install it into this one:\n"
        f"    {sys.executable} -m pip install pyserial")


# A settings-dump line: a bare lower-case key, '=', a value. Deliberately strict -
# a looser "contains = and no comma" test swallowed an ESP-IDF log line
# (`i2cInit(): ... sda=17 scl=18`) into the trace's configuration block.
SETTING_RE = re.compile(r"^[a-z][a-z0-9_]*=\S*$")

# The firmware emits TWO schemas, and this tool captures either: R7's sweep trace and
# R8's ambient survey. They are separate schemas on purpose (see survey.h) - one row
# is a test point with a link at the far end, the other a frequency bin with no far
# end at all. A header is recognised by its first fields, and a data row is then
# validated against THAT header's field count rather than against a hardcoded number,
# so adding a column to either schema does not silently start dropping rows.
HEADER_PREFIXES = ("position,tp_index,", "site_index,site_name,")

# A completed unit of work, per schema. Both are counted by --sweeps.
COMPLETION_MARKERS = ("sweep complete", "survey dump complete",
                      "survey campaign complete")


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
        self.commas = 0
        self.rows = 0

    def open(self, header, meta):
        self.header = header
        self.commas = header.count(",")
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
    # --- driving the board, so one command is the whole job ---------------
    ap.add_argument("--reset", action="store_true",
                    help="pulse the board's reset line after opening the port, so the "
                         "settings dump and CSV header are guaranteed to be seen. "
                         "Removes the 'start this BEFORE resetting the board' trap")
    ap.add_argument("--role", choices=["initiator", "responder", "survey"],
                    help="role to select in the boot window after --reset. Omit for "
                         "initiator, which is the no-press default")
    ap.add_argument("--key-after", type=float, default=0.0, metavar="SECONDS",
                    help="wait this long before sending --key, so a survey can scan "
                         "for a while and then be told to dump. Default 0")
    ap.add_argument("--key", default="",
                    help="console keys to send once the board is up (e.g. 'd' to dump "
                         "the run in progress, 'a' to dump every stored survey site). "
                         "Sent one per second, in order")
    ap.add_argument("--echo", action="store_true",
                    help="print the board's own '#' lines as they arrive. On by "
                         "default when --key is given, because a setup command "
                         "(erase, store, dump) is only useful if you can see it "
                         "confirm - the confirmation IS the output")
    ap.add_argument("--note", default="",
                    help="free text recorded in the file header - antenna height, "
                         "bearing, weather, whatever R10 asks for")
    args = ap.parse_args()
    # A --key run is a command, not a capture: its whole result is a line like
    # "# all stored surveys erased from NVS", which was being filtered out.
    echo = args.echo or bool(args.key)

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    trace = Trace(args.out, args.note)
    meta = []
    sweeps = 0
    pending = []          # data rows seen before this boot's header, see below
    # A repeated header is only a REBOOT if a fresh settings dump preceded it. The
    # survey used to reprint its header per site, and calling each of those a reboot
    # put a false seam in the middle of a perfectly good campaign trace.
    settings_since_header = False
    idle_deadline = time.time() + args.idle_timeout
    buf = b""
    reason = "sweep count reached"
    stop = False

    print(f"capturing from {args.port} -> {args.out}", flush=True)
    if args.sweeps == 0:
        print("  running until Ctrl-C", flush=True)

    ROLE_KEYS = {"initiator": b"i", "responder": b"r", "survey": b"v"}

    if args.reset:
        # RTS drives EN on this carrier and DTR drives IO0. IO0 must stay HIGH or the
        # chip enters the ROM downloader instead of the application - the same
        # strapping-pin trap that stops the role being chosen by a hold through reset
        # (see firmware/range-test/src/role.h).
        ser.dtr = False
        ser.rts = True
        time.sleep(0.15)
        ser.rts = False
        print("  reset the board", flush=True)

        if args.role:
            # SENT REPEATEDLY ACROSS THE WHOLE WINDOW, not once.
            #
            # A single write times itself against a boot whose length is not fixed -
            # ROM bootloader, Serial.begin(), then the OLED bring-up's own delays -
            # and a byte that lands before the UART is configured is simply gone. The
            # board then comes up INITIATOR, which on the walking end is a board that
            # will not echo and in survey mode is a board that transmits. Observed
            # exactly that way on hardware with a single timed write.
            #
            # select_role() drains everything available on each pass and returns on
            # the first match, so repeats after it has chosen are read by the mode's
            # own key handler. None of 'i', 'r' or 'v' is a key in any mode.
            key = ROLE_KEYS[args.role]
            deadline = time.time() + 3.5
            while time.time() < deadline:
                ser.write(key)
                ser.flush()
                time.sleep(0.15)
            print(f"  selected role: {args.role}", flush=True)
        else:
            time.sleep(3.5)   # let the role window close before any --key
    elif args.role:
        print("--role needs --reset: the role window is only open just after a reset",
              file=sys.stderr)
        return 2

    if args.key and args.key_after > 0:
        # Not a sleep(): the board is talking the whole time, and draining the port
        # here keeps its output out of the OS buffer, where a long enough wait would
        # overflow and take the CSV header with it.
        print(f"  waiting {args.key_after:.0f}s before sending keys", flush=True)
        wait_end = time.time() + args.key_after
        while time.time() < wait_end:
            buf += ser.read(4096)

    for k in args.key:
        ser.write(k.encode())
        ser.flush()
        print(f"  sent key: {k}", flush=True)
        time.sleep(1.0)

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
                    settings_since_header = True
                elif line.startswith(HEADER_PREFIXES):
                    # The header is printed once per boot, so any DATA ROW seen
                    # before it belongs to a previous run still sitting in the
                    # serial buffer. The first capture picked up a stray tp_index=8
                    # row that way and wrote 25 rows for a 24-point plan.
                    pending = []
                    if trace.f is None:
                        trace.open(line, meta)
                        print(f"  schema: {line.split(',')[0]}...", flush=True)
                    elif line != trace.header:
                        # Schema changed under us. Appending would produce a file
                        # whose columns mean two different things - refuse.
                        reason = "CSV schema changed mid-capture"
                        print(f"\n{reason} - stopping", file=sys.stderr)
                        buf, stop = b"", True
                        break
                    elif settings_since_header:
                        # A mid-walk reboot. The rows already written stay: they are
                        # real measurements of real positions. Mark the seam, because
                        # the responder owns `position` and a reboot there restarts
                        # its numbering.
                        trace.comment("board rebooted here - position numbering "
                                      "may restart, see docs/rangetest/data/README.md")
                        print("\n  board rebooted - trace continues", flush=True)
                    # Otherwise it is the same header again with no reboot in between:
                    # a benign section break. Nothing to record and nothing to warn
                    # about - the rows that follow are the same schema and keep going.
                    settings_since_header = False
                elif line.startswith("#"):
                    if echo:
                        print(f"\n  {line}", flush=True)
                    if any(m in line for m in COMPLETION_MARKERS):
                        sweeps += 1
                        print(f"\n  sweep {sweeps} complete "
                              f"({trace.rows} rows) - {line.lstrip('# ')}", flush=True)
                elif SETTING_RE.match(line):
                    meta.append(line)          # settings dump: key=value
                elif line[0].isdigit() and (
                        line.count(",") == trace.commas if trace.header
                        else line.count(",") > 6):
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

    print(f"wrote {rows} rows over {sweeps} completed unit(s) to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
