#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Tests for bench.py that need no board: the secrets.h reader, the MQTT packets against
# a fake broker, the control-file commands and `wait`. Run directly; CI's checks job
# runs it.

import contextlib
import io
import os
import socket
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402


class FakePort:
    def __init__(self):
        self.written, self.rts_log = [], []
        self._rts = False

    def write(self, b):
        self.written.append(b)

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, v):
        self._rts = v
        self.rts_log.append(v)


class SecretsTest(unittest.TestCase):
    def test_reads_strings_and_numbers(self):
        d = bench.read_defines('#define MQTT_HOST "10.0.0.1"\n#define MQTT_PORT 1884\n'
                               '// #define X "y"\n#define MQTT_PASSWORD "a\\"b"\n')
        self.assertEqual(d["MQTT_HOST"], "10.0.0.1")
        self.assertEqual(d["MQTT_PORT"], "1884")
        self.assertEqual(d["MQTT_PASSWORD"], 'a\\"b')
        self.assertNotIn("X", d)

    def test_template_has_every_field(self):
        host, port, user, _ = bench.mqtt_settings(bench.ROOT / "secrets.h.example")
        self.assertTrue(host and user)
        self.assertEqual(port, 1883)


class PacketTest(unittest.TestCase):
    def test_varint(self):
        self.assertEqual(bench._varint(0), b"\x00")
        self.assertEqual(bench._varint(127), b"\x7f")
        self.assertEqual(bench._varint(128), b"\x80\x01")
        self.assertEqual(bench._varint(16383), b"\xff\x7f")

    def test_publish_round_trip(self):
        raw = bench.publish_packet("lran/bridge/state", b"x" * 300, retain=True)
        buf = io.BytesIO(raw)
        head, body = bench.read_packet(buf.read)
        self.assertEqual(head, 0x31)
        self.assertEqual(bench.parse_publish(head, body), ("lran/bridge/state", b"x" * 300))

    def test_qos1_publish_skips_packet_id(self):
        body = bench._str("t") + b"\x00\x07" + b"hi"
        self.assertEqual(bench.parse_publish(0x32, body), ("t", b"hi"))


class FakeBrokerTest(unittest.TestCase):
    """CONNECT, SUBSCRIBE and both directions of PUBLISH against a socket."""

    def test_connect_subscribe_publish(self):
        srv = socket.socket()
        srv.bind(("127.0.0.1", 0))
        srv.listen(1)
        got = {}

        def broker():
            c, _ = srv.accept()
            rd = lambda n: c.recv(n, socket.MSG_WAITALL)  # noqa: E731
            got["connect"] = bench.read_packet(rd)
            c.sendall(b"\x20\x02\x00\x00")
            got["subscribe"] = bench.read_packet(rd)
            c.sendall(bench.publish_packet("lran/simnode1/state", b"{}"))
            got["publish"] = bench.read_packet(rd)
            time.sleep(0.2)
            c.close()

        t = threading.Thread(target=broker, daemon=True)
        t.start()
        lines = []
        m = bench.Mqtt("127.0.0.1", srv.getsockname()[1], "u", "p",
                       lambda tag, text: lines.append((tag, text)))
        threading.Thread(target=m.loop, daemon=True).start()
        for _ in range(50):
            if any("lran/simnode1/state" in x for _, x in lines):
                break
            time.sleep(0.05)
        m.publish("lran/bridge/config/set", '{"set":{}}')
        t.join(2)
        srv.close()
        self.assertEqual(got["connect"][0], 0x10)
        self.assertIn(b"MQTT", got["connect"][1])
        self.assertEqual(got["subscribe"][0], 0x82)
        self.assertIn(b"lran/#", got["subscribe"][1])
        self.assertEqual(bench.parse_publish(*got["publish"]),
                         ("lran/bridge/config/set", b'{"set":{}}'))
        self.assertIn(("MQTT", "lran/simnode1/state {}"), lines)


class CommandTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.b = bench.Bench(os.path.join(self.tmp.name, "bench.log"))
        self.port = FakePort()
        self.b.ports["SIM"] = self.port

    def tearDown(self):
        self.b.out.close()
        self.tmp.cleanup()

    def test_serial_write_and_reset(self):
        self.assertTrue(self.b.command("SIM bms on 49A1"))
        self.assertEqual(self.port.written, [b"bms on 49A1\n"])
        self.b.command("reset SIM")
        self.assertEqual(self.port.rts_log, [True, False])
        self.b.command("hold SIM")
        self.assertTrue(self.port.rts)
        self.b.command("release SIM")
        self.assertFalse(self.port.rts)

    def test_pub_without_mqtt_is_logged_not_dropped(self):
        self.b.command("pub -r lran/x 1")
        self.b.out.flush()
        log = open(os.path.join(self.tmp.name, "bench.log")).read()
        self.assertIn("<no MQTT", log)

    def test_quit(self):
        self.assertFalse(self.b.command("quit"))


class WaitTest(unittest.TestCase):
    LOG = ["  1.00 SIM boot\n", "  2.00 CMD mark aa\n", "  3.00 SIM decode 0x8C ok\n",
           "  4.00 MQTT lran/x decode 0x8C\n", "  5.00 CMD mark bb\n",
           "  6.00 SIM decode 0x8C again\n"]

    def test_since_mark_and_tag(self):
        self.assertEqual(len(bench.scan(self.LOG, "decode")), 3)
        self.assertEqual(len(bench.scan(self.LOG, "decode", since="bb")), 1)
        # A mark not yet in the log matches nothing, rather than the whole log.
        self.assertEqual(bench.scan(self.LOG, "decode", since="zz"), [])
        self.assertEqual(bench.scan(self.LOG, "decode", since="aa", tag="MQTT"),
                         ["  4.00 MQTT lran/x decode 0x8C"])

    def test_send_then_wait(self):
        with tempfile.TemporaryDirectory() as d:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                bench.main(["--dir", d, "send", "SIM ping"])
            mark = out.getvalue().strip()
            with open(os.path.join(d, "ctl.txt")) as f:
                self.assertEqual(f.read(), f"mark {mark}\nSIM ping\n")
            with open(os.path.join(d, "bench.log"), "w") as f:
                f.write(f"  1.00 CMD mark {mark}\n  2.00 SIM pong\n")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(bench.main(["--dir", d, "wait", "pong", "--since", mark,
                                             "--timeout", "1"]), 0)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(bench.main(["--dir", d, "wait", "never",
                                             "--timeout", "0.3"]), 1)


if __name__ == "__main__":
    unittest.main()
