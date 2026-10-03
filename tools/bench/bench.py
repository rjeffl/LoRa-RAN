#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Bench harness: one timestamped log of every serial console and lran/# on the broker,
# driven by commands appended to a control file.
#
#   bench.py ports                                   list serial ports
#   bench.py run --port BRG=/dev/cu.usbserial-0001 --port SIM=/dev/cu.usbmodem1101 [--mqtt]
#   bench.py send "SIM bms on 49A1" "pub lran/bridge/config/set {...}"   prints a mark id
#   bench.py wait 'decode 0x8C' --since <mark> --timeout 30
#
# Start `run` in the background, then alternate `send` and `wait`. Control-file commands:
#
#   <TAG> <text>             write <text> and a newline to that serial port
#   pub [-r] <topic> <body>  MQTT publish at QoS 0, -r to retain
#   reset <TAG>              pulse RTS, which is EN on the Heltec and XIAO adapters
#   hold <TAG> / release <TAG>   hold the board in reset, then let it go
#   mark <text>              a marker line in the log, which `wait --since` finds
#   quit                     stop `run`
#
# WHY THIS EXISTS. Four bench sessions in one week (2026-09-26 to 2026-10-02) each wrote
# this harness again in a scratchpad, then waited on it with `sleep N; grep`. A fixed
# sleep is either too short, and the check is rerun, or too long, and the session waits
# for nothing. `wait` returns as soon as the line appears.
#
# Ports open with DTR and RTS low, because asserting either on open resets an ESP32
# through the adapter's auto-reset circuit and the boot that follows is not the one
# under test.
#
# Credentials come from secrets.h at the repository root and are never printed, logged
# or put on a command line. That is why MQTT is a small client here and not
# mosquitto_pub, whose password is an argument and so visible in `ps`. Every value in
# secrets.h is treated as secret, the sandbox ones included (CLAUDE.md, Secrets).
#
# pyserial is the only dependency outside the standard library, and only `run` and
# `ports` need it. PlatformIO's interpreter has it: ~/.platformio/penv/bin/python.

import argparse
import os
import pathlib
import re
import socket
import struct
import sys
import threading
import time
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_DIR = os.environ.get("LRAN_BENCH_DIR", "/tmp/lran-bench")
DEFINE = re.compile(r'^\s*#define\s+(\w+)\s+(?:"((?:[^"\\]|\\.)*)"|(\S+))')


# --- secrets.h -------------------------------------------------------------------------

def read_defines(text):
    """Return the #define NAME value pairs of a header, string quotes removed."""
    out = {}
    for line in text.splitlines():
        m = DEFINE.match(line)
        if m:
            out[m.group(1)] = m.group(2) if m.group(2) is not None else m.group(3)
    return out


def mqtt_settings(path):
    d = read_defines(pathlib.Path(path).read_text())
    missing = [k for k in ("MQTT_HOST", "MQTT_USER", "MQTT_PASSWORD") if k not in d]
    if missing:
        raise SystemExit(f"bench.py: {path} has no {', '.join(missing)}")
    return d["MQTT_HOST"], int(d.get("MQTT_PORT", "1883")), d["MQTT_USER"], d["MQTT_PASSWORD"]


# --- MQTT 3.1.1, QoS 0 only ------------------------------------------------------------

def _varint(n):
    out = bytearray()
    while True:
        b, n = n % 128, n // 128
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _str(s):
    b = s.encode() if isinstance(s, str) else s
    return struct.pack(">H", len(b)) + b


def packet(kind, body):
    return bytes([kind]) + _varint(len(body)) + body


def connect_packet(client_id, user, password, keepalive=60):
    flags = 0x02 | 0x80 | 0x40  # clean session, username, password
    return packet(0x10, _str("MQTT") + bytes([4, flags]) + struct.pack(">H", keepalive)
                  + _str(client_id) + _str(user) + _str(password))


def subscribe_packet(pid, topic):
    return packet(0x82, struct.pack(">H", pid) + _str(topic) + b"\x00")


def publish_packet(topic, payload, retain=False):
    return packet(0x30 | (1 if retain else 0), _str(topic) + payload)


def read_packet(recv):
    """Read one packet with recv(n); return (header byte, body)."""
    head = recv(1)[0]
    n, mult = 0, 1
    while True:
        b = recv(1)[0]
        n += (b & 0x7F) * mult
        mult *= 128
        if not b & 0x80:
            break
    return head, recv(n) if n else b""


def parse_publish(head, body):
    tlen = struct.unpack(">H", body[:2])[0]
    topic = body[2:2 + tlen].decode("utf-8", "replace")
    rest = body[2 + tlen:]
    if (head >> 1) & 0x03:  # QoS 1 or 2 carries a packet id
        rest = rest[2:]
    return topic, rest


class Mqtt:
    def __init__(self, host, port, user, password, log):
        self.addr, self.user, self.password, self.log = (host, port), user, password, log
        self.sock = None
        self.lock = threading.Lock()

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("broker closed the connection")
            buf += chunk
        return buf

    def _connect(self):
        self.sock = socket.create_connection(self.addr, timeout=10)
        self.sock.sendall(connect_packet(f"lran-bench-{uuid.uuid4().hex[:8]}",
                                         self.user, self.password))
        head, body = read_packet(self._recv)
        if head != 0x20 or body[1] != 0:
            raise ConnectionError(f"CONNACK refused, code {body[1] if body else '?'}")
        self.sock.sendall(subscribe_packet(1, "lran/#"))
        self.sock.settimeout(None)
        self.log("MQTT", "<connected, subscribed lran/#>")

    def publish(self, topic, payload, retain=False):
        with self.lock:
            if self.sock is None:
                self.log("MQTT", f"<not connected; {topic} not published>")
                return
            self.sock.sendall(publish_packet(topic, payload.encode(), retain))

    def _ping(self):
        while True:
            time.sleep(30)
            try:
                with self.lock:
                    self.sock.sendall(b"\xc0\x00")
            except OSError:
                pass

    def loop(self):
        threading.Thread(target=self._ping, daemon=True).start()
        while True:
            try:
                if self.sock is None:
                    self._connect()
                head, body = read_packet(self._recv)
                if head >> 4 == 3:
                    topic, payload = parse_publish(head, body)
                    self.log("MQTT", f"{topic} {payload.decode('utf-8', 'replace')}")
            except (OSError, ConnectionError, IndexError) as e:
                self.log("MQTT", f"<{e}; reconnecting in 5 s>")
                if self.sock is not None:
                    self.sock.close()
                self.sock = None
                time.sleep(5)


# --- run -------------------------------------------------------------------------------

class Bench:
    def __init__(self, logpath):
        self.t0 = time.time()
        self.lock = threading.Lock()
        self.out = open(logpath, "a", buffering=1)
        self.ports = {}
        self.mqtt = None

    def log(self, tag, text):
        with self.lock:
            self.out.write(f"{time.time() - self.t0:9.2f} {tag} {text}\n")

    def open_port(self, dev):
        import serial
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = dev, 115200, 0.2
        s.dtr = False
        s.rts = False
        s.open()
        return s

    def reader(self, tag, dev):
        buf = b""
        while True:
            try:
                buf += self.ports[tag].read(512)
            except Exception as e:
                # A USB adapter re-enumerates after some resets; reopen rather than die.
                self.log(tag, f"<read error: {e}; reopening>")
                time.sleep(1)
                try:
                    self.ports[tag].close()
                    self.ports[tag] = self.open_port(dev)
                except Exception:
                    pass
                continue
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.log(tag, line.decode("utf-8", "replace").rstrip("\r"))

    def command(self, line):
        """Act on one control-file line. Return False on quit."""
        verb, _, rest = line.strip().partition(" ")
        self.log("CMD", line.strip())
        if verb == "quit":
            return False
        if verb == "mark":
            return True
        if verb == "pub":
            retain = rest.startswith("-r ")
            topic, _, payload = rest[3:].partition(" ") if retain else rest.partition(" ")
            if self.mqtt is None:
                self.log("CMD", "<no MQTT: start run with --mqtt>")
            else:
                self.mqtt.publish(topic, payload, retain)
        elif verb in ("reset", "hold", "release"):
            p = self.ports.get(rest.strip())
            if p is None:
                self.log("CMD", f"<no port {rest.strip()!r}>")
            elif verb == "reset":
                p.rts = True
                time.sleep(0.15)
                p.rts = False
            else:
                p.rts = verb == "hold"
        elif verb in self.ports:
            self.ports[verb].write((rest + "\n").encode())
        else:
            self.log("CMD", f"<unknown command {verb!r}>")
        return True


def cmd_run(a):
    os.makedirs(a.dir, exist_ok=True)
    logpath, ctlpath = os.path.join(a.dir, "bench.log"), os.path.join(a.dir, "ctl.txt")
    b = Bench(logpath)
    for spec in a.port:
        tag, _, dev = spec.partition("=")
        b.ports[tag] = b.open_port(dev)
        threading.Thread(target=b.reader, args=(tag, dev), daemon=True).start()
    if a.mqtt:
        b.mqtt = Mqtt(*mqtt_settings(a.secrets), b.log)
        threading.Thread(target=b.mqtt.loop, daemon=True).start()
    open(ctlpath, "a").close()
    pos = os.path.getsize(ctlpath)
    b.log("CMD", f"<run: ports {sorted(b.ports)}, mqtt {'on' if a.mqtt else 'off'}>")
    print(f"bench: logging to {logpath}, reading {ctlpath}", flush=True)
    while True:
        time.sleep(0.1)
        if os.path.getsize(ctlpath) <= pos:
            continue
        with open(ctlpath) as f:
            f.seek(pos)
            new = f.read()
            pos = f.tell()
        for line in new.splitlines():
            if line.strip() and not b.command(line):
                return 0


# --- send and wait ---------------------------------------------------------------------

def cmd_send(a):
    mark = uuid.uuid4().hex[:6]
    with open(os.path.join(a.dir, "ctl.txt"), "a") as f:
        f.write(f"mark {mark}\n")
        for line in a.lines:
            f.write(line.rstrip("\n") + "\n")
    print(mark)
    return 0


def scan(lines, pattern, since=None, tag=None):
    """Return the lines after the mark that match pattern, optionally from one source."""
    rx = re.compile(pattern)
    start = 0
    if since:
        for i, line in enumerate(lines):
            if line.rstrip().endswith(f"CMD mark {since}"):
                start = i + 1
    hits = []
    for line in lines[start:]:
        fields = line.split(None, 2)
        if tag and (len(fields) < 2 or fields[1] != tag):
            continue
        if rx.search(line):
            hits.append(line.rstrip("\n"))
    return hits


def cmd_wait(a):
    logpath = os.path.join(a.dir, "bench.log")
    deadline = time.time() + a.timeout
    while True:
        lines = []
        if os.path.exists(logpath):
            with open(logpath, errors="replace") as f:
                lines = f.readlines()
        hits = scan(lines, a.pattern, a.since, a.tag)
        if len(hits) >= a.count:
            print("\n".join(hits[:a.count] if a.first else hits))
            return 0
        if time.time() >= deadline:
            print(f"bench: timed out after {a.timeout:g} s with {len(hits)} of {a.count} "
                  f"match(es) for {a.pattern!r}; last lines:", file=sys.stderr)
            print("".join(lines[-a.tail:]), end="", file=sys.stderr)
            return 1
        time.sleep(0.2)


def cmd_ports(_):
    from serial.tools import list_ports
    for p in list_ports.comports():
        print(f"{p.device:32} {p.description}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="LRAN bench harness")
    ap.add_argument("--dir", default=DEFAULT_DIR,
                    help="where bench.log and ctl.txt live (env LRAN_BENCH_DIR)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--port", action="append", default=[], metavar="TAG=DEVICE")
    r.add_argument("--mqtt", action="store_true", help="log lran/# and allow pub")
    r.add_argument("--secrets", default=str(ROOT / "secrets.h"))
    s = sub.add_parser("send")
    s.add_argument("lines", nargs="+")
    w = sub.add_parser("wait")
    w.add_argument("pattern")
    w.add_argument("--since", help="mark id printed by send")
    w.add_argument("--tag", help="only lines from this source: a port tag, MQTT or CMD")
    w.add_argument("--timeout", type=float, default=30)
    w.add_argument("--count", type=int, default=1)
    w.add_argument("--first", action="store_true", help="print only the first --count hits")
    w.add_argument("--tail", type=int, default=15)
    sub.add_parser("ports")
    a = ap.parse_args(argv)
    return {"run": cmd_run, "send": cmd_send, "wait": cmd_wait, "ports": cmd_ports}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
