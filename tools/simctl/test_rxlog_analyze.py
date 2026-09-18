#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Host tests for BF-27's frame-log arithmetic.
#
# NO BOARD AND NO BROKER. Everything here feeds analyze() a list of records, which is why
# rxlog_analyze.py does no I/O: the part that decides what a burst showed is testable at
# a desk, and only the part that subscribes needs a broker.
#
#   python3 tools/simctl/test_rxlog_analyze.py

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from rxlog import format_report
from rxlog_analyze import (
    MAX_CREDIBLE_GAP,
    SEQ_SPACE,
    analyze,
    merge,
    seq_delta,
    verdict,
)

TYPE_STATUS = 0x04  # lran::MsgType::Status
TYPE_PING = 0x07    # lran::MsgType::Ping


def rx(index, ms, seq, peer=0xF0, msg_type=TYPE_STATUS, deaf=0, st=0):
    """One delivered reception, as the bridge publishes it."""
    return {"i": index, "ms": ms, "deaf": deaf, "d": "rx", "peer": peer,
            "type": msg_type, "schema": 0x10, "frag": 1, "seq": seq,
            "rssi": -42, "snr": 9, "st": st, "rx": 0}


def tx(index, ms, seq, peer=0xF0, deaf=0):
    return {"i": index, "ms": ms, "deaf": deaf, "d": "tx", "peer": peer,
            "type": 0x03, "schema": 0x00, "frag": 1, "seq": seq,
            "rssi": None, "snr": None, "st": 0, "rx": 5}


class SeqArithmetic(unittest.TestCase):
    def test_a_normal_step_is_one(self):
        self.assertEqual(1, seq_delta(10, 11))

    def test_the_sequence_space_wraps(self):
        # spec 5.4 - seq is a uint16. The frame after 65535 is 0, not a loss of 65535.
        self.assertEqual(1, seq_delta(SEQ_SPACE - 1, 0))
        self.assertEqual(3, seq_delta(SEQ_SPACE - 2, 1))


class Merging(unittest.TestCase):
    def test_batches_become_one_ordered_list(self):
        records, ring, transport = merge([
            {"lost": 0, "f": [rx(0, 0, 1), rx(1, 250, 2)]},
            {"lost": 0, "f": [rx(2, 500, 3)]},
        ])
        self.assertEqual([0, 1, 2], [r["i"] for r in records])
        self.assertEqual(0, ring)
        self.assertEqual(0, transport)

    def test_batches_out_of_order_are_sorted_by_index(self):
        records, _, _ = merge([
            {"lost": 0, "f": [rx(2, 500, 3)]},
            {"lost": 0, "f": [rx(0, 0, 1), rx(1, 250, 2)]},
        ])
        self.assertEqual([0, 1, 2], [r["i"] for r in records])

    def test_a_republished_batch_is_not_counted_twice(self):
        records, _, _ = merge([
            {"lost": 0, "f": [rx(0, 0, 1)]},
            {"lost": 0, "f": [rx(0, 0, 1)]},
        ])
        self.assertEqual(1, len(records))

    def test_the_rings_own_loss_is_reported_separately_from_the_transports(self):
        # THE POINT: adding these together would blame the receive path for a dropped
        # MQTT message. The ring admits 3; the index span admits 3 more.
        records, ring, transport = merge([
            {"lost": 3, "f": [rx(10, 0, 1), rx(11, 250, 2)]},
            {"lost": 3, "f": [rx(18, 500, 3)]},
        ])
        self.assertEqual(3, ring)
        # indices 10..18 is a span of 9, three records present, six missing, three of
        # which the ring owns.
        self.assertEqual(3, transport)

    def test_the_ring_loss_reported_is_the_highest_seen(self):
        # It is cumulative on the bridge, so a later batch supersedes an earlier one.
        _, ring, _ = merge([
            {"lost": 2, "f": [rx(0, 0, 1)]},
            {"lost": 7, "f": [rx(1, 250, 2)]},
        ])
        self.assertEqual(7, ring)

    def test_no_records_is_not_an_error(self):
        records, ring, transport = merge([])
        self.assertEqual([], records)
        self.assertEqual(0, ring)
        self.assertEqual(0, transport)


class Gaps(unittest.TestCase):
    def test_an_unbroken_run_loses_nothing(self):
        streams = analyze([rx(i, i * 250, i + 1) for i in range(10)])
        stream = streams[(0xF0, TYPE_STATUS)]
        self.assertEqual(10, stream.arrivals)
        self.assertEqual(0, stream.lost_frames)

    def test_a_missing_seq_is_one_loss(self):
        streams = analyze([rx(0, 0, 1), rx(1, 500, 3)])
        stream = streams[(0xF0, TYPE_STATUS)]
        self.assertEqual(1, stream.lost_frames)
        loss = stream.losses[0]
        self.assertEqual(1, loss.after_seq)
        self.assertEqual(500, loss.dt_ms)

    def test_a_run_of_missing_frames_is_one_gap_of_several(self):
        streams = analyze([rx(0, 0, 1), rx(1, 1000, 5)])
        stream = streams[(0xF0, TYPE_STATUS)]
        self.assertEqual(1, len(stream.losses))
        self.assertEqual(3, stream.losses[0].count)

    def test_expected_is_what_arrived_plus_what_did_not(self):
        streams = analyze([rx(0, 0, 1), rx(1, 1000, 5)])
        self.assertEqual(5, streams[(0xF0, TYPE_STATUS)].expected)

    def test_a_repeated_seq_is_a_duplicate_and_not_a_loss(self):
        # An RF echo or a sender retry. Root rule 2 means a retry reuses its seq.
        streams = analyze([rx(0, 0, 1), rx(1, 250, 1), rx(2, 500, 2)])
        stream = streams[(0xF0, TYPE_STATUS)]
        self.assertEqual(1, stream.duplicates)
        self.assertEqual(0, stream.lost_frames)

    def test_a_resync_is_not_forty_thousand_losses(self):
        # spec 10.3 - a resync resets the sequence. per_window.py refuses a counter that
        # went backwards for the same reason: a plausible wrong number is worse than a
        # refusal.
        streams = analyze([rx(0, 0, 60000), rx(1, 250, 1)])
        stream = streams[(0xF0, TYPE_STATUS)]
        self.assertEqual(1, stream.resyncs)
        self.assertEqual(0, stream.lost_frames)

    def test_the_credible_gap_boundary_is_still_a_loss(self):
        streams = analyze([rx(0, 0, 1), rx(1, 250, 1 + MAX_CREDIBLE_GAP)])
        self.assertEqual(MAX_CREDIBLE_GAP - 1,
                         streams[(0xF0, TYPE_STATUS)].lost_frames)

    def test_a_gap_across_the_wrap_is_read_as_a_gap(self):
        streams = analyze([rx(0, 0, SEQ_SPACE - 1), rx(1, 500, 1)])
        self.assertEqual(1, streams[(0xF0, TYPE_STATUS)].lost_frames)


class StreamSeparation(unittest.TestCase):
    def test_two_peers_do_not_share_a_sequence_space(self):
        streams = analyze([rx(0, 0, 1, peer=0xF0), rx(1, 250, 50, peer=0xF2),
                           rx(2, 500, 2, peer=0xF0), rx(3, 750, 51, peer=0xF2)])
        self.assertEqual(2, len(streams))
        self.assertEqual(0, streams[(0xF0, TYPE_STATUS)].lost_frames)
        self.assertEqual(0, streams[(0xF2, TYPE_STATUS)].lost_frames)

    def test_w11_a_ping_echo_does_not_invent_a_loss_in_the_status_stream(self):
        # W11 - a PING responder echoes the INITIATOR's seq (spec 6.6), so this node's
        # PING answer carries a number from the BRIDGE's space. Grouped with the status
        # frames it would read as a gap of hundreds and then a gap back.
        streams = analyze([
            rx(0, 0, 1, msg_type=TYPE_STATUS),
            rx(1, 100, 900, msg_type=TYPE_PING),
            rx(2, 250, 2, msg_type=TYPE_STATUS),
            rx(3, 350, 901, msg_type=TYPE_PING),
        ])
        self.assertEqual(0, streams[(0xF0, TYPE_STATUS)].lost_frames)
        self.assertEqual(0, streams[(0xF0, TYPE_PING)].lost_frames)

    def test_a_discarded_frame_does_not_advance_a_stream(self):
        # It arrived, so it is not a loss - and the receive ladder already counts it.
        # Counting it here too would report one event under two names.
        streams = analyze([rx(0, 0, 1), rx(1, 250, 2, st=4), rx(2, 500, 3)])
        stream = streams[(0xF0, TYPE_STATUS)]
        self.assertEqual(2, stream.arrivals)
        self.assertEqual(1, stream.lost_frames)


class WhatTheBridgeWasDoing(unittest.TestCase):
    def test_a_transmission_inside_the_gap_is_recorded(self):
        streams = analyze([rx(0, 0, 1), tx(1, 300, 9), rx(2, 600, 3)])
        loss = streams[(0xF0, TYPE_STATUS)].losses[0]
        self.assertTrue(loss.tx_inside)

    def test_a_transmission_outside_the_gap_is_not_attributed_to_it(self):
        # THE WHOLE POINT OF WALKING BACK ONLY TO THE PREVIOUS ARRIVAL. A transmit
        # earlier in the burst is not an explanation for this gap, and counting it as
        # one is the correlation the aggregate instruments already failed as.
        streams = analyze([tx(0, 0, 9), rx(1, 100, 1), rx(2, 350, 2), rx(3, 850, 4)])
        loss = streams[(0xF0, TYPE_STATUS)].losses[0]
        self.assertFalse(loss.tx_inside)

    def test_deaf_time_is_the_difference_of_two_running_totals(self):
        # rx_deaf_ms is cumulative (rx_deaf.h), which is why it is in every record.
        streams = analyze([rx(0, 0, 1, deaf=600), rx(1, 500, 3, deaf=643)])
        self.assertEqual(43, streams[(0xF0, TYPE_STATUS)].losses[0].deaf_ms)

    def test_a_gap_with_the_radio_in_receive_throughout_is_not_the_bridges(self):
        streams = analyze([rx(0, 0, 1, deaf=600), rx(1, 500, 3, deaf=600)])
        loss = streams[(0xF0, TYPE_STATUS)].losses[0]
        self.assertEqual(0, loss.deaf_ms)
        self.assertFalse(loss.tx_inside)
        self.assertEqual(0.0, loss.deaf_fraction)

    def test_the_deaf_share_of_the_gap_is_a_ceiling_and_not_a_verdict(self):
        # THE BUG THIS TEST EXISTS FOR, found by running the tool on the bench
        # 2026-09-17: 21 ms of deafness in a 612 ms gap was reported as "the bridge was
        # not listening". It was listening for 96.6 % of it.
        streams = analyze([rx(0, 0, 1, deaf=600), rx(1, 612, 3, deaf=621)])
        loss = streams[(0xF0, TYPE_STATUS)].losses[0]
        self.assertAlmostEqual(21.0 / 612.0, loss.deaf_fraction, places=6)
        self.assertAlmostEqual(21.0 / 612.0, loss.attributable_frames, places=6)

    def test_several_missing_frames_scale_the_ceiling(self):
        streams = analyze([rx(0, 0, 1, deaf=0), rx(1, 1000, 4, deaf=500)])
        loss = streams[(0xF0, TYPE_STATUS)].losses[0]
        self.assertEqual(2, loss.count)
        self.assertAlmostEqual(0.5, loss.deaf_fraction, places=6)
        self.assertAlmostEqual(1.0, loss.attributable_frames, places=6)

    def test_a_zero_length_gap_does_not_divide_by_zero(self):
        streams = analyze([rx(0, 100, 1, deaf=0), rx(1, 100, 3, deaf=0)])
        self.assertEqual(0.0, streams[(0xF0, TYPE_STATUS)].losses[0].deaf_fraction)


class Verdict(unittest.TestCase):
    def test_the_ceiling_is_the_sum_of_the_gaps_deaf_shares(self):
        streams = analyze([
            rx(0, 0, 1, deaf=600),
            rx(1, 500, 3, deaf=850),   # 1 lost, deaf 250 ms of 500 -> ceiling 0.5
            rx(2, 750, 4, deaf=850),
            rx(3, 1250, 6, deaf=850),  # 1 lost, deaf 0 ms -> ceiling 0
        ])
        total, tx_gaps, attributable = verdict(streams)
        self.assertEqual(2, total)
        self.assertEqual(0, tx_gaps)
        self.assertAlmostEqual(0.5, attributable, places=6)

    def test_a_gap_the_bridge_was_deaf_right_across_has_a_ceiling_of_one(self):
        streams = analyze([rx(0, 0, 1, deaf=0), rx(1, 500, 3, deaf=500)])
        _, _, attributable = verdict(streams)
        self.assertAlmostEqual(1.0, attributable, places=6)

    def test_a_clean_burst_reports_nothing_rather_than_dividing_by_zero(self):
        self.assertEqual((0, 0, 0.0), verdict(analyze([rx(i, i * 250, i + 1)
                                                       for i in range(5)])))


class Reporting(unittest.TestCase):
    """format_report is pure, so the text an operator reads is exercised here too."""

    def test_an_empty_capture_says_so_rather_than_printing_zeroes(self):
        # Nothing arriving and nothing being published look identical in a table of
        # zeroes, and they have completely different fixes.
        text = format_report([])
        self.assertIn("NO RECORDS", text)

    def test_a_clean_burst_says_there_is_nothing_to_attribute(self):
        text = format_report([{"lost": 0, "f": [rx(i, i * 250, i + 1)
                                                for i in range(10)]}])
        self.assertIn("NO LOSSES", text)

    def test_losses_while_receiving_point_away_from_this_firmware(self):
        text = format_report([{"lost": 0, "f": [
            rx(0, 0, 1, deaf=600), rx(1, 500, 3, deaf=600)]}])
        self.assertIn("AT MOST 0.00", text)
        self.assertIn("transmit path cannot be the story", text)

    def test_a_small_deafness_in_a_long_gap_does_not_become_a_verdict(self):
        # The bench case from 2026-09-17: 21 ms of a 612 ms gap. The report must not
        # read that as the bridge having been deaf for the loss.
        text = format_report([{"lost": 0, "f": [
            rx(0, 0, 1, deaf=600), rx(1, 612, 3, deaf=621)]}])
        self.assertIn("transmit path cannot be the story", text)
        self.assertNotIn("not listening", text)

    def test_losses_the_bridge_was_deaf_right_across_point_at_it(self):
        text = format_report([{"lost": 0, "f": [
            rx(0, 0, 1, deaf=0), rx(1, 500, 3, deaf=500)]}])
        self.assertIn("could account for most of this loss", text)

    def test_a_ring_overwrite_is_flagged_before_the_gaps_are_read(self):
        # A gap below may be a record that was MADE and lost, not a frame that never
        # arrived, and the reader has to know that before reading them.
        text = format_report([{"lost": 4, "f": [rx(0, 0, 1), rx(9, 500, 3)]}])
        self.assertIn("RING OVERWROTE 4", text)
        self.assertLess(text.index("RING OVERWROTE"), text.index("after seq"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
