#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# M22 / V-B12 arithmetic: one burst of frames, one publication window, one PER figure.
# No I/O, so the part that decides what a run measured is testable at a desk. Impl Plan
# 7.1, 8.1; Bridge PRD 4.4, V-B12; Decision Register 5.3.
#
# WHAT PER MEANS HERE. The denominator is the SENDER's TX_DONE count, not the count the
# operator asked for: `fault f3 flood 50` is a request, and TX_DONE is the only evidence
# on that board that a frame reached the air (simnode main.cpp). The numerator is what
# the bridge accepted. Every frame in between is an error, and the split tells you which
# kind:
#
#   heard and accepted      rx_frames - rx_dropped
#   heard and discarded     rx_dropped, itemised by counter
#   heard but corrupt       rx_crc_err (inside rx_dropped; spec 14 stage 1)
#   never heard at all      sent - rx_frames, which no counter can report
#
# A coexistence failure would show up in the last two: desense either corrupts a frame
# or loses it entirely. That is why this returns the breakdown rather than one number.
#
# WHAT INVALIDATES A WINDOW. Three things, and each returns a reason instead of a
# figure, because a PER computed across any of them is a plausible wrong number:
#
#   1. A counter went backwards. The bridge rebooted mid-window and zeroed everything
#      (the handoff has this trap by name), so the deltas describe two different boots.
#   2. More frames were accepted than were sent. Something else transmitted into the
#      window - another simnode identity answering a POLL, or the second board. The
#      handoff's rule is to difference a counter only across a window carrying nothing
#      else, and this is that rule made mechanical.
#   3. Nothing reached the air. sent == 0 measures the console, not the link.
#
# WHAT IT REPORTS RATHER THAN REFUSES. The bridge's own transmissions in the window,
# from `lran/bridge/diag/radio/state`. The SX1262 is half duplex: the bridge cannot hear
# a frame that arrives while it is answering a POLL, so its tx_frames delta is a floor
# under the losses this window will show for reasons that have nothing to do with WiFi.
# It is not an error and it is not subtracted - it is recorded, so the WiFi-saturated
# arm can be compared against an idle arm that carried the same poll load.

# Every key this module reads. A reading is a whole published document; naming the keys
# here keeps a typo from reading as a zero delta.
RX_FRAMES = "rx_frames"
RX_DROPPED = "rx_dropped"
RX_CRC_ERR = "rx_crc_err"
TX_FRAMES = "tx_frames"
CAD_BACKOFFS = "cad_backoffs"

# The receive path's interrupt accounting, added to the bridge 2026-09-17 (rx_wake.h).
# REPORTED, NEVER A GUARD - like bridge_tx, and for the same reason: they describe how a
# frame reached the ladder, not whether this window is measurable.
#
# A bridge built before that publishes neither, and delta() then returns None rather than
# a zero. That distinction is the whole point here: a zero means the interrupt path missed
# nothing, and None means nobody asked.
NO_INTERRUPT = "rx_no_interrupt"
WAKE_EMPTY = "rx_wake_empty"


def delta(before, after, key):
    """after[key] - before[key], or None if either document lacks the key."""
    if key not in before or key not in after:
        return None
    return after[key] - before[key]


def discard_deltas(before, after):
    """Every counter that moved, itemised. Excludes the two totals and rx_frames."""
    skip = (RX_FRAMES, RX_DROPPED)
    moved = {}
    for key, value in after.items():
        if key in skip or key not in before:
            continue
        if not isinstance(value, int) or not isinstance(before[key], int):
            continue
        change = value - before[key]
        if change != 0:
            moved[key] = change
    return moved


def measure(sent, rx_before, rx_after, radio_before=None, radio_after=None):
    """One burst's result.

    `sent` is the sender's TX_DONE delta across the burst. `rx_*` are two
    `lran/bridge/diag/state` documents that bracket it; `radio_*` are the matching
    `lran/bridge/diag/radio/state` documents, which are optional and only ever
    reported.

    Returns a dict that always carries `valid` and `reason`. When `valid` is True it
    also carries the counts and `per`.
    """
    result = {"sent": sent, "valid": False, "reason": None}

    heard = delta(rx_before, rx_after, RX_FRAMES)
    dropped = delta(rx_before, rx_after, RX_DROPPED)
    if heard is None or dropped is None:
        result["reason"] = "a reading is missing rx_frames or rx_dropped"
        return result

    moved = discard_deltas(rx_before, rx_after)
    bridge_tx = None
    if radio_before is not None and radio_after is not None:
        bridge_tx = delta(radio_before, radio_after, TX_FRAMES)
        cad = delta(radio_before, radio_after, CAD_BACKOFFS)
        result["bridge_cad_backoffs"] = cad
        result["bridge_no_interrupt"] = delta(radio_before, radio_after, NO_INTERRUPT)
        result["bridge_wake_empty"] = delta(radio_before, radio_after, WAKE_EMPTY)

    # Guard 1 - a counter went backwards, so the bridge rebooted inside the window.
    negative = [k for k, v in moved.items() if v < 0]
    if heard < 0 or dropped < 0 or negative or (bridge_tx is not None and bridge_tx < 0):
        result["reason"] = "a counter went backwards - the bridge rebooted mid-window"
        return result

    # Guard 3 before the arithmetic that would divide by it.
    if sent <= 0:
        result["reason"] = "no frame reached the air (sender TX_DONE did not move)"
        return result

    accepted = heard - dropped

    # Guard 2 - the window carried traffic this burst did not send.
    if accepted > sent:
        result["reason"] = (
            "the bridge accepted %d frames from %d sent - the window carried other "
            "traffic" % (accepted, sent)
        )
        return result

    result.update(
        {
            "valid": True,
            "heard": heard,
            "accepted": accepted,
            "dropped": dropped,
            "corrupt": moved.get(RX_CRC_ERR, 0),
            "never_heard": sent - heard,
            "lost": sent - accepted,
            "per": (sent - accepted) / float(sent),
            "discards": moved,
            "bridge_tx": bridge_tx,
        }
    )
    return result


def aggregate(windows):
    """Pool the valid windows. PER is pooled over frames, never averaged over bursts.

    Averaging per-burst rates weights a 5-frame burst like a 200-frame one, which is
    how a short burst's single loss becomes a 20 % headline figure.
    """
    valid = [w for w in windows if w.get("valid")]
    out = {"windows": len(windows), "valid_windows": len(valid)}
    if not valid:
        out["reason"] = "no valid window"
        return out
    sent = sum(w["sent"] for w in valid)
    accepted = sum(w["accepted"] for w in valid)
    out.update(
        {
            "sent": sent,
            "accepted": accepted,
            "lost": sent - accepted,
            "never_heard": sum(w["never_heard"] for w in valid),
            "corrupt": sum(w["corrupt"] for w in valid),
            "per": (sent - accepted) / float(sent),
            "bridge_tx": sum(w["bridge_tx"] or 0 for w in valid),
            "bridge_cad_backoffs": sum(w.get("bridge_cad_backoffs") or 0 for w in valid),
            "bridge_no_interrupt": sum(w.get("bridge_no_interrupt") or 0 for w in valid),
            "bridge_wake_empty": sum(w.get("bridge_wake_empty") or 0 for w in valid),
            "worst_per": max(w["per"] for w in valid),
        }
    )
    return out


def format_window(index, w):
    """One burst, as the run prints it."""
    if not w["valid"]:
        return "  burst %d  INVALID  %s" % (index, w["reason"])
    line = "  burst %d  sent %d  accepted %d  PER %.2f %%" % (
        index,
        w["sent"],
        w["accepted"],
        100.0 * w["per"],
    )
    parts = []
    if w["never_heard"]:
        parts.append("never heard %d" % w["never_heard"])
    if w["corrupt"]:
        parts.append("corrupt %d" % w["corrupt"])
    if w["bridge_tx"]:
        parts.append("bridge TX %d" % w["bridge_tx"])
    # Reported even when bridge_tx is zero, and that combination is the interesting one:
    # a CAD takes the radio out of receive whether or not a frame follows it, so a window
    # with backoffs and no transmission still lost receive time to media access.
    if w.get("bridge_cad_backoffs"):
        parts.append("bridge CAD backoffs %d" % w["bridge_cad_backoffs"])
    # Printed whenever the bridge publishes them, INCLUDING AT ZERO. A zero here is the
    # reading that falsifies the kIrqReadMs mechanism (engineering log, 2026-09-17), so it
    # has to be visible in a run that lost frames rather than suppressed as "nothing to
    # report". None - an older bridge that publishes neither - stays silent.
    if w.get("bridge_no_interrupt") is not None:
        parts.append("no-interrupt %d" % w["bridge_no_interrupt"])
    if w.get("bridge_wake_empty") is not None:
        parts.append("wake-empty %d" % w["bridge_wake_empty"])
    other = {k: v for k, v in w["discards"].items() if k != RX_CRC_ERR}
    if other:
        parts.append("also " + ", ".join("%s +%d" % (k, v) for k, v in sorted(other.items())))
    if parts:
        line += "  (" + "; ".join(parts) + ")"
    return line
