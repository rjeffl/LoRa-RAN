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
#   bench.py attach /dev/cu.usbmodem101 bms --until 'bms:'   a console, with no reset
#
# Start `run` in the background, then alternate `send` and `wait`. A soak's `run` goes
# outside any Claude Code session, which kills its background tasks when it ends; the
# README says how. Control-file commands:
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
        self.ping_s = 30  # half the 60 s keepalive in connect_packet

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
        # This thread must outlive a broker outage. In the first soak it read self.sock as
        # None while the broker was down, died on the AttributeError, and the broker then
        # dropped every later connection at 1.5 x keepalive (engineering log, 2026-10-25).
        while True:
            time.sleep(self.ping_s)
            try:
                with self.lock:
                    if self.sock is not None:
                        self.sock.sendall(b"\xc0\x00")
            except OSError:
                pass  # loop() sees the same failure and reconnects
            except Exception as e:
                self.log("MQTT", f"<keepalive error: {e!r}; still pinging>")

    def loop(self):
        threading.Thread(target=self._ping, daemon=True).start()
        while True:
            try:
                if self.sock is None:
                    self._connect()
                head, body = read_packet(self._recv)
                if head >> 4 == 3:
                    topic, payload = parse_publish(head, body)
                    # The retain flag on a received PUBLISH means the broker replayed a
                    # stored message, not that the node just sent it. Unmarked, a stale
                    # node/state restored after a broker reboot read as a reset
                    # (engineering log, 2026-10-25).
                    retained = "<retained> " if head & 0x01 else ""
                    self.log("MQTT", f"{retained}{topic} {payload.decode('utf-8', 'replace')}")
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
        # Local wall-clock time leads each line, so a soak's times are read, not converted
        # from the relative seconds; the first soak's first conversion was 7 min wrong
        # (engineering log, 2026-10-25). The offset keeps a run across a DST change
        # unambiguous, and the relative seconds stay for intervals.
        now = time.time()
        stamp = time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime(now))
        with self.lock:
            self.out.write(f"{stamp} {now - self.t0:9.2f} {tag} {text}\n")

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
            else:
                self._port_op(rest.strip(), lambda: self._rts(p, verb))
        elif verb in self.ports:
            self._port_op(verb, lambda: self.ports[verb].write((rest + "\n").encode()))
        else:
            self.log("CMD", f"<unknown command {verb!r}>")
        return True

    @staticmethod
    def _rts(p, verb):
        if verb == "reset":
            p.rts = True
            time.sleep(0.15)
            p.rts = False
        else:
            p.rts = verb == "hold"

    def _port_op(self, tag, op):
        # A port that has re-enumerated raises here, and the StamPLC takes about 24 s to come
        # back after a flash or a power cycle. Log the command as dropped and let reader()
        # reopen the port; dying here ended two runs (engineering log, 2026-10-24).
        try:
            op()
        except Exception as e:
            self.log("CMD", f"<{tag} not written: {e}>")


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
        # No mark yet means run has not read ctl.txt yet. Scanning from line 0 then matched
        # lines older than the mark and fired a reset early (engineering log, 2026-10-05).
        start = None
        for i, line in enumerate(lines):
            if line.rstrip().endswith(f"CMD mark {since}"):
                start = i + 1
        if start is None:
            return []
    hits = []
    for line in lines[start:]:
        fields = line.split(None, 3)  # wall clock, seconds, tag, text
        if tag and (len(fields) < 3 or fields[2] != tag):
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


def open_raw(dev):
    # No pyserial. Its open resets the StamPLC even with DTR and RTS held low, and a bare
    # open() with these settings did not, on macOS (GateLink log, 2026-11-01). Not tested on
    # Linux. HUPCL off, so the close leaves the modem lines alone too.
    import termios
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    t = termios.tcgetattr(fd)
    t[0] = t[1] = t[3] = 0
    t[2] = (t[2] | termios.CS8 | termios.CREAD | termios.CLOCAL) & ~termios.HUPCL
    t[6][termios.VMIN] = t[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, t)
    return fd


def cmd_attach(a):
    # Reads what a reset would clear, such as GateLink's RAM counters at the gate. The console
    # goes to stdout, so a `file cat` can be redirected and its CRC checked.
    fd = open_raw(a.device)
    try:
        for line in a.lines:
            os.write(fd, line.encode() + b"\n")
        pattern = re.compile(a.until.encode()) if a.until else None
        end, seen = time.time() + a.seconds, b""
        while time.time() < end:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                chunk = b""
            if not chunk:
                time.sleep(0.02)
                continue
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            seen = (seen + chunk)[-4096:]
            if pattern is not None and pattern.search(seen):
                return 0
        return 1 if pattern is not None else 0
    finally:
        os.close(fd)


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
    t = sub.add_parser("attach", help="a console that does not reset the board")
    t.add_argument("device")
    t.add_argument("lines", nargs="*", help="commands to send")
    t.add_argument("--seconds", type=float, default=5)
    t.add_argument("--until", help="stop at this pattern; exit 1 if it never comes")
    a = ap.parse_args(argv)
    return {"run": cmd_run, "send": cmd_send, "wait": cmd_wait, "ports": cmd_ports,
            "attach": cmd_attach}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
