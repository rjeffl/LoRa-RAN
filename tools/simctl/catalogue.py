# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# The Impl Plan 10.5 fault catalogue as committed scenarios, and the pure verdict
# function that judges one. Task BF-21; Impl Plan 7.2, 10.4, 10.5.
#
# NO I/O IN THIS FILE, deliberately, and for the same reason scheduler.h and command.h
# have none: the part that decides is host-tested at a desk, and the part that talks to
# a serial port and a broker is separate and thin. test_simctl.py exercises every row
# here without a board.
#
# THE ROW SET IS MIRRORED FROM firmware/simnode/src/fault.cpp AND MUST AGREE WITH IT.
# tools/checks/simctl_catalogue.py fails the build when it does not - a scenario naming
# a fault the firmware dropped, or a fault the firmware gained with no scenario, is
# exactly the silent skip that 7.2 says committed scripts exist to prevent.

from dataclasses import dataclass, field
from typing import Optional

# Spec 14.1 counters excluded from rx_dropped: normal traffic, and a health metric that
# climbed on them would climb during correct operation (spec 14.1, this repo's root
# CLAUDE.md on counter names).
NOT_DROPPED = ("rx_frag_duplicate", "rx_frag_late", "rx_dup_command")


@dataclass(frozen=True)
class Scenario:
    """One catalogue row, as simctl runs and judges it."""

    name: str
    # The spec 14.1 counter that must move at the bridge, or None for a row whose
    # correct result is that nothing is discarded.
    counter: Optional[str]
    # How much it must move by. Usually the injection's frame count when every frame is
    # refused at the same stage, and 1 when a sequence produces one verdict.
    delta: int
    # Frames the injection puts on air. rx_frames must move by at least this much -
    # without it, a silent pass and a frame that never arrived look identical at the
    # broker, which is the trap the bench found on 2026-09-16.
    frames: int
    # Console arguments after `fault <id> <name>`, e.g. ("2", "gap", "300").
    args: tuple = ()
    # Seconds to wait after arming before the reading window may close. Rows whose
    # verdict comes from a receiver tick rather than an arrival need this.
    settle_s: int = 0
    # The identity's role, when the fault requires one.
    role: str = "ROLE_HEALTH"
    why: str = ""
    # Set when this scenario is known to disagree with the table today, with the task
    # that closes it. Reported as EXPECTED-DIVERGENCE, never as a pass and never as a
    # silent skip.
    divergence: Optional[str] = None


# ---------------------------------------------------------------------------
# Rows verified from `lran/bridge/diag/state`.
#
# `delta` is not always `frames`. A row whose frames are all refused at one stage moves
# its counter once per frame (bad_length sends a short and a long payload, so 2); a row
# that is one event to the receiver moves it once however many frames carried it
# (set_displaced is 4 frames and 1 abandonment).
# ---------------------------------------------------------------------------

COUNTER_ROWS = (
    Scenario("runt", "rx_runt", 1, 1, why="spec 14 stage 2"),
    Scenario("oversize", "rx_oversize", 1, 1,
             why="stage 2a - a foreign transmitter or a misconfigured PHY, no ERROR"),
    Scenario("bad_crc", "rx_bad_crc", 1, 1,
             why="stage 3 - the application CRC16, not the PHY's"),
    Scenario("bad_ver", "rx_bad_ver", 1, 2,
             why="stage 4 - N-1 ACCEPTED, N-2 rejected (V-B10). Two frames, one discard"),
    Scenario("wrong_dst", "rx_not_addressed", 1, 1, why="stage 5, no ERROR emitted"),
    Scenario("crit_ext", "rx_unknown_hdr_ext", 1, 1,
             why="stage 5a - the only test of spec 5.8's CRITICAL_EXT"),
    Scenario("hdr_rsv", None, 0, 1,
             why="ACCEPTED and ignored - spec 4.3 forward compatibility. A discard is a bug"),
    Scenario("frag_zero", "rx_bad_frag", 1, 1, why="stage 5b - spec 5.6 declares it malformed"),
    Scenario("unknown_type", "rx_unknown_type", 1, 1, why="stage 6"),
    Scenario("unknown_schema", "rx_unknown_schema", 1, 1, why="stage 7"),
    Scenario("bad_length", "rx_bad_length", 2, 2,
             why="stage 8 - one byte short and one byte long, so the counter moves twice"),
    Scenario("frag_command", "rx_not_fragmentable", 1, 1,
             why="stage 8a - spec 11.4 rules COMMAND single-frame; the counter is the diagnosis"),
    Scenario("bad_mac", "rx_rejected_mac", 1, 1,
             why="spec 9.4 step 3 - no state change, and the fragment is not buffered"),
    Scenario("frag_timeout", "rx_reassembly_timeout", 1, 1, settle_s=10,
             why="must fire from the periodic tick, not only on the next arrival"),
    Scenario("frag_overflow", "rx_fragment_overflow", 1, 1, why="fragment index >= declared total"),
    Scenario("frag_oversize", "rx_fragment_overflow", 1, 15,
             why="a reassembled set over LRAN_MAX_SCHEMA_PAYLOAD"),
    Scenario("frag_dup", "rx_frag_duplicate", 1, 4,
             why="the set STILL COMPLETES and rx_dropped must not move"),
    Scenario("frag_late", "rx_frag_late", 1, 4,
             why="discarded, not started as a new set; rx_dropped must not move"),
    Scenario("single_frame_interleave", None, 0, 4,
             why="THE HIGHEST-VALUE ROW - spec 11.2. The set completes and nothing is counted"),
    Scenario("set_displaced", "rx_reassembly_abandoned", 1, 4,
             why="spec 11.3 displacement. BF-21 completes the displacing set, so only this moves"),
    Scenario("ctx_jump", None, 0, 1, why="accepted - the bridge adopts and resets cmd_seq"),
    Scenario("seq_jump", None, 0, 1, why="accepted - RFC 1982 arithmetic, no lockout"),
    Scenario("seq_wrap", None, 0, 2,
             why="accepted through 0xFFFF. Guards against a plain `>` locking the node out"),
)

# ---------------------------------------------------------------------------
# Rows whose verdict is not a counter.
#
# Each names the topic that answers it. simctl runs these too rather than skipping
# them: 7.2's whole argument is that a hand-run pass skips the awkward entries, and
# "nothing happens" rows are the awkward ones.
# ---------------------------------------------------------------------------

BEHAVIOUR_ROWS = (
    Scenario("ack_suppress", None, 0, 0, args=("1",), role="ROLE_GATELINK",
             why="bridge retries with the SAME seq; simnode answers DUPLICATE_CACHED (BS-3)"),
    Scenario("ack_dup", None, 0, 0, args=("1",), role="ROLE_GATELINK",
             why="two ACKs for one command, the second ignored and not a second result"),
    Scenario("ctx_reject", None, 0, 0, args=("2",), role="ROLE_GATELINK",
             why="spec 10.3 step 3 - the bridge resyncs once, is refused again, and stops"),
    Scenario("event_replay", None, 0, 2, role="ROLE_GATELINK",
             why="same (ctx_id, event_id) twice, published once (spec 16.3)"),
    Scenario("silent", None, 0, 0, args=("4",),
             why="availability goes offline after missed_poll_threshold (V-B3)"),
    Scenario("flood", None, 0, 1, args=("50", "gap", "0"),
             why="the bridge stays responsive and lora_task does not block"),
)

# Rows simctl does not drive, each with the reason. Present so the catalogue is
# complete: a row missing from this file entirely is what the consistency check
# catches, and a row silently absent from a run is what 7.2 objects to.
UNDRIVEN = {
    "cmd_replay": "the counter is the TARGET NODE's, not the bridge's - Impl Plan 10.5.1",
    "cmd_stale_seq": "the counter is the TARGET NODE's, not the bridge's - Impl Plan 10.5.1",
    "bad_phy_crc": "not injectable - the PHY CRC is hardware (Impl Plan 10.5.2)",
}

ALL_ROWS = COUNTER_ROWS + BEHAVIOUR_ROWS


def scenario(name):
    for s in ALL_ROWS:
        if s.name == name:
            return s
    return None


# ---------------------------------------------------------------------------
# The verdict. Pure: two counter documents in, a result out.
# ---------------------------------------------------------------------------


@dataclass
class Verdict:
    name: str
    ok: bool
    detail: str
    diverged: bool = False
    notes: list = field(default_factory=list)


def _delta(before, after, key):
    return int(after.get(key, 0)) - int(before.get(key, 0))


def judge(s: Scenario, before: dict, after: dict) -> Verdict:
    """Judge one counter row from two `lran/bridge/diag/state` payloads.

    `before` and `after` are the decoded JSON objects. Every check is a difference
    across the window, because a reflash resets every counter to zero and an absolute
    value means nothing.
    """
    notes = []

    # THE ARRIVAL CHECK, FIRST AND ALWAYS. A silent pass and a frame that never reached
    # the bridge are identical in every counter this row cares about, so a row that
    # expects nothing to be discarded is worthless without evidence the frames landed.
    # Found on the bench, 2026-09-16.
    frames = _delta(before, after, "rx_frames")
    if s.frames and frames < s.frames:
        return Verdict(s.name, False,
                       f"only {frames} frame(s) reached the bridge, expected at least "
                       f"{s.frames} - the injection did not arrive, so nothing below is a pass")
    if s.frames:
        notes.append(f"rx_frames +{frames}")

    dropped = _delta(before, after, "rx_dropped")

    if s.counter is None:
        # A forward-compatibility row. The whole point is that nothing is counted.
        if dropped != 0:
            return Verdict(s.name, False,
                           f"rx_dropped moved by {dropped} and this row must discard nothing")
        moved = [k for k, v in after.items()
                 if k not in ("rx_frames", "rx_dropped") and _delta(before, after, k) != 0]
        if moved:
            return Verdict(s.name, False, f"counters moved that should not have: {', '.join(moved)}")
        return Verdict(s.name, True, "nothing discarded, as required", notes=notes)

    got = _delta(before, after, s.counter)
    if got != s.delta:
        v = Verdict(s.name, False, f"{s.counter} moved by {got}, expected {s.delta}", notes=notes)
        if s.divergence:
            v.diverged = True
            v.detail += f" - KNOWN: {s.divergence}"
        return v

    # ONLY that counter. This is the table's own invariant and the reason BF-21 made
    # set_displaced complete its displacing set: a row that moves two counters cannot
    # tell its own defect from another row's.
    others = [k for k, v in after.items()
              if k not in ("rx_frames", "rx_dropped", s.counter)
              and _delta(before, after, k) != 0]
    if others:
        return Verdict(s.name, False,
                       f"{s.counter} moved by {got} as expected, but so did: {', '.join(others)}",
                       notes=notes)

    # rx_dropped is the sum of the counters spec 14.1 marks `yes`, and only those.
    want_dropped = 0 if s.counter in NOT_DROPPED else s.delta
    if dropped != want_dropped:
        why = ("it is normal traffic and excluded from rx_dropped"
               if s.counter in NOT_DROPPED else "it is a discard and included in rx_dropped")
        return Verdict(s.name, False,
                       f"rx_dropped moved by {dropped}, expected {want_dropped}: {why}",
                       notes=notes)

    notes.append(f"rx_dropped +{dropped}")
    return Verdict(s.name, True, f"{s.counter} +{got}", notes=notes)
