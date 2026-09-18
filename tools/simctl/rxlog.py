#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# BF-27 - watches `lran/bridge/diag/rxlog/state` and says which frames went missing.
# Impl Plan 6.6. The arithmetic is rxlog_analyze.py, which does no I/O; this is the half
# that needs a broker.
#
# HOW TO USE IT AGAINST A BURST. Start this first, then run the flood from the simnode
# console (`fault f3 flood 50 250`) or leave per_measure.py to drive it. The log records
# from the moment the bridge boots, but this only sees what is published while it is
# subscribed - the topic is NOT retained, deliberately (frame_log.h), so a late
# subscriber misses the start rather than replaying a stale one as though it were live.
#
#   python3 tools/simctl/rxlog.py --seconds 90 --json run.json
#   python3 tools/simctl/rxlog.py --read run.json        # no broker, re-read a capture
#
# CREDENTIALS COME FROM THE ENVIRONMENT AND NEVER FROM argv, like every other tool here:
# LRAN_MQTT_HOST, LRAN_MQTT_USER, LRAN_MQTT_PASSWORD. An argument would put the password
# in the shell history and in `ps`.
#
#   export LRAN_MQTT_HOST=... LRAN_MQTT_USER=...
#   read -rs LRAN_MQTT_PASSWORD && export LRAN_MQTT_PASSWORD

import argparse
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rxlog_analyze import K_FRAMES, analyze, merge, verdict

RXLOG_TOPIC = "lran/bridge/diag/rxlog/state"


class RxLogSubscriber:
    """Collects every batch published while it is subscribed.

    NOT next_reading()'s one-at-a-time model. A frame log is a stream and every batch is
    a different set of records, so dropping one to hold "the latest" would lose exactly
    what this is for.
    """

    def __init__(self, host, port, user, password):
        import paho.mqtt.client as mqtt

        self.batches = []
        self._lock = threading.Lock()

        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if user:
            c.username_pw_set(user, password)
        c.on_connect = lambda cl, u, f, rc, props=None: cl.subscribe([(RXLOG_TOPIC, 1)])
        c.on_message = self._on_message
        c.connect(host, port, 30)
        c.loop_start()
        self.client = c

    def _on_message(self, cl, u, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except ValueError:
            return
        with self._lock:
            self.batches.append(payload)

    def snapshot(self):
        with self._lock:
            return list(self.batches)

    def record_count(self):
        with self._lock:
            return sum(len(b.get(K_FRAMES, [])) for b in self.batches)

    def close(self):
        self.client.loop_stop()
        self.client.disconnect()


def format_report(batches):
    """The whole verdict as text. Pure, so the format is exercised by the capture file."""
    records, ring_lost, transport_lost = merge(batches)
    lines = []

    if not records:
        # Said plainly rather than as an empty table: nothing arriving and nothing being
        # published look identical in a report that prints zeroes.
        lines.append("NO RECORDS. The bridge published nothing on %s while this ran."
                     % RXLOG_TOPIC)
        lines.append("  A bridge built before BF-27 does not publish it at all.")
        return "\n".join(lines)

    lines.append("records %d  (index %d to %d)"
                 % (len(records), records[0]["i"], records[-1]["i"]))

    # REPORTED BEFORE THE LOSSES, because either of these makes the gap analysis below
    # incomplete and a reader must know that before reading it.
    if ring_lost:
        lines.append("  RING OVERWROTE %d record(s) - the bridge's log_task fell behind."
                     % ring_lost)
        lines.append("    The gaps below are missing those, so a `seq` gap here may be a"
                     " record that was made and lost, not a frame that never arrived.")
    if transport_lost:
        lines.append("  %d record(s) left the bridge and did not reach this subscriber."
                     % transport_lost)
        lines.append("    QoS 0, a broker restart, or this tool started late. Not the"
                     " receive path.")

    streams = analyze(records)
    lines.append("")
    for (peer, msg_type), s in sorted(streams.items()):
        pct = (100.0 * s.lost_frames / s.expected) if s.expected else 0.0
        lines.append("peer 0x%02x type %d: %d arrived, %d lost of %d expected (%.2f %%)"
                     % (peer, msg_type, s.arrivals, s.lost_frames, s.expected, pct))
        if s.duplicates:
            lines.append("    %d duplicate seq - a retry or an RF echo, not a loss."
                         % s.duplicates)
        if s.resyncs:
            lines.append("    %d resync - spec 10.3 reset the sequence; not counted as"
                         " loss." % s.resyncs)
        for loss in s.losses:
            lines.append("    after seq %-6d %2d missing   gap %5d ms   deaf %4d ms"
                         "   tx inside: %s"
                         % (loss.after_seq, loss.count, loss.dt_ms, loss.deaf_ms,
                            "yes" if loss.tx_inside else "no"))

    total, candidate, receiving = verdict(streams)
    lines.append("")
    if total == 0:
        lines.append("NO LOSSES. Nothing to attribute.")
        return "\n".join(lines)

    lines.append("%d frame(s) lost:" % total)
    lines.append("  %d with the bridge transmitting or deaf inside the gap" % candidate)
    lines.append("  %d with the bridge's radio in RECEIVE for the whole gap" % receiving)
    lines.append("")

    # THE READING, stated rather than left to the operator - and stated as a direction
    # for the next step, not as a conclusion. One burst is one burst.
    if receiving == 0:
        lines.append("Every loss falls where the bridge was not listening. That points"
                     " at this firmware's")
        lines.append("half duplex - which the aggregate rx_deaf_ms did NOT show"
                     " (engineering log, 2026-09-17),")
        lines.append("so a disagreement between the two is itself the finding.")
    elif candidate == 0:
        lines.append("Every loss falls while the bridge's radio was in receive. The"
                     " transmit path is not")
        lines.append("involved in any of them, which agrees with the aggregate result"
                     " and leaves the SX1262's")
        lines.append("buffer handling and the RF environment - neither answerable from"
                     " the bridge's counters.")
    else:
        lines.append("Mixed. Sweep the spacing before reading either column: a gap the"
                     " bridge was deaf")
        lines.append("across is only its fault if the deafness covers enough of the gap"
                     " to matter.")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(
        description="BF-27 - which frames the bridge's receive path lost, and where")
    ap.add_argument("--seconds", type=float, default=90.0,
                    help="how long to collect before reporting (default 90)")
    ap.add_argument("--read", help="re-read a saved capture instead of subscribing")
    ap.add_argument("--json", help="write the raw batches to this file")
    ap.add_argument("--host", default=os.environ.get("LRAN_MQTT_HOST"))
    ap.add_argument("--mqtt-port", type=int,
                    default=int(os.environ.get("LRAN_MQTT_PORT", "1883")))
    args = ap.parse_args()

    if args.read:
        with open(args.read, "r", encoding="utf-8") as fh:
            print(format_report(json.load(fh)))
        return 0

    if not args.host:
        print("ERR no broker host - pass --host or export LRAN_MQTT_HOST",
              file=sys.stderr)
        return 2

    sub = RxLogSubscriber(args.host, args.mqtt_port,
                          os.environ.get("LRAN_MQTT_USER"),
                          os.environ.get("LRAN_MQTT_PASSWORD"))
    end = time.time() + args.seconds
    try:
        while time.time() < end:
            time.sleep(1.0)
            print("\rcollecting... %4d records, %3d s left"
                  % (sub.record_count(), int(end - time.time())),
                  end="", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    finally:
        print("", file=sys.stderr)
        batches = sub.snapshot()
        sub.close()

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(batches, fh, indent=1)
        print("wrote %s" % args.json, file=sys.stderr)

    print(format_report(batches))
    return 0


if __name__ == "__main__":
    sys.exit(main())
