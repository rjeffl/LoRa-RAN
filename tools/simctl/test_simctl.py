#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Host tests for simctl's verdict logic and its consistency check. Task BF-21.
#
# NO BOARD AND NO BROKER. Everything here feeds judge() two counter documents, which is
# the whole reason catalogue.py does no I/O: the part that decides whether a row passed
# is testable at a desk, and only the part that talks to hardware needs hardware.
#
#   python3 tools/simctl/test_simctl.py

import os
import pathlib
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from catalogue import (ALL_ROWS, BEHAVIOUR_ROWS, COUNTER_ROWS, NOT_DROPPED, UNDRIVEN,
                       judge, scenario)

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


def after(**moves):
    """A second reading with the named counters advanced."""
    d = dict(ZERO)
    for k, v in moves.items():
        d[k] = d.get(k, 0) + v
    return d


class TestVerdict(unittest.TestCase):

    def test_a_discard_row_passes_when_its_own_counter_moves(self):
        s = scenario("runt")
        v = judge(s, ZERO, after(rx_frames=1, rx_runt=1, rx_dropped=1))
        self.assertTrue(v.ok, v.detail)

    # The trap the bench found on 2026-09-16: a silent pass and a frame that never
    # arrived are identical in every counter the row cares about.
    def test_a_row_whose_frames_never_arrived_is_not_a_pass(self):
        s = scenario("hdr_rsv")
        v = judge(s, ZERO, after())          # nothing moved at all
        self.assertFalse(v.ok)
        self.assertIn("did not arrive", v.detail)

        v = judge(s, ZERO, after(rx_frames=1))   # the frame landed and was accepted
        self.assertTrue(v.ok, v.detail)

    def test_a_forward_compatibility_row_fails_if_anything_is_discarded(self):
        s = scenario("hdr_rsv")
        v = judge(s, ZERO, after(rx_frames=1, rx_dropped=1, rx_bad_frag=1))
        self.assertFalse(v.ok)
        self.assertIn("discard nothing", v.detail)

    # single_frame_interleave is the highest-value row in the table and is silent by
    # construction: the offending frame belongs to no set, so nothing is counted.
    def test_single_frame_interleave_fails_if_the_set_was_abandoned(self):
        s = scenario("single_frame_interleave")
        self.assertTrue(judge(s, ZERO, after(rx_frames=4)).ok)
        v = judge(s, ZERO, after(rx_frames=4, rx_reassembly_abandoned=1, rx_dropped=1))
        self.assertFalse(v.ok)

    # The table's own invariant, and the reason BF-21 made set_displaced complete its
    # displacing set.
    def test_a_row_that_moves_a_second_counter_fails(self):
        s = scenario("set_displaced")
        self.assertTrue(
            judge(s, ZERO, after(rx_frames=4, rx_reassembly_abandoned=1, rx_dropped=1)).ok)
        v = judge(s, ZERO, after(rx_frames=4, rx_reassembly_abandoned=1,
                                 rx_reassembly_timeout=1, rx_dropped=2))
        self.assertFalse(v.ok)
        self.assertIn("but so did", v.detail)
        self.assertIn("rx_reassembly_timeout", v.detail)

    # rx_dropped is the sum of the counters spec 14.1 marks `yes`, and only those. These
    # three are normal traffic: a health metric climbing on them climbs during correct
    # operation.
    def test_normal_traffic_counters_are_excluded_from_rx_dropped(self):
        for name in ("frag_dup", "frag_late"):
            s = scenario(name)
            self.assertIn(s.counter, NOT_DROPPED)
            self.assertTrue(judge(s, ZERO, after(rx_frames=4, **{s.counter: 1})).ok, name)
            v = judge(s, ZERO, after(rx_frames=4, rx_dropped=1, **{s.counter: 1}))
            self.assertFalse(v.ok, name)
            self.assertIn("excluded from rx_dropped", v.detail)

    def test_a_discard_row_fails_when_rx_dropped_did_not_follow(self):
        s = scenario("bad_mac")
        v = judge(s, ZERO, after(rx_frames=1, rx_rejected_mac=1))  # rx_dropped flat
        self.assertFalse(v.ok)
        self.assertIn("included in rx_dropped", v.detail)

    # bad_length sends one short and one long payload, so its counter moves twice while
    # most rows move once.
    def test_the_expected_delta_is_per_row_not_per_frame(self):
        s = scenario("bad_length")
        self.assertEqual(2, s.delta)
        self.assertTrue(judge(s, ZERO, after(rx_frames=2, rx_bad_length=2, rx_dropped=2)).ok)
        self.assertFalse(judge(s, ZERO, after(rx_frames=2, rx_bad_length=1, rx_dropped=1)).ok)

    # A known disagreement is neither a pass nor a failure of the tool: it is recorded,
    # with the task that closes it, and the runner reports it as neither.
    def test_a_known_divergence_is_flagged_rather_than_failed_silently(self):
        s = scenario("bad_ver")
        self.assertIsNotNone(s.divergence)
        v = judge(s, ZERO, after(rx_frames=2, rx_bad_ver=2, rx_dropped=2))
        self.assertFalse(v.ok)
        self.assertTrue(v.diverged)
        self.assertIn("BF-22", v.detail)

    # Counters are differenced across a window; a reflash zeroes them and an absolute
    # value means nothing.
    def test_counters_are_differenced_not_read_absolutely(self):
        s = scenario("runt")
        before = dict(ZERO, rx_runt=17, rx_dropped=40, rx_frames=900)
        self.assertTrue(judge(s, before, dict(before, rx_runt=18, rx_dropped=41,
                                              rx_frames=901)).ok)

    # Poll answers move rx_frames too, so the arrival check is a floor and not equality.
    def test_extra_frames_in_the_window_do_not_fail_a_row(self):
        s = scenario("runt")
        self.assertTrue(judge(s, ZERO, after(rx_frames=6, rx_runt=1, rx_dropped=1)).ok)


class TestCatalogue(unittest.TestCase):

    def test_every_row_that_expects_a_counter_names_a_real_one(self):
        for s in COUNTER_ROWS:
            if s.counter is not None:
                self.assertIn(s.counter, ZERO, s.name)
                self.assertGreater(s.delta, 0, s.name)
            else:
                self.assertEqual(0, s.delta, s.name)

    def test_no_row_is_defined_twice(self):
        names = [s.name for s in ALL_ROWS]
        self.assertEqual(len(names), len(set(names)))
        self.assertFalse(set(names) & set(UNDRIVEN))

    def test_behaviour_rows_are_driven_but_judged_elsewhere(self):
        for s in BEHAVIOUR_ROWS:
            self.assertIsNone(s.counter, s.name)
            self.assertTrue(s.why, s.name)


class TestConsistencyCheck(unittest.TestCase):
    """The check is only worth having if it fails on drift, so make it drift."""

    CHECK = os.path.join(ROOT, "tools", "checks", "simctl_catalogue.py")
    FAULT = os.path.join(ROOT, "firmware", "simnode", "src", "fault.cpp")

    def run_check(self):
        return subprocess.run([sys.executable, self.CHECK], capture_output=True, text=True)

    def test_it_passes_on_the_committed_tree(self):
        r = self.run_check()
        self.assertEqual(0, r.returncode, r.stdout + r.stderr)

    def test_it_fails_when_the_firmware_gains_a_fault_with_no_scenario(self):
        original = pathlib.Path(self.FAULT).read_text()
        injected = original.replace(
            '{"seq_jump", FaultId::SeqJump, 1, kNone,',
            '{"newly_added", FaultId::SeqJump, 1, kNone, "x", nullptr, 0},\n'
            '    {"seq_jump", FaultId::SeqJump, 1, kNone,', 1)
        self.assertNotEqual(original, injected)
        try:
            pathlib.Path(self.FAULT).write_text(injected)
            r = self.run_check()
            self.assertEqual(1, r.returncode, "the check passed on a fault with no scenario")
            self.assertIn("newly_added", r.stdout)
        finally:
            pathlib.Path(self.FAULT).write_text(original)
        self.assertEqual(0, self.run_check().returncode, "the fixture did not restore")

    def test_it_fails_when_a_rows_frame_count_changes_under_it(self):
        original = pathlib.Path(self.FAULT).read_text()
        injected = original.replace(
            '{"frag_dup", FaultId::FragDup, 4,', '{"frag_dup", FaultId::FragDup, 9,', 1)
        self.assertNotEqual(original, injected)
        try:
            pathlib.Path(self.FAULT).write_text(injected)
            r = self.run_check()
            self.assertEqual(1, r.returncode, "the check passed on a changed frame count")
            self.assertIn("frag_dup", r.stdout)
        finally:
            pathlib.Path(self.FAULT).write_text(original)


if __name__ == "__main__":
    unittest.main(verbosity=2)
