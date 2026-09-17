#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# simctl - drives the simnode console and judges the Impl Plan 10.5 catalogue from the
# bridge's published counters. Task BF-21; Impl Plan 7.2, 10.4, 10.5.
#
# WHY THIS EXISTS, in 7.2's words: "a hand-run pass will silently skip entries, and the
# entries most likely to be skipped are the ones whose expected result is nothing
# happens". Those are the forward-compatibility rules - hdr_rsv accepted, seq_wrap
# accepted, single_frame_interleave leaving a set intact - and they break quietly.
#
# THE VERDICT COMES FROM THE BROKER, NOT THE BOARD. The bridge has no console. Its
# counters arrive on `lran/bridge/diag/state` every diag_interval_s (default 60), so a
# row is judged across one publication window and no more: the handoff's rule is to
# "difference a counter only across a window carrying nothing else", and a window per
# row is how that is honoured. A full run is therefore slow and unattended by design.
#
# CREDENTIALS COME FROM THE ENVIRONMENT AND ARE NEVER PRINTED. LRAN_MQTT_USER and
# LRAN_MQTT_PASSWORD, like LRAN_OTA_PASSWORD for an upload - export them in the shell
# that runs this, with `read -rs` for the password. They are not arguments, because an
# argument reaches argv and argv reaches the process table and the shell history.

import argparse
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from catalogue import ALL_ROWS, BEHAVIOUR_ROWS, COUNTER_ROWS, UNDRIVEN, judge, scenario

DIAG_TOPIC = "lran/bridge/diag/state"
VERSION_TOPIC = "lran/bridge/version"


# ---------------------------------------------------------------------------
# The simnode console.
# ---------------------------------------------------------------------------


class Console:
    """The simnode's line-oriented console (Impl Plan 10.4), over USB serial."""

    def __init__(self, port, baud=115200, log=None):
        import serial  # imported here so --list works with no pyserial installed

        self.log = log
        s = serial.Serial()
        s.port = port
        s.baudrate = baud
        # GPIO 0 sits on the CP2102's DTR: opening the port with DTR asserted presses
        # PRG. Set both low BEFORE open(), never after.
        s.dtr = False
        s.rts = False
        s.timeout = 0.2
        s.open()
        self.ser = s
        self.lines = []
        self._stop = threading.Event()
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self):
        buf = b""
        while not self._stop.is_set():
            buf += self.ser.read(4096)
            while b"\n" in buf:
                line, _, buf = buf.partition(b"\n")
                text = line.decode("utf-8", "replace").rstrip("\r")
                self.lines.append((time.time(), text))
                if self.log:
                    self.log.write(text + "\n")
                    self.log.flush()

    def send(self, line):
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        time.sleep(0.3)

    def since(self, when):
        return [t for ts, t in self.lines if ts >= when]

    def close(self):
        self._stop.set()
        time.sleep(0.3)
        self.ser.close()


# ---------------------------------------------------------------------------
# The broker.
# ---------------------------------------------------------------------------


class Broker:
    """Subscribes to the bridge's diagnostics and hands out one reading at a time."""

    def __init__(self, host, port, user, password):
        import paho.mqtt.client as mqtt

        self.latest = None
        self.stamp = 0.0
        self._lock = threading.Lock()
        self.version = None

        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if user:
            c.username_pw_set(user, password)
        c.on_connect = lambda cl, u, f, rc, props=None: cl.subscribe(
            [(DIAG_TOPIC, 1), (VERSION_TOPIC, 1)]
        )
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
            if msg.topic == VERSION_TOPIC:
                self.version = payload
            else:
                self.latest = payload
                self.stamp = time.time()

    def next_reading(self, timeout_s):
        """Blocks for the NEXT publication, ignoring whatever is already retained.

        A retained message read at subscribe time is not evidence of this boot, let
        alone of this window - the handoff has that trap by name. Every reading this
        returns arrived after the call.
        """
        with self._lock:
            mark = self.stamp
        end = time.time() + timeout_s
        while time.time() < end:
            with self._lock:
                if self.stamp > mark:
                    return dict(self.latest)
            time.sleep(0.25)
        return None

    def close(self):
        self.client.loop_stop()


# ---------------------------------------------------------------------------
# The run.
# ---------------------------------------------------------------------------


def run_counter_row(s, console, broker, node_id, window_s, out):
    out(f"\n--- {s.name} ---")
    out(f"    {s.why}")

    before = broker.next_reading(window_s)
    if before is None:
        out(f"    SKIP  no diag/state inside {window_s}s - is the bridge publishing?")
        return None

    args = " ".join(s.args)
    console.send(f"fault {node_id} {s.name}{' ' + args if args else ''}")
    time.sleep(1.0)
    replies = console.since(time.time() - 1.5)
    refused = [r for r in replies if r.startswith("ERR")]
    if refused:
        out(f"    FAIL  the simnode refused the fault: {refused[0]}")
        return False

    if s.settle_s:
        out(f"    waiting {s.settle_s}s for the receiver's tick")
        time.sleep(s.settle_s)

    after = broker.next_reading(window_s)
    if after is None:
        out(f"    SKIP  no second diag/state inside {window_s}s")
        return None

    v = judge(s, before, after)
    tag = "PASS" if v.ok else ("DIVERGED" if v.diverged else "FAIL")
    out(f"    {tag}  {v.detail}")
    for n in v.notes:
        out(f"          {n}")
    # A known divergence is neither a pass nor a failure of this tool. It is a recorded
    # disagreement with a task attached, and it must not be reported as either.
    return None if v.diverged else v.ok


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Run the Impl Plan 10.5 fault catalogue against the bridge.")
    ap.add_argument("--port", help="simnode serial port, e.g. /dev/cu.usbmodem2101")
    ap.add_argument("--host", default=os.environ.get("LRAN_MQTT_HOST"),
                    help="broker host (or LRAN_MQTT_HOST)")
    ap.add_argument("--mqtt-port", type=int,
                    default=int(os.environ.get("LRAN_MQTT_PORT", "1883")))
    ap.add_argument("--node", default="f0", help="simnode identity to arm faults on")
    ap.add_argument("--window", type=int, default=90,
                    help="seconds to wait for a diag/state publication (default 90, "
                         "against the bridge's 60s cadence)")
    ap.add_argument("--only", action="append", default=[],
                    help="run just this row; repeatable")
    ap.add_argument("--list", action="store_true", help="print the catalogue and exit")
    ap.add_argument("--serial-log", help="append the simnode's console output to this file")
    args = ap.parse_args(argv)

    if args.list:
        print(f"{len(COUNTER_ROWS)} counter rows, judged from {DIAG_TOPIC}:")
        for s in COUNTER_ROWS:
            c = s.counter or "(nothing discarded)"
            print(f"  {s.name:26} {c:26} +{s.delta}  {s.why}")
        print(f"\n{len(BEHAVIOUR_ROWS)} behaviour rows, driven but judged elsewhere:")
        for s in BEHAVIOUR_ROWS:
            print(f"  {s.name:26} {s.why}")
        print(f"\n{len(UNDRIVEN)} rows this tool does not drive:")
        for name, why in UNDRIVEN.items():
            print(f"  {name:26} {why}")
        return 0

    if not args.port or not args.host:
        ap.error("--port and --host (or LRAN_MQTT_HOST) are required unless --list")

    user = os.environ.get("LRAN_MQTT_USER")
    password = os.environ.get("LRAN_MQTT_PASSWORD")
    if not user:
        print("LRAN_MQTT_USER / LRAN_MQTT_PASSWORD are not set. Export them in this "
              "shell (use `read -rs` for the password); they are never passed as "
              "arguments.", file=sys.stderr)
        return 2

    rows = [s for s in COUNTER_ROWS if not args.only or s.name in args.only]
    unknown = [n for n in args.only if scenario(n) is None]
    if unknown:
        print(f"no such row: {', '.join(unknown)}", file=sys.stderr)
        return 2

    log = open(args.serial_log, "a") if args.serial_log else None
    console = Console(args.port, log=log)
    broker = Broker(args.host, args.mqtt_port, user, password)

    def out(line):
        print(line, flush=True)

    out(f"simctl - {len(rows)} row(s), one {args.window}s window each")
    time.sleep(2.0)
    if broker.version:
        out(f"bridge: version {broker.version.get('version')} git {broker.version.get('git')} "
            f"slot {broker.version.get('slot')}")
        out("  (retained - compare it against the boot you meant to test)")

    # The identity has to have spoken before the bridge will poll it, and a fault whose
    # frames never reach a registered source proves nothing. `hdr_rsv` announces any
    # role and moves no counter (this node's handoff).
    console.send(f"id add {args.node} ROLE_HEALTH")
    console.send(f"fault {args.node} hdr_rsv")

    passed, failed, skipped = [], [], []
    try:
        for s in rows:
            r = run_counter_row(s, console, broker, args.node, args.window, out)
            (passed if r is True else failed if r is False else skipped).append(s.name)
    except KeyboardInterrupt:
        out("\ninterrupted")
    finally:
        console.close()
        broker.close()

    out(f"\n{len(passed)} passed, {len(failed)} failed, {len(skipped)} not judged")
    if failed:
        out("failed: " + ", ".join(failed))
    if skipped:
        out("not judged: " + ", ".join(skipped))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
