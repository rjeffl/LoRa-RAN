#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# D1 brief section 5 - two channel captures taken over the same hours, read side by side,
# hour by hour. The arithmetic is rssi_analyze.compare_hourly(); this is the I/O half.
#
#   python3 tools/simctl/rssi_compare.py \
#       docs/bridge/data/d1-par-917400-flat-office-2026-09-19.log \
#       docs/bridge/data/d1-par-917200-handheld-office-2026-09-19.log --labels 917.4 917.2
#
# Occupancy and band counts only. A peak level from one board compared with a peak level
# from another measures direction as much as the channel (engineering log, 2026-09-19).

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rssi_analyze import compare_hourly, parse


def render(cmp, la, lb):
    edges = cmp["edges_dbm10"]
    out = []
    sa, sb = cmp["segments"]
    if sa != 1 or sb != 1:
        out.append("segments: %s %d, %s %d - pooled per UTC hour on the host clock" %
                   (la, sa, lb, sb))
    # Band cells read "a/b"; wide enough for both labels in full, so two labels that share
    # a prefix (917.4, 917.2) stay distinguishable.
    w = max(9, len(la) + len(lb) + 1)
    head = "%-13s  %11s %11s  %9s %9s" % ("hour UTC", la + " occ%", lb + " occ%",
                                           la + " fl", lb + " fl")
    for lo in edges:
        head += "  %*s" % (w, "%d dBm" % (lo // 10))
    out.append(head)
    out.append("%-13s  %11s %11s  %9s %9s" % ("", "", "", "", "") +
               "".join("  %*s" % (w, "%s/%s" % (la, lb)) for _ in edges))

    def occ(side):
        return "--" if side is None or side["occupancy"] is None else \
            "%.4f" % (100.0 * side["occupancy"])

    def fl(side):
        return "--" if side is None or side["floor_median"] is None else \
            "%.1f" % (side["floor_median"] / 10.0)

    for r in cmp["rows"]:
        a, b = r["a"], r["b"]
        line = "%-13s  %11s %11s  %9s %9s" % (r["hour"], occ(a), occ(b), fl(a), fl(b))
        for lo in edges:
            na = "--" if a is None else str(a["bands"][lo])
            nb = "--" if b is None else str(b["bands"][lo])
            line += "  %*s" % (w, "%s/%s" % (na, nb))
        out.append(line)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description="D1 brief section 5 - two captures, hour by hour")
    ap.add_argument("a", help="first capture, usually 917.4 MHz")
    ap.add_argument("b", help="second capture, the candidate")
    ap.add_argument("--labels", nargs=2, default=("A", "B"), metavar=("LABEL_A", "LABEL_B"))
    args = ap.parse_args()

    boots = []
    for path in (args.a, args.b):
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            boots.append(parse(fh))
    print(render(compare_hourly(boots[0], boots[1]), args.labels[0], args.labels[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
