#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Host tests for the M22 / V-B12 arithmetic and the sender's TX_DONE parse.
#
# NO BOARD AND NO BROKER. Everything here feeds measure() two counter documents, which
# is why per_window.py does no I/O: the part that decides what a run measured is
# testable at a desk, and only the part that talks to hardware needs hardware.
#
#   python3 tools/simctl/test_per_measure.py

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from per_measure import parse_tx_frames
from per_window import aggregate, format_window, measure

# Every counter `lran/bridge/diag/state` carries, all zero. A reading is a full
# document, so a test that omits a key would be testing a payload the bridge never
# sends.
ZERO = {
    "rx_crc_err": 0, "rx_runt": 0, "rx_oversize": 0, "rx_bad_crc": 0, "rx_bad_ver": 0,
    "rx_not_addressed": 0, "rx_unknown_hdr_ext": 0, "rx_bad_frag": 0,
    "rx_unknown_type": 0, "rx_unknown_schema": 0, "rx_bad_length": 0,
    "rx_not_fragmentable": 0, "rx_rejected_ctx": 0, "rx_rejected_mac": 0,
    "rx_unknown_src": 0, "rx_reassembly_timeout": 0, "rx_fragment_overflow": 0,
    "rx_reassembly_abandoned": 0, "rx_rejected_seq": 0, "rx_frag_duplicate": 0,
    "rx_frag_late": 0, "rx_dup_command": 0, "rx_dropped": 0, "rx_frames": 0,
}

RADIO_ZERO = {"tx_frames": 0, "cad_backoffs": 0, "errors_suppressed": 0}


def after(**moves):
    doc = dict(ZERO)
    doc.update(moves)
    return doc


def radio(**moves):
    doc = dict(RADIO_ZERO)
    doc.update(moves)
    return doc


class PerfectWindow(unittest.TestCase):
    def test_no_loss_is_zero_per(self):
        w = measure(40, ZERO, after(rx_frames=40), RADIO_ZERO, radio(tx_frames=1))
        self.assertTrue(w["valid"], w["reason"])
        self.assertEqual(w["accepted"], 40)
        self.assertEqual(w["per"], 0.0)
        self.assertEqual(w["never_heard"], 0)
        self.assertEqual(w["bridge_tx"], 1)

    def test_frames_never_heard_are_errors(self):
        # 40 on the air, 36 delivered by the radio, none discarded.
        w = measure(40, ZERO, after(rx_frames=36))
        self.assertTrue(w["valid"], w["reason"])
        self.assertEqual(w["never_heard"], 4)
        self.assertEqual(w["corrupt"], 0)
        self.assertEqual(w["lost"], 4)
        self.assertAlmostEqual(w["per"], 0.1)

    def test_phy_crc_errors_are_errors_and_are_counted_as_heard(self):
        # rx_frames counts a PHY CRC error too (rx_ladder.cpp), so a corrupt frame is
        # heard, dropped, and still a packet error.
        w = measure(40, ZERO, after(rx_frames=40, rx_crc_err=3, rx_dropped=3))
        self.assertTrue(w["valid"], w["reason"])
        self.assertEqual(w["heard"], 40)
        self.assertEqual(w["accepted"], 37)
        self.assertEqual(w["corrupt"], 3)
        self.assertEqual(w["never_heard"], 0)
        self.assertAlmostEqual(w["per"], 0.075)

    def test_both_loss_modes_at_once(self):
        w = measure(100, ZERO, after(rx_frames=90, rx_crc_err=5, rx_dropped=5))
        self.assertEqual(w["never_heard"], 10)
        self.assertEqual(w["corrupt"], 5)
        self.assertEqual(w["accepted"], 85)
        self.assertAlmostEqual(w["per"], 0.15)

    def test_a_discard_other_than_crc_is_itemised(self):
        w = measure(20, ZERO, after(rx_frames=20, rx_bad_ver=2, rx_dropped=2))
        self.assertEqual(w["discards"], {"rx_bad_ver": 2})
        self.assertEqual(w["accepted"], 18)


class InvalidWindow(unittest.TestCase):
    def test_a_reboot_mid_window_is_refused(self):
        # The bridge rebooted: every counter went back to zero, so the deltas describe
        # two different boots. A PER from this is a plausible wrong number.
        w = measure(40, after(rx_frames=500, rx_dropped=10), after(rx_frames=12))
        self.assertFalse(w["valid"])
        self.assertIn("rebooted", w["reason"])

    def test_more_accepted_than_sent_is_refused(self):
        # Another identity answered a POLL inside the window.
        w = measure(10, ZERO, after(rx_frames=14))
        self.assertFalse(w["valid"])
        self.assertIn("other", w["reason"])

    def test_nothing_on_the_air_is_refused(self):
        w = measure(0, ZERO, after(rx_frames=0))
        self.assertFalse(w["valid"])
        self.assertIn("no frame reached the air", w["reason"])

    def test_a_missing_key_is_refused_rather_than_read_as_zero(self):
        w = measure(10, {"rx_frames": 0}, {"rx_frames": 10})
        self.assertFalse(w["valid"])
        self.assertIn("missing", w["reason"])

    def test_a_backwards_radio_counter_is_refused(self):
        w = measure(10, ZERO, after(rx_frames=10), radio(tx_frames=9), RADIO_ZERO)
        self.assertFalse(w["valid"])
        self.assertIn("rebooted", w["reason"])


class Aggregate(unittest.TestCase):
    def test_per_is_pooled_over_frames_not_averaged_over_bursts(self):
        # 5 frames losing 1 is 20 %; 195 frames losing 1 is 0.51 %. Pooled over frames
        # the answer is 1 %, and averaged over bursts it would be 10.3 %.
        small = measure(5, ZERO, after(rx_frames=4))
        big = measure(195, ZERO, after(rx_frames=194))
        summary = aggregate([small, big])
        self.assertEqual(summary["sent"], 200)
        self.assertEqual(summary["lost"], 2)
        self.assertAlmostEqual(summary["per"], 0.01)
        self.assertAlmostEqual(summary["worst_per"], 0.2)

    def test_invalid_windows_are_excluded_but_counted(self):
        good = measure(10, ZERO, after(rx_frames=10))
        bad = measure(0, ZERO, ZERO)
        summary = aggregate([good, bad])
        self.assertEqual(summary["windows"], 2)
        self.assertEqual(summary["valid_windows"], 1)
        self.assertEqual(summary["sent"], 10)

    def test_no_valid_window_gives_no_figure(self):
        summary = aggregate([measure(0, ZERO, ZERO)])
        self.assertNotIn("per", summary)
        self.assertEqual(summary["reason"], "no valid window")


class SenderParse(unittest.TestCase):
    def test_reads_the_labelled_field(self):
        lines = [
            "OK radio up",
            "  tx_frames 143 tx_errors 0 tx_timeouts 0 tx_forced 0",
            "  cad_errors 0 cad_deferred 7 rx_driver_errors 0 begin_failures 0",
        ]
        self.assertEqual(parse_tx_frames(lines), 143)

    def test_a_field_added_ahead_of_it_does_not_move_the_number(self):
        lines = ["  tx_queued 9 tx_frames 143 tx_errors 0"]
        self.assertEqual(parse_tx_frames(lines), 143)

    def test_no_reply_reads_as_none_rather_than_zero(self):
        self.assertIsNone(parse_tx_frames(["OK radio up", "some other line"]))


class Formatting(unittest.TestCase):
    def test_a_clean_burst_prints_its_per(self):
        line = format_window(1, measure(40, ZERO, after(rx_frames=40)))
        self.assertIn("PER 0.00 %", line)

    def test_an_invalid_burst_prints_its_reason(self):
        line = format_window(2, measure(0, ZERO, ZERO))
        self.assertIn("INVALID", line)


if __name__ == "__main__":
    unittest.main(verbosity=2)
