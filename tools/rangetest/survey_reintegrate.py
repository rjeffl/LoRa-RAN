#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 <holder>          # D31 open - see LRAN-Decision-Register
"""Re-integrate an R8 survey trace over 500 kHz and split it by Part 15 envelope.

This is M20's residual, added by M21 (Decision Register 5.1). The field work is
DONE - the R11 re-walk of 2026-09-05 - and this tool exists so that the residual
is answered from the committed trace instead of by walking seven sites again.

    python3 tools/rangetest/survey_reintegrate.py \\
        docs/rangetest/data/2026-09-05-survey-campaign-r11.csv \\
        --out docs/rangetest/data/2026-09-06-m20-reintegration.csv

What it answers, and why each half is needed:

  - Envelope A (spec 18.2, plan of record) is BW125 and may sit anywhere in
    902-928. Its uncommitted region is ~915.2-923.0 MHz: below that is the
    modules' own grant range, and 923.3-927.5 is LoRaWAN US915 downlink.
  - Envelope B is BW500 and lives only in 903.0-914.2 MHz. Those eight centres
    are the US915 500 kHz channels, on a 1.6 MHz grid.

A BW500 receiver hears everything a BW125 one does across four times the
spectrum, so an Envelope B channel cannot be picked from 125 kHz bins by eye.

READ THE COVERAGE CAVEAT BELOW BEFORE QUOTING A NUMBER FROM THIS TOOL.
"""

import argparse
import csv
import math
import sys
from collections import OrderedDict

# The survey samples 125 kHz of receiver bandwidth every 200 kHz (data/README.md
# survey schema, and the trace's own `bw_khz=125.0`). The bins therefore do NOT
# tile the band: 62.5% of the spectrum is measured and 37.5% falls in the gaps.
# Every aggregate here is an estimate over the measured fraction, scaled up.
BIN_SPACING_HZ = 200_000
RBW_HZ = 125_000
COVERAGE = RBW_HZ / BIN_SPACING_HZ  # 0.625

CH500_HZ = 500_000

# Spec 18.2. Envelope B is the DTS path and its range IS the eight US915 500 kHz
# uplink channels: 903.0 + 1.6 MHz x k, k = 0..7, ending at 914.2.
ENVELOPE_B_LO_HZ = 903_000_000
ENVELOPE_B_HI_HZ = 914_200_000
US915_500K_STEP_HZ = 1_600_000

# Envelope A's uncommitted region. The lower bound clears the modules' grant
# ranges (both stop at 914.9); the upper bound clears US915 downlink, which
# starts at 923.3 - so 923.0 is the last 500 kHz-clean centre below it.
ENVELOPE_A_LO_HZ = 915_200_000
ENVELOPE_A_HI_HZ = 923_000_000

SENTINEL_DBM10 = -32768

# The sites that are actually in the network, present or planned (System PRD).
# The other four are candidate/monitoring locations: worth avoiding interference
# with, but they do not veto a channel the deployed link needs.
DEPLOYED_SITES = ("bridge-house", "gatelink-gate", "welllink-well")

# How far either side of a candidate to look for a strong neighbour. An SX1262's
# adjacent-channel rejection is finite, so a -54 dBm burst 600 kHz away is not
# the same as an empty band even though the candidate bin itself reads floor.
GUARD_HZ = 600_000


def dbm10_to_mw(dbm10):
    return 10.0 ** (dbm10 / 100.0)


def mw_to_dbm(mw):
    return 10.0 * math.log10(mw) if mw > 0 else float("-inf")


def read_survey(path):
    """Return (rows, meta). Rows are dicts with ints; meta holds the `#` lines.

    Per-site `# passes=` and `# hold_discipline=` lines appear BETWEEN data
    rows, so they are tracked as the file is walked rather than parsed from a
    header block.
    """
    rows = []
    meta = {"header_comments": [], "sites": OrderedDict()}
    site_ctx = {}
    reader = None
    fields = None

    with open(path, newline="") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("#"):
                body = line.lstrip("# ").strip()
                if body.startswith("site="):
                    idx = body.split("=", 1)[1].split()[0]
                    site_ctx = {"site_index": int(idx)}
                    meta["sites"].setdefault(int(idx), site_ctx)
                elif "=" in body and site_ctx:
                    k, v = body.split("=", 1)
                    site_ctx[k.strip()] = v.strip()
                elif fields is None:
                    meta["header_comments"].append(body)
                continue
            if not line.strip():
                continue
            if fields is None:
                fields = line.split(",")
                reader = csv.DictReader([], fieldnames=fields)
                continue
            values = line.split(",")
            if len(values) != len(fields):
                raise SystemExit("row has %d fields, header has %d: %r"
                                 % (len(values), len(fields), line))
            row = dict(zip(fields, values))
            for k in ("site_index", "bin_index", "freq_hz", "passes", "samples",
                      "peak_dbm10", "mean_dbm10", "floor_dbm10", "dropped"):
                row[k] = int(row[k])
            rows.append(row)

    if not rows:
        raise SystemExit("no data rows in %s - is this a survey trace?" % path)
    return rows, meta


def check_hold_discipline(meta):
    """A peak from a trace captured without hold discipline is not attributable.

    R11, data/README.md. Refusing here rather than warning: this tool's entire
    output is a channel recommendation built on peaks, and the pre-R11 campaign
    would produce a confident-looking one from peaks credited to the wrong site.
    """
    bad = [i for i, s in meta["sites"].items()
           if s.get("hold_discipline") != "1"]
    return bad


def channels_500(lo_hz, hi_hz, step_hz):
    f = lo_hz
    while f <= hi_hz:
        yield f
        f += step_hz


def aggregate_channel(site_rows, centre_hz, width_hz):
    """Aggregate the bins a receiver of `width_hz` centred at `centre_hz` hears.

    A bin is a member if its 125 kHz measurement window overlaps the channel at
    all - a receiver does not ignore an occupant sitting half in its passband.
    """
    lo = centre_hz - width_hz / 2.0
    hi = centre_hz + width_hz / 2.0
    members = [r for r in site_rows
               if (r["freq_hz"] + RBW_HZ / 2.0) > lo
               and (r["freq_hz"] - RBW_HZ / 2.0) < hi]
    if not members:
        return None

    # Floor: thermal noise adds across bandwidth. Sum the measured slices, then
    # scale for the gaps the survey never looked at. Sound for a stationary
    # floor; explicitly NOT sound for a narrowband occupant hiding in a gap.
    floor_mw = sum(dbm10_to_mw(r["floor_dbm10"]) for r in members)
    measured_hz = len(members) * RBW_HZ
    floor_mw *= (width_hz / measured_hz)

    mean_mw = sum(dbm10_to_mw(r["mean_dbm10"]) for r in members)
    mean_mw *= (width_hz / measured_hz)

    # Peak: the MAX member peak, not the sum. Peaks are held maxima of bursty
    # transmitters that are rarely on together, so summing them would invent a
    # concurrency the trace has no evidence for.
    peak_dbm = max(r["peak_dbm10"] for r in members) / 10.0

    return {
        "centre_hz": centre_hz,
        "width_hz": width_hz,
        "n_bins": len(members),
        "measured_hz": measured_hz,
        "coverage": measured_hz / width_hz,
        "floor_dbm": mw_to_dbm(floor_mw),
        "mean_dbm": mw_to_dbm(mean_mw),
        "peak_dbm": peak_dbm,
        "peak_bin_hz": max(members, key=lambda r: r["peak_dbm10"])["freq_hz"],
        "min_passes": min(r["passes"] for r in members),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", help="R8 survey CSV (must have hold_discipline=1)")
    ap.add_argument("--out", help="write the derived CSV here")
    ap.add_argument("--allow-no-hold", action="store_true",
                    help="analyse a trace without hold discipline anyway. The "
                         "peaks are NOT site-attributable; floor and mean are")
    args = ap.parse_args(argv)

    rows, meta = read_survey(args.trace)
    bad = check_hold_discipline(meta)
    if bad and not args.allow_no_hold:
        raise SystemExit(
            "sites %s report hold_discipline != 1.\n"
            "Peaks in such a trace are credited to the site the operator was "
            "walking TOWARD, not the site they were heard at (data/README.md, "
            "R11). This tool recommends a channel from peaks, so it refuses.\n"
            "Pass --allow-no-hold to read floor and mean only."
            % sorted(bad))

    by_site = OrderedDict()
    for r in rows:
        by_site.setdefault(r["site_name"], []).append(r)

    out_rows = []
    for site, site_rows in by_site.items():
        # Envelope B: the eight US915 500 kHz channels, which are exactly the
        # DTS grant range's endpoints.
        for c in channels_500(ENVELOPE_B_LO_HZ, ENVELOPE_B_HI_HZ,
                              US915_500K_STEP_HZ):
            a = aggregate_channel(site_rows, c, CH500_HZ)
            if a:
                a.update(site_name=site, envelope="B", grid="us915-500k")
                out_rows.append(a)

        # Envelope A: BW125, so the candidates are the bins themselves.
        for r in site_rows:
            if ENVELOPE_A_LO_HZ <= r["freq_hz"] <= ENVELOPE_A_HI_HZ:
                out_rows.append({
                    "site_name": site, "envelope": "A", "grid": "bin-125k",
                    "centre_hz": r["freq_hz"], "width_hz": RBW_HZ, "n_bins": 1,
                    "measured_hz": RBW_HZ, "coverage": 1.0,
                    "floor_dbm": r["floor_dbm10"] / 10.0,
                    "mean_dbm": r["mean_dbm10"] / 10.0,
                    "peak_dbm": r["peak_dbm10"] / 10.0,
                    "peak_bin_hz": r["freq_hz"], "min_passes": r["passes"],
                })

    if args.out:
        write_csv(args.out, args.trace, out_rows)
    report(by_site, out_rows, rows)
    return 0


def write_csv(path, source, out_rows):
    cols = ["site_name", "envelope", "grid", "centre_hz", "width_hz", "n_bins",
            "measured_hz", "coverage", "floor_dbm", "mean_dbm", "peak_dbm",
            "peak_bin_hz", "min_passes"]
    with open(path, "w", newline="") as fh:
        fh.write("# DERIVED, not captured. M20's residual re-integration.\n")
        fh.write("# source: %s\n" % source)
        fh.write("# regenerate: python3 tools/rangetest/survey_reintegrate.py "
                 "<source> --out <this file>\n")
        fh.write("# envelope A rows are single 125 kHz bins; envelope B rows are "
                 "500 kHz channels on the US915 grid.\n")
        fh.write("# floor/mean are power sums over the measured bins scaled to "
                 "the full channel width; peak is the max member bin.\n")
        fh.write("# COVERAGE: the survey measured 125 kHz every 200 kHz, so 37.5%"
                 " of the band was never looked at.\n")
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in sorted(out_rows, key=lambda r: (r["site_name"], r["envelope"],
                                                 r["centre_hz"])):
            r = dict(r)
            r["coverage"] = "%.3f" % r["coverage"]
            for k in ("floor_dbm", "mean_dbm", "peak_dbm"):
                r[k] = "%.1f" % r[k]
            w.writerow(r)


def guard_level(rows, centre_hz, sites=None):
    """Strongest peak within GUARD_HZ of `centre_hz`, EXCLUDING the bin itself.

    A candidate that reads floor while sitting next to a 60 dB-over-floor burst
    is not a quiet channel; it is an untested one.
    """
    near = [r for r in rows
            if abs(r["freq_hz"] - centre_hz) <= GUARD_HZ
            and r["freq_hz"] != centre_hz
            and (sites is None or r["site_name"] in sites)]
    if not near:
        return float("-inf"), None
    hit = max(near, key=lambda r: r["peak_dbm10"])
    return hit["peak_dbm10"] / 10.0, hit


def rank_envelope_a(rows, sites=None):
    """Rank 125 kHz candidates in Envelope A's uncommitted region.

    Scored on the WORST peak across the sites considered - spec 12.1 says it is
    the node's noise floor that sets its margin, and one channel serves all of
    them - then on the strongest neighbour inside the guard band.
    """
    pool = [r for r in rows if sites is None or r["site_name"] in sites]
    cands = {}
    for r in pool:
        if not (ENVELOPE_A_LO_HZ <= r["freq_hz"] <= ENVELOPE_A_HI_HZ):
            continue
        c = r["freq_hz"]
        if c not in cands or r["peak_dbm10"] > cands[c]["peak_dbm10"]:
            cands[c] = r
    out = []
    for c, r in cands.items():
        g, hit = guard_level(pool, c, sites)
        out.append({
            "centre_hz": c,
            "worst_peak_dbm": r["peak_dbm10"] / 10.0,
            "worst_site": r["site_name"],
            "guard_dbm": g,
            "guard_site": hit["site_name"] if hit else "",
            "guard_hz": hit["freq_hz"] if hit else 0,
        })
    # Sort on the worse of the two: a channel is only as good as what it has to
    # live next to.
    out.sort(key=lambda r: (max(r["worst_peak_dbm"], r["guard_dbm"] - 20.0),
                            r["worst_peak_dbm"]))
    return out


def report(by_site, out_rows, rows):
    print("=" * 78)
    print("M20 re-integration - %d sites, %d derived channels"
          % (len(by_site), len(out_rows)))
    print("=" * 78)

    print("\nENVELOPE B - the eight US915 500 kHz channels, 903.0-914.2 MHz")
    print("  Ranked on the WORST peak across the sites, same rule as Envelope A.")
    print("  A BW500 receiver's noise floor sits ~6 dB above a BW125 one's, which")
    print("  is visible in the floor column: ~-110 here against ~-116 per bin.\n")
    b_rows = [r for r in out_rows if r["envelope"] == "B"]
    for label, sites in (("all seven", None),
                         ("deployed only", DEPLOYED_SITES)):
        worst = {}
        for r in b_rows:
            if sites and r["site_name"] not in sites:
                continue
            c = r["centre_hz"]
            if c not in worst or r["peak_dbm"] > worst[c]["peak_dbm"]:
                worst[c] = r
        if not worst:
            print("  %s: no rows - none of %s appear in this trace\n"
                  % (label, ", ".join(DEPLOYED_SITES)))
            continue
        print("  %s:" % label)
        print("  %-9s %9s %9s %-16s %s"
              % ("centre", "floor", "worstpeak", "at", "verdict"))
        print("  " + "-" * 62)
        for r in sorted(worst.values(), key=lambda r: r["peak_dbm"]):
            v = ("clear" if r["peak_dbm"] < -100
                 else "occupied" if r["peak_dbm"] < -85 else "STRONG occupant")
            print("  %9.1f %9.1f %9.1f %-16s %s"
                  % (r["centre_hz"] / 1e6, r["floor_dbm"], r["peak_dbm"],
                     r["site_name"][:15], v))
        print()

    for label, sites in (("ALL SEVEN SITES", None),
                         ("DEPLOYED SITES ONLY (%s)" % ", ".join(DEPLOYED_SITES),
                          DEPLOYED_SITES)):
        print("\nENVELOPE A - 125 kHz candidates in 915.2-923.0 MHz, %s" % label)
        print("  `guard` is the strongest peak within +/-600 kHz, which a")
        print("  candidate reading floor can still be sitting next to.\n")
        ranked = rank_envelope_a(rows, sites)
        if not ranked:
            print("  no rows - none of %s appear in this trace\n"
                  % ", ".join(DEPLOYED_SITES))
            continue
        print("  %-9s %9s %-16s %9s %9s %s"
              % ("centre", "worstpeak", "at", "guard", "guard@", "guard site"))
        print("  " + "-" * 74)
        for r in ranked[:10]:
            print("  %9.1f %9.1f %-16s %9.1f %9.1f %s"
                  % (r["centre_hz"] / 1e6, r["worst_peak_dbm"],
                     r["worst_site"][:15], r["guard_dbm"],
                     r["guard_hz"] / 1e6, r["guard_site"]))
        print("\n  worst candidates, for contrast:")
        for r in ranked[-3:]:
            print("  %9.1f %9.1f %-16s %9.1f %9.1f %s"
                  % (r["centre_hz"] / 1e6, r["worst_peak_dbm"],
                     r["worst_site"][:15], r["guard_dbm"],
                     r["guard_hz"] / 1e6, r["guard_site"]))


if __name__ == "__main__":
    sys.exit(main())
