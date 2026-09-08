#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""The M21 findings note 7.6 sanity check, read off a committed sweep trace.

No RF power meter exists for this project, so the D33 EIRP ceiling is met by
CALCULATION (findings 7.2) and the calculation is checked against the only
instrument on hand: the SX1262's own RSSI, at a known short distance.

    python3 tools/rangetest/eirp_check.py TRACE \\
        --distance 1=3.0 --distance 2=6.0 --distance 3=12.0

WHAT THIS CAN AND CANNOT SETTLE. Three checks, and they are NOT equally strong.
Read the hierarchy before believing any single number:

  1. THE POWER-STEP CHECK is the sharp one, and it is immune to geometry.
     Within one position the sweep runs the same link at two conducted powers,
     so the DIFFERENCE in RSSI is unaffected by distance, height, multipath or
     antenna gain - every one of those cancels. If the requested power did not
     reach the PA, the step reads 0 dB. This is the check that catches findings
     7.6's named failures.

  2. THE CLAMP CHECK falls out of the same two points for free, and nothing else
     in this repo has ever exercised it over the air. The default plan requests
     kSx1262MaxDbm (+22 dBm) for its high point and relies on clamp_conducted()
     to bring it to the D33 ceiling. A broken clamp is not a 5 dB step, it is a
     ~26 dB one, and it is the one failure that is both silent and a compliance
     breach rather than a measurement error.

  3. THE ABSOLUTE EIRP BACK-OUT is the coarse one. SX1262 RSSI is good to
     roughly +/-3-6 dB, the antenna's 3.0 dBi is an unverified vendor claim, and
     at these distances ground reflection can move a reading several dB on its
     own. It catches a gross error - a disconnected antenna, a configuration
     that silently did not apply - and it does not measure EIRP to better than
     about +/-6 dB. Do not quote it as a compliance measurement. It is not one,
     and findings 7.6 does not claim it is.

The slope fit across positions is what says whether even (3) is trustworthy:
free space loses 20 dB per decade of distance, so a fitted slope near -20 means
the geometry behaved and the intercept means something. A slope far from it means
the site was reflective and the absolute figure should be discarded - the two
relative checks above survive regardless.
"""

import argparse
import math
import sys

SENTINEL_DBM10 = -32768

# findings 7.6 / spec 18.2. The absolute check is bounded by RSSI accuracy and an
# unverified antenna claim; these are the bands a reading is judged against.
ABS_TOLERANCE_DB = 6.0      # within this: consistent with the calculation
ABS_GROSS_DB = 12.0         # beyond this: something is actually wrong

# The power step should reproduce the difference the trace itself logged.
STEP_TOLERANCE_DB = 2.5

# A fitted path-loss slope this far from free space means the geometry, not the
# radio, is what the absolute number is measuring.
SLOPE_FREE_SPACE = -20.0
SLOPE_TOLERANCE = 8.0


def fspl_db(distance_m, freq_hz):
    """Free-space path loss. 20log10(d_m) + 20log10(f_MHz) - 27.55."""
    if distance_m <= 0:
        raise ValueError("distance must be positive")
    return (20.0 * math.log10(distance_m)
            + 20.0 * math.log10(freq_hz / 1e6) - 27.55)


def read_sweep(path):
    """Rows from a sweep trace, ints parsed, sentinel rows kept but marked."""
    rows = []
    fields = None
    notes = []
    with open(path, newline="") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("#"):
                if fields is None:
                    notes.append(line.lstrip("# ").rstrip())
                continue
            if not line.strip():
                continue
            if fields is None:
                fields = line.split(",")
                if "position" not in fields or "tp_index" not in fields:
                    raise SystemExit(
                        "%s is not a sweep trace - no `position`/`tp_index` "
                        "columns. A survey trace is a different schema; see "
                        "docs/rangetest/data/README.md" % path)
                continue
            vals = line.split(",")
            if len(vals) != len(fields):
                raise SystemExit("row has %d fields, header has %d: %r"
                                 % (len(vals), len(fields), line))
            r = dict(zip(fields, vals))
            for k, v in list(r.items()):
                try:
                    r[k] = int(v)
                except ValueError:
                    pass
            rows.append(r)
    if not rows:
        raise SystemExit("no data rows in %s" % path)
    return rows, notes


def usable(row, key):
    v = row.get(key)
    return isinstance(v, int) and v != SENTINEL_DBM10


def mean(xs):
    return sum(xs) / len(xs) if xs else None


def eirp_from_rssi(rssi_dbm, distance_m, freq_hz, rx_gain_dbi):
    """findings 7.6: EIRP = RSSI + FSPL(d) - G_rx."""
    return rssi_dbm + fspl_db(distance_m, freq_hz) - rx_gain_dbi


def group_by_power(rows):
    """Split a position's rows by the conducted power actually logged.

    Read from the trace rather than assumed. The high point is REQUESTED at the
    SX1262 maximum and clamped by the firmware, so what it ended up at is a
    property of the run, not of this tool's expectations.
    """
    out = {}
    for r in rows:
        out.setdefault(r["conducted_dbm"], []).append(r)
    return out


def analyse_position(rows, distance_m, rx_gain_dbi):
    """Everything derivable from one position's rows."""
    freq_hz = rows[0]["freq_hz"]
    gain_dbi = rows[0]["antenna_gain_dbi10"] / 10.0
    by_power = group_by_power(rows)

    legs = {}
    for power, prows in sorted(by_power.items()):
        entry = {"power_dbm": power, "n_rows": len(prows),
                 "expected_eirp_dbm": power + gain_dbi}
        for leg, col in (("uplink", "init_rssi_mean10"),
                         ("downlink", "resp_rssi_mean10")):
            vals = [r[col] / 10.0 for r in prows if usable(r, col)]
            entry[leg + "_rssi"] = mean(vals)
            entry[leg + "_n"] = len(vals)
            if distance_m and vals:
                entry[leg + "_eirp"] = eirp_from_rssi(
                    mean(vals), distance_m, freq_hz, rx_gain_dbi)
            else:
                entry[leg + "_eirp"] = None
        legs[power] = entry

    return {"distance_m": distance_m, "freq_hz": freq_hz,
            "antenna_gain_dbi": gain_dbi, "legs": legs}


def power_step(pos):
    """The geometry-immune check. Measured RSSI step vs the logged power step."""
    powers = sorted(pos["legs"])
    if len(powers) < 2:
        return None
    lo, hi = powers[0], powers[-1]
    expected = float(hi - lo)
    out = {"lo_dbm": lo, "hi_dbm": hi, "expected_db": expected, "legs": {}}
    for leg in ("uplink", "downlink"):
        a = pos["legs"][lo][leg + "_rssi"]
        b = pos["legs"][hi][leg + "_rssi"]
        out["legs"][leg] = (b - a) if (a is not None and b is not None) else None
    return out


def fit_slope(points):
    """Least-squares fit of RSSI against log10(distance).

    Returns (slope_db_per_decade, intercept_at_1m, rms_residual_db) or None.
    Free space is -20 dB/decade; a fit far from it says the site, not the radio,
    dominated the reading.
    """
    pts = [(math.log10(d), r) for d, r in points if d and r is not None]
    if len(pts) < 3:
        return None
    n = len(pts)
    mx = sum(x for x, _ in pts) / n
    my = sum(y for _, y in pts) / n
    sxx = sum((x - mx) ** 2 for x, _ in pts)
    if sxx == 0:
        return None
    sxy = sum((x - mx) * (y - my) for x, y in pts)
    slope = sxy / sxx
    intercept = my - slope * mx
    resid = [y - (slope * x + intercept) for x, y in pts]
    rms = math.sqrt(sum(r * r for r in resid) / n)
    return slope, intercept, rms


def verdict(ok, warn=False):
    return "PASS" if ok else ("WARN" if warn else "FAIL")


def report(positions, rx_gain_dbi):
    failures = []
    warnings = []

    print("=" * 78)
    print("findings 7.6 EIRP sanity check")
    print("=" * 78)

    # ---- 1. the power step, per position -----------------------------------
    print("\n1. POWER STEP - geometry-immune, the sharp check")
    print("   Does the requested conducted power actually reach the PA?")
    print("   A step of 0 dB means it does not, whatever the absolute level.\n")
    print("   %-4s %-9s %10s %10s %10s  %s"
          % ("pos", "step", "expected", "uplink", "downlink", ""))
    print("   " + "-" * 62)
    for name, pos in positions.items():
        st = power_step(pos)
        if st is None:
            print("   %-4s only one power point - step not measurable" % name)
            warnings.append("position %s has one power point" % name)
            continue
        label = "%+d->%+d" % (st["lo_dbm"], st["hi_dbm"])
        row = "   %-4s %-9s %10.1f" % (name, label, st["expected_db"])
        bad = False
        for leg in ("uplink", "downlink"):
            d = st["legs"][leg]
            row += "%10s" % ("n/a" if d is None else "%+.1f" % d)
            if d is not None and abs(d - st["expected_db"]) > STEP_TOLERANCE_DB:
                bad = True
        print(row + "  " + verdict(not bad))
        if bad:
            failures.append("position %s: power step does not track the "
                            "logged conducted power" % name)

    # ---- 2. the clamp ------------------------------------------------------
    print("\n2. D33 CLAMP - over the air, which nothing else in this repo does")
    print("   The default plan REQUESTS kSx1262MaxDbm (+22) for its high point")
    print("   and relies on clamp_conducted() to bring it to the ceiling.\n")
    for name, pos in positions.items():
        powers = sorted(pos["legs"])
        hi = powers[-1]
        ceiling = -1.0 - pos["antenna_gain_dbi"]
        ok = hi <= math.floor(ceiling * 10) / 10.0 + 0.01
        print("   %-4s highest conducted power in trace %+d dBm, ceiling for "
              "%.1f dBi is %+.0f dBm  %s"
              % (name, hi, pos["antenna_gain_dbi"], math.floor(ceiling),
                 verdict(ok)))
        if not ok:
            failures.append("position %s: a logged conducted power exceeds the "
                            "D33 ceiling - THIS IS A COMPLIANCE FAULT, not a "
                            "measurement error" % name)
        if hi == 22:
            failures.append("position %s: the high point logged +22 dBm - the "
                            "clamp did not run" % name)

    # ---- 3. the absolute back-out ------------------------------------------
    print("\n3. ABSOLUTE EIRP - coarse, +/-%.0f dB at best. Not a compliance"
          % ABS_TOLERANCE_DB)
    print("   measurement, and findings 7.6 does not claim it is.\n")
    have_distance = any(p["distance_m"] for p in positions.values())
    if not have_distance:
        print("   no --distance given, so nothing to back out.")
        print("   Checks 1 and 2 above stand without it.\n")
        warnings.append("no distances given - absolute check skipped")
    else:
        print("   %-4s %6s %8s %10s %10s %10s  %s"
              % ("pos", "d(m)", "P_cond", "expected", "uplink", "downlink", ""))
        print("   " + "-" * 66)
        for name, pos in positions.items():
            if not pos["distance_m"]:
                continue
            for power in sorted(pos["legs"]):
                e = pos["legs"][power]
                row = ("   %-4s %6.1f %8s %10.1f"
                       % (name, pos["distance_m"], "%+d" % power,
                          e["expected_eirp_dbm"]))
                worst = 0.0
                for leg in ("uplink", "downlink"):
                    v = e[leg + "_eirp"]
                    row += "%10s" % ("n/a" if v is None else "%+.1f" % v)
                    if v is not None:
                        worst = max(worst, abs(v - e["expected_eirp_dbm"]))
                mark = ("PASS" if worst <= ABS_TOLERANCE_DB
                        else "WARN" if worst <= ABS_GROSS_DB else "FAIL")
                print(row + "  " + mark + (" (%+.1f dB)" % worst if worst else ""))
                if mark == "FAIL":
                    failures.append(
                        "position %s at %+d dBm: measured EIRP is %.1f dB from "
                        "the calculated figure - check the antenna is connected "
                        "and the configuration applied" % (name, power, worst))
                elif mark == "WARN":
                    warnings.append(
                        "position %s at %+d dBm: %.1f dB from calculated - "
                        "within gross-error bounds but outside RSSI accuracy"
                        % (name, power, worst))

    # ---- 4. does the geometry support check 3 at all? ----------------------
    print("\n4. PATH-LOSS SLOPE - whether check 3 is trustworthy")
    print("   Free space loses %.0f dB per decade of distance. A fit far from"
          % abs(SLOPE_FREE_SPACE))
    print("   that means the site was reflective and the absolute figure is")
    print("   measuring the ground, not the radio.\n")
    fitted_any = False
    for leg in ("uplink", "downlink"):
        for power in sorted({p for pos in positions.values() for p in pos["legs"]}):
            pts = []
            for pos in positions.values():
                if pos["distance_m"] and power in pos["legs"]:
                    pts.append((pos["distance_m"],
                                pos["legs"][power][leg + "_rssi"]))
            fit = fit_slope(pts)
            if not fit:
                continue
            fitted_any = True
            slope, intercept, rms = fit
            ok = abs(slope - SLOPE_FREE_SPACE) <= SLOPE_TOLERANCE
            print("   %-9s %+d dBm: slope %+.1f dB/decade, rms residual %.1f dB"
                  "  %s" % (leg, power, slope, rms, verdict(ok, warn=True)))
            if not ok:
                warnings.append(
                    "%s at %+d dBm: fitted slope %+.1f dB/decade is far from "
                    "free space - discard the absolute EIRP, keep checks 1 and 2"
                    % (leg, power, slope))
    if not fitted_any:
        print("   fewer than three distances given - no fit. Checks 1 and 2")
        print("   do not need it; check 3 is unvalidated without it.\n")
        warnings.append("fewer than three distances - slope not fitted")

    # ---- summary -----------------------------------------------------------
    print("\n" + "=" * 78)
    if failures:
        print("FAIL - %d finding(s):" % len(failures))
        for f in failures:
            print("  * " + f)
    else:
        print("No failures.")
    if warnings:
        print("\n%d warning(s):" % len(warnings))
        for w in warnings:
            print("  - " + w)
    print("=" * 78)
    return 1 if failures else 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", help="a sweep (R7) trace, not a survey trace")
    ap.add_argument("--distance", action="append", default=[],
                    metavar="POS=METRES",
                    help="measured distance for a position, e.g. --distance 1=3.0. "
                         "Repeat. Without it, checks 1 and 2 still run")
    ap.add_argument("--rx-gain", type=float, default=None, metavar="DBI",
                    help="receive antenna gain. Defaults to the gain the trace "
                         "records, which is the right answer for a matched pair")
    args = ap.parse_args(argv)

    distances = {}
    for spec in args.distance:
        if "=" not in spec:
            raise SystemExit("--distance wants POS=METRES, got %r" % spec)
        k, v = spec.split("=", 1)
        distances[k.strip()] = float(v)

    rows, notes = read_sweep(args.trace)
    for n in notes[:3]:
        print("# " + n)

    by_pos = {}
    for r in rows:
        by_pos.setdefault(str(r["position"]), []).append(r)

    rx_gain = args.rx_gain
    if rx_gain is None:
        rx_gain = rows[0]["antenna_gain_dbi10"] / 10.0

    positions = {}
    for name, prows in sorted(by_pos.items(), key=lambda kv: int(kv[0])):
        positions[name] = analyse_position(prows, distances.get(name), rx_gain)

    unknown = set(distances) - set(positions)
    if unknown:
        raise SystemExit("--distance names position(s) %s, which the trace does "
                         "not contain (it has %s)"
                         % (sorted(unknown), sorted(positions)))

    return report(positions, rx_gain)


if __name__ == "__main__":
    sys.exit(main())
