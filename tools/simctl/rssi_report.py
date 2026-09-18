#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# M25 - reads a capture file written by rssi_capture.py and prints what it says. The
# arithmetic is rssi_analyze.py; this is the I/O half.
#
#   python3 tools/simctl/rssi_report.py docs/bridge/data/m25-chan-2026-09-17.log

import argparse
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rssi_analyze import hourly, parse, peak_histogram, summarise


def dbm(v):
    return "--" if v is None else "%.1f" % (v / 10.0)


def report(boots, top=12, show_hourly=True):
    out = []
    if not boots:
        out.append("NO DATA. The file holds no CHAN lines.")
        out.append("  A bridge built before M25 does not emit them; check the boot header.")
        return "\n".join(out)

    if len(boots) > 1:
        out.append("%d SEGMENTS - the bridge restarted during this capture." % len(boots))
        out.append("  Reported separately: millis() and the bucket sequence both restart,")
        out.append("  so pooling them would be two clocks added together.")
        out.append("")

    for i, b in enumerate(boots):
        s = summarise(b)
        head = "segment %d/%d" % (i + 1, len(boots))
        if b.git:
            head += "  image %s" % b.git
        if b.freq_hz:
            head += "  %.4f MHz" % (b.freq_hz / 1e6)
        head += "  from %s" % (b.buckets[0]["host"] if b.buckets else b.host_time)
        out.append(head)

        if not s["has_denominator"]:
            out.append("  NO CHANSUM LINES - this segment has no denominator.")
            out.append("    The firmware writes one per minute carrying every bucket,")
            out.append("    loud and quiet. Without them the CHAN lines are excursions")
            out.append("    divided by themselves, so no occupancy is reported.")
            out.append("")
            continue

        if s["buckets"] == 0:
            out.append("  no buckets")
            out.append("")
            continue

        out.append("  span %.2f h over %d buckets   samples %d   skipped %d (%.1f %% of"
                   " opportunities)"
                   % (s["span_hours"], s["buckets"], s["samples"], s["skipped"],
                      100.0 * s["skipped"] / max(1, s["samples"] + s["skipped"])))
        out.append("  %d bucket(s) written out individually - the rest were quiet and are"
                   " carried by the rollups" % s["buckets_logged"])
        if s["buckets_blind"]:
            out.append("  %d bucket(s) sampled NOTHING - the radio was busy for all of"
                       " them. Not quiet: unobserved." % s["buckets_blind"])
        out.append("  floor  median %s dBm   min %s   max %s"
                   % (dbm(s["floor_median"]), dbm(s["floor_min"]), dbm(s["floor_max"])))
        out.append("  peak   strongest sample %s dBm" % dbm(s["peak_max"]))
        out.append("  occupancy above %s dBm: %d of %d samples = %s"
                   % (dbm(s["threshold_dbm10"]), s["above"], s["samples"],
                      "--" if s["occupancy"] is None else "%.4f %%" % (100 * s["occupancy"])))
        if s["buckets_with_own_rx"]:
            out.append("  %d bucket(s) carried our own receptions (%d frames) - excluded"
                       " from samples by the sampler, flagged here."
                       % (s["buckets_with_own_rx"], s["own_rx"]))

        out.append("")
        out.append("  peak distribution, observed buckets:")
        for row in peak_histogram(b):
            if row["buckets"] == 0:
                continue
            hi = "and up" if row["hi"] is None else "to %s" % dbm(row["hi"])
            out.append("    %8s dBm %-12s %6d bucket(s)" % (dbm(row["lo"]), hi, row["buckets"]))

        exc = sorted(s["excursions"], key=lambda x: -x["peak"])[:top]
        if exc:
            out.append("")
            out.append("  strongest excursions:")
            for e in exc:
                out.append("    %s  peak %7s dBm  above %4d of %4d samples  seq %d"
                           % (e["host"], dbm(e["peak"]), e["above"], e["samples"], e["seq"]))
        else:
            out.append("")
            out.append("  NO EXCURSION reached the threshold in this segment.")

        if show_hourly:
            rows = hourly(b)
            if len(rows) > 1:
                out.append("")
                out.append("  by hour (UTC):")
                out.append("    %-14s %9s %9s %9s %s"
                           % ("hour", "samples", "above", "occupancy", "peak"))
                for k, r in rows.items():
                    out.append("    %-14s %9d %9d %9s %7s dBm"
                               % (k, r["samples"], r["above"],
                                  "--" if r["occupancy"] is None
                                  else "%.4f %%" % (100 * r["occupancy"]),
                                  dbm(r["peak"])))
        if b.frames:
            out.append("")
            out.append("  %d FRAME line(s) in this segment - correlate with"
                       " rxlog_analyze.py for the per-frame view." % len(b.frames))
        out.append("")

    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description="M25 - read a channel capture")
    ap.add_argument("capture", help="file written by rssi_capture.py")
    ap.add_argument("--top", type=int, default=12, help="strongest excursions to list")
    ap.add_argument("--no-hourly", action="store_true")
    args = ap.parse_args()

    with open(args.capture, "r", encoding="utf-8", errors="replace") as fh:
        boots = parse(fh)
    print(report(boots, top=args.top, show_hourly=not args.no_hourly))
    return 0


if __name__ == "__main__":
    sys.exit(main())
