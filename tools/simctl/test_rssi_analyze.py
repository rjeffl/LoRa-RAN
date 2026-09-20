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

from rssi_analyze import (NO_READING, band_buckets, coincidence, compare_hourly, episodes,
                          events, frame_times, hourly, parse, peak_histogram, periodicity,
                          summarise)
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


def frame(host, n, direction, t_ms, peer=1):
    """A FRAME line as frame_log.cpp renders it, trimmed to the fields read here."""
    return "%s,FRAME #%d %s t=%d peer=0x%02x type=3 schema=0 frag=0x01 seq=%d" % (
        host, n, direction, t_ms, peer, n)


def periodic_bursts(period_ms, count, drop_every=0, bucket_ms=1000, peak=-750):
    """Single-sample bursts every period_ms, timed on millis(), with every drop_every-th
    occurrence missed the way a burst shorter than the sampling interval is."""
    return parse(periodic_lines(period_ms, count, drop_every, bucket_ms, peak))[0]


def periodic_lines(period_ms, count, drop_every=0, bucket_ms=1000, peak=-750):
    lines = [BOOT]
    for i in range(count):
        if drop_every and i % drop_every == drop_every - 1:
            continue
        t = 10000 + i * period_ms
        lines.append(chan("2026-09-18T04:00:00Z", t // bucket_ms, t, above=1, peak=peak))
    return lines


class FrameTimes(unittest.TestCase):
    def test_transmissions_and_receptions_are_told_apart(self):
        b = parse([BOOT, frame("x", 1, "tx", 5000), frame("x", 2, "rx", 6000),
                   frame("x", 3, "tx", 4000)])[0]
        self.assertEqual([4000, 5000], frame_times(b, "tx"))
        self.assertEqual([6000], frame_times(b, "rx"))
        self.assertEqual([4000, 5000, 6000], frame_times(b))

    def test_a_frame_line_without_a_time_is_left_out_not_guessed(self):
        b = parse([BOOT, "x,FRAME #12 rx peer=0xf3 seq=4"])[0]
        self.assertEqual([], frame_times(b))


class Events(unittest.TestCase):
    def test_a_burst_straddling_a_bucket_boundary_is_one_event(self):
        b = parse([BOOT, chan("x", 10, 10000, above=1), chan("x", 11, 11000, above=1),
                   chan("x", 20, 20000, above=1)])[0]
        ev = events(b.buckets)
        self.assertEqual(2, len(ev))
        self.assertEqual(10000, ev[0]["start_ms"])


class Periodicity(unittest.TestCase):
    def test_a_fixed_period_is_found_with_occurrences_missed(self):
        b = periodic_bursts(130690, 60, drop_every=4)
        p = periodicity(band_buckets(b, -800, -700))
        self.assertTrue(p["periodic"])
        self.assertAlmostEqual(130.69, p["period_ms"] / 1000.0, places=1)
        self.assertEqual(60 - 15, p["events"])
        # Occurrence 60 was one of the dropped ones, so the span ends at occurrence 59.
        self.assertEqual(59, p["spanned"])
        self.assertAlmostEqual(45 / 59.0, p["catch_rate"])

    def test_the_period_is_in_seconds_not_in_buckets(self):
        # The slip the first analysis made: buckets that run 1009 ms instead of 1000 make
        # a fit on the bucket sequence report a period in buckets that reads as seconds.
        lines = [BOOT]
        for i in range(20):
            t = i * 130000
            lines.append(chan("x", t // 1009, t, above=1, peak=-750, dur=1009))
        p = periodicity(band_buckets(parse(lines)[0], -800, -700))
        self.assertAlmostEqual(130000, p["period_ms"], delta=1)

    def test_scattered_excursions_are_not_called_periodic(self):
        starts = [3000, 71000, 90000, 250000, 262000, 400000, 470000, 480000, 700000,
                  705000, 912000, 1001000]
        b = parse([BOOT] + [chan("x", t // 1000, t, above=1) for t in starts])[0]
        p = periodicity(b.buckets)
        self.assertFalse(p["periodic"])

    def test_too_few_events_gives_no_verdict_rather_than_a_guess(self):
        b = periodic_bursts(130690, 4)
        self.assertIsNone(periodicity(b.buckets))

    def test_mostly_multi_period_gaps_are_not_a_clock(self):
        # Five events on a 100 s grid with most occurrences missing: each fits the line,
        # but only one gap in four is a single period.
        starts = [0, 300000, 600000, 700000, 1100000]
        b = parse([BOOT] + [chan("x", t // 1000, t, above=1) for t in starts])[0]
        p = periodicity(b.buckets)
        self.assertFalse(p["periodic"])


class Coincidence(unittest.TestCase):
    def test_buckets_beside_our_own_transmissions_score_high(self):
        tx = [10000 + 30000 * i for i in range(10)]
        b = parse([BOOT] + [chan("x", t // 1000, t - 200) for t in tx])[0]
        c = coincidence(b.buckets, tx)
        self.assertEqual(10, c["near"])
        self.assertEqual(1.0, c["rate"])
        self.assertLess(c["chance"], 0.2)

    def test_chance_is_the_share_of_the_timeline_the_windows_cover(self):
        # Two buckets bound a 100 s span; two transmissions, each window 2.5 s wide.
        b = parse([BOOT, chan("x", 0, 0), chan("x", 100, 100000)])[0]
        c = coincidence(b.buckets, [30000, 60000])
        self.assertAlmostEqual(0.05, c["chance"])

    def test_overlapping_windows_are_not_counted_twice(self):
        b = parse([BOOT, chan("x", 0, 0), chan("x", 100, 100000)])[0]
        c = coincidence(b.buckets, [30000, 30500])
        self.assertAlmostEqual(0.03, c["chance"])

    def test_no_transmissions_gives_no_figure(self):
        b = parse([BOOT, chan("x", 0, 0)])[0]
        self.assertIsNone(coincidence(b.buckets, []))


class Episodes(unittest.TestCase):
    def test_busy_buckets_close_together_are_one_episode(self):
        b = parse([BOOT, chan("x", 100, 100000, above=30, peak=-930),
                   chan("x", 105, 105000, above=20, peak=-920),
                   chan("x", 130, 130000, above=4, peak=-940)])[0]
        e = episodes(b)
        self.assertEqual(2, len(e))
        self.assertEqual(6, e[0]["span"])
        self.assertEqual(2, e[0]["busy"])
        self.assertEqual(50, e[0]["above"])
        self.assertEqual(-920, e[0]["peak"])

    def test_single_sample_hits_do_not_make_an_episode(self):
        b = parse([BOOT, chan("x", 100, 100000, above=1), chan("x", 101, 101000, above=2)])[0]
        self.assertEqual([], episodes(b))


class HistogramDetail(unittest.TestCase):
    def test_each_band_says_how_much_occupancy_it_carries(self):
        b = parse([BOOT, chan("x", 0, 0, peak=-750, above=1),
                   chan("x", 1, 1000, peak=-930, above=36),
                   chan("x", 2, 2000, peak=-935, above=4)])[0]
        h = {r["lo"]: r for r in peak_histogram(b)}
        self.assertEqual(1, h[-800]["single"])
        self.assertEqual(40, h[-1000]["above"])
        self.assertEqual(0, h[-1000]["single"])


class ReportingSources(unittest.TestCase):
    def test_a_periodic_source_is_named_with_its_period(self):
        # The report needs a rollup before it prints anything; see Reporting.
        lines = periodic_lines(130690, 40, drop_every=5) + [
            chansum("2026-09-18T05:30:00Z", 0, 5300, above=32, peak=-750)]
        t = report(parse(lines))
        self.assertIn("PERIODIC every 130.69 s", t)

    def test_a_band_near_our_transmissions_is_compared_with_chance(self):
        lines = [BOOT]
        for i in range(10):
            t = 10000 + 30000 * i
            lines.append(chan("x", t // 1000, t - 200, above=1, peak=-1090))
            lines.append(frame("x", i, "tx", t))
        lines.append(chansum("x", 0, 300, above=10, peak=-1090))
        t = report(parse(lines))
        self.assertIn("near our own tx 10 of 10", t)
        self.assertIn("chance", t)

    def test_the_largest_episode_is_listed(self):
        b = parse([BOOT, chan("2026-09-18T04:02:57Z", 100, 100000, above=36, peak=-930),
                   chansum("2026-09-18T04:03:00Z", 60, 119, above=36, peak=-930)])
        self.assertIn("2026-09-18T04:02:57Z", report(b))


class CompareHourly(unittest.TestCase):
    """D1 brief section 5 - two captures over the same hours, side by side."""

    A = [BOOT,
         chansum("2026-09-17T20:01:00Z", 0, 59, samples=6000, above=60, fmean=-1160),
         chansum("2026-09-17T20:02:00Z", 60, 119, samples=6000, above=0, fmean=-1150),
         chan("2026-09-17T20:01:30Z", 30, 30000, above=40, peak=-950),
         chan("2026-09-17T20:01:31Z", 31, 31000, above=20, peak=-1050),
         chansum("2026-09-17T21:00:00Z", 120, 179, samples=6000, above=6)]
    B = [BOOT.replace("917400000", "917200000"),
         chansum("2026-09-17T20:01:00Z", 0, 59, samples=6000, above=600, fmean=-1120),
         chan("2026-09-17T20:01:30Z", 30, 30000, above=600, peak=-1080)]

    def test_occupancy_is_per_hour_against_samples_taken(self):
        c = compare_hourly(parse(self.A), parse(self.B))
        h20 = c["rows"][0]
        self.assertEqual("2026-09-17T20", h20["hour"])
        self.assertAlmostEqual(60 / 12000, h20["a"]["occupancy"])
        self.assertAlmostEqual(600 / 6000, h20["b"]["occupancy"])

    def test_an_hour_one_file_does_not_cover_is_none_not_zero(self):
        c = compare_hourly(parse(self.A), parse(self.B))
        h21 = c["rows"][1]
        self.assertEqual("2026-09-17T21", h21["hour"])
        self.assertIsNotNone(h21["a"])
        self.assertIsNone(h21["b"])

    def test_bands_count_logged_buckets_by_peak(self):
        c = compare_hourly(parse(self.A), parse(self.B))
        a = c["rows"][0]["a"]["bands"]
        b = c["rows"][0]["b"]["bands"]
        self.assertEqual(1, a[-1000])   # -95 dBm
        self.assertEqual(1, a[-1100])   # -105 dBm
        self.assertEqual(0, a[-900])
        self.assertEqual(1, b[-1100])   # -108 dBm

    def test_the_floor_is_a_median_and_skips_no_reading(self):
        lines = self.A + [chansum("2026-09-17T20:03:00Z", 180, 239, fmean=NO_READING)]
        c = compare_hourly(parse(lines), parse(self.B))
        self.assertEqual(-1150, c["rows"][0]["a"]["floor_median"])

    def test_segments_are_counted_so_pooling_is_visible(self):
        rebooted = self.A + [BOOT.replace("20:00:00", "21:30:00"),
                             chansum("2026-09-17T21:31:00Z", 0, 59, samples=6000, above=0)]
        c = compare_hourly(parse(rebooted), parse(self.B))
        self.assertEqual((2, 1), c["segments"])
        # Both segments' 21:xx rollups land in one row, on the host clock.
        self.assertEqual(12000, c["rows"][1]["a"]["samples"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
