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
    def explained_by_the_bridge(self):
        """True when this firmware was demonstrably not listening for some of the gap.

        DEAF TIME AND A TRANSMISSION ARE BOTH ACCEPTED AS EXPLANATIONS, and neither is
        proof: the radio being out of receive for 200 ms of a 250 ms gap makes the loss
        the bridge's, while 2 ms of it does not. The threshold is the caller's, because
        it depends on the spacing being swept. What this property claims is only that
        the bridge is a CANDIDATE for this gap - the interesting number is how many gaps
        it is not a candidate for at all.
        """
        return self.tx_inside or self.deaf_ms > 0


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
                    tx_inside=_transmitted_between(records, i),
                )
            )
        stream._last = rec

    for stream in streams.values():
        if hasattr(stream, "_last"):
            del stream._last
    return streams


def _transmitted_between(records, i):
    """True when a Tx record sits between records[i] and the Rx record before it.

    Walks back over the records the log ordered between the two arrivals, which is what
    makes this a per-gap answer rather than a correlation across a whole burst.
    """
    for rec in reversed(records[:i]):
        if rec[K_DIR] == DIR_TX:
            return True
        if rec[K_DIR] == DIR_RX and int(rec[K_STATUS]) == STATUS_OK:
            return False
    return False


def verdict(streams):
    """The split the whole instrument exists to produce.

    Returns (total, bridge_candidate, receiving_throughout). The third is the one to
    read: a loss the bridge cannot be blamed for, because its own radio was in receive
    for every millisecond of the gap.
    """
    total = 0
    candidate = 0
    for stream in streams.values():
        for loss in stream.losses:
            total += loss.count
            if loss.explained_by_the_bridge:
                candidate += loss.count
    return total, candidate, total - candidate
