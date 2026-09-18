#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# M25 - what a long channel capture says about 917.4 MHz. The arithmetic does no I/O, so
# the part that decides what a twelve-hour run showed is testable at a desk; the same
# split per_window.py and rxlog_analyze.py make.
#
# WHAT THE CAPTURE IS EVIDENCE ABOUT. Decision Register 3.4 keeps the channel "under
# observation" after D1 and names `cad_backoffs` as the instrument. A LoRa CAD sees a LoRa
# preamble at one spreading factor; it cannot see the Z-Wave or Insteon FSK on this
# property at any level. Raw RSSI can. This turns the capture into the occupancy figure
# D33's third standing condition actually needs.
#
# THREE THINGS IT REFUSES TO DO, each of which would produce a confident wrong number:
#
#   1. NEVER POOL ACROSS A REBOOT WITHOUT SAYING SO. The bridge's millis() restarts and
#      the bucket sequence restarts with it, so two segments concatenated look like one
#      run that jumped backwards in time. Segments are kept apart and counted.
#   2. NEVER READ A SKIPPED BUCKET AS A QUIET ONE. A bucket whose samples are all skips
#      looked at nothing. Its floor is absent, not low. Occupancy is reported against
#      samples actually taken, and the skipped fraction is reported beside it.
#   3. NEVER ATTRIBUTE OUR OWN TRAFFIC TO THE CHANNEL. The sampler already excludes a
#      reception in progress, but a bucket carrying our own receptions is flagged so a
#      reader can drop it rather than wonder.

BOOT_TAG = "CHAN-BOOT"
CHAN_TAG = "CHAN"
SUM_TAG = "CHANSUM"
FRAME_TAG = "FRAME"

# chan_monitor.h's kNoReading.
NO_READING = -32768


class Boot:
    """One run of the bridge: everything between two CHAN-BOOT lines."""

    def __init__(self, git=None, freq_hz=None, bucket_ms=None, occupied_dbm10=None,
                 wait_ms=None, host_time=None):
        self.git = git
        self.freq_hz = freq_hz
        self.bucket_ms = bucket_ms
        self.occupied_dbm10 = occupied_dbm10
        self.wait_ms = wait_ms
        self.host_time = host_time
        self.buckets = []   # CHAN lines: the buckets that saw something
        self.rollups = []   # CHANSUM lines: every bucket, loud or quiet
        self.frames = []


def parse(lines):
    """Capture lines to a list of Boot segments.

    A capture that begins mid-run has no CHAN-BOOT to open it, so an implicit segment is
    started - losing the first hours of an overnight run because the header scrolled past
    is not an acceptable failure.
    """
    boots = []
    cur = None

    for raw in lines:
        line = raw.rstrip("\n").rstrip("\r")
        if not line or line.startswith("#"):
            continue

        host, _, payload = line.partition(",")
        if not payload:
            continue
        if payload.startswith("#"):
            continue

        if payload.startswith(BOOT_TAG + ","):
            f = payload.split(",")
            cur = Boot(host_time=host)
            if len(f) >= 8:
                cur.git = f[1]
                cur.freq_hz = _int(f[2])
                cur.bucket_ms = _int(f[5])
                cur.occupied_dbm10 = _int(f[6])
                cur.wait_ms = _int(f[7])
            boots.append(cur)
            continue

        if payload.startswith(SUM_TAG + ","):
            f = payload.split(",")
            if len(f) < 15:
                continue
            r = {
                "host": host, "from_seq": _int(f[1]), "to_seq": _int(f[2]),
                "buckets": _int(f[3]), "span_ms": _int(f[4]), "samples": _int(f[5]),
                "skipped": _int(f[6]), "above": _int(f[7]), "own_rx": _int(f[8]),
                "notable": _int(f[9]), "blind": _int(f[10]), "peak": _int(f[11]),
                "floor_min": _int(f[12]), "floor_max": _int(f[13]),
                "floor_mean": _int(f[14]),
            }
            if None in r.values():
                continue
            if cur is None:
                cur = Boot(host_time=host)
                boots.append(cur)
            if cur.rollups and r["from_seq"] < cur.rollups[-1]["from_seq"]:
                cur = Boot(host_time=host)
                boots.append(cur)
            cur.rollups.append(r)
            continue

        if payload.startswith(CHAN_TAG + ","):
            f = payload.split(",")
            if len(f) < 11:
                continue
            b = {
                "host": host, "seq": _int(f[1]), "start_ms": _int(f[2]),
                "dur_ms": _int(f[3]), "samples": _int(f[4]), "skipped": _int(f[5]),
                "above": _int(f[6]), "own_rx": _int(f[7]), "peak": _int(f[8]),
                "floor": _int(f[9]), "mean": _int(f[10]),
            }
            if None in b.values():
                continue
            if cur is None:
                cur = Boot(host_time=host)
                boots.append(cur)
            # A sequence that went backwards without a header is a reboot the capture
            # missed - start a segment rather than pool two clocks.
            if cur.buckets and b["seq"] < cur.buckets[-1]["seq"]:
                cur = Boot(host_time=host)
                boots.append(cur)
            cur.buckets.append(b)
            continue

        if payload.startswith(FRAME_TAG):
            if cur is None:
                cur = Boot(host_time=host)
                boots.append(cur)
            cur.frames.append({"host": host, "text": payload})

    return boots


def _int(s):
    try:
        return int(s)
    except (TypeError, ValueError):
        return None


def summarise(boot, occupied_dbm10=None):
    """One segment's occupancy. Returns a dict; every count says what it counted.

    THE TOTALS COME FROM THE ROLLUPS, NOT FROM THE PER-BUCKET LINES. The firmware writes
    a CHAN line only for a bucket that saw something, so summing those would divide the
    excursions by themselves and report an occupancy near 100 %. The CHANSUM lines carry
    every bucket, loud and quiet, and they are the denominator. The CHAN lines are the
    detail - which second, how loud, how many samples of it.

    A capture holding CHAN lines and no CHANSUM lines is reported as having no
    denominator rather than being given a plausible one.
    """
    thr = occupied_dbm10 if occupied_dbm10 is not None else (boot.occupied_dbm10 or -1100)
    rs = boot.rollups
    bs = boot.buckets

    samples = sum(r["samples"] for r in rs)
    skipped = sum(r["skipped"] for r in rs)
    above = sum(r["above"] for r in rs)
    own_rx = sum(r["own_rx"] for r in rs)
    span_ms = sum(r["span_ms"] for r in rs)
    buckets = sum(r["buckets"] for r in rs)
    blind = sum(r["blind"] for r in rs)

    floors = sorted(r["floor_mean"] for r in rs if r["floor_mean"] != NO_READING)
    fmins = [r["floor_min"] for r in rs if r["floor_min"] != NO_READING]
    fmaxs = [r["floor_max"] for r in rs if r["floor_max"] != NO_READING]
    peaks = [r["peak"] for r in rs if r["peak"] != NO_READING]

    return {
        "has_denominator": bool(rs),
        "buckets": buckets,
        "buckets_observed": buckets - blind,
        "buckets_blind": blind,
        "buckets_logged": len(bs),
        "buckets_with_own_rx": sum(1 for b in bs if b["own_rx"] > 0),
        "samples": samples,
        "skipped": skipped,
        "own_rx": own_rx,
        "span_ms": span_ms,
        "span_hours": span_ms / 3_600_000.0,
        "threshold_dbm10": thr,
        "above": above,
        # Duty cycle AGAINST SAMPLES TAKEN, never against wall clock: the sampler does not
        # look while the radio is transmitting, and pretending otherwise inflates the
        # denominator with time nothing was observed.
        "occupancy": (above / samples) if samples else None,
        "floor_median": floors[len(floors) // 2] if floors else None,
        "floor_min": min(fmins) if fmins else None,
        "floor_max": max(fmaxs) if fmaxs else None,
        "peak_max": max(peaks) if peaks else None,
        "excursions": [b for b in bs if b["peak"] != NO_READING and b["peak"] >= thr],
    }


def hourly(boot, occupied_dbm10=None):
    """Per-hour occupancy, keyed by the host timestamp's hour.

    THE REASON A LONG RUN IS WORTH MORE THAN A LONGER BENCH SESSION: a neighbour's
    equipment has a schedule, and a figure pooled over twelve hours hides it.
    """
    thr = occupied_dbm10 if occupied_dbm10 is not None else (boot.occupied_dbm10 or -1100)
    rows = {}
    for x in boot.rollups:  # the rollups, for the reason summarise() gives
        key = x["host"][:13]  # YYYY-MM-DDTHH
        r = rows.setdefault(key, {"samples": 0, "above": 0, "skipped": 0,
                                  "peak": None, "buckets": 0})
        r["buckets"] += x["buckets"]
        r["samples"] += x["samples"]
        r["above"] += x["above"]
        r["skipped"] += x["skipped"]
        if x["peak"] != NO_READING and (r["peak"] is None or x["peak"] > r["peak"]):
            r["peak"] = x["peak"]
    for r in rows.values():
        r["occupancy"] = (r["above"] / r["samples"]) if r["samples"] else None
        r["threshold_dbm10"] = thr
    return dict(sorted(rows.items()))


def peak_histogram(boot, edges_dbm10=(-1150, -1100, -1000, -900, -800, -700, -600)):
    """How many observed buckets peaked in each band. Bands, not a single threshold,
    because 'something was here' and 'something loud was here' are different findings."""
    out = []
    # The CHAN lines only - a histogram of the buckets that saw something is what it is
    # for, and the quiet ones would all pile into the lowest band and say nothing.
    observed = [b for b in boot.buckets if b["samples"] > 0 and b["peak"] != NO_READING]
    for i, lo in enumerate(edges_dbm10):
        hi = edges_dbm10[i + 1] if i + 1 < len(edges_dbm10) else None
        n = sum(1 for b in observed if b["peak"] >= lo and (hi is None or b["peak"] < hi))
        out.append({"lo": lo, "hi": hi, "buckets": n})
    return out


def frame_loss_windows(boot, radius_ms=2000):
    """Host timestamps of buckets around which a FRAME line was recorded.

    Deliberately coarse. The exact correlation belongs to rxlog_analyze.py, which reads
    the MQTT frame log with its own arithmetic; this only answers whether the capture
    holds any frame traffic worth correlating at all.
    """
    if not boot.frames:
        return []
    return [f["host"] for f in boot.frames]
