#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 <holder>          # D31 open - see LRAN-Decision-Register
"""Host tests for survey_reintegrate.py.

    python3 tools/rangetest/test_survey_reintegrate.py

WHY THIS FILE EXISTS. This tool turns a committed trace into a channel
recommendation that D1 is decided from, and it is the only step between the
measurement and the decision. Its arithmetic is dB-to-linear power summing, which
is exactly the kind of thing that is wrong by 6 dB or by a factor of the bin count
and still produces a plausible-looking table. Three of the invariants below are
sign or scale errors that would not be visible in the output.

The fourth is the refusal path: a survey captured without hold discipline yields
peaks credited to the wrong site (data/README.md, R11), and this tool ranks
channels BY peak. Analysing such a trace silently is the failure mode that would
put a confident wrong number in front of D1.
"""

import io
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import survey_reintegrate as sr  # noqa: E402

FAILED = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok   {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name}  {detail}")


def make_trace(path, rows, hold="1", sites=("test-site",)):
    """Write a minimal but structurally real survey trace."""
    with io.open(path, "w") as fh:
        fh.write("# LRAN range test trace, captured 2026-01-01 00:00:00\n")
        fh.write("# note: synthetic\n")
        fh.write("# bw_khz=125.0\n")
        fh.write("site_index,site_name,bin_index,freq_hz,passes,samples,"
                 "peak_dbm10,mean_dbm10,floor_dbm10,dropped\n")
        for i, site in enumerate(sites):
            fh.write(f"# site={i} {site}\n")
            fh.write("# passes=10\n")
            fh.write(f"# hold_discipline={hold}\n")
            for b, (freq, peak, mean, floor) in enumerate(rows):
                fh.write(f"{i},{site},{b},{freq},10,100,"
                         f"{peak},{mean},{floor},0\n")


def flat_band(peak=-1130, mean=-1140, floor=-1160):
    """130 bins at 200 kHz from 902.0, all reading the same level."""
    return [(902_000_000 + 200_000 * b, peak, mean, floor) for b in range(130)]


def test_500k_floor_is_6db_above_the_125k_floor():
    """Thermal noise integrates with bandwidth: 10*log10(500/125) = 6.02 dB.

    The bins do not tile the band - 125 kHz measured every 200 kHz - so a naive
    sum of members lands ~2 dB low. The coverage scaling is what makes this 6.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, flat_band(floor=-1160))
        rows, _ = sr.read_survey(str(p))
        a = sr.aggregate_channel(rows, 909_400_000, sr.CH500_HZ)
        delta = a["floor_dbm"] - (-116.0)
        check("500 kHz floor is 6.0 dB above the 125 kHz floor",
              abs(delta - 6.02) < 0.15, f"got {delta:+.2f} dB")


def test_peak_is_the_max_member_not_the_sum():
    """Peaks are held maxima of bursty transmitters, rarely concurrent.

    Summing them would invent a concurrency the trace cannot evidence, and would
    inflate a channel containing three quiet neighbours into an occupied one.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        band = flat_band()
        for b, (f, _, m, fl) in enumerate(band):
            if f == 909_400_000:
                band[b] = (f, -600, m, fl)   # -60.0 dBm in one bin
        make_trace(p, band)
        rows, _ = sr.read_survey(str(p))
        a = sr.aggregate_channel(rows, 909_400_000, sr.CH500_HZ)
        check("channel peak is the loudest member bin",
              abs(a["peak_dbm"] - (-60.0)) < 0.01, f"got {a['peak_dbm']}")
        check("channel peak reports which bin it came from",
              a["peak_bin_hz"] == 909_400_000, str(a["peak_bin_hz"]))


def test_membership_includes_a_bin_straddling_the_channel_edge():
    """A receiver does not ignore an occupant sitting half in its passband.

    A 125 kHz bin centred 280 kHz off a 500 kHz channel's centre still overlaps
    it (280 - 62.5 = 217.5 < 250), so it must count. Testing exclusion by centre
    frequency alone would drop exactly the adjacent occupant that matters.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, flat_band())
        rows, _ = sr.read_survey(str(p))
        a = sr.aggregate_channel(rows, 909_400_000, sr.CH500_HZ)
        lo, hi = 909_400_000 - 300_000, 909_400_000 + 300_000
        check("500 kHz channel spans the straddling bins, not just the inner ones",
              a["n_bins"] == 3, f"n_bins={a['n_bins']}")
        check("channel coverage is under 1.0 - the survey left gaps",
              0.7 < a["coverage"] < 0.8, f"coverage={a['coverage']:.3f}")
        del lo, hi


def test_guard_band_sees_a_neighbour_the_candidate_bin_cannot():
    """The trap this tool exists to catch.

    A candidate reading floor 400 kHz from a -54 dBm burst is not a quiet
    channel. Scoring on the candidate bin alone would rank it first.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        band = flat_band()
        for b, (f, _, m, fl) in enumerate(band):
            if f == 916_000_000:
                band[b] = (f, -540, m, fl)
        make_trace(p, band)
        rows, _ = sr.read_survey(str(p))
        g, hit = sr.guard_level(rows, 916_400_000)
        check("guard band finds the loud neighbour",
              abs(g - (-54.0)) < 0.01 and hit["freq_hz"] == 916_000_000,
              f"guard={g}")
        ranked = sr.rank_envelope_a(rows)
        top = ranked[0]["centre_hz"]
        check("a bin adjacent to the burst is not ranked first",
              abs(top - 916_000_000) > sr.GUARD_HZ,
              f"top candidate {top/1e6:.1f} MHz")


def test_envelope_a_candidates_stay_inside_the_uncommitted_region():
    """915.2-923.0: above both modules' grant ranges, below US915 downlink."""
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, flat_band())
        rows, _ = sr.read_survey(str(p))
        cands = [r["centre_hz"] for r in sr.rank_envelope_a(rows)]
        check("no candidate below 915.2 MHz",
              min(cands) >= sr.ENVELOPE_A_LO_HZ, f"{min(cands)/1e6:.1f}")
        check("no candidate above 923.0 MHz - 923.3 is US915 downlink",
              max(cands) <= sr.ENVELOPE_A_HI_HZ, f"{max(cands)/1e6:.1f}")


def test_envelope_b_grid_is_the_eight_us915_channels():
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, flat_band())
        rows, _ = sr.read_survey(str(p))
        chans = list(sr.channels_500(sr.ENVELOPE_B_LO_HZ, sr.ENVELOPE_B_HI_HZ,
                                     sr.US915_500K_STEP_HZ))
        check("eight Envelope B channels", len(chans) == 8, str(len(chans)))
        check("the last one is 914.2 MHz, the grant range's top",
              chans[-1] == 914_200_000, str(chans[-1]))
        del rows


def test_a_trace_without_hold_discipline_is_refused():
    """The refusal that keeps a wrong number away from D1."""
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, flat_band(), hold="0")
        rows, meta = sr.read_survey(str(p))
        check("hold_discipline=0 is detected", sr.check_hold_discipline(meta))
        try:
            sr.main([str(p)])
            check("main() refuses a no-hold trace", False, "it did not")
        except SystemExit as e:
            check("main() refuses a no-hold trace", "hold_discipline" in str(e),
                  str(e)[:60])
        # main() prints a full report; capture it so the test output stays
        # readable, and so a crash inside report() is still caught.
        saved, sys.stdout = sys.stdout, io.StringIO()
        try:
            sr.main([str(p), "--allow-no-hold"])
            ok, detail = True, ""
        except SystemExit as e:
            ok, detail = False, str(e)[:60]
        finally:
            sys.stdout = saved
        check("--allow-no-hold overrides the refusal", ok, detail)
        del rows


def test_per_site_comment_lines_are_attributed_to_the_right_site():
    """`# hold_discipline=` sits BETWEEN data rows, not in a header block.

    Parsing it as a file-level header would let one disciplined site vouch for
    six that were not.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, flat_band()[:4], sites=("a", "b"))
        rows, meta = sr.read_survey(str(p))
        check("both sites parsed", len(meta["sites"]) == 2,
              str(list(meta["sites"])))
        check("each site carries its own hold flag",
              all(s.get("hold_discipline") == "1"
                  for s in meta["sites"].values()))
        check("rows keep their site name",
              {r["site_name"] for r in rows} == {"a", "b"})


def main():
    print("survey_reintegrate.py")
    test_500k_floor_is_6db_above_the_125k_floor()
    test_peak_is_the_max_member_not_the_sum()
    test_membership_includes_a_bin_straddling_the_channel_edge()
    test_guard_band_sees_a_neighbour_the_candidate_bin_cannot()
    test_envelope_a_candidates_stay_inside_the_uncommitted_region()
    test_envelope_b_grid_is_the_eight_us915_channels()
    test_a_trace_without_hold_discipline_is_refused()
    test_per_site_comment_lines_are_attributed_to_the_right_site()

    if FAILED:
        print(f"\n{len(FAILED)} FAILED: {', '.join(FAILED)}")
        return 1
    print("\nall survey_reintegrate.py tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
