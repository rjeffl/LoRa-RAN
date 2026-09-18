#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# BF-27 arithmetic: batches of frame records in, a list of losses and what the bridge was
# doing across each one out. No I/O, so the part that decides what a run measured is
# testable at a desk - the same split per_window.py and per_measure.py make.
#
# WHY THIS EXISTS RATHER THAN A HUMAN READING THE TOPIC. A `--gap 250` burst is ~240
# records and the answer is a pattern across them. Reading that by eye is how a cluster
# of three gets called scattered.
#
# WHAT THE WHOLE THING IS FOR. Two candidates for the receive knee are closed and both
# were closed by TOTALS - rx_no_interrupt over a burst, rx_deaf_ms over a window. What
# those cannot answer is POSITION. A flood's frames carry an incrementing status `seq`,
# so a gap in the arrivals is a frame that did not get here, and the records on either
# side of it say what the bridge was doing while it did not:
#
#   the bridge transmitted inside the gap      the firmware's own half duplex
#   the radio was deaf inside the gap          a CAD, or a transmission, or both
#   the bridge was in receive throughout       NOT this firmware - the chip or the air
#
# The third column is the one that matters. If the losses fall there, the receive path's
# remaining suspects are the SX1262's buffer handling and the RF environment, and neither
# is answerable from the bridge's own accounting.
#
# W11 - GROUP BY (peer, type) BEFORE READING A GAP AS A LOSS. A `PING` responder echoes
# the INITIATOR's `seq` (spec 6.6), so a node's PING answers carry values from the
# bridge's sequence space and land in the middle of that node's status numbering. Mixing
# them invents losses on a link that lost nothing. Grouping by type separates them; the
# PING stream's own gaps are reported apart, because a gap there means something else.

# Every key this module reads out of a published batch. Named, so a typo reads as a
# KeyError rather than as a clean run that found nothing.
K_LOST = "lost"
K_FRAMES = "f"
K_INDEX = "i"
K_MS = "ms"
K_DEAF = "deaf"
K_DIR = "d"
K_PEER = "peer"
K_TYPE = "type"
K_SEQ = "seq"
K_STATUS = "st"
K_RX = "rx"

DIR_RX = "rx"
DIR_TX = "tx"

# lran::Status::Ok. A record carrying anything else is a frame that arrived and was
# discarded, which is a different event from one that never arrived.
STATUS_OK = 0

# frame_log.h's kStatusNotRun - the ladder never saw the frame.
STATUS_NOT_RUN = 0xFF

# spec 5.4 - `seq` is a uint16 and wraps.
SEQ_SPACE = 1 << 16

# A jump larger than this is not a run of losses. spec 10.3's resync resets a node's
# sequence to 1 and a reboot does the same, so a delta of tens of thousands is a NEW
# SEQUENCE, not 40 000 lost frames. Refusing to call it a loss is the same rule
# per_window.py applies to a counter that went backwards: a plausible wrong number is
# worse than a refusal.
MAX_CREDIBLE_GAP = 1000


class Stream:
    """One (peer, type) sequence space, and what happened in it."""

    def __init__(self, peer, msg_type):
        self.peer = peer
        self.type = msg_type
        self.arrivals = 0
        self.losses = []  # one entry per gap - see Loss
        self.resyncs = 0  # a `seq` jump too large to be loss (spec 10.3)
        self.duplicates = 0  # the same `seq` twice: a retry, or an RF echo

    @property
    def lost_frames(self):
        return sum(loss.count for loss in self.losses)

    @property
    def expected(self):
        return self.arrivals + self.lost_frames


class Loss:
    """A gap in one stream's `seq`, and what the bridge was doing across it."""

    def __init__(self, after_seq, count, dt_ms, deaf_ms, tx_inside):
        self.after_seq = after_seq
        self.count = count
        self.dt_ms = dt_ms  # between the arrivals either side of the gap
        self.deaf_ms = deaf_ms  # rx_deaf_ms accumulated across the same interval
        self.tx_inside = tx_inside  # the bridge transmitted during it

    @property
    def deaf_fraction(self):
        """Share of the gap the bridge's radio was out of receive. 0.0 when it was not.

        AND THIS IS AN UPPER BOUND ON THE BRIDGE'S RESPONSIBILITY FOR THE GAP, which is
        the only honest thing to do with it. `deaf_ms` is a total and says nothing about
        WHERE in the gap the deafness fell, so the chance it covered the instant the
        missing frame would have arrived is at most its share of the gap. A run whose
        gaps are 612 ms with 21 ms of deafness in them puts that ceiling at 3.4 %, and
        calling such a gap "the bridge was not listening" is the confident and wrong
        verdict this module exists to refuse.

        A TRANSMISSION NEEDS NO SEPARATE TERM. rx_deaf_ms already counts CAD and
        transmission together (rx_deaf.h), so a transmit inside the gap is in this
        figure. `tx_inside` says what KIND of deafness it was; the arithmetic is here.
        """
        return (float(self.deaf_ms) / self.dt_ms) if self.dt_ms > 0 else 0.0

    @property
    def attributable_frames(self):
        """Frames in this gap the bridge's deafness could account for, at most."""
        return self.count * self.deaf_fraction


def seq_delta(prev, cur):
    """`cur - prev` in the uint16 sequence space (spec 5.4), wrap-correct."""
    return (cur - prev) % SEQ_SPACE


def merge(batches):
    """Batches to one ordered record list, with what went missing on the way.

    Returns (records, ring_lost, transport_lost).

    THE TWO LOSSES ARE DIFFERENT AND MUST NOT BE ADDED TOGETHER. `ring_lost` is what the
    bridge's ring overwrote before log_task drained it - a bridge that fell behind, and
    a defect. `transport_lost` is a record that left the bridge and did not reach here -
    QoS 0, a broker restart, a subscriber that joined late. Only the first says anything
    about the firmware under investigation, and reporting the sum would attribute a
    dropped MQTT message to the receive path.
    """
    by_index = {}
    ring_lost = 0
    for batch in batches:
        ring_lost = max(ring_lost, int(batch.get(K_LOST, 0)))
        for rec in batch.get(K_FRAMES, []):
            by_index[int(rec[K_INDEX])] = rec

    if not by_index:
        return [], ring_lost, 0

    records = [by_index[i] for i in sorted(by_index)]

    # The bridge numbers every record it makes, so a hole in the indices is a record
    # that existed and is not here. Subtracting the ring's own admission leaves what the
    # transport lost.
    span = records[-1][K_INDEX] - records[0][K_INDEX] + 1
    missing = span - len(records)
    transport_lost = max(0, missing - ring_lost)
    return records, ring_lost, transport_lost


def analyze(records):
    """Records to per-stream loss.

    Returns {(peer, type): Stream}. Only a DELIVERED reception advances a stream: a
    frame that arrived and was discarded is counted by the receive ladder already, and
    folding it in here would report it twice under two different names.
    """
    streams = {}
    for i, rec in enumerate(records):
        if rec[K_DIR] != DIR_RX or int(rec[K_STATUS]) != STATUS_OK:
            continue

        key = (int(rec[K_PEER]), int(rec[K_TYPE]))
        stream = streams.get(key)
        if stream is None:
            stream = Stream(*key)
            streams[key] = stream

        if stream.arrivals == 0:
            stream.arrivals = 1
            stream._last = rec
            stream._last_pos = i
            continue

        prev = stream._last
        delta = seq_delta(int(prev[K_SEQ]), int(rec[K_SEQ]))
        stream.arrivals += 1

        if delta == 1:
            pass
        elif delta == 0:
            stream.duplicates += 1
        elif delta > MAX_CREDIBLE_GAP:
            stream.resyncs += 1
        else:
            stream.losses.append(
                Loss(
                    after_seq=int(prev[K_SEQ]),
                    count=delta - 1,
                    dt_ms=int(rec[K_MS]) - int(prev[K_MS]),
                    # rx_deaf_ms is cumulative, so the deafness BETWEEN two records is
                    # the difference of their fields - which is the whole reason the
                    # running total is in every record rather than published on its own.
                    deaf_ms=int(rec[K_DEAF]) - int(prev[K_DEAF]),
                    tx_inside=_transmitted_between(records, stream._last_pos, i),
                )
            )
        stream._last = rec
        stream._last_pos = i

    for stream in streams.values():
        for attr in ("_last", "_last_pos"):
            if hasattr(stream, attr):
                delattr(stream, attr)
    return streams


def _transmitted_between(records, prev_pos, pos):
    """True when a Tx record sits between two arrivals of the SAME stream.

    Bounded by the two positions rather than by "walk back to the previous delivered
    frame", which was wrong the moment a second node transmitted: another peer's frame
    arriving inside the gap would end the walk early and hide the bridge's own
    transmission behind it. This bench has one sender, so the difference never showed -
    which is exactly why it is worth fixing before one with two does.
    """
    for rec in records[prev_pos + 1:pos]:
        if rec[K_DIR] == DIR_TX:
            return True
    return False


def verdict(streams):
    """The figure the whole instrument exists to produce.

    Returns (total_lost, tx_gaps, attributable). `attributable` is the CEILING on how
    many of the lost frames the bridge's own deafness could account for - the sum of
    each gap's deaf share, which is that gap's upper bound.

    A CEILING RATHER THAN A CLASSIFICATION, AND DELIBERATELY NO THRESHOLD. Splitting the
    losses into "the bridge's" and "not the bridge's" needs a line drawn at some deaf
    fraction, and every value for that line is an assumption about where in the gap the
    deafness fell - which is exactly what the instrument cannot see. A ceiling needs no
    such assumption: 2 lost frames against a ceiling of 0.07 says the transmit path
    cannot be the story here, with nothing taken on faith.
    """
    total = 0
    tx_gaps = 0
    attributable = 0.0
    for stream in streams.values():
        for loss in stream.losses:
            total += loss.count
            attributable += loss.attributable_frames
            if loss.tx_inside:
                tx_gaps += 1
    return total, tx_gaps, attributable
