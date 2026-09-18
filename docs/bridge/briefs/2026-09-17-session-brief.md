# Bridge session brief — 2026-09-17

**Document:** `LRAN-Bridge-Session-Brief-2026-09-17`
**Status:** **Dated record.** It describes the bridge at the end of the session that built
BF-27's frame log, and it is not updated. [`HANDOFF.md`](../HANDOFF.md) and the
[engineering log](../engineering-log.md) are the current record, and where this brief
disagrees with them, they win.
**Source:** the session's published brief, converted to Markdown on 2026-09-18. "I" is
the Claude Code session that wrote it.
**Prepared from:** commit `24879ca`

> **What was removed in the conversion.** The original listed each pull request's base
> branch and merge state, and its first recommendation was a merge order. Root `CLAUDE.md`
> keeps branch and PR state out of every document, because it is false by the time anyone
> reads it, so those parts are not here. Everything else is as the session wrote it.

## Where the bridge stands, and what is actually unresolved

BF-27's frame log shipped and answered its question. In doing so it undermined the
premise the last three sessions were built on.

- **Nothing was left half-finished.** Every task started this session is committed. What
  is "paused" is a decision I was not entitled to make, not work I abandoned.
- **BF-27's raw frame log is built and it closed the transmit path per frame** — a
  ceiling of 0.52 frames against nine lost.
- **The 1 s spacing knee did not reproduce.** Ten bursts put 250 ms at 2.50 % and
  2000 ms at 1.90 %. That morning the same arms read 5.6 % and 0 %.
- **The open question changed shape.** It is no longer "why does a closely-spaced frame go
  missing" but "is spacing the variable at all" — and no experiment on record can answer
  that.
- **One decision is genuinely yours and blocks two tasks** — the runtime configuration
  route.

## What shipped

Three bodies of work, each on its own pull request:

| PR | Contents |
|---|---|
| #73 | **Deaf-time instrument.** `cad_free` and `rx_deaf_ms`, the `enter_mode()` single point of change, plus the measurement that ruled out the transmit path in aggregate |
| #74 | **BF-23 discovery.** Home Assistant discovery generation, `json_writer.h`, 26 generated `/ha/` example payloads with a CI check that fails when they drift from the firmware |
| #75 | **BF-27 frame log.** The ring, the drain, `rxlog.py`, the ten-burst measurement, and the documentation the result forced |

### This session's work, specifically

- **Firmware.** `frame_log.{h,cpp}` — a lock-free single-producer ring recording every
  frame in and out with source, type, schema, RSSI, SNR, discard reason and the running
  `rx_deaf_ms`. `log_task` drains it to serial and to `lran/bridge/diag/rxlog/state`; it
  had been an idle `vTaskDelay` loop since BF-11.
- **Tooling.** `rxlog.py` reads the topic, `rxlog_analyze.py` does the arithmetic with no
  I/O, matching the `per_measure`/`per_window` split. 36 host tests, wired into CI.
- **Tests.** Bridge host suite 192 → 212. Full native total 471.
- **Measurement.** Ten bursts, 385 flood frames, committed as a re-readable session record
  under `docs/bridge/data/`.
- **Documents.** Implementation Plan §6.6.1 (v0.38), Tasks v0.27, an engineering-log
  entry, both context files, and the handoff.

## What the measurement established

Two candidates for the receive losses were already closed by earlier sessions, both by
*totals*: `rx_no_interrupt` across a burst, and `rx_deaf_ms` across a window. A total
cannot say anything about *which* frames went missing, which is what was left.

### The transmit path is closed, now per frame

Of the nine frames lost across ten bursts, the bridge's own deafness can account for **at
most 0.52**. That is a ceiling, not an estimate: `rx_deaf_ms` is a total and never says
*where* in a gap the deafness fell, so its share of the gap is the most of that gap it
could have covered. Four of those gaps contain a bridge transmission and still cannot
account for their losses — the largest single contribution is 11 % of one gap.

### Three cross-checks came free, and all three are clean

- **376 receptions, every one announced by its own interrupt.** Zero found by the timed
  read — `rx_no_interrupt` confirmed per frame rather than per burst.
- **Zero corrupt.** No PHY CRC failure, no header error, no driver error.
- **All 376 passed the receive ladder.** Nothing discarded at any spec §14 stage.
- **RSSI −39 to −36 dBm, SNR +10 to +12 dB** across twenty minutes. Three dB of spread.
  The link is not marginal in any ordinary sense.
- **The ring never overwrote a record**, so none of the above is the instrument losing its
  own output.

Every frame that reached the radio was delivered perfectly. The lost ones never reached it
at all.

## The finding nobody was looking for

The same ten bursts failed to reproduce the spacing curve that the whole investigation has
been framed on.

| # | Arm | Frames | Lost | PER | Uptime |
|--:|---|--:|--:|--:|--:|
| 1 | gap 250 ms | 40 | 2 | 5.00 % | 163 s |
| 2 | gap 250 ms | 40 | 3 | 7.50 % | 355 s |
| 3 | gap 2000 ms | 25 | 2 | 8.00 % | 411 s |
| 4 | gap 2000 ms | 40 | 0 | 0 % | 551 s |
| 5 | gap 2000 ms | 40 | 0 | 0 % | 677 s |
| 6 | gap 250 ms | 40 | 0 | 0 % | 791 s |
| 7 | gap 250 ms | 40 | 0 | 0 % | 859 s |
| 8 | gap 250 ms | 40 | 0 | 0 % | 904 s |
| 9 | gap 250 ms | 40 | 0 | 0 % | 950 s |
| 10 | gap 250 ms | 40 | 2 | 5.00 % | 998 s |

**Pooled: 250 ms gives 7 losses in 280 frames (2.50 %); 2000 ms gives 2 in 105
(1.90 %).** At this sample size those are indistinguishable — and that is the point. The
morning's result was a *large* separation, 5.6 % against 0 %, and it has not held.

Every loss falls in runs 1–3 and run 10. Runs 4–9 are clean and span both spacings. A
2000 ms control produced 8 % and a 250 ms arm produced 0 %, in the same session, on the
same boards. Within a lossy run the losses sit close together — three and four frames
apart at 250 ms, five apart at 2000 ms.

> **This retracts nothing.** The morning's runs measured what they measured, and dated
> records are not rewritten. What changed is what can be inferred from them.
>
> **The confound is real and it is in every sweep on record, including mine.** Each ran
> one spacing to completion before starting the next. A slow change in the environment and
> an effect of spacing produce the same data under that design, and nothing on file
> separates them.

What turns on it: if the knee is environmental, then the 1 s threshold recorded that
morning — and the `backoff_max_ms` reasoning that leaned on it — describe a quiet
afternoon rather than a property of this firmware.

## What is unresolved

### The runtime configuration route — yours to decide

Three options, not equivalent, and I will not pick one by default because each commits the
fleet to something different:

- **A general `config/set`** with `/lib/lran-config/` behind it — commits a fleet-wide
  interface before its callers exist.
- **A narrow single-purpose topic** for the diagnostic interval alone — freezes a
  Home-Assistant-visible token that cannot be renamed later.
- **A serial-only lever**, like BF-26's deferred fallback — leaves root rule 8 unmet on a
  node that cannot be reflashed without a walk to the gate.

It blocks **BF-23's timing-lever half** and **BF-26**, and through BF-23 it blocks
**V-B12's saturated arm**. Spec §16.2.1 leaves the payload undefined deliberately: *"a
payload specified before its first caller is a guess carrying a version number."*

### Is spacing the variable? — needs one experiment

Answerable in about thirty minutes of bench time with no new code: alternate 250 ms and
2000 ms bursts inside one session, `rxlog.py` collecting. Blocked runs cannot answer it;
interleaved ones can.

### Spec §16.2's retention rule — needs a spec decision

The frame log publishes **unretained** where §16.2's table says a `/state` leaf is
retained. A retained frame log replays a finished burst as though it were arriving now —
§16.3's own argument reaching a topic §16.3 does not cover. I did the non-misleading thing
and raised the deviation rather than settling it in firmware. §16.2's table predates any
streaming diagnostic and I think it needs an amendment.

### R-3.2c is unmet — found, now recorded

The PRD says WiFi RSSI shall be published as a diagnostic. `wifi_rssi_dbm()` exists and is
read into the OLED status page and **nowhere else** — no MQTT topic carries it. Verified by
reading every caller. It belongs in BF-24's table, whose row does not mention it. This was
raised in conversation last session and written down nowhere; it is now in the handoff
under *work no task owns*.

### Carried over, not caused here — pre-existing

- **`/lib/lran-config/` does not exist and has no task** — the root of the decision above.
- **The Implementation Plan cites PRD v0.6; the PRD is at v0.12.** Deliberately not bumped
  — §§2.1, 2.2, 3.2, 7.1 and 8 need reconciling first. The more useful half is *why
  nothing caught it*: the citation check covers protocol-spec citations only, so no check
  reads a PRD-to-plan or plan-to-tasks citation.
- **BF-27's other three tools** — dummy publish, bridge-side simulators, packet loopback.
  Untouched, and blocking nothing.
- **Six task stacks unmeasured**; the first-attempt `AUTH_FAIL` at every boot remains
  unexplained.

## Recommendations

1. **Run the interleaved sweep before BF-24, not after.** Thirty minutes, no code, and it
   decides whether three sessions of reasoning about a 1 s threshold stand. BF-24 decodes
   a fragmented `STATUS` — the exact traffic pattern in question — so going first means
   debugging one problem instead of two. If the losses turn out to be environmental and
   bursty, that also changes what `poll_reply_timeout_ms` and `backoff_max_ms` should be
   justified by.
2. **Decide the config route, or defer it explicitly with a task number.** If you want my
   read: the **narrow single-purpose topic** is the worst of the three, because it spends
   the one thing that cannot be taken back — an HA-visible token — to buy the least.
   Between the other two, the serial-only lever is cheap and reversible and leaves root
   rule 8 visibly unmet, which is a better failure than a fleet-wide interface designed
   without callers. But this is a fleet-interface commitment and it should be your call,
   not a default I inherited.
3. **Raise the §16.2 retention amendment against the specification.** The firmware
   currently does something the spec's table does not permit. That is exactly the
   situation the repo's rule says to surface rather than paper over, and it will recur for
   any future streaming diagnostic.
4. **Add R-3.2c to BF-24's scope before starting it.** It is a one-line publication once
   the policy exists, and it is the kind of requirement that stays unmet for a year because
   no row names it.
5. **Consider extending the citation check to document-to-document citations.** Two stale
   citations were found by hand this session — the plan's PRD reference and the context
   file's plan reference. The mechanism that catches protocol-spec drift does not read
   these, so they are found only when someone happens to look.

## Bench and verification

Three boards connected. The bridge runs `1375c3f`, confirmed on `lran/bridge/version` —
not `-dirty`, so flashed from a clean tree. The XIAO had rebooted between sessions and
lost its hand-built identities; they were rebuilt (`id add f3 ROLE_FAULT`, `disable f1`)
with a fresh context. **Check `id list` before believing any run** — that trap cost real
time this session and is now in the handoff.

The whole ten-burst session re-reads with no board and no broker:

```bash
python3 tools/simctl/rxlog.py --read docs/bridge/data/bf27-framelog-session-2026-09-17.json
```

Two bugs in my own tooling were found by running it and are fixed with regression tests:
it first called 21 ms of deafness inside a 612 ms gap "the bridge was not listening" (it
reports a ceiling now), and its transmit search stopped at any peer's frame rather than the
same stream's — invisible on a one-sender bench, wrong on any other.
