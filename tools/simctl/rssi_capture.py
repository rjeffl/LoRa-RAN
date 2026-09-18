#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# M25 - a long unattended capture of the bridge's serial output to a file.
#
# WHAT IT IS FOR. Start it at the end of one session, analyse the file in the next.
# `rssi_analyze.py` reads what this writes, and the file is meant to be readable months
# later on a machine with none of today's context - so this adds a host timestamp to every
# line and otherwise changes nothing.
#
# THREE TRAPS THIS TOOL EXISTS TO AVOID, all of them in docs/bridge/HANDOFF.md:
#
#   1. OPENING THE PORT REBOOTS THE BOARD unless DTR and RTS are held low before open().
#      A capture that reboots the bridge loses the bridge's uptime clock and restarts the
#      very thing it is measuring. simctl's Console does the same dance.
#   2. A BACKGROUND CAPTURE PIPED INTO tail WRITES AN EMPTY FILE, because the pipe block
#      buffers. This writes the file itself and flushes on an interval, so a capture that
#      is killed after nine hours still has nine hours in it.
#   3. THE BRIDGE PRINTS MORE THAN CHANNEL BUCKETS. frame_log.h's FRAME lines share the
#      port. Everything is kept - the correlation between a lost frame and a busy channel
#      is the whole point, and it needs both streams from one clock.
#
# IT DOES NOT PARSE. A capture tool that understands its payload is a capture tool that
# drops a line it did not expect. Parsing is rssi_analyze.py's job, against the file.
#
#   python3 tools/simctl/rssi_capture.py --port /dev/cu.usbserial-0001 \
#       --out docs/bridge/data/m25-chan-$(date +%F).log --hours 8
#
# Run it detached for a long capture, and it is safe to leave:
#
#   nohup python3 tools/simctl/rssi_capture.py ... > /tmp/m25.progress 2>&1 &

import argparse
import os
import sys
import time
from datetime import datetime, timezone

DEFAULT_FLUSH_S = 10.0


def iso_now():
    """UTC, seconds resolution. ANCHORS THE BRIDGE'S millis() TO WALL CLOCK, which is the
    one thing the board cannot supply and the one thing needed to line a capture up
    against anything outside it - a neighbour's schedule, a weather change, a session."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def open_port(port, baud):
    """Opens without resetting the board. DTR and RTS are set low BEFORE open()."""
    import serial

    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 1.0
    s.dtr = False
    s.rts = False
    s.open()
    return s


def capture(port, baud, out_path, deadline, flush_s, progress):
    """Reads lines until the deadline or Ctrl-C. Returns a small summary dict.

    RECONNECTS RATHER THAN EXITING. A USB hub that drops for a second at 3 am must not
    end an eight-hour run; the gap is recorded as a marker line so the analysis can see
    it rather than reading straight across it.
    """
    import serial

    stats = {"lines": 0, "chan": 0, "frame": 0, "boots": 0, "reconnects": 0}
    last_flush = time.time()
    last_note = time.time()
    ser = None

    with open(out_path, "a", encoding="utf-8", errors="replace") as fh:
        fh.write("# M25 capture opened %s port=%s baud=%d\n" % (iso_now(), port, baud))
        fh.flush()

        while time.time() < deadline:
            if ser is None:
                try:
                    ser = open_port(port, baud)
                    fh.write("%s,#PORT-OPEN\n" % iso_now())
                    fh.flush()
                except (OSError, serial.SerialException) as exc:
                    fh.write("%s,#PORT-WAIT,%s\n" % (iso_now(), exc))
                    fh.flush()
                    time.sleep(5.0)
                    continue

            try:
                raw = ser.readline()
            except (OSError, serial.SerialException) as exc:
                fh.write("%s,#PORT-LOST,%s\n" % (iso_now(), exc))
                fh.flush()
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                stats["reconnects"] += 1
                time.sleep(2.0)
                continue

            if raw:
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                if line:
                    fh.write("%s,%s\n" % (iso_now(), line))
                    stats["lines"] += 1
                    if line.startswith("CHAN-BOOT"):
                        stats["boots"] += 1
                    elif line.startswith("CHAN,"):
                        stats["chan"] += 1
                    elif line.startswith("FRAME"):
                        stats["frame"] += 1

            now = time.time()
            if now - last_flush >= flush_s:
                fh.flush()
                os.fsync(fh.fileno())
                last_flush = now
            if progress and now - last_note >= 60.0:
                left = (deadline - now) / 3600.0
                print("%s  %d lines (%d chan, %d frame, %d boots, %d reconnects), "
                      "%.2f h left" % (iso_now(), stats["lines"], stats["chan"],
                                       stats["frame"], stats["boots"],
                                       stats["reconnects"], left),
                      file=sys.stderr, flush=True)
                last_note = now

        fh.write("# M25 capture closed %s\n" % iso_now())
        fh.flush()

    if ser is not None:
        ser.close()
    return stats


def main():
    ap = argparse.ArgumentParser(
        description="M25 - capture the bridge's serial output for a long unattended run")
    ap.add_argument("--port", default="/dev/cu.usbserial-0001",
                    help="the BRIDGE's port (default /dev/cu.usbserial-0001)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True, help="capture file; appended to, never truncated")
    ap.add_argument("--hours", type=float, default=8.0, help="how long to run (default 8)")
    ap.add_argument("--flush-seconds", type=float, default=DEFAULT_FLUSH_S,
                    help="how often to fsync, so a killed run keeps what it had")
    ap.add_argument("--quiet", action="store_true", help="no per-minute progress line")
    args = ap.parse_args()

    deadline = time.time() + args.hours * 3600.0
    print("capturing %s -> %s for %.2f h; Ctrl-C ends it early and keeps the file"
          % (args.port, args.out, args.hours), file=sys.stderr, flush=True)

    try:
        stats = capture(args.port, args.baud, args.out, deadline,
                        args.flush_seconds, not args.quiet)
    except KeyboardInterrupt:
        print("\ninterrupted - the file is intact up to the last flush", file=sys.stderr)
        return 0

    print("done: %d lines (%d chan, %d frame, %d boots, %d reconnects) -> %s"
          % (stats["lines"], stats["chan"], stats["frame"], stats["boots"],
             stats["reconnects"], args.out), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
