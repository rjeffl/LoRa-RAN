#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""Host tests for eirp_check.py.

    python3 tools/rangetest/test_eirp_check.py

WHY THIS FILE EXISTS. This tool decides whether the D33 clamp and the configured
TX power are doing what the documents claim, and its output is a PASS/FAIL a
reader will act on. Two of its three checks are dB arithmetic where a sign error
or a swapped term still prints a confident, plausible table.

The clamp check is the one that must not be wrong in the permissive direction: a
tool that says PASS while a board is emitting +22 dBm has converted a compliance
fault into a reassurance. It is tested from both sides.
"""

import io
import math
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import eirp_check as ec  # noqa: E402

FAILED = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok   {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name}  {detail}")


COLS = ("position,tp_index,freq_hz,sf,cr_denom,conducted_dbm,antenna_gain_dbi10,"
        "payload_len,probes_sent,echoes_recv,resp_heard,per_pct100,"
        "init_rssi_mean10,init_rssi_min10,init_rssi_max10,init_snr_mean10,"
        "init_snr_min10,init_snr_max10,resp_rssi_mean10,resp_rssi_min10,"
        "resp_rssi_max10,resp_snr_mean10,resp_snr_min10,resp_snr_max10,"
        "phy_crc_err,foreign,filler_err")


def make_trace(path, points, gain_dbi10=30, freq=915_000_000):
    """points: list of (position, conducted_dbm, rssi_dbm) -> one row each."""
    with io.open(path, "w") as fh:
        fh.write("# LRAN range test trace, captured 2026-01-01 00:00:00\n")
        fh.write("# note: synthetic\n")
        fh.write(COLS + "\n")
        for i, (pos, power, rssi) in enumerate(points):
            r10 = int(round(rssi * 10))
            fh.write(f"{pos},{i},{freq},7,5,{power},{gain_dbi10},16,8,8,8,0,"
                     f"{r10},{r10},{r10},50,50,50,{r10},{r10},{r10},50,50,50,"
                     f"0,0,0\n")


def run(argv):
    """Call main(), capturing the report. Returns (exit_code, text)."""
    saved, sys.stdout = sys.stdout, io.StringIO()
    try:
        code = ec.main(argv)
        text = sys.stdout.getvalue()
    except SystemExit as e:
        code, text = e.code, sys.stdout.getvalue()
    finally:
        out, sys.stdout = sys.stdout, saved
    return code, text


def test_fspl_matches_the_documented_figure():
    """Spec 18.2 / findings 8.1 quote 75.3 dB at 152 m, 915 MHz."""
    v = ec.fspl_db(152.0, 915_000_000)
    check("FSPL at 152 m / 915 MHz is the documented 75.3 dB",
          abs(v - 75.3) < 0.15, f"got {v:.2f}")
    check("FSPL rises 6.02 dB per doubling of distance",
          abs((ec.fspl_db(20.0, 915e6) - ec.fspl_db(10.0, 915e6)) - 6.02) < 0.02)
    check("FSPL rises 20 dB per decade of distance",
          abs((ec.fspl_db(100.0, 915e6) - ec.fspl_db(10.0, 915e6)) - 20.0) < 0.02)


def test_eirp_backout_inverts_the_forward_calculation():
    """The check is only meaningful if it is the forward path run backwards."""
    eirp, d, g = -1.0, 10.0, 3.0
    rssi = eirp - ec.fspl_db(d, 915e6) + g
    back = ec.eirp_from_rssi(rssi, d, 915e6, g)
    check("EIRP backs out to what was put in",
          abs(back - eirp) < 1e-9, f"got {back}")


def test_power_step_is_measured_not_assumed():
    """The expected step comes from the trace's own conducted_dbm column.

    Assuming 5 dB would be wrong the moment the configured antenna gain changes
    - which it already has once in this repo, from 2.0 to 3.0 dBi.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, [(0, -9, -50.0), (0, -4, -45.0)])
        rows, _ = ec.read_sweep(str(p))
        pos = ec.analyse_position(rows, 10.0, 3.0)
        st = ec.power_step(pos)
        check("expected step is read from the trace", st["expected_db"] == 5.0)
        check("measured uplink step matches", abs(st["legs"]["uplink"] - 5.0) < 1e-9)


def test_a_power_setting_that_never_applied_is_caught():
    """The failure findings 7.6 names: RSSI does not move with the request."""
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, [(0, -9, -50.0), (0, -4, -50.1)])
        code, text = run([str(p)])
        check("a flat power step fails", code == 1, f"exit {code}")
        check("and says so in words", "does not track" in text)


def test_the_clamp_check_fails_a_trace_that_exceeds_the_ceiling():
    """A -1 dBm EIRP ceiling with 3.0 dBi gain permits -4 dBm conducted.

    This is the direction that must never pass quietly: a tool reporting PASS
    while a board emitted +22 dBm has turned a compliance fault into comfort.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, [(0, -9, -50.0), (0, 22, -19.0)])
        code, text = run([str(p)])
        check("an unclamped +22 dBm point fails", code == 1, f"exit {code}")
        check("and is named a compliance fault, not a measurement error",
              "COMPLIANCE FAULT" in text or "clamp did not run" in text)

        p2 = Path(d) / "u.csv"
        make_trace(p2, [(0, -9, -50.0), (0, -4, -45.0)])
        code2, _ = run([str(p2)])
        check("a correctly clamped trace passes", code2 == 0, f"exit {code2}")


def test_a_disconnected_antenna_is_a_gross_absolute_failure():
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        # 30 dB below what 10 m of free space would give.
        good = -1.0 - ec.fspl_db(10.0, 915e6) + 3.0
        make_trace(p, [(0, -9, good - 35), (0, -4, good - 30)])
        code, text = run([str(p), "--distance", "0=10.0"])
        check("a 30 dB absolute shortfall fails", code == 1, f"exit {code}")
        check("and points at the antenna", "antenna is connected" in text)


def test_a_good_geometry_run_passes_everything():
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        pts = []
        for pos, dist in ((0, 3.0), (1, 6.0), (2, 12.0)):
            for power in (-9, -4):
                eirp = power + 3.0
                pts.append((pos, power,
                            eirp - ec.fspl_db(dist, 915e6) + 3.0))
        make_trace(p, pts)
        code, text = run([str(p), "--distance", "0=3.0",
                          "--distance", "1=6.0", "--distance", "2=12.0"])
        check("a clean synthetic run reports no failures", code == 0,
              f"exit {code}")
        # Assert the fit numerically rather than on the printed string: the CSV
        # stores tenths, so a synthetic free-space run rounds to -20.1 and a
        # string match on "-20.0" tests the rounding, not the arithmetic.
        rows, _ = ec.read_sweep(str(p))
        by_pos = {}
        for r in rows:
            by_pos.setdefault(str(r["position"]), []).append(r)
        pts = []
        for name, dist in (("0", 3.0), ("1", 6.0), ("2", 12.0)):
            a = ec.analyse_position(by_pos[name], dist, 3.0)
            pts.append((dist, a["legs"][-4]["uplink_rssi"]))
        slope, intercept, rms = ec.fit_slope(pts)
        check("and fits a free-space slope",
              abs(slope - ec.SLOPE_FREE_SPACE) < 0.5 and rms < 0.2,
              f"slope {slope:.2f} rms {rms:.2f}")
        check("whose intercept recovers the EIRP",
              abs((intercept + 20.0 * math.log10(1.0)) -
                  ec.eirp_from_rssi(pts[0][1], pts[0][0], 915e6, 3.0)
                  + ec.fspl_db(1.0, 915e6) - 3.0) < 0.5,
              f"intercept {intercept:.2f}")


def test_slope_fit_flags_a_reflective_site():
    """Two-ray ground reflection decays at ~40 dB/decade, not 20.

    The point of the fit is not to model the site - it is to tell the operator
    that the absolute number is measuring the ground.
    """
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        pts = []
        for pos, dist in ((0, 3.0), (1, 6.0), (2, 12.0)):
            rssi = -40.0 - 40.0 * math.log10(dist / 3.0)
            pts.append((pos, -4, rssi))
        make_trace(p, pts)
        code, text = run([str(p), "--distance", "0=3.0",
                          "--distance", "1=6.0", "--distance", "2=12.0"])
        check("a 40 dB/decade site is flagged", "far from" in text)
        del code


def test_a_survey_trace_is_refused():
    """The two schemas are different and confusing them is silent nonsense."""
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "s.csv"
        with io.open(p, "w") as fh:
            fh.write("# survey\n")
            fh.write("site_index,site_name,bin_index,freq_hz,passes,samples,"
                     "peak_dbm10,mean_dbm10,floor_dbm10,dropped\n")
            fh.write("0,a,0,902000000,10,100,-1130,-1140,-1160,0\n")
        code, text = run([str(p)])
        check("a survey trace is refused, not misread", code != 0)
        check("and the message names the right document",
              "not a sweep trace" in text or "not a sweep trace" in str(code),
              repr(code))


def test_sentinel_rssi_is_not_read_as_a_measurement():
    """-32768 means no reading. Averaging it in would invent a -3276.8 dBm."""
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.csv"
        make_trace(p, [(0, -9, -50.0), (0, -9, -52.0)])
        txt = io.open(p).read().replace("-500,-500,-500,50", "-32768,-32768,-32768,50")
        io.open(p, "w").write(txt)
        rows, _ = ec.read_sweep(str(p))
        pos = ec.analyse_position(rows, 10.0, 3.0)
        e = pos["legs"][-9]
        check("sentinel rows are excluded from the mean",
              e["uplink_n"] == 1 and abs(e["uplink_rssi"] - (-52.0)) < 1e-9,
              f"n={e['uplink_n']} rssi={e['uplink_rssi']}")


def main():
    print("eirp_check.py")
    test_fspl_matches_the_documented_figure()
    test_eirp_backout_inverts_the_forward_calculation()
    test_power_step_is_measured_not_assumed()
    test_a_power_setting_that_never_applied_is_caught()
    test_the_clamp_check_fails_a_trace_that_exceeds_the_ceiling()
    test_a_disconnected_antenna_is_a_gross_absolute_failure()
    test_a_good_geometry_run_passes_everything()
    test_slope_fit_flags_a_reflective_site()
    test_a_survey_trace_is_refused()
    test_sentinel_rssi_is_not_read_as_a_measurement()

    if FAILED:
        print(f"\n{len(FAILED)} FAILED: {', '.join(FAILED)}")
        return 1
    print("\nall eirp_check.py tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
