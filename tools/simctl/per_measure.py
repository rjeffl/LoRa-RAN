#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# per_measure - drives one simnode identity to flood the bridge and reports PER from the
# bridge's published counters. The instrument for M22 / V-B12: bridge LoRa PER with WiFi
# idle versus saturated. Impl Plan 7.1, 8.1; Bridge PRD 4.4; Decision Register 5.3.
#
# TWO ARMS, ONE INSTRUMENT. The idle arm runs against a quiet WiFi link; the saturated
# arm runs the same bursts while the bridge's WiFi is loaded. Only the load differs, so
# the arms are comparable only if everything else is held still - which is what --quiet
# and the guards in per_window.py are for. `--arm` is recorded in the output and changes
# nothing about the run.
#
# THE BRIDGE CANNOT YET SATURATE ITS OWN WiFi. Nothing reaches g_diag_interval_s at
# runtime (task_runtime.cpp carries TODO(BF-23)) and the bridge has no console, so its
# WiFi transmits one diagnostic document a minute and no more. The saturated arm waits
# for BF-23. Until then this tool measures the idle arm, which is the baseline the
# saturated arm is compared against.
#
# WHY 0xF3 IN ROLE_FAULT. A polled identity answers POLLs, and every answer is a frame
# the bridge counts in rx_frames - which is this measurement's numerator. ROLE_FAULT
# answers no POLL (simnode node.cpp), so the only frames this identity sends are the
# burst. 0xF3 is in the bridge's registry already (registry.h), so its frames pass the
# ladder rather than counting as rx_unknown_src.
#
# CREDENTIALS COME FROM THE ENVIRONMENT AND ARE NEVER PRINTED, exactly as simctl takes
# them: LRAN_MQTT_USER and LRAN_MQTT_PASSWORD, exported in the shell that runs this, the
# password with `read -rs`. An argument reaches argv, and argv reaches the process table
# and the shell history.
#
#   export LRAN_MQTT_HOST=... LRAN_MQTT_USER=...
#   read -rs LRAN_MQTT_PASSWORD && export LRAN_MQTT_PASSWORD
#   python3 tools/simctl/per_measure.py --port /dev/cu.usbmodem2101 --arm idle

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from per_window import aggregate, format_window, measure
from simctl import Broker, Console

RADIO_TOPIC = "lran/bridge/diag/radio/state"


class RadioBroker(Broker):
    """Broker, plus the radio diagnostics this measurement reads alongside the counters.

    Subclassed rather than copied so the retained-message rule in next_reading() - a
    retained document is not evidence of this window - holds for both topics at once.
    """

    def __init__(self, *args, **kwargs):
        self.radio = None
        super().__init__(*args, **kwargs)
        self.client.subscribe([(RADIO_TOPIC, 1)])

    def _on_message(self, cl, u, msg):
        if msg.topic == RADIO_TOPIC:
            try:
                with self._lock:
                    self.radio = json.loads(msg.payload.decode("utf-8"))
            except ValueError:
                pass
            return
        super()._on_message(cl, u, msg)

    def radio_snapshot(self):
        with self._lock:
            return dict(self.radio) if self.radio else None


def parse_tx_frames(lines):
    """The sender's TX_DONE count, from the simnode's `radio` reply.

    The reply's second line is `  tx_frames N tx_errors N ...`. Read the labelled
    field rather than a position, because a field added ahead of it would otherwise
    move the number silently.
    """
    for text in lines:
        parts = text.split()
        if "tx_frames" in parts:
            i = parts.index("tx_frames")
            if i + 1 < len(parts):
                try:
                    return int(parts[i + 1])
                except ValueError:
                    return None
    return None


def sender_tx_frames(console, timeout_s=3.0):
    mark = time.time()
    console.send("radio")
    end = time.time() + timeout_s
    while time.time() < end:
        value = parse_tx_frames(console.since(mark))
        if value is not None:
            return value
        time.sleep(0.2)
    return None


def quiet_the_bench(console, keep, out):
    """Disable every identity but `keep`, so nothing else answers a POLL.

    An identity left enabled on either board answers polls, and each answer lands in
    the bridge's rx_frames - the numerator here. per_window.py catches the result as
    'the window carried other traffic', but catching it after a 20-minute run is worse
    than preventing it.
    """
    mark = time.time()
    console.send("id list")
    time.sleep(0.6)
    ids = []
    for text in console.since(mark):
        for token in text.replace(",", " ").split():
            if len(token) == 2 and token.lower().startswith("f"):
                try:
                    int(token, 16)
                except ValueError:
                    continue
                ids.append(token.lower())
    for node in sorted(set(ids)):
        if node != keep.lower():
            console.send("disable " + node)
            print("  disabled %s" % node, file=out)
    return sorted(set(ids))


def run_burst(console, broker, node, count, gap_ms, settle_s, window_s, out):
    """One burst: bracket it with two publications and the sender's own TX_DONE count."""
    rx_before = broker.next_reading(window_s)
    if rx_before is None:
        return {"valid": False, "reason": "no diagnostic publication before the burst", "sent": 0}
    radio_before = broker.radio_snapshot()
    tx_before = sender_tx_frames(console)
    if tx_before is None:
        return {"valid": False, "reason": "the simnode did not answer `radio`", "sent": 0}

    console.send("fault %s flood %d gap %d" % (node, count, gap_ms))

    # Wait for the burst to finish before closing the window. The fault fires one frame
    # per gap, and each frame does CAD and may back off (backoff_max_ms 1500, D1), so
    # the floor is count * (gap + airtime) and the ceiling is well above it. Poll
    # TX_DONE until it stops moving rather than predicting the total.
    deadline = time.time() + settle_s + count * (gap_ms / 1000.0 + 2.0)
    last, stable_since = tx_before, None
    while time.time() < deadline:
        time.sleep(1.0)
        now = sender_tx_frames(console)
        if now is None:
            continue
        if now != last:
            last, stable_since = now, None
            continue
        if stable_since is None:
            stable_since = time.time()
        elif time.time() - stable_since >= settle_s and last > tx_before:
            break

    tx_after = last
    sent = tx_after - tx_before
    # The window must close AFTER the last frame, or the frames still in flight are
    # counted as losses in this burst and as excess traffic in the next.
    rx_after = broker.next_reading(window_s)
    if rx_after is None:
        return {"valid": False, "reason": "no diagnostic publication after the burst", "sent": sent}
    radio_after = broker.radio_snapshot()

    w = measure(sent, rx_before, rx_after, radio_before, radio_after)
    w["asked"] = count
    return w


def main(argv=None):
    ap = argparse.ArgumentParser(description="M22 / V-B12 - bridge LoRa PER from one simnode")
    ap.add_argument("--port", required=True, help="simnode serial port, e.g. /dev/cu.usbmodem2101")
    ap.add_argument("--node", default="f3", help="simnode identity to flood from (default f3)")
    ap.add_argument("--arm", default="idle", choices=("idle", "saturated"),
                    help="which M22 arm this run records; changes nothing about the run")
    ap.add_argument("--count", type=int, default=40, help="frames per burst")
    ap.add_argument("--gap", type=int, default=250, help="ms between frames in a burst")
    ap.add_argument("--bursts", type=int, default=3, help="how many bursts to run")
    ap.add_argument("--window", type=int, default=150,
                    help="seconds to wait for a diagnostic publication (interval is 60)")
    ap.add_argument("--settle", type=float, default=6.0,
                    help="seconds TX_DONE must hold still before a burst counts as finished")
    ap.add_argument("--host", default=os.environ.get("LRAN_MQTT_HOST"), help="broker host")
    ap.add_argument("--mqtt-port", type=int, default=int(os.environ.get("LRAN_MQTT_PORT", "1883")))
    ap.add_argument("--keep-others", action="store_true",
                    help="do NOT disable the board's other identities (they will contaminate)")
    ap.add_argument("--serial-log", help="append the simnode's console output to this file")
    ap.add_argument("--json", help="write the run to this file as JSON")
    args = ap.parse_args(argv)

    if not args.host:
        print("ERR no broker host - pass --host or export LRAN_MQTT_HOST", file=sys.stderr)
        return 2

    out = sys.stdout
    log = open(args.serial_log, "a") if args.serial_log else None
    console = Console(args.port, log=log)
    broker = RadioBroker(args.host, args.mqtt_port, os.environ.get("LRAN_MQTT_USER"),
                         os.environ.get("LRAN_MQTT_PASSWORD"))

    try:
        print("M22 / V-B12 - arm: %s" % args.arm, file=out)
        print("bursts %d x %d frames, gap %d ms, from %s" %
              (args.bursts, args.count, args.gap, args.node), file=out)

        # Opening the port rebooted the board, so the identity table is back to its boot
        # state and 0xF3 is not in it.
        console.send("id add %s ROLE_FAULT" % args.node)
        time.sleep(0.5)
        if not args.keep_others:
            quiet_the_bench(console, args.node, out)

        windows = []
        for i in range(1, args.bursts + 1):
            w = run_burst(console, broker, args.node, args.count, args.gap,
                          args.settle, args.window, out)
            windows.append(w)
            print(format_window(i, w), file=out)

        summary = aggregate(windows)
        print("", file=out)
        if summary.get("valid_windows"):
            print("PER %.2f %% over %d frames in %d valid window(s); worst burst %.2f %%" %
                  (100.0 * summary["per"], summary["sent"], summary["valid_windows"],
                   100.0 * summary["worst_per"]), file=out)
            print("  never heard %d, corrupt %d, bridge transmissions in window %d, "
                  "bridge CAD backoffs %d" %
                  (summary["never_heard"], summary["corrupt"], summary["bridge_tx"],
                   summary["bridge_cad_backoffs"]), file=out)
        else:
            print("NO RESULT - %s" % summary.get("reason"), file=out)

        if args.json:
            with open(args.json, "w") as f:
                json.dump({"arm": args.arm, "node": args.node, "count": args.count,
                           "gap_ms": args.gap, "version": broker.version,
                           "windows": windows, "summary": summary}, f, indent=2)
            print("wrote %s" % args.json, file=out)

        return 0 if summary.get("valid_windows") else 1
    finally:
        console.close()
        broker.close()
        if log:
            log.close()


if __name__ == "__main__":
    sys.exit(main())
