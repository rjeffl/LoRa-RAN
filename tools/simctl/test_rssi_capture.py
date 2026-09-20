#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Host tests for rssi_capture.py's port handling: the --reset-on-open pulse, and the rule
# that a reconnect never resets the board.
#
# NO BOARD, NO SERIAL PORT AND NO PYSERIAL. A fake port records every control-line change
# in order, and the capture loop takes its opener, clock and sleep as arguments, so a
# ten-hour run's worth of reconnects plays out in milliseconds.
#
#   python3 tools/simctl/test_rssi_capture.py

import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import rssi_capture
from rssi_capture import RESET_HOLD_S, capture, pulse_reset


class FakePort:
    """Records dtr/rts assignments and sleeps into a shared log, in order, and replays a
    script of readline() results: bytes are returned, an exception is raised, and an
    exhausted script returns b"" as an idle port does."""

    def __init__(self, log, script=()):
        object.__setattr__(self, "log", log)
        object.__setattr__(self, "script", list(script))
        object.__setattr__(self, "closed", False)

    def __setattr__(self, name, value):
        self.log.append((name, value))
        object.__setattr__(self, name, value)

    def readline(self):
        if not self.script:
            return b""
        item = self.script.pop(0)
        if isinstance(item, BaseException):
            raise item
        return item

    def close(self):
        object.__setattr__(self, "closed", True)


class FakeClock:
    """Advances one second per call, so a deadline is a count of loop turns."""

    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 1.0
        return self.t


def run_capture(ports, turns, reset_on_open, open_errors=0):
    """Runs capture() against a list of fake ports, handed out one per successful open.
    Returns (stats, file text, the shared control-line log)."""
    log = []
    queue = list(ports)
    failures = [open_errors]

    def opener(port, baud):
        if failures[0] > 0:
            failures[0] -= 1
            raise OSError("no such port")
        return queue.pop(0)

    def sleep(seconds):
        log.append(("sleep", seconds))

    for p in ports:
        object.__setattr__(p, "log", log)

    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "cap.log")
        clock = FakeClock()
        stats = capture("/dev/fake", 115200, out, deadline=turns, flush_s=1e9,
                        progress=False, reset_on_open=reset_on_open, opener=opener,
                        clock=clock, sleep=sleep)
        with open(out, encoding="utf-8") as fh:
            text = fh.read()
    return stats, text, log


class PulseReset(unittest.TestCase):
    def test_dtr_is_released_before_en_is_pulsed(self):
        # DTR asserted while EN rises boots the ROM bootloader, which reads as a dead
        # bridge. So DTR goes low first, then RTS is held and released.
        log = []
        pulse_reset(FakePort(log), sleep=lambda s: log.append(("sleep", s)))
        self.assertEqual([("dtr", False), ("rts", True), ("sleep", RESET_HOLD_S),
                          ("rts", False)], log)

    def test_en_is_held_at_least_as_long_as_esptool_holds_it(self):
        self.assertGreaterEqual(RESET_HOLD_S, 0.1)


class ResetOnOpen(unittest.TestCase):
    def test_the_first_open_resets_the_board_and_says_so_in_the_file(self):
        port = FakePort([], [b"CHAN-BOOT,abc1234,917200000,9,1250,1000,-1100,10\n"])
        stats, text, log = run_capture([port], turns=4, reset_on_open=True)
        self.assertEqual(1, stats["resets"])
        self.assertIn(("rts", True), log)
        self.assertIn(",#RESET\n", text)
        # The marker precedes the banner, so a reader sees the tool caused the boot.
        self.assertLess(text.index("#RESET"), text.index("CHAN-BOOT"))
        self.assertEqual(1, stats["boots"])

    def test_without_the_flag_the_control_lines_are_left_alone(self):
        port = FakePort([], [b"CHAN,1,1000,1000,100,0,0,0,-1150,-1160,-1155\n"])
        stats, text, log = run_capture([port], turns=4, reset_on_open=False)
        self.assertEqual(0, stats["resets"])
        self.assertNotIn(("rts", True), log)
        self.assertNotIn("#RESET", text)

    def test_a_reconnect_never_resets_the_board(self):
        # A USB drop at 3 am must not restart the thing being measured.
        first = FakePort([], [b"CHAN-BOOT,abc1234,917200000,9,1250,1000,-1100,10\n",
                              OSError("device disconnected")])
        second = FakePort([], [b"CHAN,9,9000,1000,100,0,0,0,-1150,-1160,-1155\n"])
        stats, text, log = run_capture([first, second], turns=8, reset_on_open=True)
        self.assertEqual(1, stats["reconnects"])
        self.assertEqual(1, stats["resets"])
        self.assertEqual(1, text.count("#RESET"))
        self.assertEqual(2, text.count("#PORT-OPEN"))
        self.assertEqual(1, log.count(("rts", True)))
        self.assertLess(text.index("#RESET"), text.index("#PORT-LOST"))

    def test_the_reset_waits_for_the_first_open_that_succeeds(self):
        # A port that is not there yet is not an open. The reset belongs to the first
        # open that returns a port, not to the first attempt.
        port = FakePort([])
        stats, text, log = run_capture([port], turns=6, reset_on_open=True, open_errors=2)
        self.assertEqual(2, text.count("#PORT-WAIT"))
        self.assertEqual(1, stats["resets"])
        self.assertLess(text.rindex("#PORT-WAIT"), text.index("#RESET"))


class CommandLine(unittest.TestCase):
    def run_main(self, argv):
        seen = {}

        def fake_capture(*args, **kwargs):
            seen["kwargs"] = kwargs
            return {"lines": 0, "chan": 0, "frame": 0, "boots": 0, "reconnects": 0,
                    "resets": 0}

        saved = (rssi_capture.capture, sys.argv, sys.stderr)
        rssi_capture.capture = fake_capture
        sys.argv = ["rssi_capture.py"] + argv
        sys.stderr = open(os.devnull, "w")
        try:
            rssi_capture.main()
        finally:
            sys.stderr.close()
            rssi_capture.capture, sys.argv, sys.stderr = saved
        return seen["kwargs"]

    def test_the_flag_reaches_the_capture_loop(self):
        kw = self.run_main(["--out", "x.log", "--reset-on-open"])
        self.assertTrue(kw["reset_on_open"])

    def test_the_default_is_no_reset(self):
        # Opening the port must not reset the board unless someone asked for it; that is
        # the first trap this tool exists to avoid.
        kw = self.run_main(["--out", "x.log"])
        self.assertFalse(kw["reset_on_open"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
