#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Host tests for the interleaved sweep's arithmetic.
#
# NO BOARD AND NO BROKER. Everything here builds bursts in memory, which is why
# sweep_analyze.py does no I/O: the part that decides which variable a sweep separated
# is testable at a desk, and only the part that drives two boards needs them.
#
#   python3 tools/simctl/test_sweep_analyze.py

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from sweep_analyze import (
    MIN_LOSSES_FOR_DIRECTION,
    SEPARATION_RATIO,
    Burst,
    burst_from,
    by_arm,
    by_arm_and_half,
    by_half,
    format_report,
    pool,
    slice_records,
    split_halves,
    verdict,
)
from sweep_interleave import arm_name, format_blast, parse_blast, schedule

PEER = 0xF3
TYPE_STATUS = 0x04


def rx(index, ms, seq, peer=PEER, deaf=0, st=0):
    """One delivered reception, as the bridge publishes it."""
    return {"i": index, "ms": ms, "deaf": deaf, "d": "rx", "peer": peer,
            "type": TYPE_STATUS, "schema": 0x10, "frag": 1, "seq": seq,
            "rssi": -42, "snr": 9, "st": st, "rx": 0}


def burst(n, arm, sent, lost, gap_ms=250):
    return Burst(n, arm, gap_ms, sent, sent - lost)


def alternating(losses_a, losses_b, sent=40):
    """Bursts A,B,A,B... with one loss count per burst of each arm, in run order."""
    out = []
    n = 1
    for la, lb in zip(losses_a, losses_b):
        out.append(burst(n, "gap250", sent, la, 250))
        out.append(burst(n + 1, "gap2000", sent, lb, 2000))
        n += 2
    return out


class Schedule(unittest.TestCase):
    def test_the_arms_alternate(self):
        self.assertEqual(schedule([250, 2000], 1), [250, 2000])

    def test_the_lead_swaps_each_pair(self):
        # A plain A,B,A,B rotation puts every A before its own B, so an effect that
        # decays through a pair would land entirely on B.
        self.assertEqual(schedule([250, 2000], 3),
                         [250, 2000, 2000, 250, 250, 2000])

    def test_each_arm_runs_once_per_pair(self):
        order = schedule([250, 2000], 5)
        self.assertEqual(order.count(250), 5)
        self.assertEqual(order.count(2000), 5)


class Blaster(unittest.TestCase):
    """V-B12's loaded arm: the arm names and the blaster's totals line."""

    def test_the_loaded_arm_is_named_for_its_rate(self):
        self.assertEqual(arm_name(2000), "gap2000")
        self.assertEqual(arm_name(2000, 4000), "blast4000")

    def test_the_arms_alternate_by_load(self):
        arms = [(2000, 0), (2000, 4000)]
        self.assertEqual(schedule(arms, 2),
                         [(2000, 0), (2000, 4000), (2000, 4000), (2000, 0)])

    def test_the_totals_line_parses(self):
        got = parse_blast("blast: off sent=1200 bytes=1766400 fail=3 err=12 down=0"
                          " ms=30000 kbps=471")
        self.assertEqual(got, {"state": "off", "sent": 1200, "bytes": 1766400, "fail": 3,
                               "err": 12, "down": 0, "ms": 30000, "kbps": 471})

    def test_the_start_line_is_not_a_totals_line(self):
        # `blast: on kbps=.. bytes=.. to host:9` carries no counts; taking it for the
        # totals would record the asked-for rate as the achieved one.
        self.assertIsNone(parse_blast("blast: on kbps=4000 bytes=1472 to 192.0.2.52:9"))

    def test_other_bridge_output_is_ignored(self):
        self.assertIsNone(parse_blast("roll: f1 rolled to ctx 0x1234"))
        self.assertIsNone(parse_blast("blast: refused - kbps 1..50000, bytes 16..1472"))

    def test_a_missing_report_is_shown_not_dropped(self):
        text = format_blast([{"blast_kbps": 4000, "blast": None}])
        self.assertIn("no totals reported", text)


class Slicing(unittest.TestCase):
    def test_the_slice_opens_after_i_before_and_closes_on_i_after(self):
        records = [rx(i, i * 100, i) for i in range(10)]
        got = slice_records(records, {"i_before": 3, "i_after": 6})
        self.assertEqual([r["i"] for r in got], [4, 5, 6])

    def test_no_i_before_means_from_the_start(self):
        # The first burst of a session runs before the bridge has published anything.
        records = [rx(i, i * 100, i) for i in range(4)]
        got = slice_records(records, {"i_before": None, "i_after": 2})
        self.assertEqual([r["i"] for r in got], [0, 1, 2])


class OneBurst(unittest.TestCase):
    def test_arrivals_come_only_from_the_flooding_peer(self):
        records = [rx(0, 0, 1), rx(1, 300, 2), rx(2, 600, 9, peer=0xF1)]
        b = burst_from({"arm": "gap250", "gap_ms": 250, "sent": 3, "n": 1}, records, PEER)
        self.assertEqual(b.arrived, 2)
        self.assertEqual(b.lost, 1)

    def test_a_discarded_frame_is_not_an_arrival(self):
        # It arrived and the ladder counted it. Folding it in here reports it twice.
        records = [rx(0, 0, 1), rx(1, 300, 2, st=5)]
        b = burst_from({"arm": "gap250", "gap_ms": 250, "sent": 2, "n": 1}, records, PEER)
        self.assertEqual(b.arrived, 1)

    def test_more_arrivals_than_frames_sent_refuses_the_burst(self):
        records = [rx(0, 0, 1), rx(1, 300, 2), rx(2, 600, 3)]
        b = burst_from({"arm": "gap250", "gap_ms": 250, "sent": 2, "n": 1}, records, PEER)
        self.assertFalse(b.valid)
        self.assertIn("only 2 were sent", b.invalid_reason)

    def test_a_burst_that_sent_nothing_refuses(self):
        b = burst_from({"arm": "gap250", "gap_ms": 250, "sent": 0, "n": 1}, [], PEER)
        self.assertFalse(b.valid)
        self.assertIn("sent nothing", b.invalid_reason)

    def test_a_ring_overwrite_refuses_the_burst(self):
        # A missing record and a missing frame are indistinguishable once the ring has
        # overwritten, so the figure is refused rather than reported low.
        records = [rx(0, 0, 1), rx(1, 300, 2)]
        b = burst_from({"arm": "gap250", "gap_ms": 250, "sent": 2, "n": 1}, records,
                       PEER, ring_lost=3)
        self.assertFalse(b.valid)
        self.assertIn("ring overwrote 3", b.invalid_reason)

    def test_a_seq_gap_is_carried_through_with_its_detail(self):
        records = [rx(0, 0, 1, deaf=0), rx(1, 600, 3, deaf=21)]
        b = burst_from({"arm": "gap250", "gap_ms": 250, "sent": 3, "n": 1}, records, PEER)
        self.assertEqual(len(b.seq_losses), 1)
        self.assertEqual(b.seq_losses[0].count, 1)
        self.assertAlmostEqual(b.seq_losses[0].deaf_fraction, 21.0 / 600.0)


class Pooling(unittest.TestCase):
    def test_frames_are_pooled_not_bursts_averaged(self):
        # The 2026-09-17 case: averaging the bursts gives 4.4 %, pooling gives 5.6 %.
        bursts = [burst(1, "gap250", 50, 5), burst(2, "gap250", 50, 4),
                  burst(3, "gap250", 50, 1), burst(4, "gap250", 50, 1),
                  burst(5, "gap250", 50, 3)]
        sent, lost, per = pool(bursts)
        self.assertEqual((sent, lost), (250, 14))
        self.assertAlmostEqual(per, 5.6)

    def test_a_refused_burst_contributes_nothing(self):
        bad = burst(2, "gap250", 50, 0)
        bad.invalid_reason = "refused"
        sent, lost, _per = pool([burst(1, "gap250", 50, 5), bad])
        self.assertEqual((sent, lost), (50, 5))

    def test_arms_group_in_the_order_they_first_ran(self):
        names = [arm for arm, _figures in by_arm(alternating([1], [2]))]
        self.assertEqual(names, ["gap250", "gap2000"])

    def test_the_halves_are_cut_by_burst_count_not_by_clock(self):
        # A 2000 ms burst takes several times as long as a 250 ms one, so a clock cut
        # would put most of one arm in one half - the confound this tool removes.
        bursts = alternating([3, 3, 0, 0], [3, 3, 0, 0])
        halves = by_half(bursts)
        self.assertEqual([label for label, _f in halves], ["first half", "second half"])
        self.assertEqual(halves[0][1][1], 12)
        self.assertEqual(halves[1][1][1], 0)


class CrossTab(unittest.TestCase):
    def test_one_cell_per_arm_per_half(self):
        cells = by_arm_and_half(alternating([3, 3, 0, 0], [1, 1, 0, 0]))
        self.assertEqual([(arm, half) for arm, half, _f in cells],
                         [("gap250", "first half"), ("gap2000", "first half"),
                          ("gap250", "second half"), ("gap2000", "second half")])

    def test_the_cells_hold_each_arms_own_frames(self):
        # 2026-09-21's shape: the dense arm loses more in both halves and grows through
        # the session, while the sparse arm holds still.
        cells = by_arm_and_half(alternating([3, 0, 5, 4], [1, 0, 1, 0]))
        figures = {(arm, half): f for arm, half, f in cells}
        self.assertEqual(figures[("gap250", "first half")][1], 3)
        self.assertEqual(figures[("gap250", "second half")][1], 9)
        self.assertEqual(figures[("gap2000", "first half")][1], 1)
        self.assertEqual(figures[("gap2000", "second half")][1], 1)

    def test_an_odd_burst_count_puts_the_extra_burst_in_the_second_half(self):
        first, second = split_halves([burst(n, "gap250", 40, 0) for n in range(5)])
        self.assertEqual((len(first), len(second)), (2, 3))


class Verdict(unittest.TestCase):
    def test_too_few_losses_claims_no_direction(self):
        direction, text = verdict(alternating([2, 0, 0, 0], [2, 0, 0, 0]))
        self.assertIsNone(direction)
        self.assertIn("too few", text)
        self.assertIn(str(MIN_LOSSES_FOR_DIRECTION), text)

    def test_arms_apart_and_halves_together_points_at_spacing(self):
        direction, text = verdict(alternating([3, 3, 3, 3], [0, 0, 0, 0]))
        self.assertEqual(direction, "spacing")
        self.assertIn("SPACING", text)

    def test_halves_apart_and_arms_together_points_at_time(self):
        direction, text = verdict(alternating([3, 3, 0, 0], [3, 3, 0, 0]))
        self.assertEqual(direction, "time")
        self.assertIn("TIME", text)

    def test_losses_in_one_arm_and_one_half_separate_nothing(self):
        # Every loss sits in the first half AND in one arm, so the second half shows
        # nothing to compare and the two explanations are indistinguishable.
        direction, text = verdict(alternating([6, 6, 0, 0], [0, 0, 0, 0]))
        self.assertIsNone(direction)
        self.assertIn("does not hold inside both", text)

    def test_an_arm_effect_that_holds_in_both_halves_survives_a_time_trend(self):
        # 2026-09-21's shape. Pooling by half moves too, so the two-way splits alone
        # would refuse; the cross-tab shows the dense arm losing more in each half.
        direction, text = verdict(alternating([3, 0, 5, 4], [1, 0, 1, 0]))
        self.assertEqual(direction, "spacing")
        self.assertIn("BOTH halves", text)
        self.assertIn("time effect rides on top", text)
        self.assertIn("gap250 went from", text)
        self.assertIn("other arm held still", text)

    def test_neither_split_moving_separates_nothing(self):
        direction, text = verdict(alternating([2, 2, 2, 2], [2, 2, 2, 2]))
        self.assertIsNone(direction)
        self.assertIn("Neither split moved", text)

    def test_the_separation_ratio_is_the_line_and_is_named(self):
        self.assertGreaterEqual(SEPARATION_RATIO, 1.0)
        # 4 lost against 2 is exactly the ratio, and it counts as separated.
        direction, _text = verdict(alternating([4, 4, 4, 4], [2, 2, 2, 2]))
        self.assertEqual(direction, "spacing")


class Report(unittest.TestCase):
    def test_a_refused_burst_says_so_in_the_run_order_table(self):
        bad = burst(2, "gap2000", 40, 0, 2000)
        bad.invalid_reason = "the simnode sent nothing"
        text = format_report([burst(1, "gap250", 40, 1), bad], PEER)
        self.assertIn("REFUSED - the simnode sent nothing", text)

    def test_both_splits_are_printed_before_the_verdict(self):
        text = format_report(alternating([3, 3, 0, 0], [3, 3, 0, 0]), PEER)
        self.assertLess(text.index("pooled by arm"), text.index("pooled by when"))
        self.assertLess(text.index("pooled by when"), text.index("TIME"))

    def test_no_bursts_is_said_plainly(self):
        # Nothing driven and nothing published look identical in a table of zeroes.
        self.assertIn("NO BURSTS", format_report([], PEER))


if __name__ == "__main__":
    unittest.main(verbosity=2)
