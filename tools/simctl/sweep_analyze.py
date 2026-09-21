#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# The arithmetic of an interleaved spacing sweep: bursts in, a per-arm and a per-time
# figure out, and a verdict that refuses to pick between them when the run cannot.
# No I/O, so the part that decides what a sweep showed is testable at a desk - the same
# split per_window.py and rxlog_analyze.py make.
#
# WHY AN INTERLEAVED SWEEP NEEDS ITS OWN ARITHMETIC. Every sweep on record ran one
# spacing to completion before starting the next, so a slow change in the environment
# and an effect of spacing produce the same table. The 2026-09-17 frame-log session
# measured 8 % at a 2000 ms gap and 0 % at 250 ms in one afternoon, after a morning that
# measured 5.6 % and 0 % the other way round. Alternating the arms inside one session
# separates them, and separating them means pooling the SAME frames two ways: by arm,
# and by when in the session they were sent.
#
# POOLED OVER FRAMES, NEVER AVERAGED OVER BURSTS. The 2026-09-17 entry settled this:
# averaging five bursts gave 4.4 % where pooling gave 5.6 %, and with uneven bursts a
# short burst's single loss becomes the headline.

# Every key this module reads out of a burst mark, named so a typo reads as a KeyError
# rather than as a clean run that found nothing.
K_ARM = "arm"
K_GAP = "gap_ms"
K_SENT = "sent"
K_I_BEFORE = "i_before"
K_I_AFTER = "i_after"

# A direction is stated only when one split separates and the other does not, by at
# least this ratio. Some threshold has to exist for any comparison to say anything, so
# it is named here rather than buried in a sentence: a reader who disagrees can see what
# they are disagreeing with, and test_sweep_analyze.py pins the behaviour at it.
SEPARATION_RATIO = 2.0

# Below this many lost frames, no split is called in either direction. A single frame
# either way moves a ratio past 2.0 when the totals are small, and a sweep that lost
# four frames has not measured anything about spacing.
MIN_LOSSES_FOR_DIRECTION = 8


class Burst:
    """One burst of one arm, and what the two instruments say about it.

    `sent` is the sender's own TX_DONE delta and `arrived` is the count of delivered
    receptions the bridge logged from that peer. The difference is the loss, because
    neither instrument can be talked into a frame the other did not see.
    """

    def __init__(self, n, arm, gap_ms, sent, arrived, ring_lost=0, seq_losses=None):
        self.n = n
        self.arm = arm
        self.gap_ms = gap_ms
        self.sent = sent
        self.arrived = arrived
        self.ring_lost = ring_lost
        self.seq_losses = seq_losses or []
        self.invalid_reason = None

    @property
    def lost(self):
        return self.sent - self.arrived

    @property
    def per(self):
        return (100.0 * self.lost / self.sent) if self.sent else 0.0

    @property
    def valid(self):
        return self.invalid_reason is None


def slice_records(records, mark):
    """The records the bridge made during one burst.

    Bounded by record index rather than by time, because the index is the bridge's own
    numbering and needs no clock shared with this host. `i_before` is the highest index
    seen when the burst was ordered, so the slice opens after it.
    """
    lo = mark.get(K_I_BEFORE)
    hi = mark.get(K_I_AFTER)
    out = []
    for rec in records:
        i = int(rec["i"])
        if lo is not None and i <= lo:
            continue
        if hi is not None and i > hi:
            continue
        out.append(rec)
    return out


def burst_from(mark, records, peer, ring_lost=0):
    """One burst mark plus its records, judged.

    Refuses the burst rather than reporting a figure when the two instruments disagree
    in a way that makes the figure meaningless - the rule per_window.py already applies
    to a counter that went backwards. A plausible wrong number is worse than a refusal.
    """
    from rxlog_analyze import analyze  # local, so this module stays import-light

    streams = analyze(records)
    arrived = 0
    seq_losses = []
    for (stream_peer, _type), stream in streams.items():
        if stream_peer != peer:
            continue
        arrived += stream.arrivals
        seq_losses.extend(stream.losses)

    b = Burst(mark.get("n"), mark[K_ARM], mark[K_GAP], int(mark[K_SENT]), arrived,
              ring_lost=ring_lost, seq_losses=seq_losses)

    if b.sent == 0:
        b.invalid_reason = "the simnode sent nothing"
    elif arrived > b.sent:
        # Another identity was still enabled, or a burst's tail landed in this slice.
        b.invalid_reason = ("%d frames arrived from peer 0x%02x and only %d were sent"
                            % (arrived, peer, b.sent))
    elif ring_lost:
        b.invalid_reason = ("the bridge's log ring overwrote %d record(s), so an arrival"
                            " may be missing from the log rather than from the air"
                            % ring_lost)
    return b


def pool(bursts):
    """Sent, lost and PER pooled over frames. Invalid bursts contribute nothing."""
    sent = sum(b.sent for b in bursts if b.valid)
    lost = sum(b.lost for b in bursts if b.valid)
    per = (100.0 * lost / sent) if sent else 0.0
    return sent, lost, per


def by_arm(bursts):
    """{arm: (sent, lost, per)}, in the order the arms first ran."""
    order = []
    groups = {}
    for b in bursts:
        if b.arm not in groups:
            groups[b.arm] = []
            order.append(b.arm)
        groups[b.arm].append(b)
    return [(arm, pool(groups[arm])) for arm in order]


def split_halves(bursts):
    """The valid bursts, cut in two by position in the rotation.

    Cut by burst count rather than by wall-clock time, because a 2000 ms burst takes
    several times as long as a 250 ms one and a clock cut would put most of one arm in
    one half - which is the confound this whole instrument exists to remove.
    """
    valid = [b for b in bursts if b.valid]
    cut = len(valid) // 2
    return valid[:cut], valid[cut:]


def by_half(bursts):
    """The same frames pooled by WHEN they were sent, not by how they were spaced."""
    first, second = split_halves(bursts)
    return [("first half", pool(first)), ("second half", pool(second))]


def by_arm_and_half(bursts):
    """Both splits at once: one cell per arm per half.

    THE TWO-WAY SPLITS CANNOT SEE AN EFFECT THAT RIDES ON ANOTHER ONE. If the losses
    concentrate in one arm AND grow through the session, pooling by arm and pooling by
    half both move, and each looks like the whole story. The cross-tab asks the question
    that separates them: does the arm ordering hold INSIDE each half, where the passage
    of time is held still?

    Returns [(arm, half_label, (sent, lost, per))], arms in first-run order.
    """
    halves = zip(("first half", "second half"), split_halves(bursts))
    arms = [arm for arm, _figures in by_arm(bursts)]
    out = []
    for label, group in halves:
        for arm in arms:
            out.append((arm, label, pool([b for b in group if b.arm == arm])))
    return out


def _arm_gaps(bursts):
    """{arm: gap_ms}, so the denser arm can be named rather than assumed to be first."""
    return {b.arm: b.gap_ms for b in bursts}


def _ratio(split):
    """The larger PER over the smaller, across a two-way split.

    Returns None when the split cannot be compared at all: fewer than two sides, or
    nothing lost on either side. One side at zero and the other above it is infinite
    rather than incomparable, because that is the clearest separation there is.
    """
    pers = [per for _label, (_sent, _lost, per) in split]
    if len(pers) != 2 or max(pers) <= 0.0:
        return None
    if min(pers) <= 0.0:
        return float("inf")
    return max(pers) / min(pers)


def _arm_holds_within(cells, arms, label):
    """Does the same arm lose more, by the ratio, inside this half?

    Returns True, False, or None when the half carries no losses at all and so
    demonstrates nothing either way. A half with nothing lost is not agreement.
    """
    per = {arm: figures[2] for arm, half, figures in cells if half == label}
    lost = sum(figures[1] for arm, half, figures in cells if half == label)
    if lost == 0 or len(per) != 2:
        return None
    worse = max(arms, key=lambda a: per[a])
    other = [a for a in arms if a != worse][0]
    if per[other] <= 0.0:
        return worse
    return worse if per[worse] / per[other] >= SEPARATION_RATIO else False


def verdict(bursts):
    """Which variable this sweep separated, stated only when one of them separated.

    Returns (direction, text). `direction` is "spacing", "time", or None.

    THE CROSS-TAB IS ASKED FIRST, because the two-way splits cannot tell an arm effect
    from a time effect riding on it. An arm ordering that holds inside BOTH halves
    survives the passage of time by construction: within a half, time is held as still
    as this rotation can hold it.
    """
    _sent, lost, _per = pool(bursts)
    arms = [arm for arm, _figures in by_arm(bursts)]
    halves = by_half(bursts)

    if lost < MIN_LOSSES_FOR_DIRECTION:
        return None, ("%d lost frame(s) is too few to separate spacing from time; no"
                      " direction is claimed. At least %d is the line this tool draws."
                      % (lost, MIN_LOSSES_FOR_DIRECTION))

    arm_ratio = _ratio(by_arm(bursts))
    half_ratio = _ratio(halves)
    arm_separates = arm_ratio is not None and arm_ratio >= SEPARATION_RATIO
    half_separates = half_ratio is not None and half_ratio >= SEPARATION_RATIO

    cells = by_arm_and_half(bursts)
    first = _arm_holds_within(cells, arms, "first half")
    second = _arm_holds_within(cells, arms, "second half")

    if arm_separates and first and second and first == second:
        text = ("The %s arm loses more in BOTH halves, so this run points at SPACING and"
                " the finding survives the split by time." % first)
        if half_separates:
            # Both effects are present. Naming which arm carried the time effect is the
            # part a two-way split throws away.
            moved = _arm_moved_between_halves(cells, arms)
            text += (" A time effect rides on top of it: %s."
                     % (moved or "both arms moved between the halves"))
        return "spacing", text

    if half_separates and not arm_separates:
        return "time", ("The halves differ and the arms do not, so this run points at"
                        " TIME, not spacing. Every sweep before this one ran its arms in"
                        " blocks and could not tell the two apart.")
    if arm_separates and half_separates:
        return None, ("Both splits moved and the arm ordering does not hold inside both"
                      " halves, so this run separates nothing. The arms and the halves"
                      " are not independent when the losses sit in one arm's turn in one"
                      " half of the rotation - read the run-order table.")
    if arm_separates:
        return None, ("The arms differ overall but the ordering does not hold inside both"
                      " halves, so the difference may be where the losses fell in the"
                      " rotation rather than how the frames were spaced.")
    return None, ("Neither split moved by a factor of %.1f, so this run separates"
                  " nothing. The losses are spread across both arms and both halves."
                  % SEPARATION_RATIO)


def _arm_moved_between_halves(cells, arms):
    """Which arms changed between the halves, by the same ratio, and in which direction."""
    moved = []
    for arm in arms:
        per = {half: figures[2] for a, half, figures in cells if a == arm}
        lo, hi = per.get("first half", 0.0), per.get("second half", 0.0)
        ratio = _ratio([("first half", (0, 0, lo)), ("second half", (0, 0, hi))])
        if ratio is not None and ratio >= SEPARATION_RATIO:
            moved.append("%s went from %.2f %% to %.2f %%" % (arm, lo, hi))
    if not moved:
        return None
    if len(moved) == len(arms):
        return " and ".join(moved)
    return " and ".join(moved) + ", while the other arm held still"


def format_report(bursts, peer):
    """The whole sweep as text. Pure, so a saved capture exercises the format."""
    lines = []
    if not bursts:
        lines.append("NO BURSTS. Nothing was driven, or the capture holds no marks.")
        return "\n".join(lines)

    lines.append("interleaved spacing sweep - peer 0x%02x, %d burst(s)" % (peer, len(bursts)))
    lines.append("")
    lines.append("run order:")
    lines.append("   #  arm        gap ms   sent  arrived   lost      PER")
    for b in bursts:
        if not b.valid:
            lines.append("  %2s  %-9s %6d   REFUSED - %s" % (b.n, b.arm, b.gap_ms,
                                                             b.invalid_reason))
            continue
        lines.append("  %2s  %-9s %6d %6d %8d %6d %7.2f %%"
                     % (b.n, b.arm, b.gap_ms, b.sent, b.arrived, b.lost, b.per))

    lines.append("")
    lines.append("pooled by arm:")
    for arm, (sent, lost, per) in by_arm(bursts):
        lines.append("  %-9s %5d sent, %3d lost, %6.2f %%" % (arm, sent, lost, per))

    lines.append("")
    lines.append("the same frames pooled by when they were sent:")
    for label, (sent, lost, per) in by_half(bursts):
        lines.append("  %-11s %5d sent, %3d lost, %6.2f %%" % (label, sent, lost, per))

    # THE TABLE THE VERDICT IS READ FROM. Either two-way split above can be produced by
    # the other one, and only this one holds time still while it compares the arms.
    lines.append("")
    lines.append("both at once - does the arm ordering hold inside each half?")
    for arm, label, (sent, lost, per) in by_arm_and_half(bursts):
        lines.append("  %-11s %-9s %5d sent, %3d lost, %6.2f %%"
                     % (label, arm, sent, lost, per))

    # REPORTED AFTER BOTH SPLITS AND BEFORE THE DETAIL, because a reader who stops here
    # must not be left holding two tables and no statement about them.
    lines.append("")
    _direction, text = verdict(bursts)
    lines.append(text)

    gaps = [(b, loss) for b in bursts if b.valid for loss in b.seq_losses]
    if gaps:
        lines.append("")
        lines.append("where the losses fell, from the frame log:")
        for b, loss in gaps:
            lines.append("  burst %s (%s): %d frame(s) after seq %d, gap %d ms, deaf"
                         " %d ms (%.1f %% ceiling)%s"
                         % (b.n, b.arm, loss.count, loss.after_seq, loss.dt_ms,
                            loss.deaf_ms, 100.0 * loss.deaf_fraction,
                            ", bridge transmitted inside" if loss.tx_inside else ""))
    return "\n".join(lines)
