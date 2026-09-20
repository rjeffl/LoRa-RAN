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
# --reset-on-open reboots the bridge ONCE, on the first open, so the capture starts with the
# boot banner and its CHAN-BOOT line. That line is printed only from setup(), and it is the
# one record of which image and which frequency a capture measured. Without the flag, a
# capture started after a flash begins mid-run and the file never says what it was taken
# on. A reconnect later in the run never resets: restarting the board at 3 am would restart
# the thing being measured.
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

# How long EN is held low. esptool's hard reset holds it for 100 ms; a little more costs
# nothing and survives a slow USB bridge.
RESET_HOLD_S = 0.12


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


def pulse_reset(ser, sleep=time.sleep):
    """Reboots the board into its application, not into the bootloader.

    On the Heltec V3's CP2102 auto-reset circuit, RTS drives EN and DTR drives GPIO0.
    Holding DTR deasserted keeps GPIO0 high, so the chip boots normally; asserting RTS
    pulls EN low, and releasing it lets the chip run. THE ORDER MATTERS: DTR first. With
    DTR asserted while EN rises, GPIO0 is low and the board comes up in the ROM
    bootloader, silent, which reads as a capture of a dead bridge.
    """
    ser.dtr = False
    ser.rts = True
    sleep(RESET_HOLD_S)
    ser.rts = False


def _port_errors():
    """The exceptions a port raises. pyserial's SerialException is an IOError, so OSError
    alone would do; naming it keeps the intent readable. The host tests run without
    pyserial installed, and fall back to OSError."""
    try:
        import serial
        return (OSError, serial.SerialException)
    except ImportError:
        return (OSError,)


def capture(port, baud, out_path, deadline, flush_s, progress, reset_on_open=False,
            opener=None, clock=time.time, sleep=time.sleep):
    """Reads lines until the deadline or Ctrl-C. Returns a small summary dict.

    RECONNECTS RATHER THAN EXITING. A USB hub that drops for a second at 3 am must not
    end an eight-hour run; the gap is recorded as a marker line so the analysis can see
    it rather than reading straight across it.

    reset_on_open reboots the board after the FIRST open only, and writes a #RESET marker
    so the file says the tool did it. `opener`, `clock` and `sleep` are for the host tests.
    """
    errors = _port_errors()
    opener = opener or open_port

    stats = {"lines": 0, "chan": 0, "frame": 0, "boots": 0, "reconnects": 0, "resets": 0}
    last_flush = clock()
    last_note = clock()
    ser = None
    opened_before = False

    with open(out_path, "a", encoding="utf-8", errors="replace") as fh:
        fh.write("# M25 capture opened %s port=%s baud=%d\n" % (iso_now(), port, baud))
        fh.flush()

        while clock() < deadline:
            if ser is None:
                try:
                    ser = opener(port, baud)
                    fh.write("%s,#PORT-OPEN\n" % iso_now())
                    fh.flush()
                except errors as exc:
                    fh.write("%s,#PORT-WAIT,%s\n" % (iso_now(), exc))
                    fh.flush()
                    sleep(5.0)
                    continue
                if reset_on_open and not opened_before:
                    pulse_reset(ser, sleep)
                    fh.write("%s,#RESET\n" % iso_now())
                    fh.flush()
                    stats["resets"] += 1
                opened_before = True

            try:
                raw = ser.readline()
            except errors as exc:
                fh.write("%s,#PORT-LOST,%s\n" % (iso_now(), exc))
                fh.flush()
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                stats["reconnects"] += 1
                sleep(2.0)
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

            now = clock()
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
    ap.add_argument("--reset-on-open", action="store_true",
                    help="reboot the board once on the first open, so the file starts with "
                         "its boot banner and CHAN-BOOT line; never on a reconnect")
    args = ap.parse_args()

    deadline = time.time() + args.hours * 3600.0
    print("capturing %s -> %s for %.2f h; Ctrl-C ends it early and keeps the file"
          % (args.port, args.out, args.hours), file=sys.stderr, flush=True)

    try:
        stats = capture(args.port, args.baud, args.out, deadline,
                        args.flush_seconds, not args.quiet,
                        reset_on_open=args.reset_on_open)
    except KeyboardInterrupt:
        print("\ninterrupted - the file is intact up to the last flush", file=sys.stderr)
        return 0

    print("done: %d lines (%d chan, %d frame, %d boots, %d reconnects) -> %s"
          % (stats["lines"], stats["chan"], stats["frame"], stats["boots"],
             stats["reconnects"], args.out), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
