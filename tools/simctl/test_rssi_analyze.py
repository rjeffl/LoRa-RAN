#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Host tests for M25's channel-capture arithmetic.
#
# NO BOARD AND NO SERIAL PORT. Everything here feeds parse() a list of lines, which is why
# rssi_analyze.py does no I/O.
#
#   python3 tools/simctl/test_rssi_analyze.py

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from rssi_analyze import NO_READING, hourly, parse, peak_histogram, summarise
from rssi_report import report

BOOT = "2026-09-17T20:00:00Z,CHAN-BOOT,abc1234,917400000,9,1250,1000,-1100,10"


def chan(host, seq, start_ms, samples=100, skipped=0, above=0, own_rx=0,
         peak=-1150, floor=-1160, mean=-1155, dur=1000):
    """A CHAN line: one bucket that saw something."""
    return "%s,CHAN,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d" % (
        host, seq, start_ms, dur, samples, skipped, above, own_rx, peak, floor, mean)


def chansum(host, from_seq, to_seq, buckets=60, span=60000, samples=6000, skipped=0,
            above=0, own_rx=0, notable=0, blind=0, peak=-1150, fmin=-1160, fmax=-1155,
            fmean=-1158):
    """A CHANSUM line: every bucket in the window, loud and quiet. The denominator."""
    return "%s,CHANSUM,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d" % (
        host, from_seq, to_seq, buckets, span, samples, skipped, above, own_rx,
        notable, blind, peak, fmin, fmax, fmean)


class Parsing(unittest.TestCase):
    def test_a_boot_header_opens_a_segment_and_carries_the_settings(self):
        b = parse([BOOT, chan("2026-09-17T20:00:01Z", 0, 0)])
        self.assertEqual(1, len(b))
        self.assertEqual("abc1234", b[0].git)
        self.assertEqual(917400000, b[0].freq_hz)
        self.assertEqual(1000, b[0].bucket_ms)
        self.assertEqual(-1100, b[0].occupied_dbm10)
        self.assertEqual(1, len(b[0].buckets))

    def test_a_capture_that_began_mid_run_still_parses(self):
        # Losing an overnight run because the header scrolled past is not acceptable.
        b = parse([chan("2026-09-17T20:00:01Z", 412, 412000)])
        self.assertEqual(1, len(b))
        self.assertEqual(1, len(b[0].buckets))
        self.assertIsNone(b[0].git)

    def test_a_reboot_starts_a_new_segment(self):
        b = parse([BOOT, chan("2026-09-17T20:00:01Z", 0, 0),
                   BOOT.replace("20:00:00", "21:00:00"),
                   chan("2026-09-17T21:00:01Z", 0, 0)])
        self.assertEqual(2, len(b))

    def test_a_sequence_going_backwards_segments_even_with_no_header(self):
        # A reboot whose header the capture missed. Pooling two millis() clocks would
        # produce a span that runs backwards.
        b = parse([chan("2026-09-17T20:00:01Z", 50, 50000),
                   chan("2026-09-17T20:00:02Z", 51, 51000),
                   chan("2026-09-17T20:05:00Z", 0, 0)])
        self.assertEqual(2, len(b))
        self.assertEqual(2, len(b[0].buckets))
        self.assertEqual(1, len(b[1].buckets))

    def test_frame_lines_are_kept_not_discarded(self):
        b = parse([BOOT, "2026-09-17T20:00:01Z,FRAME #12 rx t=1 peer=0xf3 seq=4"])
        self.assertEqual(1, len(b[0].frames))

    def test_comments_and_port_markers_are_ignored(self):
        b = parse(["# M25 capture opened", "2026-09-17T20:00:00Z,#PORT-OPEN",
                   BOOT, chan("2026-09-17T20:00:01Z", 0, 0)])
        self.assertEqual(1, len(b))
        self.assertEqual(1, len(b[0].buckets))

    def test_a_malformed_line_is_skipped_rather_than_crashing_a_12_hour_capture(self):
        b = parse([BOOT, "2026-09-17T20:00:01Z,CHAN,1,2,3", chan("2026-09-17T20:00:02Z", 0, 0)])
        self.assertEqual(1, len(b[0].buckets))


class RollupParsing(unittest.TestCase):
    def test_a_rollup_line_parses(self):
        b = parse([BOOT, chansum("2026-09-17T20:01:00Z", 0, 59)])
        self.assertEqual(1, len(b[0].rollups))
        self.assertEqual(60, b[0].rollups[0]["buckets"])

    def test_a_rollup_sequence_going_backwards_segments(self):
        b = parse([chansum("2026-09-17T20:01:00Z", 600, 659),
                   chansum("2026-09-17T20:02:00Z", 0, 59)])
        self.assertEqual(2, len(b))

    def test_a_short_rollup_line_is_skipped(self):
        b = parse([BOOT, "2026-09-17T20:01:00Z,CHANSUM,1,2,3"])
        self.assertEqual(0, len(b[0].rollups))


class Summary(unittest.TestCase):
    def test_occupancy_comes_from_the_rollups_not_the_excursion_lines(self):
        # THE FAILURE THIS GUARDS. The firmware writes a CHAN line only for a bucket that
        # saw something. Summing those as the denominator divides the excursions by
        # themselves and reports an occupancy near 100 % on a nearly-silent channel.
        b = parse([BOOT,
                   chan("2026-09-17T20:00:05Z", 5, 5000, samples=100, above=20, peak=-800),
                   chansum("2026-09-17T20:01:00Z", 0, 59, samples=6000, above=20,
                           notable=1, peak=-800)])
        s = summarise(b[0])
        self.assertEqual(6000, s["samples"])
        self.assertEqual(20, s["above"])
        self.assertAlmostEqual(20 / 6000, s["occupancy"], places=9)
        self.assertEqual(1, s["buckets_logged"])
        self.assertEqual(60, s["buckets"])

    def test_a_capture_with_no_rollups_reports_no_denominator(self):
        b = parse([BOOT, chan("2026-09-17T20:00:05Z", 5, 5000, samples=100, above=20)])
        s = summarise(b[0])
        self.assertFalse(s["has_denominator"])
        self.assertIsNone(s["occupancy"])

    def test_blind_buckets_are_counted_from_the_rollups(self):
        b = parse([BOOT, chansum("2026-09-17T20:01:00Z", 0, 59, buckets=60,
                                 samples=5900, skipped=100, blind=1)])
        s = summarise(b[0])
        self.assertEqual(60, s["buckets"])
        self.assertEqual(1, s["buckets_blind"])
        self.assertEqual(59, s["buckets_observed"])

    def test_an_all_blind_window_reports_no_occupancy_rather_than_zero(self):
        b = parse([BOOT, chansum("2026-09-17T20:01:00Z", 0, 59, buckets=60, samples=0,
                                 skipped=6000, blind=60, peak=NO_READING,
                                 fmin=NO_READING, fmax=NO_READING, fmean=NO_READING)])
        s = summarise(b[0])
        self.assertIsNone(s["occupancy"])
        self.assertIsNone(s["floor_median"])
        self.assertIsNone(s["peak_max"])

    def test_the_floor_range_spans_every_rollup(self):
        b = parse([BOOT,
                   chansum("2026-09-17T20:01:00Z", 0, 59, fmin=-1165, fmax=-1150, fmean=-1160),
                   chansum("2026-09-17T20:02:00Z", 60, 119, fmin=-1158, fmax=-1140, fmean=-1150)])
        s = summarise(b[0])
        self.assertEqual(-1165, s["floor_min"])
        self.assertEqual(-1140, s["floor_max"])

    def test_own_receptions_are_flagged(self):
        b = parse([BOOT, chan("2026-09-17T20:00:01Z", 0, 0, own_rx=3, above=1),
                   chansum("2026-09-17T20:01:00Z", 0, 59, own_rx=3, above=1, notable=1)])
        s = summarise(b[0])
        self.assertEqual(1, s["buckets_with_own_rx"])
        self.assertEqual(3, s["own_rx"])

    def test_excursions_are_the_logged_buckets_that_reached_the_threshold(self):
        b = parse([BOOT,
                   chan("2026-09-17T20:00:02Z", 1, 1000, peak=-540, above=2),
                   chansum("2026-09-17T20:01:00Z", 0, 59, above=2, notable=1, peak=-540)])
        s = summarise(b[0])
        self.assertEqual(1, len(s["excursions"]))
        self.assertEqual(-540, s["excursions"][0]["peak"])
        self.assertEqual(-540, s["peak_max"])

    def test_the_span_is_summed_from_the_rollups(self):
        b = parse([BOOT,
                   chansum("2026-09-17T20:01:00Z", 0, 59, span=60000),
                   chansum("2026-09-17T20:02:00Z", 60, 119, span=61000)])
        s = summarise(b[0])
        self.assertEqual(121000, s["span_ms"])


class Hourly(unittest.TestCase):
    def test_hours_are_separated_so_a_schedule_is_visible(self):
        b = parse([BOOT,
                   chansum("2026-09-17T20:30:00Z", 0, 59, samples=6000, above=0),
                   chansum("2026-09-17T21:30:00Z", 60, 119, samples=6000, above=3000,
                           peak=-600)])
        rows = hourly(b[0])
        self.assertEqual(["2026-09-17T20", "2026-09-17T21"], list(rows))
        self.assertAlmostEqual(0.0, rows["2026-09-17T20"]["occupancy"])
        self.assertAlmostEqual(0.5, rows["2026-09-17T21"]["occupancy"])
        self.assertEqual(-600, rows["2026-09-17T21"]["peak"])


class Histogram(unittest.TestCase):
    def test_buckets_land_in_the_band_their_peak_falls_in(self):
        b = parse([BOOT,
                   chan("2026-09-17T20:00:01Z", 0, 0, peak=-1160),
                   chan("2026-09-17T20:00:02Z", 1, 1000, peak=-1120),
                   chan("2026-09-17T20:00:03Z", 2, 2000, peak=-540)])
        h = {r["lo"]: r["buckets"] for r in peak_histogram(b[0])}
        self.assertEqual(1, h[-1150])   # -1120 lands in [-1150, -1100)
        self.assertEqual(1, h[-600])    # -540 lands in [-600, and up)

    def test_an_unobserved_bucket_is_in_no_band(self):
        b = parse([BOOT, chan("2026-09-17T20:00:01Z", 0, 0, samples=0, skipped=100,
                              peak=NO_READING, floor=NO_READING, mean=NO_READING)])
        self.assertEqual(0, sum(r["buckets"] for r in peak_histogram(b[0])))


class Reporting(unittest.TestCase):
    def test_an_empty_file_says_so_rather_than_printing_zeroes(self):
        self.assertIn("NO DATA", report(parse([])))

    def test_a_quiet_channel_is_stated_plainly(self):
        b = parse([BOOT, chansum("2026-09-17T20:01:00Z", 0, 59)])
        t = report(b)
        self.assertIn("NO EXCURSION", t)

    def test_a_capture_without_rollups_says_it_has_no_denominator(self):
        b = parse([BOOT, chan("2026-09-17T20:00:05Z", 5, 5000, above=20)])
        t = report(b)
        self.assertIn("NO CHANSUM LINES", t)
        self.assertNotIn("occupancy above", t)

    def test_a_reboot_is_announced_before_the_numbers(self):
        b = parse([BOOT, chansum("2026-09-17T20:01:00Z", 0, 59),
                   BOOT.replace("20:00:00", "21:00:00"),
                   chansum("2026-09-17T21:01:00Z", 0, 59)])
        t = report(b)
        self.assertIn("2 SEGMENTS", t)
        self.assertLess(t.index("SEGMENTS"), t.index("segment 1/2"))

    def test_a_loud_excursion_is_listed_with_its_wall_clock_time(self):
        b = parse([BOOT, chan("2026-09-17T20:17:42Z", 0, 0, peak=-540, above=3),
                   chansum("2026-09-17T20:18:00Z", 0, 59, above=3, notable=1, peak=-540)])
        t = report(b)
        self.assertIn("2026-09-17T20:17:42Z", t)
        self.assertIn("-54.0", t)

    def test_blind_buckets_are_called_unobserved_not_quiet(self):
        b = parse([BOOT, chansum("2026-09-17T20:01:00Z", 0, 59, buckets=60, samples=0,
                                 skipped=6000, blind=60, peak=NO_READING,
                                 fmin=NO_READING, fmax=NO_READING, fmean=NO_READING)])
        t = report(b)
        self.assertIn("Not quiet: unobserved", t)


if __name__ == "__main__":
    unittest.main(verbosity=2)
