# Bridge Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Impl Plan §5.3 names this file.

**Entries from 2026-09-10 to 2026-09-16 are in
[`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md)**,
moved there unedited on 2026-09-18. A document citing an engineering-log entry by one of
those dates means that file. Root `CLAUDE.md` says when the log is split next.

---

## 2026-09-17 — M22's idle arm: 5.6 % PER at one metre, and no explanation that fits

**B3b is accepted and V-B12 moved to B4** (Impl Plan §8.1). Before it moved, its idle arm
was measured, because it is the baseline the saturated arm is compared against and nothing
blocked it. **The number is worse than expected and the cause is not established.** Both
facts are the point of this entry.

### The run

`tools/simctl/per_measure.py`, new today. Bridge on `24f7993`, XIAO + Wio simnode sending,
Heltec simnode quieted first. Five bursts of 50 frames, 250 ms apart, from **`0xF3` in
`ROLE_FAULT`** — a role that answers no `POLL`, so the only frames that identity sends are
the burst. Every other identity on both boards was disabled, so nothing else was on air.

```
PER 5.60 % over 250 frames in 5 valid window(s); worst burst 10.00 %
  never heard 14, corrupt 0, bridge transmissions in window 19
```

| burst | sent | accepted | never heard | bridge TX | bridge `cad_backoffs` |
|---|---|---|---|---|---|
| 1 | 50 | 45 | 5 | 3 | 8 |
| 2 | 50 | 46 | **4** | **0** | **0** |
| 3 | 50 | 49 | 1 | 7 | 5 |
| 4 | 50 | 49 | 1 | 3 | 0 |
| 5 | 50 | 47 | 3 | 6 | 5 |

Committed as `docs/bridge/data/m22-idle-2026-09-17.json`.

### What the numbers rule out

**`corrupt 0` across all 250 frames.** Not one frame arrived and failed its CRC:
`rx_crc_err` never moved, and neither did any other §14 counter. Every loss is a frame the
radio never delivered at all. At one metre, with −4 dBm into a 3.0 dBi antenna, an RF
explanation does not fit — and there is no marginal-link story that produces zero corrupt
frames alongside 14 missing ones.

**Burst 2 rules out the bridge's own media access as a complete explanation.** The obvious
first theory was half duplex: the SX1262 cannot hear a frame arriving while it answers a
`POLL`, and a CAD takes the radio out of receive (Impl Plan §747, and the 2026-09-10 entry
above). Burst 2 has **zero transmissions and zero CAD backoffs** and still lost 4 frames of
50. Across the five bursts the losses do not track either column.

**What `cad_backoffs` does not count is worth knowing before the next attempt.** It counts
a *busy* CAD result only (`media_access.cpp`). A CAD that returns free still took the radio
out of receive and increments nothing, and `cad_deferred` covers only the
reception-in-progress case. So a zero in that column is not a claim that the radio stayed
in receive — it is a claim that no CAD found the channel busy.

### What is left, and not tested today

**Receive turnaround is the remaining candidate.** At SF9 a frame of this size runs roughly
250–330 ms, and the injector's gap is measured from when it fired, so the frames are close
to back to back. Whether the bridge can read one frame and be listening again before the
next one starts is untested. A control at a 2000 ms gap was started at the end of the
session to separate that from link PER; **its result is not in this entry.**

**This is not a WiFi measurement and must not be read as one.** The saturated arm does not
exist on this firmware — nothing reaches `g_diag_interval_s` at runtime — so today's figure
says nothing about R-4.4 either way. **M22 stays open.** What it now has is a baseline, an
instrument, and a narrowed question.

### Two decisions worth keeping

- **PER is pooled over frames, never averaged over bursts.** Averaging these five bursts
  gives 4.4 %; pooling gives 5.6 %. The difference is small here and would not be with
  uneven burst sizes, where a short burst's single loss becomes the headline.
- **A window that cannot carry a figure returns a reason instead of one.** Three guards, all
  host-tested: a counter that went backwards (the bridge rebooted and zeroed everything),
  more frames accepted than were sent (something else transmitted into the window), and
  nothing on the air. The second one is the handoff's *"difference a counter only across a
  window carrying nothing else"* made mechanical.

---

## 2026-09-17 — the control: 0 % at a 2000 ms gap, so the 5.6 % is spacing, not the link

**The wide-gap control the entry above left open has run, and it answers the question.**
Same instrument, same identity, same bridge image:

```
PER 0.00 % over 40 frames in 2 valid window(s); worst burst 0.00 %
  never heard 0, corrupt 0, bridge transmissions in window 6, bridge CAD backoffs 0
```

| gap | frames | never heard | PER |
|---|---|---|---|
| 250 ms | 250 | 14 | **5.60 %** |
| 2000 ms | 40 | 0 | **0.00 %** |

Committed as `docs/bridge/data/m22-idle-control-gap2000-2026-09-17.json`. The bridge
transmitted in both control windows (6 times across the two), so this is not a quiet-bench
artefact — it lost nothing while doing the same work it was doing during the 250 ms run.

**Two things differed from the 250 ms run, not one, and the second is worth stating rather
than glossing.** The control was run with `--keep-others`, so `0xF1` stayed enabled on the
XIAO instead of being disabled. It contributed nothing: `accepted` equalled `sent` exactly
in both windows, and a single frame from another identity would have made `accepted` exceed
`sent` and tripped the guard that refuses the window. So the comparison holds, but it holds
because of a guard rather than because the runs were identical. **A repeat should disable
the others in both arms.**

**The losses are a function of inter-frame spacing.** At SF9 a frame of this size runs
roughly 250–330 ms, and the injector measures its gap from when it fired, so at `gap 250`
the frames are close to back to back and at `gap 2000` they are not. Nothing else differs.

**What this closes.** The previous entry listed three candidates and ruled out two by
measurement. The third — that the bridge cannot read one frame and be listening again
before the next one starts — is the one left standing, and the control is consistent with
it. **It is consistent with, not proof of**: this measures the bridge's behaviour at two
spacings and does not instrument the turnaround itself. What would prove it is a capture
that shows where the second frame goes, and that is not built.

**What this changes for M22, and it is the practical part.** The saturated arm must run at
a spacing whose idle PER is zero, or a WiFi effect cannot be told from this one. **`gap
2000` is a measured zero and `gap 250` is not**, so the saturated arm inherits the wide
gap and a longer run rather than the dense one. Written into the instrument's defaults is
deliberately *not* the answer — the dense case is worth keeping runnable, because it is the
only thing that has made this visible.

**What it does not change.** The bridge still drops frames offered back to back on a clean
bench at one metre, and nothing in the protocol prevents a node from sending that way — a
fragmented `STATUS` is exactly that pattern. **That is a real question about the receive
path, and it is not M22's.** It is recorded in the handoff's *Open* section rather than
being folded into a WiFi measurement that would obscure it.

---

## 2026-09-17 — the threshold is at 1 s, not at frame airtime, and that names a constant

**Backing the gap off in steps changed the conclusion**, which is why the sweep was worth
running rather than stopping at a spacing that happened to give zero. Same instrument, same
identity, same bridge image, 100 frames per point:

| gap | frames | never heard | corrupt | PER | bridge `cad_backoffs` |
|---|---|---|---|---|---|
| 250 ms | 250 | 14 | 0 | **5.60 %** | 18 |
| 400 ms | 100 | 5 | 0 | **5.00 %** | 11 |
| 700 ms | 100 | 1 | 0 | **1.00 %** | 8 |
| 1100 ms | 100 | 0 | 0 | **0.00 %** | 0 |
| 2000 ms | 40 | 0 | 0 | **0.00 %** | 0 |

Committed as `docs/bridge/data/m22-idle-sweep-gap{400,700,1100}-2026-09-17.json`.

### What the shape rules out

**It is not frame airtime.** At SF9 a frame this size runs roughly 250–330 ms, so the
obvious theory was that the bridge needs about one frame time to turn around. **At 400 ms
the frames are no longer back to back and the loss rate did not move** — 5.00 % against
5.60 %. A turnaround cost of one frame time would have collapsed there, and it did not.

**The losses fall away between 700 ms and 1100 ms.** That brackets **`kIrqReadMs`, which
is 1000** (`lora_link.cpp`). `service_receive` runs when DIO1 has fired **or** when that
poll interval has elapsed:

```cpp
if (!g_dio1 && elapsed(now_ms, g_last_irq_read_ms) < kIrqReadMs) return;
g_dio1 = false;
```

**`g_dio1` is a single `bool`, not a count.** Two frames whose interrupts both land before
one `service_receive` pass collapse into one pass, which reads one packet — and `readData()`
clears the SX1262's IRQ register, so the second frame's `RX_DONE` goes with it. The flag is
false again, so the next opportunity is the next DIO1 or the 1 s poll. Above 1100 ms every
frame gets its own pass regardless; below it, consecutive frames can share one.

### What this is, and what it is not

**It is a correlation with a named constant, not a proof.** The measurement shows where the
losses stop; it does not show a frame being dropped at that line. **What would prove it:**
change `kIrqReadMs` and re-run the sweep — if the knee moves with the constant, the
mechanism is this one. A counter on the path would settle it outright, and none exists.
**Neither is built, and this is recorded as unproved on purpose.**

**Media access is not the explanation, and the `cad_backoffs` column should not be read as
one.** It tracks offered load, because denser traffic makes a CAD more likely to find the
channel busy — so it falls with the gap for reasons that have nothing to do with the losses.
The discriminator remains the 250 ms run's burst 2: **zero transmissions, zero CAD backoffs,
four frames lost of fifty.**

### What it changes

- **M22's saturated arm runs at `--gap 2000`**, unchanged. 1100 ms is the measured knee and
  2000 ms is the margin either side of it; there is no reason to sit on the edge.
- **The receive path has a real question against it**, and it now has a number and a
  suspect rather than a shrug. A fragmented `STATUS` is exactly this traffic pattern, and
  **BF-24's decode work is what will meet it first**.
- **`gap` below about 1 s is not a valid bench configuration for anything that counts
  frames.** Two of the §10.5 catalogue's rows drive multi-frame injections; they judge
  counters rather than totals and are unaffected, but a future row that counts arrivals
  would be measuring this instead.

## 2026-09-17 — DIO1 is a level output read on its rising edge, and that is a mechanism, not a correlation

**Read out of the pinned driver, not measured.** The 1 s knee entry above named `kIrqReadMs`
as a suspect on the strength of where the losses stop. Reading RadioLib 7.7.1 turns the
suspicion into a described mechanism — one that is still unconfirmed on hardware, and that
now has a counter pointed at it.

### What the driver does

**`SX126x::setDio1Action` attaches on the rising edge** (`SX126x_config.cpp`:
`GpioInterruptRising`). **DIO1 itself is a level output**: the SX1262 holds it asserted for
as long as a masked interrupt is set and drops it only when `clearIrqStatus()` runs. The
mask `startReceive()` installs is `RADIOLIB_IRQ_RX_DEFAULT_MASK` — **`RX_DONE` alone**.

So a second `RX_DONE` raised while the first is still pending produces **no edge**. The line
never went low, so there is nothing to rise, and `on_dio1` never runs for that frame. The
frame sits in the radio's buffer with nothing to announce it.

**`readData()` clears the whole register** (`SX126x.cpp`), which is what eventually drops the
line — and takes any second frame's `RX_DONE` with it.

### What that makes `kIrqReadMs`

**The timed read is not belt and braces here; it is the only thing that finds such a frame.**
That reframes the constant: it is not a redundant poll behind a working interrupt, it is the
deadline a frame has to beat. A frame whose edge was swallowed survives if the timed read
reaches it before the next `readData()` clears the register, and is lost if it does not.

**This predicts the measured curve** — losses below a spacing of about 1 s, none above it —
**from the constant rather than from the constant's neighbourhood.** It also explains why the
knee is not at frame airtime, which was the hypothesis the 400 ms sweep point refuted.

**One thing the timed read is not optional for.** `HEADER_ERR` is not in the DIO1 mask, so
spec §14 stage 1's header half can only ever be found by the timed read. Removing the read
would stop counting those discards entirely, which root rule 4 forbids. **Any fix has to keep
a path that reads the register without an edge.**

### The counters, and what would falsify this

Two, in `LoraStats` on `lran/bridge/diag/radio/state`, with the classification lifted into
`rx_wake.h` so it has host tests:

- **`rx_no_interrupt`** — `RX_DONE` found by the timed read with no edge behind it. **A frame
  the interrupt path missed and the timer recovered.**
- **`rx_wake_empty`** — an edge arrived and the register held neither `RX_DONE` nor
  `HEADER_ERR`. The frame that raised it was cleared by an earlier `readData()`.

**Bridge-local, not spec §14.1**, decided rather than left open: §14.1 is a normative
registry of receive-ladder *discards* carried on the wire by schema `0xF0`. Neither of these
is a discard, neither has a `Status` or a stage, and both describe a driver no node need
share. `queues.h` reasons the same way about queue overflow.

**What falsifies the mechanism: `rx_no_interrupt` at zero during a 250 ms burst that still
loses frames.** That would say every frame got its own interrupt and the losses are
somewhere else entirely — and it would kill the `kIrqReadMs` story outright rather than
weakening it.

**What confirms it: `rx_no_interrupt` non-zero at *every* spacing, including the 2000 ms
control where PER is 0.** Missed edges would then be constant and the *recovery* interval
would be what separates a run that loses frames from one that does not. That is a sharper
claim than "the knee moved", and it is readable at the broker in one burst.

**Neither reading has been taken.** The bridge board is running `24f7993` and this needs a
USB reflash.

## 2026-09-17 — the falsifier fired: `kIrqReadMs` is not the mechanism

**`rx_no_interrupt` read zero across 190 frames spanning both arms, including the arm that
lost frames.** The entry above named the reading that would kill its own hypothesis, and
that is the reading that came back. **The `kIrqReadMs` story is dead as stated**, and this
entry supersedes the two above it on the mechanism while leaving their measurements intact.

The bridge board was reflashed from a clean tree at `4c83f3d` for these runs.

### The two runs

| Arm | Frames | Lost | PER | Bridge TX | CAD backoffs | `rx_no_interrupt` | `rx_wake_empty` |
|---|---|---|---|---|---|---|---|
| `--gap 250`, 3 × 50 | 150 | 4 | **2.67 %** | 10 | 11 | **0** | **0** |
| `--gap 2000`, 2 × 20 | 40 | 0 | **0 %** | 9 | 2 | **0** | **0** |

`rx_crc_err`, `rx_dropped` and `rx_driver_errors` were zero in both. **The spacing effect
reproduced** — it is not an artefact of the 2026-09-16 session.

### What that rules out

**The interrupt path delivered every `RX_DONE` the radio raised, at both spacings.** Not one
frame was found by the timed read. So the timed read never had to recover anything, which
means it was never the deadline a frame had to beat — the prediction the entry above made
from the driver's edge-versus-level reading.

**The predicted confirming signature did not appear either.** That entry said missed edges
would show as `rx_no_interrupt` non-zero at *every* spacing, with the recovery interval
separating a lossy run from a clean one. Zero at both spacings says there were no missed
edges to recover.

**Bridge transmissions do not predict losses.** Across the eight `--gap 250` bursts now on
record, transmissions against frames lost run 3/5, 0/4, 7/1, 3/1, 6/3, 4/2, 3/1, 3/1 — the
burst with no transmissions lost the most, and the burst with seven lost one. The control's
second burst transmitted six times and lost nothing.

### What it does not rule out, and this is the honest limit of the instrument

**A frame whose `RX_DONE` was cleared by a neighbouring `readData()` before any pass looked
is invisible to both counters.** In that case the first frame's edge is real, so the pass is
classified `Packet` and `rx_wake_empty` stays zero too. **The counters were built to catch
the recovery path and they cannot see this one.**

**It is too small to be the whole story.** That window is one `readBuffer` SPI transaction —
of order 1–2 ms against a 250–550 ms spacing, so roughly 0.3 % against a measured 2.67 %.
It is an order of magnitude short. **Stated as arithmetic, not as a dismissal:** it could be
a component.

### The rest of the radio document, read once at the end of both runs

`cad_deferred` **15**, `cad_backoffs` **18**, `tx_forced` **2**, `tx_frames` **32**,
`q_rx_high_water` **1**, every queue drop **0**, every §14.1 counter **0**, `rx_frames`
**186** against 190 sent.

Three things follow. **The RX queue was never under pressure** — depth 1 at its worst, so
nothing was lost after the ladder. **The media-access path was busy even though few
transmissions completed**: 15 CADs were not started because a reception was in progress,
which is the guard working. And **`tx_forced` fired twice** — spec §12.3's transmit-regardless,
the one path on which the bridge deliberately transmits into a channel it has not cleared,
and therefore the one that can destroy an arriving frame. Two of them against four lost
frames is not an explanation, but it is the only counter on record that describes the
bridge knowingly deafening itself.

### What is left

**A frame the radio never reported at all.** Not corrupt, not discarded, not queued and
dropped, not missed by the interrupt — and more likely the closer the frames are spaced.
Nothing in the firmware currently sees below `RX_DONE`.

**Two things would separate the remaining candidates, and neither is built:**

1. **The raw frame log — BF-27**, already a `TODO` in `service_receive`. Flood frames carry
   an incrementing status `seq` (`fault.cpp`), so a log of arrivals says **which** frames go
   missing. Scattered points at the chip or the air; clustered after a bridge action points
   at the firmware.
2. **A burst with the bridge provably never leaving receive.** Every transmit is preceded by
   a CAD, and **`cad_backoffs` counts only a *busy* CAD** — a free one still takes the radio
   out of receive and increments nothing. Running the flood with no node for the bridge to
   poll removes the whole path rather than measuring around it.

**Run 2 first.** It is one bench session with no firmware change, and it settles whether the
answer is above or below the driver.

### What stands from the entries above

- **The measurements.** 5.6 % at 250 ms falling to 0 % at 1100 ms, five spacings, nothing
  corrupt at any of them. Today's runs reproduce the endpoints.
- **The bench rule.** A measurement that counts frames still has to space them above 1 s.
- **The `HEADER_ERR` constraint.** It is not in the DIO1 mask, so the timed read is the only
  path that finds spec §14 stage 1's header half. That is unaffected by any of this, and it
  still forbids removing the read.
- **The counters stay.** They are cheap, they are published, and they turned a suspicion
  into a closed question in one bench session. Root rule 4 wanted them regardless.

## 2026-09-17 — the transmit path is ruled out: the same deaf time loses 6 % and 0 %

**The radio spends 1.07–1.10 % of every window out of receive, and that figure does not
move when the PER moves from 6 % to 0 %.** Media access does not explain the knee. This
entry closes the second of the two steps the entry above it named, and it supersedes
nothing: the measurements there stand.

The bridge board was reflashed from a clean tree at `d2212c9`.

### What was missing, and what was built

**`cad_backoffs` counts a *busy* CAD only** — spec §12.3 made it the channel's instrument,
not the radio's. A CAD that returns free still takes the radio out of receive and
incremented nothing, so no run on record could say whether the bridge was listening when a
frame went missing.

Two numbers now sit on `lran/bridge/diag/radio/state`, bridge-local for the reason
`lora_stats.h` already gives for `rx_wake.h`'s pair:

- **`cad_free`** — the CAD outcome nothing recorded.
- **`rx_deaf_ms`** — milliseconds outside receive, CAD and transmission together, measured
  from leaving receive to `startReceive()` re-arming it.

**`rx_deaf_ms` is the one that decides it, and a count would not have.** Bridge
transmissions were already tested against losses burst by burst and predicted nothing
(the entry above). A count of CADs is the same kind of correlation; a duration can be set
against the window it was differenced over and compared with the PER measured over that
same window. The interval also carries `lora_task`'s own latency in noticing `CAD_DONE`
and re-arming, which no count reaches.

### The two runs

| Arm | Burst | Sent | Lost | PER | Bridge TX | `cad_backoffs` | `cad_free` | `rx_deaf_ms` | Window | Deaf |
|---|---|---|---|---|---|---|---|---|---|---|
| `--gap 250` | 1 | 50 | 3 | **6.00 %** | 3 | 5 | 2 | 659 ms | 59 904 ms | **1.100 %** |
| | 2 | 50 | 1 | **2.00 %** | 3 | 4 | 3 | 643 ms | 59 950 ms | **1.073 %** |
| | 3 | 50 | 1 | **2.00 %** | 3 | 4 | 3 | 647 ms | 60 179 ms | **1.075 %** |
| `--gap 2000` | 1 | 20 | 0 | **0 %** | 3 | 0 | 3 | 643 ms | 60 099 ms | **1.070 %** |
| | 2 | 20 | 0 | **0 %** | 3 | 0 | 3 | 639 ms | 59 859 ms | **1.068 %** |

`rx_no_interrupt` and `rx_wake_empty` read zero in all five, as they did on 2026-09-17's
earlier runs. Nothing was corrupt and no §14.1 counter moved.

### Why this is decisive, and it is not the fraction

**The deaf time spans 639–659 ms — a 3 % spread — while the PER spans 6 % to 0 %.** The
same three polls, the same ~1.07 % of the window, in the burst that lost three frames and
in the burst that lost none. Whatever separates a lossy burst from a clean one, the
transmit path holds the radio out of receive for the same length of time in both.

That argument does not depend on comparing 1.08 % with 3.33 %, and it is worth saying why
the comparison is avoided. **The window is 60 s and the burst occupies only part of it**, so
a deaf fraction measured over the window understates the fraction during the burst if the
deafness is concentrated there. It is not — the polls are spread across the window — but the
constancy across five bursts settles the question without needing that assumption.

### The instrument agrees with the spec, which is the check that it works

**Three `POLL`s at SF9 are 555 ms of airtime** (§15.1: 185 ms each). Measured deaf time in
the same windows is 639–659 ms. **The 84–104 ms excess is three CADs and three re-arms**,
so roughly 28–35 ms per transmit cycle, of which a SF9 CAD is a few 4.1 ms symbols. The
counter reproduces an independently computed number and the remainder is the thing it was
built to see. **`lora_task` is not slow to re-arm receive** — that candidate is answered in
passing.

### The hole `cad_free` closed, in one row

**The control's `cad_backoffs` is 0 and its `cad_free` is 3, per burst.** On the old
instrument that window read as a bridge that never contended for the channel at all, while
the radio in fact left receive three times and stayed out for 643 ms. Every control run
before today carries the same blind spot.

**A second reading follows from it.** The `--gap 250` arm shows 13 backoffs against the
control's 0 and **the same deaf time**, which says those backoffs mostly cost no receive
time — consistent with `cad_deferred`, a CAD never started because a frame was arriving.
Stated as consistency, not as measurement: the per-window `cad_deferred` deltas were not
captured.

### What is left

**A frame the radio never reported at all**, and now with one more candidate gone. Not
corrupt, not discarded, not queued and dropped, not missed by the interrupt, and **not lost
to a radio that was busy transmitting**. More likely the closer the frames are spaced.

**BF-27's raw frame log is the next step and it is now the only one on the list.** Flood
frames carry an incrementing status `seq` (`fault.cpp`), so a log of arrivals says *which*
frames go missing: scattered points at the chip or the air, clustered after a bridge action
points at the firmware. The two cheap instruments are spent, and both came back clean.

**It still matters beyond M22.** A fragmented `STATUS` is exactly this traffic pattern, and
**BF-24's decode work meets it first**.

---

## 2026-09-17 — the frame log lands, and the spacing curve does not reproduce

**Ten bursts, 385 flood frames, 9 lost. At a 250 ms gap the loss rate was 2.50 %; at
2000 ms it was 1.90 %.** Those two are the arms that measured 5.6 % and 0 % this morning.
Today they are indistinguishable.

**BF-27's raw frame log is built** (Impl Plan §6.6): one record per frame in and out, with
source, type, schema, RSSI, SNR, the discard reason and the running `rx_deaf_ms`, drained
by `log_task` to serial and to `lran/bridge/diag/rxlog/state`. `tools/simctl/rxlog.py`
reads it. The session's records are committed as
[`data/bf27-framelog-session-2026-09-17.json`](./data/bf27-framelog-session-2026-09-17.json)
and re-read with `rxlog.py --read`.

### What the log says about the mechanism, per frame

The question the two spent instruments could not answer was **position**. It now has an
answer, and it agrees with them.

| Of the 9 lost frames | |
|---|---|
| Gaps with a bridge transmission inside | 4 of 9 |
| Deaf share of the gap, per loss | 3.43 % to 11.05 % |
| **Frames the bridge's own deafness can account for, at most** | **0.52 of 9** |

**That ceiling is the honest form of the number and the tool refuses to report anything
stronger.** `rx_deaf_ms` is a total and says nothing about *where* in a gap the deafness
fell, so its share of the gap is the most of that gap it could have covered. Four gaps
contain a transmission and still cannot account for their losses: the largest single
contribution is 11 % of one gap.

> **An earlier draft of the tool got this wrong and the bench caught it.** It classified
> any gap with non-zero deaf time as "the bridge was not listening", which turned 21 ms
> inside a 612 ms gap into a verdict against the firmware. The first run printed exactly
> that, and it was wrong: the bridge was listening for 96.6 % of that gap. A ceiling needs
> no threshold and no assumption about where the deafness fell, so that is what it reports
> now. `test_rxlog_analyze.py` carries the case.

### Three cross-checks the log gives for free, and all three are clean

**376 receptions, every one of them `Packet`.** Zero `Orphan` — no frame in this session
was found by the timed read, so **no interrupt was missed at any spacing**, which is
`rx_no_interrupt` read per frame instead of per burst. Zero `PhyCrc` and zero
`HeaderError`: **nothing arrived corrupt**. Zero `DriverError`.

**All 376 passed the receive ladder with `Status::Ok`.** Nothing was discarded at any spec
§14 stage.

**RSSI −39 to −36 dBm, SNR +10 to +12 dB, across all ten bursts.** Three dB of spread over
twenty minutes. The link is not marginal in any ordinary sense.

**The ring never overwrote a record** in any run, so `log_task` outran the drain at both
spacings and none of the above is an artifact of the instrument losing its own output.

### The finding that was not being looked for

**The losses cluster in time and not by spacing.** In order of running, with the bridge's
own uptime clock:

| # | Arm | Frames | Lost | PER | First record at |
|---|---|---|---|---|---|
| 1 | gap 250 | 40 | 2 | 5.00 % | 163 s |
| 2 | gap 250 | 40 | 3 | 7.50 % | 355 s |
| 3 | gap 2000 | 25 | 2 | 8.00 % | 411 s |
| 4 | gap 2000 | 40 | 0 | 0 % | 551 s |
| 5 | gap 2000 | 40 | 0 | 0 % | 677 s |
| 6 | gap 250 | 40 | 0 | 0 % | 791 s |
| 7 | gap 250 | 40 | 0 | 0 % | 859 s |
| 8 | gap 250 | 40 | 0 | 0 % | 904 s |
| 9 | gap 250 | 40 | 0 | 0 % | 950 s |
| 10 | gap 250 | 40 | 2 | 5.00 % | 998 s |

Every loss falls in runs 1–3 or run 10. Runs 4–9 are clean and they span both spacings.
**A 2000 ms control produced 8 % and a 250 ms arm produced 0 %, in the same session, on
the same boards.** Within a lossy run the losses sit close together — 3 and 4 frames
apart at 250 ms, 5 apart at 2000 ms.

**This is stated as an observation, not as a conclusion, and it does not retract this
morning's entries.** Those runs measured what they measured; today's measure something
different on the same bench. What it does do is make **spacing a suspect variable rather
than the established one**, and the whole investigation since the first M22 burst has been
framed on the assumption that spacing is what moves the number.

### What would settle it

**Interleave the arms instead of running them in blocks.** Every sweep so far, including
this one, ran one spacing to completion before starting the next, so a slow change in the
environment is indistinguishable from an effect of spacing. Alternating 250 and 2000 within
one session separates them, and it is cheap — a burst is 45 s.

**That test is the one to run before BF-24 rather than after.** If the knee is
environmental, the 1 s threshold recorded on 2026-09-17 and the `backoff_max_ms` reasoning
that leaned on it are describing a quiet afternoon rather than a property of this firmware.

### What is no longer open

**The bridge's transmit path, as a per-frame explanation.** It was ruled out this morning
by constancy across bursts; it is now ruled out inside each individual gap, by a ceiling
that never exceeds 11 % of any one of them.

---

## 2026-09-17 — the bench is not a quiet channel, and the instrument that would have said so is blind

**The operator named three families of 900 MHz equipment running on the property: YoLink
LoRa products, Z-Wave and Insteon.** Only YoLink is recorded anywhere in the repo. That
turns the receive path's losses from an unexplained firmware behaviour into a question with
a named alternative, and it puts three problems on the table that matter more than the
anomaly that surfaced them.

### What the survey actually measured at the bridge's own location

From `docs/rangetest/data/2026-09-05-survey-campaign-r11.csv`, site `bridge-house`:

| Frequency | Peak | Mean | Floor |
|---|---|---|---|
| **917.4 MHz — the operating channel** | **−113.0 dBm** | −114.4 dBm | −116.0 dBm |
| 916.0 MHz — the YoLink cluster's peak | **−54.0 dBm** | −113.8 dBm | −116.0 dBm |
| 908.4 / 912.0 / 920.0 / 904.0 MHz | −112 to −113 dBm | ≈ −114 dBm | ≈ −116 dBm |

**The YoLink hub's peak sits 60 dB above its own mean in the same bin.** That is §5.4's
"every occupant bursty" as a number: it is on the air for a small fraction of the samples.

### What that evidence supports, and what it does not

**It does not support co-channel capture at the bench.** The wanted signal is −37 dBm at
one metre (BF-27's frame log, RSSI −39 to −36 across 376 receptions). The loudest thing at
`bridge-house` is −54 dBm, **17 dB below the wanted signal and 1.4 MHz away**. Nothing the
survey saw is strong enough or close enough to take the receiver off a −37 dBm frame.

**It cannot exclude an intermittent occupant either, and the survey said so first.** Each
bin holds 653 samples across 74 passes — a fraction of a second of dwell, spread thin. An
emitter with a low duty cycle is likely to be missed outright. §5.4: *"absence of a peak was
already weak evidence; the gaps make it weaker."* **A loss pattern clustered in time is
exactly what that sampling design could not have detected**, which is why the behavioural
observation and the survey do not contradict each other.

### Three findings, and the third is the one to act on

**1. The equipment inventory is incomplete and it is load-bearing.** Decision Register §3.1
records *"four YoLink temperature sensors and a switch, on a YoLink hub, all inside the
dwelling"* and nothing else. **Z-Wave and Insteon appear nowhere.** Which Z-Wave matters:
the classic US band is near 908.4 MHz at low power, while Z-Wave Long Range uses 912 MHz
and 920 MHz and permits far higher power. The survey read all three at floor — briefly.
**The frequencies and power classes of the Z-Wave and Insteon equipment are unconfirmed
here and are recorded as needing confirmation rather than as fact.**

**2. D33 standing condition 3 has an instrument that cannot test it.** The condition is
*"the ambient survey finds no co-channel occupant on the chosen frequency"*, and §3.4 names
`cad_backoffs` as the instrument that keeps the channel under observation. It cannot do the
job, for two reasons found this week:

- **`cad_backoffs` counts a busy CAD only.** Established 2026-09-17 and answered with
  `cad_free`; every run before that date reads a free CAD as no contention at all.
- **LoRa CAD detects a LoRa preamble at the configured spreading factor.** It is
  structurally blind to Z-Wave and Insteon FSK, and to LoRa at another SF. **Two of the
  three named families cannot move it at any signal level.**

This is the pattern root `CLAUDE.md` names: a load-bearing premise whose falsifying check is
tracked somewhere that cannot fire. **D33's status is not changed here** — that belongs to
the register and to the operator. The gap is raised as **M25**.

**3. No link has ever been measured on the operating channel.** Every walk, every bench
trace and B1b's gate-bearing run were captured at **915.0 MHz**, the range test's
provisional frequency — which §3.4 already flags as `weather-island`'s own peak. **917.4 MHz
has a survey behind it and no link measurement at all.**

### The margin inversion, which is why this outranks the bench anomaly

| | Wanted signal at the bridge | Loudest neighbour at the bridge |
|---|---|---|
| Bench, 1 m | **−37 dBm** | −54 dBm — **17 dB below** the wanted signal |
| Gate, 87 m | **≈ −100 dBm** (B1b median at 915.0) | −80 to −89 dBm — **10 to 20 dB above** it |

**The ordering flips by roughly 46 dB between the bench and the deployed link.** Bench
behaviour under interference is therefore not predictive of GateLink's, and it is optimistic
in the wrong direction. A 2 % loss rate at one metre is not reassuring about 87 m; it is a
figure taken where the wanted signal dominates everything else on the property.

### What is being built for it

**A channel-occupancy sampler on the bridge**, reusing BF-27's ring and drain. Periodic
`getRssiInst()` from `lora_task` while the radio is in receive, accumulated into fixed
buckets and emitted to serial for a long unattended capture. **It is modulation-agnostic**,
which is the whole point: it sees FSK, LoRa at any spreading factor, and anything else that
puts energy in the channel — the three things CAD cannot.

**Its timestamps are `millis()`, the same clock the frame log stamps**, so a loss and a
channel excursion can be put on one timeline rather than argued about separately.

---

## 2026-09-18 — M25's ten-hour capture: 917.4 MHz is not empty, and it cannot explain a loss at one metre

**The channel is occupied at a low duty cycle by at least two sources, one of them strictly
periodic, and nothing in ten hours came within 34 dB of the bench's wanted signal.** So the
capture does not explain the 2026-09-17 losses at one metre. It does bear on the gate link and
on D33 standing condition 3, and both of those questions go to the operator rather than being
settled here.

**The run.** The bridge board sampled RSSI on 917.4 MHz from 2026-09-18T03:43:21Z to
13:43:22Z, image `ecc2e6f` from a clean tree, with both simnodes powered down. It is one
segment with no reboot: 9.99 h, 35,950 one-second buckets, 3,568,828 samples. 25,407
opportunities were skipped (0.7 %), no bucket was blind, and no bucket carried one of our own
receptions. The capture is `docs/bridge/data/m25-chan-baseline-2026-09-17.log`; the file name
carries the date the capture was started.

```bash
python3 tools/simctl/rssi_report.py docs/bridge/data/m25-chan-baseline-2026-09-17.log
```

| | |
|---|---|
| Floor, per-minute mean | median **−115.5 dBm**, range −117.0 to −115.0 |
| Strongest sample | **−71.0 dBm** |
| Occupancy above −110 dBm | 3,256 of 3,568,828 samples, **0.0912 %** |
| Occupancy by hour (UTC) | 0.048 % to 0.228 %; 04:00 highest, 09:00 and 10:00 lowest |
| Seconds with any sample at or above −100 dBm | **374 of 35,950** (1.04 %) |

**The floor matches M20's.** M20 put the floor at −115 to −118 dBm at every site; this run
held within −117 to −115 for ten hours.

### Four kinds of excursion, told apart by level and shape

**1. A periodic source near −75 dBm.** 201 buckets peaked between −80 and −70 dBm, almost all
at −74 to −77. 198 of the 201 held exactly one sample above threshold. Their spacing is the
signature: 151 of the 200 gaps are 130 or 131 buckets, and every other gap is a whole multiple
of it. Fitting occurrence number against bucket sequence gives a **period of 130.66 s**, with
no residual larger than 0.54 s across ten hours. Over that span the source fired about 274
times and the sampler caught 201, or 73 %. **If each sample is instantaneous and 10 ms apart,
a 73 % catch rate means a burst of about 7 ms**; that is an estimate from the catch rate, not
a measurement of the burst.

**It is not the bridge.** The bridge transmitted 1,200 polls during the run (600 each to
`0x01` and `0x02`, which were powered down and never answered). 16 of the 201 buckets, 8.0 %,
fall within 1.5 s before or 1 s after a poll. A bucket placed at random falls in the same
window 8.1 % of the time.

**2. A source near −93 dBm that talks in bursts and in episodes.** 144 buckets peaked between
−100 and −90 dBm, mostly at −92 to −94. Many hold 4 or 8–10 samples above threshold, so tens
of milliseconds. Some runs last seconds with 20–40 % of samples above threshold: the longest
began at 04:02:57 and spanned 49 s, and it alone accounts for 543 of the run's 3,256 samples
above threshold. **This source is what lifts 04:00 to 0.228 %.** It has no period that the
same fit finds.

**3. Occasional hits near −85 dBm**, 4–5 samples each, a few times an hour at most (04:36,
04:37, 07:09, 07:13, 07:14, 10:41, 13:13 among the busier buckets). 29 buckets in all peaked
between −90 and −80 dBm.

**4. Weak single hits at −110 to −101 dBm.** 795 buckets, 610 of them with a single sample
above threshold and most peaking at −110 or −109 dBm. The threshold is only 5.5 dB above the
median floor, so **this band cannot be separated from the tail of the noise floor on this
evidence.** The few with 3–5 samples at −108 to −101 dBm look like a distant transmitter, but
the capture cannot show it.

**None of these is identified.** Which property device transmits every 130.66 s is for **M26**,
which completes the §3.1 inventory. The period is specific enough to match against a device's
documented heartbeat.

**Nor can the capture say whether a source is co-channel.** `getRssiInst()` reports energy
inside the receiver's 125 kHz bandwidth, and a strong signal just outside it can leak in.
M20's 200 kHz bins around 917.4 are the place to look first; M20 saw 917.4 itself at floor, on
653 samples, which a 7 ms burst every 130 s would almost always escape.

### What it does not explain: the 2026-09-17 losses at one metre

**Nothing seen could have blanked a frame at one metre.** On the bench the wanted signal is
−37 dBm. The strongest sample in ten hours was −71 dBm, 34 dB below it. The 2026-09-17 losses
were frames the radio never delivered, not frames that arrived corrupt, and an interferer 34 dB
down does not produce that.

**The sender cannot lose a frame to a busy channel either.** The simnode's transmit path goes
through `lib/lran-link`'s media access, which on a busy CAD backs off and retries up to
`cad_retries` times and then transmits regardless (`media_access.h`, spec 12.3). A busy
channel delays a flood frame; it does not remove one.

**The capture did not cover the hours of the losses.** The M22 runs were committed at
14:11 UTC on 2026-09-17 and the frame-log bursts at 02:09 UTC on 2026-09-18; the capture began
at 03:43 UTC. Occupancy moved by a factor of almost five between hours of this run, so a busier
afternoon is not excluded. **What the capture does exclude is an occupant loud enough to matter
at one metre in the hours it covered.** The channel candidate for the bench losses is weaker
than it was yesterday, and it is not closed.

**So the interleaved spacing sweep is next**, as the handoff already ordered it. It separates
time from spacing directly. Running a capture alongside it would not work: the sampler reads
our own preambles as excursions, which is why the simnodes were powered down for this run.

### What it does bear on: the gate link and D33

**At the gate, the ordering inverts.** The entry before this one puts the gate's wanted signal
at about −100 dBm at the bridge. Source 1 is about 26 dB above that and source 2 about 7 dB
above it, so an uplink frame that overlaps either one is likely lost. For the periodic source
alone, a frame of airtime T overlaps a burst with probability about (T + 7 ms) / 130.66 s,
which is **0.85 % for a maximum SF9 frame of 1107 ms** and less for a shorter one. Source 2 adds
to that and has no period to calculate from. **That is an estimate from bench-position RSSI,
not a measured loss rate at 87 m**, and no link has yet been measured at 917.4 MHz.

**D33 standing condition 3 says the survey finds no co-channel occupant on the chosen
frequency, and losing a condition reopens D33.** This capture finds energy in 917.4's receive
bandwidth from at least two sources. Whether that is a co-channel occupant, and whether it
reopens D33, is recorded in the Decision Register and decided by the operator. **D33's status
is not changed here.**

### Method, and what is not in the repository

`rssi_report.py` produced every figure in the first table. The period fit, the correlation with
the bridge's polls, the four-band split and the episode grouping (buckets with three or more
samples above threshold, joined when 10 s apart or less) came from an ad hoc script run against
the committed capture. **That script is not committed**, so those figures can be recomputed from
the capture but not yet by a tested tool. Folding them into `rssi_analyze.py` with host tests
would make them reproducible the way the rest of M25's arithmetic is.

---

## 2026-09-18 — M25's source analysis moves into `rssi_analyze.py`, and corrects two figures in the entry above

**The period is 130.69 s, not 130.66 s, and the chance baseline for coinciding with a poll is
8.3 %, not 8.1 %.** Neither correction changes a conclusion in the entry above. The entry
stands as written; this one supersedes those two numbers.

**The period was fitted on the wrong axis.** The ad hoc script fitted occurrence number
against bucket sequence, which gives a period in buckets. A bucket runs 1000 ms nominally but
sometimes 1007 or 1009 ms, so 130.66 buckets is 130.69 s once each event is timed by its
`millis()` start. The largest residual on that axis is 0.52 s, not 0.54.

**The chance baseline was sampled rather than computed.** The script placed 20,000 buckets at
random and got 8.1 %. The tool now takes the share of the capture's timeline covered by the
windows around each transmission, which is 8.3 %. The periodic source's own rate, 16 of 201 or
8.0 %, is unchanged and still at chance.

**The exact baseline turned up one thing the entry above missed.** Buckets peaking at −110 to
−101 dBm fall near one of our own transmissions **14.2 %** of the time: 113 of 795 buckets
where about 66 are expected. The excess sits in the buckets that contain a transmission: 78
hits where about 26 would be expected. So part of that weakest band is associated with the
bridge's own transmit cycle, not with the channel. **The mechanism is not established.** A reading
taken as the radio returns to receive, before it settles, would produce it, but that is a
hypothesis and nothing here tests it. The excess is about 50 buckets, a few percent of the
occupancy figure. It does not touch the periodic source, whose rate is at chance, or the
−93 dBm source, at 9.7 % against 8.3 %.

**What the tool now reports.** `rssi_report.py` prints each band's share of the samples above
threshold and its count of single-sample buckets. For each band it gives a periodicity verdict
fitted on `millis()`, and the rate at which the band falls near our own transmissions,
alongside chance. It then lists the largest episodes. The arithmetic is in `rssi_analyze.py`:
`periodicity()`, `coincidence()`, `episodes()`, `events()`, `frame_times()` and
`band_buckets()`, with 18 new host tests in `tools/simctl/test_rssi_analyze.py`. One test pins
the axis mistake: buckets 1009 ms long must still give a period in milliseconds.

```bash
python3 tools/simctl/rssi_report.py docs/bridge/data/m25-chan-baseline-2026-09-17.log
python3 tools/simctl/test_rssi_analyze.py
```

---

## 2026-09-18 — M25's periodic source matches the property's Davis Vantage Pro2

**The 130.69 s source at 917.4 MHz is very likely the Davis weather station's hop cycle.** The
match is in frequency, period and burst length. No Davis packet has been decoded on 917.4 MHz,
so this is a strong candidate rather than an identification. The research is in
[`LRAN-Site-RF-Inventory`](../shared/LRAN-Site-RF-Inventory.md) §6, which also covers the
property's Z-Wave, Insteon and YoLink equipment.

| | Davis Vantage Pro2, from published sources | M25's periodic source, measured |
|---|---|---|
| Frequency | One of 51 hop channels at **917.434 MHz**, 34 kHz from LRAN's centre | Inside the 125 kHz receive bandwidth at 917.4 MHz |
| Period | 51 channels × 2.5625 s at transmitter ID 1 = **130.6875 s** | **130.69 s**, largest residual 0.52 s |
| Burst | 16 bytes at 19.2 kbps = **6.7 ms** | About 7 ms, estimated from a 73 % catch rate |

**The operator confirmed a Davis Vantage Pro2 on the property on 2026-09-18**, and did not
know it transmits in the 900 MHz band. Its transmitter sits at M20's survey position 2,
`weather-island`, **on the line between the bridge and GateLink's site**. So both ends of the
link hear it, and its level at the gate has not been measured. None of the property's other inventoried systems has a
nominal channel within 1.4 MHz of 917.4 MHz.

**The console's transmitter ID is the check.** ID 1 predicts 130.6875 s. Any other ID predicts
a different period and would rule the Davis out as this source.

**M20's survey cannot confirm it or rule it out.** At no site do bins containing a Davis
channel read louder than the others. A 6.7 ms burst that returns to one channel every 130.7 s
rarely falls inside one bin's 653-sample dwell, which is why a ten-hour watch on one channel
was needed to see it.

**Two things follow, and neither is decided here.** The Davis channels either side of
917.434 MHz are 916.934 and 917.936 MHz, so a channel such as 917.2 MHz would contain no
Davis hop; moving LRAN is a D1 matter. The inventory also finds that M20's −54 dBm at
916.0 MHz in the house sits on Z-Wave's 100 kbps channel, not on YoLink's 923.3 MHz, which
contradicts Decision Register §5.4's candidate attribution. Both go to the register, with the
D33 question M25 already raised.

**M25's −93 dBm episodic source is still unexplained.** A Davis station sends one packet per
hop, so it does not produce episodes lasting seconds on one channel.

---

## 2026-09-18 — the Davis transmitter ID is 1, so the prediction held

**The operator read transmitter ID 1 on the Davis console**, and noted that it is probably the
factory default. The entry above named this as the check: ID 1 predicts a hop cycle of
51 × 2.5625 s = 130.6875 s, and any other ID predicts a different period and would have ruled
the Davis out. The prediction was written before the console was read.

**The fit agrees to about 8 parts per million.** M25's period, fitted on `millis()`, is
130.6865 s, 1 ms per cycle short of the prediction. A transmitter crystal's tolerance covers
that.

**So M25's periodic source is the Davis Vantage Pro2**, identified by frequency, period, burst
length and transmitter ID. No Davis packet has been decoded on 917.4 MHz. The entry above
called it a strong candidate; this entry supersedes that description, and the entry itself
stands as written.

**This settles the channel question for this source, and not D33.** One Davis hop channel,
917.434 MHz, lies inside LRAN's receive bandwidth, so the periodic source is co-channel, not a
neighbour leaking in. D33 standing condition 3 requires no co-channel occupant on the chosen
frequency. Whether a 6.7 ms burst every 130.69 s reopens D33 is the operator's decision, recorded
in the Decision Register. M25's −93 dBm episodic source is still unidentified.

---

## 2026-09-18 — the Dakota Alert driveway sensor is 433.92 MHz, not a neighbour

**The operator runs a Dakota Alert driveway occupancy sensor** in place of the gate controller,
which LRAN cannot reach yet. Its transmitter is within 30 ft of the gate controller, at M20's
survey position 1, `gatelink-gate`, and its receiver is near the production bridge location.
The operator expected 433 MHz and asked for it to be checked.

**Every Dakota Alert Part 15 grant opened is 433.92 MHz**, from the 2004 Driveway Radio to the
2024 DCHT-4000 hose transmitter, and none of the company's 19 FCC IDs shows a 902–928 MHz
grant. Its second harmonic is 867.84 MHz, below the band. So it does not bear on 917.4 MHz or on
any channel LRAN might move to.

**One caveat, if the sensor turns out to be a MURS model.** Dakota Alert's MURS line transmits at
151.8–154.6 MHz, up to 1.1 W ERP, and the sixth harmonics of those channels fall at
910.9–911.6 and 927.4–927.6 MHz. That is well clear of 917.4 MHz and of 917.2 MHz, and inside
the band's top edge. The model number decides it.
[`LRAN-Site-RF-Inventory`](../shared/LRAN-Site-RF-Inventory.md) §7 has the grants.

---

## 2026-09-18 — the Dakota Alert sensor is a DAPT-4000, at 433.92 MHz

**The operator read the model as DAPT-4000**, a 4000-series dual-probe directional vehicle
sensor, and its manual gives 433 MHz. That closes the MURS caveat in the entry above: the sensor
is not VHF, and nothing it transmits reaches 902–928 MHz. Its likely FCC ID is QK8PB-4000,
"PB-4000 Directional Probe Alarm", at 433.92 MHz under §15.231. The match is by product name,
not read from the unit's label.

---

## 2026-09-18 — the 917.2 MHz capture: no Davis, and an evening source at −89 dBm that M25's hours never covered

**917.2 MHz passes the first of the D1 brief's three tests and cannot be judged on the other
two.** Over ten hours, no source there was periodic, so the Davis hop that M25 found at 917.4 MHz
is absent. Occupancy read 0.3260 % against M25's 0.0912 %. Most of the excess comes from a source
near −89 dBm that ran from about 18:00 to 23:50 UTC, and M25 covered none of those hours.
[`LRAN-D1-Frequency-Change-Brief`](../shared/LRAN-D1-Frequency-Change-Brief.md) §5 compares
captures only over the same hours, so this capture cannot say whether 917.2 MHz is busier than
917.4 MHz or whether the evening is busier than the night. **Choosing the next capture is the
operator's call.**

**The run.** The bridge board sampled RSSI on 917.2 MHz from 2026-09-18T15:14:06Z to
2026-09-19T01:14:04Z, with both simnodes powered down. It ran image `5e752e9`, the capture-only
build on branch `capture-917200`, which is `ecc2e6f` with `kPhy.freq_hz` alone changed. The run is
one segment: 9.99 h, 35,950 buckets and 3,568,897 samples, with 25,359 opportunities skipped
(0.7 %). A scratch wrapper reset the board once on open so that `CHAN-BOOT` was recorded. The
capture is `docs/bridge/data/m25-chan-917200-2026-09-18.log`.

```bash
python3 tools/simctl/rssi_report.py docs/bridge/data/m25-chan-917200-2026-09-18.log
```

| | 917.2 MHz, 15:14 to 01:14 UTC | 917.4 MHz, M25, 03:44 to 13:43 UTC |
|---|---|---|
| Floor, per-minute mean | median **−115.9 dBm**, range −117.0 to −114.0 | median −115.5 dBm, range −117.0 to −115.0 |
| Strongest sample | **−79.0 dBm** | −71.0 dBm |
| Occupancy above −110 dBm | 11,636 of 3,568,897 samples, **0.3260 %** | 3,256 of 3,568,828, 0.0912 % |
| Occupancy by full hour (UTC) | 0.064 % to 0.668 % | 0.048 % to 0.228 % |
| Periodic source | **none in any band** | 130.69 s, the Davis |
| Buckets peaking −80 to −70 dBm | 1 | 201 |
| Buckets peaking −90 to −80 dBm | **356**, 6,698 samples above | 29, 74 samples above |
| Buckets peaking −100 to −90 dBm | 227, 2,758 samples above | 144, 1,756 samples above |
| Buckets peaking −110 to −100 dBm | 953, 2,169 samples above | 795, 1,211 samples above |

### Test 1, no periodic source: passed

**No band at 917.2 MHz is periodic.** The −80 dBm band holds one bucket, at 16:18:53 UTC, which
peaked at −79 dBm with 20 of 100 samples above threshold. A Davis hop covers one sample. Over ten
hours the Davis cycles about 275 times, and at 917.4 MHz M25 caught 73 % of them. So the Davis
leaves no trace at 917.2 MHz, 0.2 MHz from its nearest hop. **The receiver's rejection of a Davis
burst at 0.25 MHz offset is still not measured.** This capture shows only that none reached
−110 dBm.

### Test 2, occupancy no higher than 0.0912 %: failed as read, and not comparable

**The ten-hour figure is 3.6 times M25's, and the excess is confined to five hours.**

| Hour (UTC) | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 00 | 01 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Occupancy, % | 0.148 | 0.132 | 0.115 | **0.476** | **0.365** | **0.515** | **0.668** | **0.579** | 0.197 | 0.064 | 0.144 |

Hours 15 and 01 are partial. The hours outside 18 to 22 read 0.064 % to 0.197 %, inside M25's
hourly range of 0.048 % to 0.228 %. **No hour of this run overlaps an hour of M25's**, so the
comparison §5 asks for cannot be made from these two files.

### Test 3, the episodic source no stronger than at 917.4 MHz: not answerable

**Two sources account for the busy hours, and they differ in level.**

1. **Hour 18 holds episodes at −94 to −96 dBm** in the −100 to −90 dBm band, clustered between
   18:01 and 18:26 UTC. That band holds 1,556 samples above threshold in hour 18 and 37 to 246 in
   every other hour. By level and shape, these episodes match M25's −93 dBm episodic source, which
   peaked at −91 to −94 dBm at 917.4 MHz.
2. **A source at −90 and −89 dBm runs from 17:57 to 23:49 UTC and nowhere else.** 328 of the 356
   buckets peaking between −90 and −80 dBm peak at exactly −90 or −89 dBm, and all 328 fall in
   that window. They hold a median of 17 of 100 samples above threshold, so the source is
   above threshold for about a sixth of each busy second. They form 166 events with no period. Hours 19 to 23 hold 48, 85, 113, 81 and
   20 of those buckets, and every other hour holds three or fewer. **This source is what lifts hours 19
   to 22.** Nothing at M25's 917.4 MHz resembles it: that band held 29 buckets in ten hours, with
   peaks spread from −90 to −81 dBm.

**The build machine's clock runs at UTC−4**, so the second source ran from about 14:00 to 20:00
local time. Whether it has anything to do with activity on the property, the capture cannot say.

**Source 2 is either specific to 917.2 MHz or specific to the evening, and the capture cannot tell
which.** M25 never listened at 917.4 MHz between 14:00 and 01:00 UTC. So test 3 cannot be settled
as §5 frames it. Source 1 peaked 2 to 3 dB weaker than at 917.4 MHz. Source 2 is 2 to 5 dB stronger
than M25's episodic source, in hours M25 did not cover.

### What it bears on

**At the gate, source 2 would sit about 11 dB above the wanted signal.** The gate's frames arrive
at the bridge near −100 dBm, and source 2 peaks at −89 dBm at the bridge. A frame that overlaps
one of its busy seconds is likely lost. That is an estimate from bench-position RSSI, as M25's
was, and no link has been measured at 917.2 MHz.

**The weakest band's association with our own transmissions held.** 200 of the 953 buckets
peaking at −110 to −101 dBm fall near a bridge transmission, 21.0 %, against 14.2 % at 917.4 MHz
and 8.3 % by chance. The 3.72-hour reading of 24.7 % is superseded by this figure. The mechanism
is still not established. The −100 and −90 dBm bands sit at 8.8 % and 9.8 %, at chance, so neither
source above is tied to the bridge's transmit cycle.

**Two captures would settle the hour question, and they answer different things.**

- **917.4 MHz from 15:14 UTC** says whether source 2 is on 917.4 MHz in the evening too. If it
  is, the evening is busier on both channels, and source 2 does not count against 917.2 MHz. If it
  is not, source 2 belongs to 917.2 MHz.
- **917.2 MHz from 03:43 UTC** answers §5's test 2 against M25 directly, and says nothing about
  source 2.

The first changes what §5 can conclude; the second applies §5 as written. **The 917.6 MHz capture
now running starts at 03:43 UTC and covers M25's hours only**, so it will be comparable with M25
and will not show whether source 2 reaches 917.6 MHz.

### The handover and `--reset-on-open`

**The unattended handover worked.** The 917.2 MHz capture exited at 01:14:09Z. The arming script
flashed `91e63dd` on its first attempt at 01:14:19Z, removed its copy of `secrets.h`, and started
the 917.6 MHz capture at 03:43:07Z.

**`--reset-on-open` worked on its first hardware run.** The 917.6 MHz file opens with `#RESET`,
then the banner's `PHY: 917.6 MHz` line and `CHAN-BOOT,91e63dd,917600000`, all within the first
second.

**The 917.6 MHz file name carries the UTC date, and M25's carries the local one.** M25's capture
began at 03:43 UTC on 2026-09-18 and is named `2026-09-17`; the 917.6 MHz capture began at the same
hour a day later and is named `2026-09-19`. The two are one night apart, not two.

---

## 2026-09-19 — a listen-only receiver, so the candidate frequencies can share their hours

**`firmware/chan-capture/` is built and has not yet run on hardware.** It is a receiver that
samples one channel's RSSI about 100 times a second, writes the bridge's `CHAN`, `CHANSUM` and
`CHAN-BOOT` lines, and never transmits. It exists because the entry above could not separate
frequency from time of day. With one of these on each candidate frequency, a capture compares
channels over the same hours by construction.
[`LRAN-D1-Frequency-Change-Brief`](../shared/LRAN-D1-Frequency-Change-Brief.md) v0.2 §5 now
asks for that run in place of captures taken one after another.

**The sampler moved into `lib/lran-link/`**, from `firmware/bridge/src/`, so the bridge and the
new image summarise a capture with one implementation. Its 23 host tests moved with it and
pass. The bridge's host suite and its Heltec target build unchanged.

**Why a new image and not the capture-only bridge images.** Those poll, so several of them a
metre apart would put their own frames into each other's captures, at 0.2 MHz offsets where the
receiver's rejection has not been measured. Two of them on one broker would also fight over
the MQTT client ID `lran-bridge`. `tools/checks/chan_capture_never_transmits.py` now fails CI
if the new image's source calls a transmit, a CAD, `setOutputPower`, WiFi or Bluetooth.

**Its files differ from the bridge's in three ways**, and `firmware/chan-capture/CLAUDE.md`
lists them: no `FRAME` lines, a fixed 10 ms sampling period, and `own_rx` counting LRAN-PHY
frames heard rather than frames received as the bridge.

**Occupancy cannot be corrected for a receiver's bias after the fact.** The firmware counts
samples at or above a fixed −110 dBm and keeps only each second's peak, so a receiver that
reads 2 dB hot counts more occupancy and the file cannot say so. That is why brief §5 opens
with a calibration hour, every receiver on 917.4 MHz under the same Davis hop, and rotates the
receivers if they disagree. **The rotation thresholds in §5, 1 dB and 20 %, are proposals**,
sized to the differences tests 2 and 3 judge, and are the operator's to change.

**Untested:**

- **No capture has run on this image.** The frequency command, NVS storage and the sampling
  period are exercised only by the host tests and the build.
- **`rssi_capture.py --reset-on-open` is untested on the XIAO.** It was built for the Heltec's
  CP2102. On the XIAO's native USB the reset may work, may do nothing, or may enter the
  bootloader, and the port disappears and returns across it.

**The simnodes were plugged in briefly during the 917.6 MHz capture**, between about 04:03 and
04:13 UTC by the operator's estimate, then unplugged. Both carry simnode firmware on 917.4 MHz.
The capture over that window holds only weak, short events, −96 to −110 dBm and 2 to 10
samples each, and no run of consecutive samples as long as an SF9 frame. Minutes 04:10 to 04:12
counted 10, 17 and 20 samples above threshold, a little above their neighbours and within what
the episodic source produces. **So nothing in the file is attributable to the boards**, and the
window is recorded here so an analysis can exclude it.

**One citation the check does not read is stale.** `lib/lran-link/library.json` cites Protocol
Specification **v0.11**; the specification is at v0.12. `tools/checks/spec_citation_version.py`
does not read `library.json`, so nothing caught it. It is left as found, because reconciling
comes before the citation changes.

---

## 2026-09-19 — the XIAO + Wio Kit radiates a carrier near 917.4 MHz, whatever it is tuned to

**A narrowband signal at about −73 dBm appears on 917.4 MHz at the simnode Heltec whenever
the XIAO + Wio-SX1262 Kit is powered, starting some tens of seconds after the XIAO boots.** It
goes when the XIAO is unplugged and returns when it is plugged back in. It does not move when
the XIAO is retuned. The source is the XIAO assembly's hardware, and which part of it is not
established. Both boards were running `firmware/chan-capture/` at `f86c5dd`, which transmits
nothing, a metre or less apart on the bench.

**What was seen, in order, all in UTC on 2026-09-19:**

| Time | XIAO | Heltec at 917.4 MHz |
|---|---|---|
| 04:25 | 917.4 MHz, first image | Nothing above −110 dBm, over 6 s |
| 04:29–04:31 | 917.4 MHz, panel-off image | Quiet about 10 s after its own reset, then every sample above −110 dBm. Floor −74 dBm, peak −70 to −73 dBm, flat to 1 dB for 68 s |
| 04:35 | **unplugged** | Floor −114 dBm, nothing above −110 dBm, over 32 s |
| 04:36 | **plugged back in** | Quiet for 43 s, then −70 dBm from mid-bucket 44 |
| 04:38 | 917.4 MHz | Nothing above −110 dBm, over 20 s |
| 04:38–04:40 | **retuned to 917.2 MHz** and rebooted | Loud from bucket 36, about 40 s after the XIAO's reboot, peaks −70 to −72 dBm |

**The XIAO does not hear it.** Its own floor at 917.4 MHz read −109 dBm with a mean of
−107 dBm, 5 to 7 dB above the Heltec's quiet floor, and its strongest sample was −82 dBm.
**The bridge's 917.6 MHz capture does not show it either**: its floor held at −115 dBm through
the whole window. So the signal sits inside 917.4 MHz's receive bandwidth and outside
917.6 MHz's.

**Ruled out:**

- **The XIAO's receiver leaking its local oscillator.** That leak would follow the tuning, and
  retuning the XIAO to 917.2 MHz left the carrier at 917.4 MHz.
- **The OLED panel.** Switching it off at boot left the XIAO's floor unchanged, and the carrier
  appeared after the panel was off.
- **Anything on the property.** A receiver a metre away cannot see a −73 dBm carrier that
  arrives from outside while a second receiver beside it sees nothing, and the carrier leaves
  when the XIAO is unplugged.

**Not established:** which part radiates. The candidates are the ESP32-S3 and its clocks, the
expansion board, the Kit's own circuitry, and the USB cable as an antenna. Nor is it
established whether the delay after boot and the gaps are regular, or whether the XIAO
radiates it while running simnode firmware. **Three tests would separate them:** power the XIAO
from a battery pack with no USB data, remove the Wio Kit's antenna (safe on a receiver that
never transmits), and log both boards side by side for an hour.

**What it bears on:**

- **Brief §5's parallel run.** The XIAO cannot sit a metre from another receiver on 917.4 MHz,
  and a receiver next to it is measuring the XIAO. Until the source is found, the run has two
  usable receivers, both Heltecs.
- **Every bench measurement with the XIAO powered.** The XIAO has been the target-radio simnode
  on 917.4 MHz since 2026-09-14, and the flooding node in M22's sweeps. A −73 dBm carrier sits
  36 dB below the bench's −37 dBm wanted signal, and a LoRa CAD does not detect a carrier, so it
  is not an obvious cause of the one-metre losses. **Whether it was present during those runs
  is not known**, because no capture ran with the XIAO powered. M25's capture ran with both
  simnodes powered down.
- **GateLink.** Its module is the header-board Wio-SX1262 (p-6379), not this Kit, on a
  different host. Whether that board radiates the same way is untested.

**The XIAO now samples 917.2 MHz**, stored in its NVS by the retune above. Its next boot says
so in `CHAN-BOOT`.

---

## 2026-09-19 — the "carrier" was a stuck receiver in the Heltec, set off by the bridge's polls

**This entry supersedes the one above, which blamed the XIAO + Wio Kit. The XIAO is not the
source, and there was no carrier.** The simnode Heltec, running `firmware/chan-capture/` at
917.4 MHz, read a flat −74 dBm because its own receiver stuck after each of the bridge's polls
at 917.6 MHz, a metre away. Restarting receive clears it, and the image now restarts receive
every 100 ms. The entry above stands as written, as a record of what was believed at the time.

**Why the entry above was wrong.** Its unplug test watched the Heltec for 32 s, and its quiet
"before" window for 20 s. Every watch began by opening the Heltec's port, which reboots it, and
the reading then jumped some seconds later. Those two watches were too short to see a jump that
had nothing to do with the XIAO. A continuous capture with no reboots settled it: the reading
jumped at 04:49:10 and stayed at −74 dBm after the XIAO was unplugged at about 04:53.

**The trigger is the bridge's poll.** Every jump came within a second of a poll in the bridge's
917.6 MHz capture file, six of six:

| Heltec reading jumps to −74 dBm | Bridge poll |
|---|---|
| 04:37:08 | 04:37:09 |
| about 04:39:08 | 04:39:09 |
| about 04:47:08 | 04:47:09 |
| 04:49:10 | 04:49:09 |
| 04:51:20, back after a brief dip | 04:51:19 |
| 04:55:10 | 04:55:09 |

**The state is invisible to the radio's own flags.** A watchdog on `PREAMBLE_DETECTED` and
`HEADER_VALID` never fired, and a live probe at 05:07 read the IRQ status as **0x0000** in the
middle of an episode, with RSSI at −73.0 dBm. So the modem was not mid-reception. A forced
restart then read −113.0 dBm 50 ms later, and the next poll, at 05:07:19, stuck it again. The
level it sticks at is close to the level of the poll itself, which reads −70 dBm. Why the
receiver holds it is not established.

**Neither Vext nor the OLED panel is involved.** The image from before the panel change, which
never touches Vext, stuck at 04:55:10 after the poll at 04:55:09.

**The fix: restart receive every 100 ms, just after a sample.** Over 3.5 minutes and eight
polls on build `6bf9a38`'s code, no plateau appeared. Each poll read as about 19 samples at
−70 dBm, about 190 ms, which fits a short SF9 frame. The floor held at −114 dBm. The Davis was
caught twice, at 05:09:54 and 05:12:04, 130 s apart. One sample in 3.5 minutes was skipped. The
cost is that a frame longer than 100 ms is rarely decoded, so `own_rx` undercounts in these
files; the energy is still sampled.

**Why the bridge never showed it.** The bridge restarts receive after each of its own
transmissions, so a stuck state would end at its next poll. M25's ten hours show no plateau.
**What this does not establish** is whether the bridge's receiver sticks between polls when a
strong signal on another channel arrives. At the bench that costs nothing, because a −37 dBm
frame clears a −74 dBm stuck floor by 37 dB. At the gate, where the wanted signal is about
−100 dBm, a receiver stuck 40 dB high would miss frames until its next transmission. **That is
an open question, not a finding.** The check that would settle it is a bridge-side capture with
a strong off-channel LoRa burst and no poll for a minute afterwards, and no task owns it yet.

**The XIAO's own floor is still unexplained.** It read −109 dBm with a mean of −107 dBm on the
build without the restart, 5 to 7 dB above the Heltec. Whether it was stuck too, from boot, is
the next thing to check on the new build.

**Two things the 917.6 MHz capture's log entry should carry.** The simnode Heltec sat on
917.4 MHz beside the bridge from 04:24 onward, never transmitting, and the XIAO did the same
from 04:24 to about 04:53 and again from 04:36. Neither transmits, so neither can put energy on
917.6 MHz; the entry should say so rather than leave the reader to wonder.

---

## 2026-09-19 — the 917.6 MHz capture: no Davis, and a −46 dBm source no other capture has shown

**917.6 MHz passes the D1 brief's first test and fails the other two as read.** No source there
was periodic, so the Davis hop that M25 found at 917.4 MHz is absent. Occupancy read 0.1467 %
against M25's 0.0912 % over the same UTC hours one day earlier. A source at −45 to −47 dBm
appeared 18 times, 25 dB above anything M25 or the 917.2 MHz capture recorded. The two captures
are a day apart, so neither failure separates frequency from day.
[`LRAN-D1-Frequency-Change-Brief`](../shared/LRAN-D1-Frequency-Change-Brief.md) §5's parallel
run is the comparison that can.

**The run.** The bridge board sampled RSSI on 917.6 MHz from 2026-09-19T03:43:07Z to
13:43:08Z. It ran image `91e63dd`, the capture-only bridge build, polling peers 0x01 and 0x02
once a minute each; neither was powered. The run is one segment: 9.99 h, 35,950 buckets and
3,568,887 samples, with 25,385 opportunities skipped (0.7 %). `rssi_capture.py
--reset-on-open` opened it. The capture is `docs/bridge/data/m25-chan-917600-2026-09-19.log`.

```bash
python3 tools/simctl/rssi_report.py docs/bridge/data/m25-chan-917600-2026-09-19.log
```

| | 917.6 MHz, 2026-09-19, 03:43 to 13:43 UTC | 917.4 MHz, M25, 2026-09-18, 03:44 to 13:43 UTC |
|---|---|---|
| Floor, per-minute mean | median −115.1 dBm, range −117.0 to −113.0 | median −115.5 dBm, range −117.0 to −115.0 |
| Strongest sample | **−45.0 dBm** | −71.0 dBm |
| Occupancy above −110 dBm | 5,236 of 3,568,887 samples, **0.1467 %** | 3,256 of 3,568,828, 0.0912 % |
| Periodic source | **none in any band** | 130.69 s, the Davis |
| Buckets peaking −60 dBm and up | **19**, 82 samples above | 0 |
| Buckets peaking −80 to −70 dBm | 26, 94 samples above | 201, 216 samples above |
| Buckets peaking −90 to −80 dBm | 68, 279 samples above | 29, 74 samples above |
| Buckets peaking −100 to −90 dBm | 234, 1,648 samples above | 144, 1,756 samples above |
| Buckets peaking −110 to −100 dBm | 828, **3,135** samples above | 795, 1,211 samples above |

### What was on the bench

**Nothing else transmitted on LRAN's PHY after 04:24:25 UTC.** Both simnode boards ran simnode
firmware on 917.4 MHz from about 04:03 to 04:13 UTC, by the operator's estimate, and the XIAO
booted simnode firmware again from about 04:22 until it was flashed at 04:24:25. From then on,
both ran `firmware/chan-capture/`, which calls no transmit. The simnode Heltec sat on 917.4 MHz
about a metre from the bridge board for the rest of the capture and was rebooted many times
between 04:25 and 05:27. The XIAO was unplugged by 05:40, when the last handoff was written. The 2026-09-19 entries above
have the detail.

### Test 1, no periodic source: passed

**No band at 917.6 MHz is periodic, so the Davis leaves no trace 0.166 MHz from its 917.434 MHz
hop.** At 917.4 MHz, M25 caught 73 % of about 274 Davis cycles, each at −74 to −77 dBm. Here,
none reached −110 dBm. So the receiver rejects a Davis burst at 0.166 MHz offset by at least
33 dB, assuming the Davis reached the bench at the same level both nights. The
handoff listed rejection at 0.25 MHz as unmeasured. A channel filter rejects more as the offset
grows, so 917.2 MHz's neighbouring hops, 0.234 and 0.266 MHz away, should be rejected at least
as well. That is an inference; 0.25 MHz itself is still not measured.

### Test 2, occupancy no higher than 917.4 MHz's: failed as read, a day apart

**917.6 MHz read higher than M25 in eight of the nine full hours**, by 1.7 to 3.1 times.

| Hour (UTC) | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|
| 917.6 MHz, 2026-09-19, % | 0.107 | 0.170 | 0.170 | 0.153 | 0.139 | 0.149 | 0.135 | 0.148 | 0.165 |
| 917.4 MHz, 2026-09-18, % | 0.228 | 0.093 | 0.101 | 0.078 | 0.068 | 0.048 | 0.048 | 0.068 | 0.062 |

**Almost all of the excess sits in the weakest band.** 917.6 MHz counted 1,980 more samples above
threshold than M25, and 1,924 of them peak at −110 to −101 dBm. The band holds about as many
buckets at both frequencies, 828 against 795, but the buckets at 917.6 MHz are longer: 200 of the
828 hold one sample above threshold, against 610 of M25's 795. **The excess was there before any
other board was powered.** Hour 03, from 03:43 to 04:00, held 93 samples above threshold in that
band against M25's 22.

**The weakest band is not tied to the bridge's transmissions at 917.6 MHz.** 79 of its 828
buckets fall near one, 9.5 %, against 8.3 % by chance. The same figure read 14.2 % at 917.4 MHz
and 21.0 % at 917.2 MHz. Why it differs between frequencies is not established.

### Test 3, no episodic source stronger than at 917.4 MHz: failed

**Two sources decide it.**

1. **The −93 dBm episodic source reads about the same.** Seven of the eight largest episodes peak at
   −91 to −96 dBm, and one at −82 dBm, against −91 and −92 dBm at 917.4 MHz. The −100 to −90 dBm band holds more buckets,
   234 against 144, and a similar number of samples above threshold, 1,648 against 1,756.
2. **A source at −45 to −47 dBm appears in 18 buckets, and nothing like it appears in either
   other capture.** A nineteenth bucket, at 13:22:25 UTC, peaked at −51 dBm with 7 samples above
   threshold and may be the same source. The 18 run from 04:33:13 to 12:16:45 UTC, 00:33 to
   08:16 local time. Each holds 1 to 6 samples above threshold, so each burst lasts about 10 to
   60 ms. They arrive one to four an hour with no period. Twice two arrive less than a minute apart,
   10:18:17 and 10:18:53, and 11:21:46 and 11:21:55. The buckets at 09:22:06 and 09:22:07 are
   consecutive and may hold one burst. Two of the 19 fall near a bridge transmission, at chance.

**The level held within 2 dB across nine hours**, which fits one transmitter at a fixed position
and power. It is unidentified. At the gate, where frames arrive near −100 dBm, a burst at this
level would sit about 54 dB above the wanted signal. That estimate comes from bench-position
RSSI, as M25's did.

**One timing needs checking before the source counts against 917.6 MHz.** The simnode Heltec
went onto the bench at 04:24, on `chan-capture` at 917.4 MHz, and the first burst came at
04:33. M25 and the 917.2 MHz capture, which ran with no other board powered, show nothing above
−71 dBm. Reading −46 dBm a metre away needs about −14 dBm radiated, from the 31.7 dB free-space
loss at 1 m, and the image calls no transmit. So this is a coincidence to test, not an
attribution. **A 917.6 MHz capture with no other board powered would settle it**: if the
bursts continue, the board is not their source.

**Z-Wave at 916.00 MHz is an unlikely source.** It sits 1.6 MHz below 917.6 MHz and 1.4 MHz
below 917.4 MHz, where M25 saw nothing above −71 dBm. That was a different night, and the test
below found nothing either.

### The thermostat test at 13:43 to 13:44 UTC

**The operator sent three status requests to a Z-Wave thermostat 6 to 8 m from both Heltecs,
and no receiver recorded anything that can be tied to them.** The operator's log places the
requests between 13:43 and 13:44 UTC (09:43 to 09:44 EDT), to within about 10 s. That window
straddles the unattended handover between captures:

| UTC | Receivers | Seen |
|---|---|---|
| 13:42:50 to 13:43:08 | bridge board, 917.6 MHz | One sample at −110 dBm in each of two buckets, 13:42:53 and 13:42:54. That band ran about 1.4 buckets a minute over the capture, so two are background |
| 13:43:08 to 13:44:03 | **none** | The 917.6 MHz capture had closed. The script flashed the bridge board and rebooted both boards to check them |
| 13:44:03 to 13:45:13 | both Heltecs, 917.4 MHz | **No sample above −110 dBm on either board** |

**Any request after 13:44:03 put nothing above −110 dBm on 917.4 MHz**, 1.40 MHz above Z-Wave's
100 kbps channel. Three things limit what that shows:

- **The thermostat's data rate is not known.** At 40 or 9.6 kbps it transmits on 908.4 MHz,
  9 MHz away, and the test says nothing about 916.00 MHz.
- **The sampler can miss a short frame.** It reads instantaneous RSSI once every 10 ms. A Z-Wave
  frame at 100 kbps lasts a few milliseconds, by estimate from the rate, so one frame is caught
  with a probability of roughly its length over 10 ms. An exchange of several frames is likely,
  not certain, to land at least one sample.
- **Which requests fell after 13:44:03 is not known**, because the times are good to about 10 s.

**The events just after the window are not the thermostat.** Both boards caught a weak event
at 13:45:30, five samples each at −107 and −105 dBm, and the simnode Heltec caught another at
13:46:54 at −109 dBm. Both fall outside the window. Single samples at −73 dBm at 13:46:06 and
13:48:17 are 131 s apart, the Davis period.

**Repeating the test with times to the second would answer it.** During day 1 the boards sit on
917.4 and 917.2 MHz, 1.4 and 1.2 MHz above 916.00 MHz, so one repeat covers two offsets.

---

## 2026-09-19 — the calibration hour: the two Heltecs agree on floor and occupancy, and differ by direction

**The two receivers pass brief §5.2 step 3 on floor and occupancy, and their Davis peaks are too
unstable to calibrate against.** Floors agreed to 0.3 dB and occupancies to 3.5 %. Over the whole
hour the median Davis peaks differ by 1.0 dB, which is at the rule's limit rather than over it.
But the bridge board's Davis reading stepped up by about 8 dB at 14:09 UTC while the simnode
Heltec's did not move. So the offset between two receivers depends on where the signal comes
from, and no single figure corrects one to the other. **No swap is called for by the rule as
written.** Day 1 started on the planned assignment at 14:44:30.

**The run.** Both Heltecs ran `firmware/chan-capture/` on 917.4 MHz from 13:44:03 to 14:44:03
UTC. `rssi_capture.py --reset-on-open` reset both together. They were the bridge board (flat
case, the 3.0 dBi stick, image `35c8471`) and the simnode Heltec (handheld case, its own antenna,
image `6bf9a38`).
They sat about a metre apart on the bench, and nothing else was powered. Each file holds one
segment of 3,550 buckets and 354,999 samples, with one opportunity skipped. Both booted in the
same second and sample every 10 ms, so their samples fall within about 2 ms of each other.

```bash
python3 tools/simctl/rssi_report.py docs/bridge/data/d1-cal-917400-flat-2026-09-19.log
python3 tools/simctl/rssi_report.py docs/bridge/data/d1-cal-917400-handheld-2026-09-19.log
```

| | Bridge board | Simnode Heltec | Rule, brief §5.2 step 3 |
|---|---|---|---|
| Floor, median per-minute mean | −115.2 dBm | −114.9 dBm | within 1 dB: **met** |
| Occupancy above −110 dBm | 436 samples, 0.1228 % | 421 samples, 0.1186 % | within 20 %: **met**, 3.5 % apart |
| Davis peak, median over the hour | −72 dBm, 21 of 27 hops caught | −73 dBm, 19 of 27 caught | within 1 dB: **at the limit** |
| Davis peak, median before 14:09 | −73 dBm | about −73.5 dBm | |
| Davis peak, median after 14:09 | **−65 dBm** | −74 dBm | |

**Every source both receivers can hear, they heard in the same bucket.** 53 of the bridge board's
65 buckets with a sample above threshold have a partner in the simnode Heltec's file. A Davis hop
lasts 6.7 ms and each receiver samples every 10 ms. So one receiver often caught a hop at −72 dBm
while the other caught its edge at −105 dBm or missed it.

**The step at 14:09 belongs to the bridge board's position, not to its receiver.** From 14:10 on,
eight of its twelve Davis readings sit at −64 or −65 dBm, against −72 to −73 dBm before. The
simnode Heltec read −71 to −77 dBm throughout. Before 14:20, both boards read the −93 dBm
episodic source at −94 to −95 dBm. After 14:30, the bridge board read it 3 to 7 dB *weaker* than
the simnode Heltec, at −93 to −95 dBm against −88 to −90 dBm. A receiver that ran hot would read both
sources hot. A change in antenna orientation or surroundings changes the gain toward one
direction and not the other, and the Davis and the episodic source arrive from different
directions. **What changed at 14:09 is not recorded.** The operator was sending the thermostat
requests below at that time.

**What that means for day 1.** A peak level compared across the two files carries this
direction-dependent uncertainty, up to about 9 dB. Occupancy, which tests 2 and 3 mostly rest
on, agreed to 3.5 % over the hour.

**`rssi_report.py` split the Davis across two bands on the bridge board**: −80 dBm before the
step and −70 dBm after. It reported the Davis as periodic in the first, with 10 of 26 hops caught,
and as not periodic in the second. The Davis figures in the table assign hops by their position on
the 130.69 s lattice instead, using a scratch script that is not in the repository.

### Z-Wave on the property does not reach −110 dBm on 917.4 MHz

**The ZEN17 in the basement is the better test, and it shows nothing.** It reports water pressure
every 30 s, about 120 reports an hour. It is built on 700-series silicon and reports to an Aeotec
Gen5 stick, a 500-series controller, so both ends support 100 kbps on 916.00 MHz. That the link
actually uses that rate is inferred from the silicon and not confirmed. A report and its ACK at
100 kbps add up to a few milliseconds of air each, so a sampler reading every 10 ms would catch a
large share if they reached −110 dBm. The excursions were checked for pairs separated by 29 to 31,
59 to 61 and 89 to 91 s:

| Capture | Excursions peaking below −85 dBm | Pairs at those spacings | Average pairs per 1 s of spacing |
|---|---|---|---|
| Calibration hour, bridge board | 44 | **1 at 30 s, 0 at 60 s**, 3 at 90 s | 0.51 |
| Calibration hour, simnode Heltec | 43 | **2 at 30 s, 0 at 60 s**, 8 at 90 s | 0.58 |
| 917.6 MHz, 10 h | 1,101 | 24 at 30 s, 46 at 60 s | 35.1 |
| 917.4 MHz, M25, 10 h | 948 | 38 at 30 s, 65 at 60 s | 26.6 |
| 917.2 MHz, 10 h | 1,531 | 102 at 30 s, 136 at 60 s | 79.1 |

**A ZEN17 visible at 917.4 MHz would have put dozens of events an hour on a 30 s grid.** The
calibration hour has none: its pairs at 30 and 60 s sit at or below the average for any spacing.
The simnode Heltec's eight pairs near 90 s come from episodes several buckets long, which pair
with each other, and 30 and 60 s show nothing to match. M25 and the 917.2 MHz capture show a slight excess at 30 s, about two
standard deviations, which is not a period. Their excess at 60 s is the bridge's own polls, once a
minute to each of two peers; the calibration hour had no bridge. **So Z-Wave's 100 kbps channel,
1.4 MHz below 917.4 MHz, stays below −110 dBm at the bench**, provided the ZEN17 uses it.

**The thermostat test could not have seen the thermostat.** It is a Trane TCONT624 from about
2014, which the operator believes is not Z-Wave Plus. A device of that age transmits at 9.6 or
40 kbps on 908.4 MHz, 9 MHz below 917.4 MHz. The repeat at 10:09:00, 10:11:00 and 10:13:00 EDT
matched nothing: the controller's log (Indigo) stamped the reports at 14:09:00, 14:11:30 and
14:13:00 UTC. The minute summaries covering 14:09:00 and 14:11:30 counted no sample above
−110 dBm on either board. The nearest excursion to 14:13:00 came 11 s after it, and Indigo can
group entries under one stamp later than the traffic, not earlier. Each board also caught
excursions at 14:09:47 and 14:10:49 that match the background: short, −98 to −106 dBm, three to
five samples, the same shape as five events between 13:44 and 14:09. **The HVAC fan's hourly
cycle leaves no trace either.** Sorted by minute of the hour, minutes :00 to :01 and :10 to :11
are no busier than the rest in any of the three ten-hour captures.

### What the captures establish for D1

**Four findings hold across every capture so far:**

1. **The Davis is the only periodic occupant, and only at 917.4 MHz.** It visits for 6.7 ms every
   130.69 s, a duty of 0.005 %, and reads −64 to −77 dBm at the bench depending on the receiver's
   position. It leaves no trace at 917.2 or 917.6 MHz, so the receiver rejects it by at least
   33 dB at 0.166 MHz offset.
2. **The −93 dBm episodic source appears at all three frequencies at similar levels.** Brief §5.3
   says a source like that does not separate the candidates.
3. **Z-Wave does not reach any candidate**, on the ZEN17's evidence, if its link runs at 100 kbps.
4. **Occupancy runs 0.1 to 0.3 % at every candidate**, and almost all of it lies below −100 dBm.

**Overlap per frame is about 1 % from each source, at bench levels, before any retry.** A
maximum-length SF9 frame lasts 1,107 ms. It overlaps a Davis hop with probability (1.107 +
0.0067) / 130.69, which is 0.85 %. In the calibration hour, 14 non-Davis events on the bridge board
reached −100 dBm, the level of a GateLink frame at the bridge. With bucket granularity that
bounds overlap at about 0.8 % per frame. The Davis is periodic and a retry goes out within
`backoff_max_ms` = 1500 ms, so two consecutive frames cannot both land on its hop. These are
bench-position figures. GateLink sits nearer the Davis transmitter, and nothing has been measured
there.

**Each candidate has one finding still open:**

| Frequency | Against it | What settles it |
|---|---|---|
| 917.4 MHz | The Davis is a co-channel occupant, so D33 standing condition 3 is not met as written | An operator decision to accept a 0.005 % periodic occupant, recorded in the Decision Register |
| 917.2 MHz | The −89 dBm source from 17:57 to 23:49 UTC on 2026-09-18 | **Day 1**, now running: whether 917.4 MHz shows the source over the same hours |
| 917.6 MHz | 18 bursts at −45 to −47 dBm | A capture at 917.6 MHz with no other board powered |

---

## 2026-09-19 — the parallel run moves to the production bridge location and restarts

**Day 1 at the bench was stopped at 15:28 UTC, 44 minutes in, and the run restarted at the
bridge's target location with its own calibration hour.** The bench was needed for another
project. A capture that spans a move is two captures, and the first calibration hour showed that
moving one board changes its reading of a source by up to 8 dB, so no figure could be carried
across the move. The 44 minutes covered none of the evening hours that day 1 exists to test.
Their two files, `d1-par-917400-flat-2026-09-19.log` and `d1-par-917200-handheld-2026-09-19.log`,
stay uncommitted. The arming script was stopped first. Its background captures ignored SIGINT,
as background jobs of a non-interactive shell do, and were ended with SIGTERM.

**The new position, as the operator described it.** The office, against the NW wall, at desk
height: M20's `bridge-house` site. The bridge board sits at its target location with the 3.0 dBi
stick, and the simnode Heltec sits about 1.5 m from it. Also powered nearby: the laptop running
the captures on AC power, an external monitor, and a Bluetooth keyboard and mouse. Bluetooth
works at 2.4 GHz, outside the band; the monitor is recorded because it is new to these captures.

**The run.** Nothing was reflashed. The bridge board runs `chan-capture` `35c8471` on
`/dev/cu.usbserial-0001`, and the simnode Heltec runs `6bf9a38`, now on `/dev/cu.usbserial-3`.
Both were identified by MAC before the start. The calibration hour began at 15:37:59 UTC with
both on 917.4 MHz. The 24-hour run follows it, with the simnode Heltec on 917.2 MHz. The files
carry `office` in their names:

| File | Board | Frequency |
|---|---|---|
| `d1-cal-917400-flat-office-2026-09-19.log` | bridge board | 917.4 MHz, 1 h |
| `d1-cal-917400-handheld-office-2026-09-19.log` | simnode Heltec | 917.4 MHz, 1 h |
| `d1-par-917400-flat-office-2026-09-19.log` | bridge board | 917.4 MHz, 24 h |
| `d1-par-917200-handheld-office-2026-09-19.log` | simnode Heltec | 917.2 MHz, 24 h |

**Every earlier capture was taken at the bench**, so a figure from these files compared with M25,
the 917.2 MHz capture or the 917.6 MHz capture also compares two locations. Day 1's own test does
not: it compares two frequencies over the same hours at one location.

## 2026-09-19 — the office calibration hour: the external monitor raised both floors, and the run restarted without it

**The first office calibration hour failed brief §5.2 step 3 on all three counts, and the cause
was the external monitor.** With the monitor off, a second calibration hour passed on floor and
occupancy and failed on the Davis peak alone, which depends on direction. Day 1 started at
17:56:58 UTC with the monitor disconnected, and runs as armed.

### The first hour, monitor on

The hour ran from 15:37:59 to 16:37:59 UTC, with both boards on 917.4 MHz at the positions the
previous entry records:

| | Bridge board | Simnode Heltec | Step 3 |
|---|---|---|---|
| Floor, median per-minute mean | −114.6 dBm | **−112.8 dBm** | 1.8 dB apart: fails |
| Davis, median peak | −66 dBm | −60 dBm | 6 dB apart: fails |
| Occupancy above −110 dBm | 512 samples, 0.144 % | **42,224 samples, 11.9 %** | 83 times: fails |

**The simnode Heltec's occupancy was its own floor.** 3,336 of its 3,401 excursions fell in the
−110 dBm band. Its floor sat about 2.8 dB under the threshold, so ordinary noise crossed it.

**Both floors fell together for three minutes.** From 15:47 to 15:49 UTC the simnode Heltec read
−116.0 dBm and the bridge board −115.9 dBm, and the simnode Heltec's occupancy went to zero.
Both came back at 15:50. The operator knew of no transmitter nearby. The monitor blanks when the
screen does, and sits about 0.3 m from the simnode Heltec and about 1 m from the bridge board.
The simnode Heltec's USB port on the laptop is next to the HDMI port.

**Disconnecting the monitor settled it.** The operator unplugged the monitor and switched it off
at 16:52 UTC. Day 1 had started at 16:38:25 and was running, so the step shows in its files:

| Per-minute floor | 16:50–16:52, monitor on | 16:53–16:55, monitor off |
|---|---|---|
| Bridge board, 917.4 MHz | −114.7 dBm | **−115.9 dBm** |
| Simnode Heltec, 917.2 MHz | −112.3 dBm | **−116.0 dBm** |
| Simnode Heltec, samples above −110 dBm per minute | 1,792–2,232 | **7–17** |

**The monitor's noise reached 917.2 MHz as it reached 917.4 MHz**, so it raised the floor
without favouring either frequency. It cost the simnode Heltec 3.7 dB at 0.3 m and the bridge
board 1.2 dB at 1 m. **The bridge board is at its production position**, so that 1.2 dB is what
the monitor costs the bridge's receiver whenever both are on. It has not been measured at any
other spacing.

**Day 1 was stopped at 16:56:06 UTC and the run restarted from a fresh calibration hour.** The
four files taken with the monitor on are committed under `-monitor` names, as evidence for this
entry rather than as D1 data:

| File | What it holds |
|---|---|
| `d1-cal-917400-flat-office-monitor-2026-09-19.log` | The first calibration hour, bridge board |
| `d1-cal-917400-handheld-office-monitor-2026-09-19.log` | The first calibration hour, simnode Heltec |
| `d1-par-917400-flat-office-monitor-2026-09-19.log` | Day 1's first 18 minutes, bridge board, and the step at 16:53 |
| `d1-par-917200-handheld-office-monitor-2026-09-19.log` | Day 1's first 18 minutes, simnode Heltec, and the step at 16:53 |

### The second hour, monitor off

This hour ran from 16:56:32 to 17:56:32 UTC. Nothing else changed: the same boards, images,
positions, laptop and Bluetooth devices.

| | Bridge board | Simnode Heltec | Step 3 |
|---|---|---|---|
| Floor, median per-minute mean | −115.9 dBm | −116.0 dBm | 0.1 dB apart: passes |
| Davis, median peak | −68 dBm, 25 of about 27 hops | −60 dBm, 20 hops | 8 dB apart: fails |
| Occupancy above −110 dBm | 2,527 samples, 0.712 % | 2,663 samples, 0.750 % | 5.4 % apart: passes |

**Step 3 acts on the next day, not on day 1.** It puts each receiver on the other's frequency
*for the next day*. Day 1 therefore runs as armed, and the swap applies to day 2 if day 2 is
run. The failure is on the Davis peak alone. At the bench, the bridge board's Davis reading
stepped 8 dB with nothing recorded as moved, so a peak compared across two boards measures
direction as much as either receiver. Day 1 should be read on occupancy, as the previous
calibration entry concluded.

**164 of the bridge board's 169 recorded buckets coincide with the simnode Heltec's**, so the two
boards heard the same channel.

**917.4 MHz carried an episodic source near −95 dBm through hour 17 UTC.** On the bridge board,
buckets peaking between −100 and −90 dBm rose from 2 to 9 per ten minutes over 15:40–16:30 to 20
to 28 per ten minutes over 17:00–17:30. The monitor could not have hidden them: they sit about
18 dB above even the simnode Heltec's raised floor. The largest episodes peaked at −95 dBm and lasted 2 to 17 s. The
band holds 2,222 of the hour's 2,527 samples above threshold, which is why this hour's occupancy
is six times the bench calibration hour's 0.123 %. The bench hour covered other hours, at another
location.

**This is the signature 2026-09-18's 917.2 MHz capture found in hour 18**, episodes at −94 to
−96 dBm. It is not the −89 dBm evening source: the −90 to −80 dBm band holds 6 buckets here,
against 356 in that capture. Whether the −89 dBm source reaches 917.4 MHz is day 1's question,
from 17:57 UTC.

**Day 1's files** keep the names the previous entry gave them. Both boards were checked by MAC
and frequency before the start: the bridge board `35c8471` on 917.4 MHz, the simnode Heltec
`6bf9a38` on 917.2 MHz. Both 24-hour captures end at about 17:57 UTC on 2026-09-20.

---

## 2026-09-20 — day 1 is in: 917.2 MHz is busier in all 24 hours, and the Davis holds its clock

**Both 24-hour captures ran to completion and closed clean.** Opened 2026-09-19T17:56:58Z,
closed 2026-09-20T17:56:58Z, 23.99 h over 86,350 buckets, **8,634,999 samples each with one
skipped sample each**, 1440 `CHANSUM` minutes each and no gap. The bridge board ran
`chan-capture` `35c8471` on 917.4 MHz and the simnode Heltec `6bf9a38` on 917.2 MHz, both
confirmed from the files' `CHAN-BOOT` lines. Nothing moved, nothing transmitted and the
external monitor stayed off for the whole run.

| File | Board | Frequency |
|---|---|---|
| `d1-par-917400-flat-office-2026-09-19.log` | bridge board | 917.4 MHz |
| `d1-par-917200-handheld-office-2026-09-19.log` | simnode Heltec | 917.2 MHz |

### The result

| | 917.4 MHz | 917.2 MHz |
|---|---|---|
| Occupancy above −110 dBm | **0.2095 %** | **0.3050 %** |
| Median floor | −115.9 dBm | −116.0 dBm |
| Strongest sample | −42.0 dBm | −39.0 dBm |
| −90 to −80 dBm buckets | **68** | **713** |
| −70 to −60 dBm buckets | **510** | 33 |

**917.2 MHz carried more occupancy in 24 of 24 full hours**, by 1.06 to 2.64 times, mean 1.45.
No hour ran the other way. Hour-by-hour occupancy on the two channels correlates at
**r = 0.978**, so the property's own activity moves both receivers together and the 917.2 MHz
excess sits on top of that common signal. This is what the parallel run was for.

**Brief §5.3 test 1 fails for 917.2 MHz** on its occupancy and episodic-source conditions.

**The gain bias does not explain the −90 to −80 dBm band.** If that source reached 917.4 MHz
8 dB down it would land in the −100 to −90 dBm band, which instead tracks across the two files
at 838 buckets against 1043 — a difference of 205, nowhere near the 713 it would have to
absorb. The −110 to −100 dBm row is weighed lightly as before: the simnode Heltec's floor sits
0.1 dB lower and more excursions clear the fixed threshold.

### The Davis, confirmed over a full day

**The 917.4 MHz −70 to −60 dBm band fits 130.6882 s with a median residual of 0.54 s**, across
660 occurrences, with 92 % of the 509 caught events within 2 s of the grid and a catch rate of
0.77. The band holds 16 to 25 buckets an hour, every hour. The 2026-09-18 figure of 130.69 s
holds, now over 24 hours rather than one.

### The 917.2 MHz source is not the Davis and is not an evening source

**It runs in all 24 hours**, 15 to 51 buckets an hour, heaviest at 04 and 12–13 UTC. The
2026-09-18 entry read it as an evening source because that capture ran 17:57 to 23:49 UTC. It
is continuous, which is worse than the brief assumed when it weighed this source against the
Davis's 6.7 ms every 130.69 s.

**Its gaps cluster at 130 s often enough to suggest the Davis at a reduced level, and it is
not.** Two tests rejected that: only 22 of its 577 events, 3.8 %, fall within ±2 s of a
917.4 MHz Davis event, against 4.0 % with the times shifted by 65 s as a control; and only 13
of 577, 2 %, land within 2 s of a 130.69 s grid. It stays unidentified.

### One wideband event, on both channels

**At 13:25:22Z both boards recorded their strongest excursion of the day in the same second** —
−42.0 dBm at 917.4 MHz and −39.0 dBm at 917.2 MHz, one bucket wide, 4 and 9 samples above
threshold, and the only −60 dBm-and-up bucket in either file. At least 200 kHz wide, keyed once
in 24 hours, unidentified. It reads on both channels, so it does not separate them.

### `rssi_report.py` called the Davis "not periodic", and the tool is wrong

**The printed verdict for the −70 dBm band was "not periodic (509 events)"**, on the same
events that fit a clock to half a second. Two defects in `periodicity()` combine and both grow
with capture length: a gap shorter than half the guessed period is charged a whole period, so a
foreign event invents an occurrence — 687 against 660 here, dragging the fitted period to
125.59 s — and the verdict gates on the **maximum** residual, 660.7 s here against a median of
0.54 s, so one foreign event flips a clean clock. Neither shows over one hour.

**Left unfixed by operator direction**, since no capture is planned and the firmware is parked.
The figures above were computed by seeding a fit with a known period and reading the residuals
directly, not from the tool's verdict. `firmware/chan-capture/CLAUDE.md` and the `periodicity()`
docstring both carry the warning now.

### What this entry does not do

**It records no decision.** The operator's direction on reading this was that **917.4 MHz is the
target moving forward**, which declines brief §6's move. D1's and D33's status live in the
Decision Register and are not changed here, and the brief is not yet marked superseded.
[`LRAN-D1-Parallel-Capture-Analysis`](../shared/LRAN-D1-Parallel-Capture-Analysis.md) carries
the full reading.

---

## 2026-09-21 — the interleaved sweep: spacing is real, and a second variable rides on it

**Two sweeps, 1280 flood frames, 27 lost. At a 250 ms gap the loss rate was 3.91 %; at
2000 ms it was 0.31 %. The denser arm lost more in all four run-by-half cells**, which is
what no sweep before this one could show.

**Every sweep on record until today ran one spacing to completion before starting the
next**, so a slow change in the environment and an effect of spacing produced the same
table. That is why 2026-09-17's morning measured 5.6 % at 250 ms falling to 0 % at
2000 ms, and why the same afternoon measured 2.50 % and 1.90 % with a 2000 ms control
that lost 8 %. Impl Plan §8.1 named the interleaved run as the thing that would settle
it. It has now run twice.

### The runs

`tools/simctl/sweep_interleave.py`, new today. Bridge on **`5c5f324`**, read from its own
`lran/bridge/version`; `firmware/bridge/` is byte-identical between that commit and
`main`, and the branch's only firmware differences are the `NodeConfigV1` rename and
CONFIG handling in the protocol library, which a `STATUS` flood never reaches.

XIAO + Wio simnode sending from **`0xF3` in `ROLE_FAULT`**, a role that answers no `POLL`,
so the only frames that identity sends are the burst. Both simnode ports were held open
for the whole sweep and the Heltec simnode was silenced outright, because a disabled
identity re-enables itself on the next boot.

| | run 1 | run 2 |
|---|---|---|
| Started, UTC | 14:46:06 | 15:05:33 |
| Wall clock | 15.9 min | 15.8 min |
| Frames | 640 | 640 |
| Lost | 17 | 10 |

Committed as [`data/sweep-interleaved-2026-09-21-run1.json`](./data/sweep-interleaved-2026-09-21-run1.json)
and [`data/sweep-interleaved-2026-09-21-run2.json`](./data/sweep-interleaved-2026-09-21-run2.json),
re-read with `sweep_interleave.py --read`.

**Sixteen bursts of 40 frames each, alternating, with the leading arm swapped every pair**:
250, 2000, 2000, 250, 250, 2000, … A plain rotation puts every 250 ms burst before its own
2000 ms burst, so an effect that decayed through a pair would land entirely on one arm.

### What the cross-tab says, and why the two-way splits do not say it

**Pooled by arm, both runs separate. Pooled by session half, run 1 separates too** — 1.25 %
against 4.06 % — and reading either split on its own gives a different answer. Holding time
still inside each half and comparing the arms there is what tells them apart.

| | 250 ms | 2000 ms |
|---|---|---|
| run 1, first half | 3 / 160 — **1.88 %** | 1 / 160 — **0.62 %** |
| run 1, second half | 12 / 160 — **7.50 %** | 1 / 160 — **0.62 %** |
| run 2, first half | 7 / 160 — **4.38 %** | 0 / 160 — **0 %** |
| run 2, second half | 3 / 160 — **1.88 %** | 0 / 160 — **0 %** |
| **pooled, both runs** | 25 / 640 — **3.91 %** | 2 / 640 — **0.31 %** |

**Four cells out of four put the denser arm higher.** The sparse arm lost two frames in
two sweeps, and both sat in a gap containing a bridge transmission.

**A second variable moves the dense arm's magnitude, and it is not a trend.** Run 1 went
1.88 % → 7.50 % through the session; run 2 went 4.38 % → 1.88 %, the other way. So the
dense arm's rate varies within a session by a factor of four in either direction while the
sparse arm sits still. **What that variable is remains unknown**, and it is the reason a
single block-ordered sweep could land anywhere between 0 % and 8 %.

### The frame log's cross-checks, and all of them are clean

**1253 receptions across the two runs, every one of them `Packet`.** Zero `Orphan`, so no
interrupt was missed at either spacing. Zero `PhyCrc`, zero `HeaderError`, zero
`DriverError`: **nothing arrived corrupt**. Every reception passed the receive ladder with
`Status::Ok`, so nothing was discarded at any spec §14 stage. **Every loss is a frame the
radio never delivered.**

**The ring never overwrote and nothing was lost in transport** in either run, so the gaps
are frames rather than bookkeeping.

**The bridge's own deafness can account for at most 1.15 of run 1's 17 losses and 0.99 of
run 2's 10.** Twenty-four gaps across the two runs, seven with a bridge transmission
inside; the largest single ceiling is 35.5 % of one gap and most are 3 to 7 %. The 250 ms
losses sit almost entirely in **612 ms gaps** — one frame missing between two arrivals
about two frame periods apart.

**The link is 12 dB weaker than the 2026-09-17 sessions and still nowhere near marginal.**
RSSI ran −52 to −48 dBm against that session's −39 to −36 dBm, because the bridge board
moved to its office production position for the D1 capture and has not moved back. SNR ran
+10 to +12 dB throughout. **Absolute rates here are not comparable with 2026-09-17's**; the
comparison inside each sweep is.

### One number the seq-gap count cannot produce

**`sent` minus `arrived` is 17 in run 1 where the `seq` gaps total 16.** The missing one is
a frame lost at a burst boundary, which no `seq` gap can see because there is no later
arrival to bound it. The sender's own TX_DONE delta is the denominator for that reason.

### What this closes, and what it does not

**Spacing is a variable.** The afternoon of 2026-09-17 made it a suspect rather than an
established one, and two interleaved sweeps put it back — on this geometry, at these two
spacings.

**The mechanism is still not known**, and nothing here narrows it. The three candidates
inside the bridge were ruled out on 2026-09-17 and stay ruled out; M25 found nothing on the
channel loud enough to matter at one metre. A frame the radio never delivered, announced by
no interrupt and leaving no counter, remains unexplained.

**Only two spacings ran.** The 1100 ms knee recorded on 2026-09-17 was not re-measured, and
the sweeps say nothing about where between 250 ms and 2000 ms the rate falls. Impl Plan
§8.1's table stands as taken.

**A loss rate measured at one metre is still not evidence about 87 m**, and it is optimistic
in the wrong direction.

---

## 2026-09-21 — BF-32 on air, and the three defects the bench found that the host could not

**The configuration path works end to end**: a `config/set` from Home Assistant applies
the bridge's half, sends the node's half as a `CONFIG`, and publishes one `config/ack`
carrying both. **Three defects stood between the host tests passing and that being true,
and none of them was visible at a desk.**

### What ran

Bridge on the bench board, XIAO + Wio simnode as `0xF1` in `ROLE_GATELINK`, sandbox
broker. Every figure below is from that session.

| Published to `lran/simnode1/config/set` | What came back |
|---|---|
| `{"set":{"dedup_cache_depth":16}}` | `{"op":"set","persist":"applied_not_persisted","results":{"dedup_cache_depth":{"status":"ok","value":16}}}` |
| `{"set":{"poll_interval_s":90,"backoff_max_ms":1200}}` | one ack carrying **both halves**, the bridge's row and the node's |
| `{"op":"get_all"}` | the bridge's per-node row **and** the node's own values |

`persist` reads `applied_not_persisted` because a simnode has no nonvolatile store, which
is spec §8.11 being honest rather than a fault.

**On the bridge's own topic**, measured the same afternoon: a set applies and persists, a
value outside its range is **clamped and said so** (`cad_retries` 99 → 10), an unknown
name is `unknown_param` with `not_applied`, a payload that is not JSON is refused whole
with a reason, `restore_defaults` puts every row back, and **a reboot restored the stored
values from NVS on both scopes** — `Config: 3 stored value(s) restored`.

### Defect 1 — the simnode answered a CONFIG under the wrong `seq`

**The bridge reported `unknown` for a set the simnode had already applied.** The frame log
shows why:

```
FRAME #5 tx peer=0xf1 type=10 schema=18 seq=1     the CONFIG
FRAME #6 rx peer=0xf1 type=11 schema=18 seq=3     the CONFIG_ACK
```

**`send_config_ack()` took its `seq` from the identity's status space for both the
solicited and the unsolicited case.** Spec §7.4.1: *"A solicited answer repeats the
request's `seq` on every message."* Correlation is by `seq` (§9.2), so an answer under
another number correlates to nothing — the bridge was waiting on 1 and 3 arrived.

**The specification is what settled it**, not the bridge's convenience: `send_config_ack`
now takes a `reply_seq`, and `kUseStatusSeq` asks for the status space for the unsolicited
readback that answers no request (**D45**). **A host test could not have found this.** Both
sides were internally consistent and the simnode's own suite passes either way; it took two
boards and a frame log.

### Defect 2 — a stack overflow on the first set aimed at a node

```
Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (sched)
```

**A `ConfigJob` is about a kilobyte** — the CONFIG payload, the names and the bridge half's
results — **and it was a local on `sched_task`'s 3072-byte stack.** It now receives
straight into static storage, which is the real correction.

**The size was wrong as well as the allocation, and the measurement is what says so.** With
the job off the stack, `uxTaskGetStackHighWaterMark` on the deepest path reported **84 bytes
free**. Raised to **5120**, which leaves about 2 KB. This node's `CLAUDE.md` asks for a size
corrected from a measurement rather than doubled after a crash, so the figure is now logged
on every configuration resolution.

### Defect 3 — a set's ACK blanked the rows it did not name

**After `{"set":{"poll_interval_s":90,"backoff_max_ms":1200}}`, the retained
`config/state` showed `dedup_cache_depth: null`** — a value the previous set had put there
and nothing had changed.

The state mirror replaced wholesale. **That is right for a `GET_ALL` answer, which
describes a node's entire table, and wrong for a `SET`'s ACK, which describes only the
parameters it set.** Two named methods now: a readback replaces, a set's results merge.
Spec §7.4.1 argues the replacing case explicitly — a parameter the node stopped reporting
must not keep a value from an older answer and read as current — and the merging case is
its mirror image.

### Two size limits moved, each because a check fired

**`kMaxPayloadLen` 768 → 1024, `MQTT_MAX_PACKET_SIZE` 1024 → 2048.** Two documents arrived
near the old line in one afternoon:

- **`test_diag`** refused the radio document once the config queue added a sixth pair of
  queue counters, at every counter's `UINT32_MAX`.
- **`test_config`** left about **twenty bytes** spare on the bridge's own `get_all` answer.
  GateLink's counted 25 parameters (**W10**) would have crossed it.

**The failure both would have produced is a DROPPED publication**, so the entity keeps a
stale value and nothing says why. Both tests build the worst document their table can
produce; each fired while the code was being written rather than at the broker.

### One bound set from an answer rather than chosen

**`kMaxConfigSetEntries` is 8, and it is not a round number.** Every name in a `set` gets a
result in the one `config/ack` that answers it. At the longest name the parser accepts, an
unknown one costs about 82 bytes against about 700 of room, so nine names would not fit —
and a document that does not fit is dropped, leaving the operator with no answer at all
rather than a partial one. It was 16 until the fit test said otherwise.

## 2026-09-23 — BF-23's lever half: each bridge row reaches its consumer, host-tested only

**What changed.** Each bridge row of BF-32's configuration table now reaches the code it
configures, except `simnode_diag_enable`, which is BF-26's. Before this, Home Assistant
could set a value, NVS could keep it, and `config/state` could report it, while the lever
went on running its compile-time default. Impl Plan §4.4.2 has the design and the reasons
behind it. No board was touched, so **none of it is confirmed on air.**

### The handoff's list was the right one, and the marker was not

A grep for `TODO(BF-23)` found four sites. The handoff's table named eight consumers, and
all eight are wired. `missed_poll_threshold`, `command_ack_timeout_ms`, `cmd_retries` and
`config_readback_timeout_ms` carried no marker at all. **A TODO marker is not an
inventory.** The table is, and root rule 8 is the reason each row exists.

### Two gaps the design had to close before any code

- **A set with a node half returns early.** `handle_config_set()` queues the node half's
  job and returns before the block that republishes `config/state`. A lever publish placed
  beside that block would never run for a `poll_interval_s` set together with a node's own
  rows. That set would reach the store and not the scheduler. The publish sits
  immediately after the store write instead.
- **A shorter poll interval waited out the longer one.** `PollScheduler::on_sent()` fixes a
  node's next due time when its poll goes out. Dropping 3600 s to 60 s would have
  produced no poll for up to an hour. `retime()` now counts the new interval from the last
  poll.

### One test caught a mistake in the test

The first `retime()` test asserted no poll at 120 199 ms. GateLink had come due at 60 s on
its new 30 s interval. The scheduler was right and the expectation was wrong. The test now
moves GateLink out of the way first.

### Found in passing, not fixed here

**`g_config` is used from two tasks with no lock between them.** `mqtt_task` calls
`apply()`, `restore_defaults()`, `read_all()` and `state()`. `sched_task` calls
`note_readback()`, `note_set_results()` and `state()` when a node transaction resolves
(`publish_config_resolution()`). Neither `ConfigStore` nor `Store` takes a lock. BF-32
introduced this, not the lever half: `levers_from()` runs in `setup()` and then only on `mqtt_task`, which is
the only task that writes the stores. It is recorded in the handoff for its own change.

### What the bench owes

- `levers: gen N` on serial at boot, carrying the values NVS restored rather than the
  defaults.
- A `diag_interval_s` set that changes the spacing of `lran/bridge/diag/state`. That is
  also V-B12's saturated-arm lever (Impl Plan §8.1).
- A `poll_interval_s` set on a simnode, with the next poll arriving on the new interval
  counted from the last one.
- `sched_task`'s high-water mark after all three. It logs one on every configuration
  resolution.

## 2026-09-23 — BF-23's lever half on air: all four bench checks pass

**All four checks the lever-half entry above listed pass on the bridge board**, flashed over
USB from `7edfe17`, a committed tree. One simnode was on the bench, the Heltec in the
handheld case. The broker was down when the session started and came back about 12 minutes
later. Every step below ran after that.

| Check | What the bench showed |
|---|---|
| NVS restore at boot | Set `diag_interval_s` to 20 on the bridge and `poll_interval_s` to 60 on `simnode2`, then reset the board. Serial showed `Config: 2 stored value(s) restored`, then `levers: gen 2 - diag 20 s`. `lran/bridge/diag/state` resumed at 20 s spacing |
| `diag_interval_s` moves `diag/state` | Set 60 → 20 s. Serial showed `levers: gen 4 - diag 20 s`. Publications went from 60 s spacing to 807, 827, 847 and 867 s on the host clock, 20 s apart to within 10 ms |
| `poll_interval_s` counts from the last poll | f2 polled at 995.05 on a 20 s interval, and a set to 60 s arrived at about 1005. The next poll went out at 1055.05, exactly last + 60, not set + 60 |
| `sched_task` high-water mark | **1976 bytes free** of 3072, on both a completed `get_all` (outcome 1, on f1) and one abandoned after an `unknown` (outcomes 2 then 4, on f2) |

**The poll-interval set also shortened the interval.** The first set, 60 → 20 s, arrived
just as the old 60 s poll fell due, so it could not tell "counted from the last poll" from
"counted from the set". The 20 → 60 s set above does tell them apart. After the first set,
f2 was polled at 906, 935 and 955. The 29 s gap was f2 waiting behind a poll to GateLink
`0x01`, which is offline and held its 10 s reply window open from 925. That is the
scheduler's one-poll-outstanding rule, not the lever.

### My mistake: a `get_all` aimed at a `ROLE_HEALTH` identity

The first `get_all` went to f2, a `ROLE_HEALTH` identity, and the simnode answers
`CONFIG` only in `ROLE_GATELINK` (`node.cpp`). Its `stats f2` showed `unhandled 1`. The
bridge behaved as spec §7.4 asks: `unknown` at 8 s, then a readback that was abandoned at
15 s, **two `config/ack` publications by design**, the second carrying the readback's
outcome. The repeat on f1, added as `ROLE_GATELINK` because the XIAO that normally holds
f1 was unplugged, completed with outcome 1. **Use a `ROLE_GATELINK` identity for any
configuration check on the node half.**

### A defect the run found: `get_all` counts as a change

`read_all()` reports the store's persist status, which is `persisted`, so
`handle_config_set()` treats a `get_all` as a set that changed something:

- it republished the lever board, which moved the generation 8 → 10 → 12 over two `get_all`
  requests;
- it set `bridge_changed` on the node job, so `sched_task` republished `simnode2`'s
  retained `config/state` on the `unknown` and on the abandoned readback, with the same
  contents, at 1089 and 1104.

Both contradict the comments at those sites. A republished lever board is harmless, because
`sched_levers()` retimes only a node whose interval moved. A retained document rewritten with
its own contents is a new message to every subscriber, which spec §16.7.4's rule exists to
prevent. Fixed on this branch, in its own commit.

### Bench state left behind

Both overrides are cleared, each with `restore_defaults`: on `lran/bridge/config/set`
for `diag_interval_s`, and on `lran/simnode2/config/set` for the per-node row, which the
bridge's own restore does not reach. The simnode holds f1 in `ROLE_GATELINK` in RAM only, and loses it
on its next boot.

### Still owed

**V-B12's saturated arm** (Impl Plan §8.1): its lever is confirmed, and the arm has not
run.

## 2026-09-23 — The configuration lock, `config_ack_timeout_ms`, and a `seq` gap after a bridge restart

**Three changes on `b4-bf23-levers` were flashed from `52727b8` and checked on the bridge
board**, and the run found a gap in the specification.

- **`get_all` is no longer a change.** `config_set_changed()` now decides whether a set
  changed a stored value, from the op as well as the persist status. A `get_all` to f2
  left the lever generation at 4, and neither the `unknown` nor the abandoned readback
  republished `lran/simnode2/config/state`.
- **`ConfigLock`** guards `g_config` between `mqtt_task` and `sched_task`, which is Impl
  Plan §6.7.6. No deadlock or stall showed in the run. `sched_task`'s high-water mark
  read 2324 and 2120 bytes free, against 1976 before, so the lock did not deepen its
  stack. A race the lock closes does not show on a bench whether or not the lock is
  there, so this run shows only that the lock does no harm.
- **`config_ack_timeout_ms`** (`0x000C`) reaches `ConfigPath`. With the row at 3000, the
  `unknown` for a `CONFIG` sent at 1836.47 published at 1839.43, 2.95 s later instead
  of 8 s. The boot line now prints `config ack %u ms`.

### A bridge restart reuses command `seq` values a node has already seen

**After the reflash, the bridge's first `CONFIG` to f1 was answered from the node's dedup
cache and not applied.** The simnode logged `config f1 <- 00 seq 1: dedup hit,
DUPLICATE_CACHED, not applied`. The bridge had rebooted, so its command `seq` for f1
started again at 1. The simnode had not rebooted, so f1 kept its `ctx_id`, its
`rx_high_water` and its dedup cache, which still held seq 1 from the `get_all` sent before
the reflash.

A `get_all` recovers from that, by readback. It went `unknown` at 3 s and completed at
outcome 3, `ReadbackOk`. **A command would not recover.** Spec §9.4 step 4 returns the
cached result of a different, earlier command and does not execute it, so Home Assistant
would see a gate command succeed while the gate did not move. `CommandPath::on_ack()`
publishes a `DUPLICATE_CACHED` as acknowledged, with the cached `detail`. A `seq` at or
below the high-water mark that is not cached draws `REJECTED_SEQ` at step 5. The bridge
ends that command as a rejection and does not resync, so each later command fails in turn
and spends one `seq`, until the bridge passes the node's high-water mark.

**The specification does not cover this case.** §10.2 resets the bridge's command `seq`
only when the bridge learns a new `ctx_id`, and §10.1 says the bridge has no context of
its own. Nothing covers a bridge restart against a node whose context survived it. For
GateLink that is every bridge reflash, OTA update or power cut. Not fixed here: the
specification is binding, so this needs a decision. Candidates are to persist the command
`seq` in NVS, to have the node reject a stale `seq` in a way that makes the bridge resync,
or to have the bridge force a new node context after its own boot.

**The bench reproduces it in one step.** Send any authenticated frame to a `ROLE_GATELINK`
simnode identity, reboot the bridge without rebooting the simnode, and send another.

## 2026-09-23 — BF-34 built: every node's context rolls after a bridge boot, host-tested only

**BF-34 builds spec v0.13 §10.6 on branch `b4-bf34-context-roll`.** It closes the gap the
entry above found. Nothing is on air yet. The library suite passes 135 tests, the bridge
suite 334 and the simnode suite 115. The `heltec`, `simnode-heltec` and `simnode-xiao-wio`
targets all build.

- **Library.** `Cmd::RollContext` (`0x12`), `kRollContextGuard` (`0xA5`) and
  `CommandGate::any_in_flight()`. An evicted in-flight entry does not count as in flight.
- **Simnode.** A roll skips the gate. It answers `ACTUATOR_BUSY` while a command is in
  flight. Otherwise it takes a new `ctx_id` and resets the gate and `tx_seq`, and changes
  nothing else.
- **Bridge.** `context_roll.{h,cpp}`. Every node starts pending and is rolled when first
  heard. Until its roll completes, a command is refused on `cmd/ack` and a node-held
  `config/set` is refused whole on `config/ack`. `ctx_rolls` and `ctx_roll_failed` are on
  `lran/bridge/diag/state`.

**The operator decided two points during the build.** Both depart from the letter of what
was written, and Impl Plan §6.2.2 records them:

1. **A bench row keeps the 2026-09-14 heard-first poll rule.** Spec §10.6 step 1 asks for a
   boot `POLL` to each registered node. The poll scheduler already sends one to every
   production row. A bench row gets none, and rolls when the bridge first hears it.
2. **Every simnode role answers `ROLL_CONTEXT`**, where the BF-34 row named
   `ROLE_GATELINK` alone. A `ROLE_RANGE` or `ROLE_HEALTH` identity that ignored the roll
   would draw another on every frame the bridge heard, which puts roll traffic inside a
   sweep.

**The first consequence to expect on the bench.** After a bridge reflash, a simnode
identity refuses commands and `CONFIG` until the bridge hears it. `push f1` starts the
roll. `traps.md` has the entry, which replaces the one telling the operator to reboot the
simnode as well.

**What the bench run must show**, using the one-step reproduction above:

1. Flash both boards from a committed tree, and hold both serial ports open for the run.
2. Send f1 a `get_all` and a command, so its cache holds seq 1 and 2.
3. Reflash the bridge and leave the simnode running.
4. Send f1 a command before it is heard. Expect `{"outcome":"context_roll_pending"}` on
   `cmd/ack`, and `cmd_refused_roll_pending` 1.
5. `push f1`. Expect `roll: f1 rolled to ctx …` on the bridge, and `roll f1 <- 00 seq 1:
   ctx … -> …, ACCEPTED` on the simnode. `ctx_rolls` should read 1.
6. Send the command again. Expect it executed and `acked`, and **no `DUPLICATE_CACHED`**,
   which is the falsifier for the whole task.

Three branches that are hard to reach on air are host-tested only. An `ack f1 suppress 1`
before step 5 reaches spec §10.6 step 4, where `REJECTED_CTX` completes the roll. An
`ack f1 delay` with a command in flight reaches the `ACTUATOR_BUSY` retry. A roll aimed
at no board exhausts its retries and counts `ctx_roll_failed`.

**One interaction for the next state-mirror work.** The open item that invalidates the
state mirror when a node's `ctx_id` changes must not fire on a roll. A roll changes the
`ctx_id` and keeps the node's configuration (spec §10.6 node step 2), so a readback there
would cost a frame and tell the bridge nothing new.

## 2026-09-23 — BF-34 on air: no `DUPLICATE_CACHED` after a bridge reflash, and all three host-only branches pass

**The six steps in the entry above pass on air, and the falsifier did not appear.** After
the bridge reflash, f1's first command executed and was acknowledged, where the same
sequence drew `DUPLICATE_CACHED` before BF-34. All three boards ran `98b4b04`, flashed from
a clean tree, and every banner showed that hash without `-dirty`. The bridge board was at
its production position. The XIAO held f1 in `ROLE_GATELINK` from NVS, and its port stayed
open from 14:41:51 to 14:50:03 EDT. The link ran −27 to −26 dBm at +11 dB SNR. The full
trace, with serial, MQTT and control lines interleaved, is
[`data/bf34-bench-2026-09-23.log`](./data/bf34-bench-2026-09-23.log).

| Step | What happened | Verdict |
|---|---|---|
| 1 | All three boards flashed from `98b4b04`. The harness held the XIAO's port and the bridge's port open. It closed the bridge's port only for the reflash | done |
| — | A new bridge image starts with every roll pending, so `push f1` rolled f1 first: `ctx 0xaa825cb7 -> 0xdb4d658f`, one attempt | pass |
| 2 | A `get_all` took seq 1 and an `OPEN` took seq 2. Both completed, and the `OPEN` was `acked` | done |
| 3 | Bridge reflashed from the same tree. The XIAO did not reboot | done |
| 4 | An `OPEN` before f1 was heard drew `{"outcome":"context_roll_pending"}` on `cmd/ack` in 1.2 s. Nothing went on air, and `cmd_refused_roll_pending` read 1 | pass |
| 5 | `push f1`. The simnode logged `roll f1 <- 00 seq 1: ctx 0xdb4d658f -> 0x14f23d00, ACCEPTED`, and the bridge logged `roll: f1 rolled to ctx 0x14f23d00 after 2 attempt(s)`. `ctx_rolls` read 1 | pass |
| 6 | An `OPEN` went out at seq 1. The simnode logged `OPEN ACCEPTED`, and `cmd/ack` carried `"outcome":"acked"`. **No `DUPLICATE_CACHED` anywhere in the trace** | pass |

**`sched_task` read 2320 bytes free of 3072** on the first configuration resolution, the
`get_all` in step 2. That is inside the 1976–2324 range measured before the roll existed,
so `sched_roll()` did not measurably deepen the task. It was the run's only configuration
resolution.

### Step 5's first roll attempt went unanswered, and the cause is not known

**The roll that step 5 started took two attempts.** The session's other two rolls without
an injected fault took one each: the first roll after flashing, and the re-roll after the
`ctx_roll_failed` run below. In that run the bridge heard f1's push, marked
simnode1 online and sent its heard-first `POLL` before the roll. The roll followed 210 ms
after the `POLL`, at 14:45:09.04. The simnode logged no receipt of that first roll, and the
bridge heard no poll reply. The retry at +2.7 s was accepted, and the bridge credited that
ACK as the poll's answer: `poll: f1 answered in 3498 ms`. In every other roll the roll went
first and the poll followed 4–18 s later. That fits the node being busy with the `POLL`
when the roll arrived. **It is not shown**: the simnode does not log a `POLL` or its reply,
so this trace cannot say what the node was doing at 14:45:09. The retry cost about 3 s. If
it recurs, the roll could wait for the poll window, or the heard-first `POLL` could wait for
the roll. Both are bridge-side changes, and neither is made here.

### The three host-tested-only branches, each after its own bridge restart

Each run closed and reopened the bridge's port, which reboots the board and clears every
roll. The XIAO kept running throughout.

1. **Spec §10.6 step 4, `REJECTED_CTX` completing a roll.** The sequence was `ack f1
   suppress 1`, then `push f1`. The node accepted the roll (`ctx 0x14f23d00 ->
   0x5a232bf0`) and withheld its ACK. The retry reused seq 1 and still carried the old
   `ctx_id`, and the node answered `REJECTED_CTX (frame ctx 0x14f23d00, own 0x5a232bf0)`.
   The bridge logged `rolled to ctx 0x5a232bf0 after 2 attempt(s)`. The next `OPEN`
   executed and was `acked`. **Pass.**
2. **`ACTUATOR_BUSY`.** The sequence was `ack f1 delay 15000` and an `OPEN`, then the bridge
   restart while that `OPEN` was in flight, then `push f1`. The node answered the first two
   roll attempts with `a command is in flight, ACTUATOR_BUSY`, 4.3 s apart. When the
   delayed `OPEN` ACK went out, the third attempt was accepted (`ctx 0x5a232bf0 ->
   0xb0a90e9c`), and the bridge logged `after 3 attempt(s)`. **Pass.** One limit shows
   here. A command held longer than `cmd_retries` + 1 roll attempts, 13–17 s at the
   defaults, ends the roll in `ctx_roll_failed`. It stays pending until the node is next
   heard, which is the designed outcome. This session did not publish `cmd_ack_ignored`
   between the stale `OPEN` ACK and the next restart, so it did not read whether that ACK
   was counted.
3. **`ctx_roll_failed`.** The sequence was `push f1`, then `disable f1` 0.6 s later, which
   was before the roll arrived. Four roll attempts went unanswered, at seq 1 each time,
   and the bridge logged `roll: f1 FAILED, no answer to 4 attempt(s); still pending`. An
   `OPEN` sent afterwards drew `context_roll_pending`. After `enable f1` and `push f1`, a
   new roll went out at seq 2 and was accepted in one attempt. The next `OPEN` executed at
   seq 1 and was `acked`. The next `lran/bridge/diag/state` read `ctx_rolls` 1 and
   `ctx_roll_failed` 1. `diag/cmd/state` read `roll_sent` 5 and `roll_retries` 3. **Pass.**

**The failed roll spent seq 1, and the new roll took seq 2**, which is root rule 2 applied
to the roll. The retries of one roll reuse its `seq`, and a new roll takes the next one.

**BF-34 is confirmed on air.** Impl Plan §6.2.2, the Firmware Tasks row and the bridge
`CLAUDE.md` now say so.

## 2026-09-23 — V-B12's saturated arm is deferred: its lever cannot saturate WiFi

**`diag_interval_s` is not a WiFi load.** Impl Plan §8.1 names it as the saturated arm's
lever, and BF-23 made it settable. Its floor is 10 s (`lran-config` table row `0x0002`). At
that floor, `sched_diag()` publishes three small documents every 10 s. M22 and Bridge PRD
§4.4 ask for a sustained MQTT or iperf flood. A run labelled `--arm saturated` on this lever
would have closed V-B12 without testing R-4.4. Nothing ran on the bench.

A bench-only UDP blaster on the bridge was chosen for planning. Loading WiFi that hard on
the house network would slow every 2.4 GHz device on its channel, so the operator will set
up the IoT network for the run instead. The
[bench network brief](./briefs/2026-09-23-vb12-bench-network-brief.md) has the options and
the decisions still open. §8.1's lever is not corrected yet; that edit rides with the
blaster.

---

## 2026-09-23 — V-B12's blaster built, and the frame log survives a 20 Mbps load

**The saturated arm now has a load, and a first calibration pair ran on the IoT network.**
Impl Plan §8.1.2 records the design. This entry records the bench.

### The bench network

The operator set it up as the [bench network brief](./briefs/2026-09-23-vb12-bench-network-brief.md)
prefers, with one change: the broker is not on the Mac.

- The Mac is on the IoT network's 2.4 GHz WiFi (channel 6, WPA2) and keeps internet
  access through it. The brief's first option held.
- The broker is another IoT host, at <iot-broker-ip>:1883. The credentials in `secrets.h`
  authenticate against it.
- The bridge runs `v_b12_blaster`, flashed over USB from `9bd01b3`. Its `secrets.h` points
  at the IoT network. **Restore the house values and reflash `heltec` when V-B12 is done.**

**The first flash could not join the network.** `secrets.h` misspelled the IoT network's SSID,
and the bridge logged `NO_AP_FOUND` on every attempt. The operator corrected the name
(redacted 2026-09-23 for the public repository). After that the bridge logged one `AUTH_FAIL` and one `ASSOC_FAIL` in its first
two seconds, then associated and stayed up.

### What the blaster achieves

Eight seconds at each rate, 1472-byte payloads, to the broker host's discard port:

| Asked | Achieved | Refused by the stack |
|---|---|---|
| 2000 kbps | 1996 kbps | 0 |
| 8000 kbps | 7997 kbps | 0 |
| 20000 kbps | 19719 kbps | 129, `ENOMEM` |

**About 20 Mbps is the ceiling on this link.** The refusals start there, so a higher
rate adds refusals rather than load.

**The first image used `WiFiUDP`, and it logged every refused send at error level.** With
the link down that came to more than 600 lines in 8 s. `9bd01b3` sends on a raw lwIP
socket instead, and counts polls with the link down as `down`, apart from `fail`.

### The calibration pair

`sweep_interleave.py --gaps 2000 --blast-kbps 20000 --pairs 1 --count 20`, flooding `f3`
from the XIAO, with the Heltec simnode quieted:

| Arm | Sent | Lost | Blaster |
|---|---|---|---|
| `gap2000`, idle | 21 | 0 | — |
| `blast20000` | 20 | 1 | 19854 kbps achieved, 82136 packets, 476 refused, 0 down |

**The ring did not overwrite at 19854 kbps.** The frame log is therefore usable as the
saturated arm's counter at the full load, and the brief's fourth decision needs no rate
cap below the link's ceiling. The loss fell in a 4000 ms gap with a bridge transmission
inside it, like both of 2026-09-21's losses at 2000 ms.

**One lost frame is not a result**, and the tool says so: eight is its line. The sweep
that answers V-B12 is still to run.

## 2026-09-23 — V-B12 measured: saturating WiFi cost the bridge no measurable PER

**V-B12 is met, and M22 closes.** Two interleaved sweeps ran with the blaster at 20000
kbps, and the loaded arm lost no more than the idle arm's known signature explains. Impl
Plan §8.1.3 records what this changes. This entry records the bench.

### The first attempt had no load

The first run 1 was stopped after three bursts. Its two loaded bursts achieved 1996 kbps
with 70428 sends refused, then 473 kbps with 81644 refused. The frame log published no
records for the second one. The calibration pair earlier the same day had achieved 19854
kbps with 476 refused.

**The IoT network had moved from channel 6 to channel 1.** The Mac reported channel 1,
−41 dBm signal and −86 dBm noise, and it was sending at MCS 0, 14 Mbps. A signal that
strong at the lowest rate points to a busy channel. A 10 s blast after the run stopped
reached 11349 kbps, so the link was variable rather than dead.

The operator pinned the nearest access point to channel 6. The Mac then reported channel
6 at −66 dBm and MCS 5. Two 10 s blasts reached 6140 kbps and then 18737 kbps, the first
probably while the access point settled. The sweep ran after that.

### The two sweeps

`sweep_interleave.py --gaps 2000 --blast-kbps 20000 --pairs 6`, flooding `f3` from the
XIAO with the Heltec simnode quieted. The bridge ran `v_b12_blaster` at `9bd01b3`, and the
simnodes ran `98b4b04`. The frames are in
[`data/sweep-vb12-2026-09-23-run1.json`](./data/sweep-vb12-2026-09-23-run1.json) and
[`-run2.json`](./data/sweep-vb12-2026-09-23-run2.json).

| Run | Idle, lost of sent | Loaded, lost of sent | Loaded bursts achieved | Refused per loaded burst |
|---|---|---|---|---|
| 1 | 0 of 241 | 0 of 240 | 19191 to 19521 kbps | 2999 to 4477 |
| 2 | 0 of 241 | 2 of 240 | 16346 to 17849 kbps | 15692 to 25810 |
| Pooled | **0 of 482** | **2 of 480, 0.42 %** | | |

**Both losses fell in run 2's sixth burst, one of its loaded ones.** Each followed a
4000 ms gap with a bridge transmission inside it, and `rx_deaf_ms` was 234 ms across the
window. Both of 2026-09-21's idle losses at 2000 ms had the same signature. The tool
declines to order the arms on two losses.

**RSSI and SNR did not move with the load.** Both arms had a median of −22 dBm and +11 dB
in both runs. Run 1's range ran to −35 dBm in both arms alike, and run 2's stayed within
−23 to −21 dBm. The ring did not overwrite in any burst, and no poll found the link down.

**Run 2 carried less load than run 1**, with five to six times the refusals. Every burst
still achieved more than 80 % of the rate asked, so each one counts under §8.1.2's test.
The cause was not investigated. The channel was shared with the rest of the IoT network.

**What the bench cannot show.** Every frame arrived about 100 dB above the sensitivity
floor: the specification's link budget lists about −123 dBm at SF7, and SF9's is lower
still. A noise-floor rise smaller than that margin costs nothing here and could still
cost frames at 87 m.

---

## 2026-09-23 — BF-26 on air: the bench gate holds both ways, and a first cut published while off

**The bench is restored.** The operator put the house network's values back in
`secrets.h`. The bridge had still been running `v_b12_blaster` at `9bd01b3`, and the broker
still held its retained `lran/bridge/availability offline`. The BF-26 branch was flashed
from a clean tree over USB. The banner read `0e7ded5` with no `-dirty`, then `d9624c0` after the
fix below, and the bridge came up `online` on the house broker at <sandbox-broker-ip>.

**The run.** The bridge's and the XIAO's serial ports stayed open for the whole session,
and `push f1` announced `f1`. Set and clear went to `lran/bridge/config/set`, and
`mosquitto_sub` on `lran/#` and `homeassistant/#` recorded what reached the broker. The
Heltec simnode was not touched.

| Step | Bridge log | At the broker |
|---|---|---|
| Boot, flag default off, `f1` heard | `simnode diag off`; `availability: simnode1 online` | No simnode `availability`, `diag/state` or discovery config (fixed build) |
| Set `true` | `levers: gen 4 … simnode diag on` | Ack `persisted`; `simnode1/availability online`; `simnode1/diag/state` at −22 dBm, +11 dB; 48 discovery configs for `simnode0`–`3`, every one `ent_cat: diagnostic` |
| Reboot, flag persisted on | `Config: 1 stored value(s) restored`; `simnode diag on` | `simnode1` `online` and `diag/state` again once heard |
| Set `false` | `levers: gen 4 … simnode diag off` | Ack `persisted`; one `simnode1/availability offline`; all 48 configs still retained; no simnode `diag/state` in the 75 s after, across a diagnostics cycle that published GateLink's |
| Reboot, flag persisted off, `f1` heard | `simnode diag off`; `availability: simnode1 online` | Nothing on either simnode topic in 80 s, with the retained values cleared first |

**The first build published `offline` while the flag was off.** It made a bench node read
`offline` whenever its availability changed with the flag clear, on the reasoning that
this would also cover a reboot that lost an unpersisted `true`. On air, the boot row above
left `lran/simnode1/availability offline` retained before any set. Spec §16.6 says that
with the flag clear, bench frames are *"not published"*, and it allows `offline` only as
the flag clears. The fix publishes `offline` once, on the tick the flag clears, and never
otherwise. The host tests passed both versions, because they tested the rule the code
implemented, not the specification's.

**What this leaves.** The `applied_not_persisted` reboot case is uncovered, as Impl Plan
§4.2a.1 says. A simnode's `config/state` was retained at the broker for all four
identities before the flag was ever set, so the bridge publishes a bench node's
configuration and command answers regardless of the flag. §4.2a.1 raises that against
§16.6's *"exclusively"*. The flag was left **off, persisted**. The 48 simnode discovery
configs stay retained, as §16.6 intends, so Home Assistant now carries four simnode devices
whose entities read unavailable.

---

## 2026-09-23 — BF-24 built: the publication policy, host-tested, with no production frame to publish

**BF-24 is built and host-tested; nothing has gone on air.** `publish.{h,cpp}` turns each
`STATUS` into retained documents, and `app_task` queues them. Impl Plan §6.3.1 records the
keys and the choices. `test_publish` has 21 cases, and the native suite passed 359 of 359.
The `heltec` image builds at 51.1 % RAM.

**Three decisions were the operator's**, taken at the start of the session:

- Three bridge rows, not two. `republish_interval_s` (900 s), `bms_stale_s` (600 s) and
  `cell_mv_deadband` (5 mV) are now `0x000D`–`0x000F`. Their names become permanent Home
  Assistant object IDs.
- SNTP in this task. Spec §7.2.9 has the bridge publish `last_traversal` as an absolute
  time, and until today no task owned a wall clock.
- GateLink's discovery entities in the same branch, so that the state is visible in HA.

**No bench frame can exercise the documents.** A simnode is a bench node, and spec §16.6
keeps its `STATUS` off every production topic whichever way the flag is set. The first
real document therefore arrives with GateLink, or with BF-27's dummy publish.

**Stale blocks needed a second availability topic, and no new topic.** R-5.2b asks for
unavailable, and a `null` reading shows as unknown in HA. The `solar` and `battery`
documents carry `available`. Their entities list that document in `avty` with
`avty_mode: all`, beside the node's own availability. Home Assistant's discovery code
expands `~` and the abbreviations inside `avty` entries. That was read from `discovery.py`
on `home-assistant/core`'s `dev` branch on 2026-09-23, not tried against an HA instance.

**`republish_interval_s` has left GateLink's count.** Library Plan §4 listed it among
GateLink's unnamed rows. It is now the bridge's row, because spec §16.4 makes publication
the bridge's. GateLink's implied rows fall from six to five, about 204 bytes of 193.

---

## 2026-09-23 — BF-25 built: events publish once, and two gaps in what the documents assumed

**BF-25 is built and host-tested; nothing has gone on air.** `PublicationPolicy::on_event()`
publishes each `EVENT` to `lran/<node>/event/<name>` with retain clear, once. Impl Plan
§6.3.2 records the topic, the keys and the choices. `test_events` has 15 cases, and the
native suite passed 375 of 375. The `heltec` image builds at 51.8 % RAM, up from 51.1 %.

**Spec §7.3's deduplication rule would drop every follow-up.** It says the bridge suppresses
an `(src, ctx_id, event_id)` triple it has already published. The paragraph after it has a
follow-up carry its first edge's `event_id` with `event_flags` bit 0 set. Read together, the
literal rule withholds every follow-up, so the direction a classification resolves never
reaches HA. The operator chose to add the follow-up bit to the key. The first edge and its
follow-up now publish once each, and a retransmission of either is withheld. The wording is
under the handoff's *Open* for the next spec revision.

**PubSubClient 2.8 cannot publish at QoS 1.** Its `publish()` takes no QoS argument, and
every publication from this bridge has gone at QoS 0 since BF-12. `PublishMessage` carried
a `qos` field that nothing read. Spec §16.3 requires QoS 1 for events. The operator chose to
tag events QoS 1, have `make_publish()` refuse an event asked for at QoS 0, and record the
transport's limit rather than change libraries in this branch. D5 names espMqttClient as
the fallback.

**A failed publish lost the message, and for an event nothing would replace it.** Impl Plan
§4.3.1 accepted the loss for state, because the node's next frame republishes. An event is
recorded as published when it is queued, so a retransmission would be withheld as a repeat.
`drain_publish_queue()` now holds a failed event and tries it first on the next pass. The
pass stops at the failure, so newer state cannot overtake it. This is target code and has no
host test. It covers the failure the bridge can see, which is most of what QoS 1 would add.

**What this leaves.** V-B8 has not been run: no bench node can send an `EVENT`, because spec
§16.6 keeps a simnode's events off every topic. GateLink at B6, or an event mode in BF-27's
dummy publish, would run it. While the broker is down, the publish queue fills with state
and refuses the newest message, so an event raised during a long outage is lost. That is
§5.2.1's per-class question, and it is under *Open*.


---

## 2026-09-23 — BF-27's dummy publish: §6.3's rules on air, and two clocks that move

**The dummy publish is built, and its frames went through the real policy to the sandbox
broker.** A `dummy` line on the bridge's USB serial console becomes a `STATUS` or `EVENT`
under GateLink's address. `app_task` hands it to `PublicationPolicy` and to nothing else.
Impl Plan §6.6.2 records the choices. `test_dummy` has 10 cases, and the native suite
passed 386 of 386. The `heltec` image builds at 52.3 % RAM.

**The operator settled two questions before the code.** A dummy frame carries the
production node's own address, so it exercises the topics B6 will use, and every document
says `synthetic: true`. The console is the trigger, not an MQTT topic, because anything on
the broker could otherwise raise a gate event. The operator also noted that the broker and
Home Assistant here are a sandbox VM, so synthetic rows in GateLink's history there need
no cleanup.

**An `EVENT` could not carry the mark, so the event payload gained a key.** Spec §7.3 gives
an `EVENT` no `status_reason`. `on_event()` now takes a `synthetic` argument, `app_task`
passes `true` for a dummy frame and `false` for a radio frame, and every event payload
carries it. No event had been published before today, so no automation reads the old key
set.

**The run.** `republish_interval_s` was set to 60 for it and restored to 900 afterwards.
`lran/gatelink/availability` was set to `online` by hand, because the watchdog holds a node
that never answers `offline`. Captured with `mosquitto_sub` on `lran/gatelink/#`:

| Step | At the broker |
|---|---|
| First `STATUS` | Five documents, each `synthetic: true` |
| The same `STATUS` again | Nothing; `unchanged` rose by 5 |
| `cell1_mv` 3310 → 3313 | Nothing from `battery/state`: inside the 5 mV deadband |
| `cell1_mv` → 3320 | `battery/state` republished |
| `enclosure_temp_c10=na load_ma=na` | `enclosure_temp_c: null` and `load_ma: null` |
| `mppt_flags=2` | `solar/state` with `available: false` and every reading `null` |
| `bms_age_s=900` | The same for `battery/state` (over `bms_stale_s`, 600) |
| Two `vehicle_while_held_open`, the second a follow-up, then `fire_asserted` | Three events, `event_id` 1, 1 and 2, each `synthetic: true` |
| The same `STATUS`, 65 s later | All five documents again; `heartbeats` rose by 4 |

A retained-only subscription afterwards listed the five documents and no `event` topic.
`dummy set status_reason=0` and `dummy status simnode1` were refused.

**`detect/state` republished on every dummy frame in the first run.** The template held
`last_traversal_age_s` at 3600 while the bridge's clock moved. Spec §7.2.9 has the bridge
publish the traversal as `utc_at_rx − age`, so every frame computed a new time, 3 to 6 s
on. That was more than `kTraversalJitterS` allows. A real node's age grows with its clock.
The dummy now advances `last_traversal_age_s` and `uptime_s` by the time between frames.
On a second run, four frames 5 s apart published `detect/state` twice. The first of those
had `last_traversal: null`, because SNTP had not answered in the 22 s since boot.

**The same run shows `node/state` republishing on every frame, and a real GateLink will do
the same.** `uptime_s` is in that document and changes between any two polls. So
publish-on-change never withholds `node/state`, and HA records it once per poll. Whether
uptime belongs in the change hash is a policy question, left under the handoff's *Open*.

**What this leaves.** Every reading was taken at the broker. Nobody has looked at the
entities in Home Assistant, and V-B8's HA restart and discovery refresh have not been run.


---

## 2026-09-24 — V-B8 in Home Assistant: each event fired once through two restarts and two refreshes

**V-B8 passes on the sandbox.** Three synthetic events each fired a Home Assistant
automation once. None fired again across two HA restarts, a reload of the MQTT integration
and a broker restart that made the bridge republish discovery. The bridge ran the image
from BF-27's run, `bee046b-dirty`, with no reflash. HA was 2026.9.3, driven through its REST
API with a long-lived token.

**The instrument was an automation triggered on `lran/gatelink/event/#`**, in `queued` mode,
writing one logbook row per message with the topic, `event_id`, `ctx_id`, `follow_up` and
`synthetic`. A second row for one event would be the V-B8 failure. The automation was
deleted after the run.

| Step | HA automation fires, total |
|---|---|
| `dummy event gatelink vehicle_while_held_open` | 1 |
| HA restart (`homeassistant.restart`) | 1 |
| MQTT integration reload (`config_entries` reload) | 1 |
| Mosquitto add-on restart; the bridge reconnected and republished its discovery set | 1 |
| The same event as a follow-up, then `fire_asserted` | 3, one row each |
| A second HA restart | 3 |

The last two rows are the positive control: the automation still fired after the refreshes,
so the absence of a replay is not a dead subscription. A retained-only subscription to
`lran/#` afterwards found no `event` topic.

**The broker restart republished discovery.** `on_mqtt_connected()` resets
`g_discovery_cursor` on every connect (`task_runtime.cpp`). The bridge does not subscribe
to `homeassistant/status`. HA's own restart needed nothing from it, because every
discovery config is retained.

**§6.3's rules read the same in HA as at the broker.** A dummy `STATUS` populated all
GateLink entities, and `binary_sensor.gatelink_synthetic_data` read `on`. After
`mppt_flags=2` and `bms_age_s=900`, 20 solar and battery entities went `unavailable`. They
stayed that way through both HA restarts, because the `available: false` documents are
retained. `bms_reading_age` stayed available and read 900, as did `bms_link_rssi`, which is
the reading that says why the rest are stale. Temperatures show in °F: this HA uses the
imperial unit system and converts the declared °C.

**The hand-set `online` was overwritten, and Impl Plan §6.6.2 said it would not be.**
Opening the serial port reset the bridge at 10:04, so the watchdog started over.
`lran/gatelink/availability` was set to `online` by hand at 10:04:38. At 10:06:16, after three missed polls,
the watchdog published its first `offline` transition over it, and HA showed all 53
GateLink entities as `unavailable` when read after the first restart. §6.6.2's rule holds only once
that first transition has happened. Set `online` more than three poll intervals after the
bridge boots, or set it again. The broker restart overwrote it a second time, as §6.6.2
says. §6.6.2 now says both.

**Opening the USB serial port resets the bridge**, even with DTR and RTS held low before
`open()` on macOS. One process held the port for the whole run, fed through a FIFO.

**What this leaves.** V-B8 ran with synthetic events on a bridge at its desk. The transport
is still PubSubClient at QoS 0; the handoff's *Open* keeps that item. B4's §6.3 criterion
has now been shown in HA as well as at the broker.

---

## 2026-09-24 — B4's acceptance tally: the discovery set read at the broker and in HA

**Every retained discovery config names its own node's availability, and HA holds one
device per node.** That read is the only new evidence behind Impl Plan §8.2's tally. The
rest comes from the 2026-09-23 and 2026-09-24 entries above. No board was touched, and the
bridge was still running BF-27's image.

**The read.** A paho subscriber held `homeassistant/#` and `lran/+/availability` at the
sandbox broker for 4 s and kept retained messages only. HA's `/api/template` counted MQTT
entities per device.

| Device | Configs at the broker | Entities in HA |
|---|---|---|
| LoRa Bridge | 6 | 6 |
| GateLink | 53 | 53 |
| WellLink | 8 | 8 |
| Simnode 0 to 3 | 12 each | 12 each |

All 115 configs list `lran/<node>/availability` for the node their `~` names. Of
GateLink's, 33 list only that, 10 add `battery/state` and 10 add `solar/state`, each of
those 20 with `avty_mode: all`. Retained availability read `online` for the bridge and
`offline` for GateLink and WellLink, and no simnode had one.

**The read cannot show V-B4's republish.** Mosquitto persists retained messages across a
restart, so a config present after one says nothing about the bridge. A subscriber held
through the restart would tell them apart: the broker's copies reach it with the retain
flag set, and the bridge's republished ones with it clear.

---

## 2026-09-24 — V-B4 passes: a broker restart brought every production config back from the bridge

**V-B4 passes.** After a Mosquitto restart, the bridge republished all 67 of its production
discovery configs within 5 s of the broker coming back, and the broker's persisted copies
arrived beside them. The bridge ran BF-27's image with no reflash, and nobody opened its
serial port.

**The instrument told the two sources apart by the retain flag.** A paho subscriber on
`homeassistant/#` and `lran/+/availability` reconnected at 1 s intervals and resubscribed on
every connect. The broker delivers its stored copies to a new subscription with the retain
flag set, and forwards a live publish to an existing subscription with it clear. The
subscriber had to resubscribe before the bridge reconnected, or the bridge's configs would
have arrived as stored copies. It did, by 3 s.

**The run**, in seconds from the subscriber's first connect. The restart went through HA's
`hassio.addon_restart` at 10:27:06.

| Time (s) | Event |
|---|---|
| 5.0 | Restart requested |
| 5.1 | The subscriber disconnected |
| 9.2 | The subscriber resubscribed, and 115 configs and three availability topics arrived with retain set |
| 12.3 | The bridge's `lran/bridge/availability online` arrived with retain clear |
| 12.3 to 13.6 | GateLink's 53 configs, retain clear, then both nodes' `offline` |
| 13.6 to 13.8 | WellLink's 8 configs, retain clear |
| 13.8 to 13.9 | The bridge's 6 configs, retain clear |

**The 48 simnode configs arrived with retain set only**, which is the negative control.
`simnode_diag_enable` is off, persisted, since BF-26's run, and the bridge publishes no bench
discovery while it is off (Impl Plan §4.2a). So the flag separates the configs the bridge
sent from the ones the broker kept.

**One more message arrived with retain clear, at 20.4 s**, on a topic under
`homeassistant/` that held no config. The script did not record its name. It is not in the
retained set afterwards, which still holds 115 configs. Home Assistant's MQTT integration
publishes `online` to `homeassistant/status` when it reconnects, and that is the likely
source.

**Afterwards**, HA listed all 115 entities, and 109 read `unavailable`: GateLink's 53 and
WellLink's 8, whose nodes are `offline`, and the 48 simnode entities. The bridge's 6 were
available.

## 2026-09-24 — B4b opened: spec §12.4 leaves the fleet open, and D59 is proposed

**No code was written for BF-33.** The handoff asked for spec §12.4 to be read before the
code, and the read found the mechanism undefined for more than one node. The operator
directed a spec revision first. Spec v0.14's §12.4.1 to §12.4.4 and Decision Register §2.4
(D59) hold the draft.

**One gap would have reverted every node on a working link.** §12.4 step 4 confirms a
change only with an authenticated frame, and §9.2 leaves `POLL` unauthenticated. A bridge
that did nothing but poll on the new settings would see every node revert at
`phy_trial_s`.

**The first fix proposed in this session stranded a node, and it was caught while the text
was being written.** The proposal had the bridge send each node a confirming `CONFIG` `GET`
as soon as it retuned. A node commits on that frame, so a near node could commit while a
far node never heard the new settings. The bridge would then revert, and the near node
would be left on settings nobody used. The draft now splits the step. The bridge first
hears every node on the new settings by `POLL`, which commits nothing, and only then
commits itself and confirms each node. The same order closes a bridge restart mid-trial,
because D58's roll is an authenticated frame sent on the settings the bridge committed.

**One case stays open as W17.** A node heard in the first half that then misses every
confirming `GET` reverts after the bridge has committed.

**Found in passing**: spec §12.4 cited a §12.1a that has never existed, corrected in the
draft. The simnode's `apply_config` still says a large `GET_ALL` has no specified split,
which D57 overtook; that line is under the handoff's *Open*.

## 2026-09-24 — D59 accepted, and the specification moves to v0.14

**The operator accepted D59 as drafted**, and put W17 after GateLink's deployment. The
specification's header moved to v0.14 the same day. The citation sweep moved 26 binding
citations, each document reconciled with v0.14 first. The System PRD's version column had
fallen behind in six rows unrelated to v0.14, and the sweep brought them level.

**W4 gained one vector**, `event_phy_reverted`, for 78 in all. The 77 vectors committed
before it kept their bytes, compared field by field against a copy taken before
regeneration. The codec's `EventType` gained `PhyReverted`, and that forced two switches
to name it: the bridge's `event_type_name()`, which `-Wswitch` would otherwise fail, and
the simnode's console table. The native suites pass: 135 in `lran-protocol`, 17 in the
bridge's `test_events` with one new case, and 115 in the simnode.

## 2026-09-24 — BF-33 split in four, and its library half built

**BF-33 did not fit one session, so the operator split it.** The documents alone took about
half of a 200k-token budget. The four slices are these: `lran-config`'s table and store;
the bridge's §12.4.1 fleet machine with the retune in `lora_task`; the simnode's §12.4.2
half; and the bench run B4b's row asks for. This session built the first slice. No firmware
behaves differently yet, because no `Store` enables the trial. Library Plan §4 v0.19 records
the API.

**Adding the bridge's six PHY rows broke `test_config`.** With 21 global rows, the bridge's
`get_all` answer counts to about 1216 bytes, and `lran/bridge/config/state` to about 1134,
against a `kMaxPayloadLen` of 1024. Both figures come from an offline count at each row's
longest value, not from a board. The operator chose to raise `kMaxPayloadLen` to 1536.
`MQTT_MAX_PACKET_SIZE` was already 2048. The publish queue's 32 slots grow by about 16 KB,
and the `heltec` build reads 195,032 bytes of RAM, 59.5 %. Two stack buffers were sized by
`kMaxPayloadLen`, and both changed. `publish_cmd_ack()` runs on `sched_task` and now uses
128 bytes. The version document built on each broker connect is now static, so `mqtt_task`
does not hold two payloads on its stack at once. **Read both tasks' high-water marks at the
next flash**, because nothing on a board has checked this change.

**Two questions go to the specification.** Neither blocks the next slice.

- **`RESTORE_DEFAULTS` keeps the committed PHY group.** The spec's §8.10 and D52 say it
  clears every override. Applied to the PHY group, it would retune one node to D1's
  defaults while the fleet stayed where it was, which §12.4 exists to prevent. The store
  keeps the group, and the next revision should say so.
- **A node's `CONFIG_ACK` for a PHY trial reads `APPLIED_NOT_PERSISTED`.** That is the
  truth about the trial values, and §12.4.1 step 4 checks only the values. §12.4.2 step 2
  says `APPLIED_NOT_PERSISTED` "does not extend to the PHY group", which was written about
  a node with no store. The two readings should be reconciled in the text.

The native suites pass: 25 in `lran-config` with nine new cases, 387 in the bridge and 115
in the simnode. The `heltec` and `simnode-xiao-wio` targets build, and `run_ci_local.py`
passes.

## 2026-09-24 — BF-33 slice 2: the bridge's fleet machine, host-built

**The bridge's half of spec §12.4.1 is built and host-tested, and it has not run on a
board.** `phy_change.{h,cpp}` is the state machine, with no Arduino dependency, like
`config_path.h`. `sched_task` drives it, and `lora_task` applies the retune. The native
suite is 406 cases, 19 of them new, and the `heltec` build reads 197,264 bytes of RAM,
60.2 %. No change can complete on air yet. Every simnode answers a PHY `SET` `READ_ONLY`
until slice 3 builds §12.4.2, so a change today ends `not_accepted` at the first node.

**Four things the reading found, each of which shaped the code:**

- **The boot restore went through `Store::apply()`**, which now opens a PHY trial. A
  stored group would have come back as a trial at every boot. `nvs_restore()` now calls
  `ConfigStore::restore()`, which calls `Store::restore()`.
- **`ConfigPath` cannot carry the fan-out.** It runs one transaction and publishes a
  per-node `config/ack` when that transaction resolves. §16.7.5 needs one answer on the
  bridge's topic when the whole change ends. `PhyChange` claims its own `CONFIG_ACK`s
  first in `config_on_ack()`, as the roll does in `cmd_on_ack()`.
- **`lora_link` set the PHY once, in `lora_start()`.** `lora_request_phy()` hands new
  settings to `lora_task` under `g_diag_mux`. `lora_task` applies them through
  `radio_begin()` on the first pass that finds the radio receiving, nothing of ours
  arriving and nothing queued to send.
- **The table and `PhyConfig` count in different units.** Bandwidth is whole kHz in the
  table and tenths in `PhyConfig`. `phy_config_from()` converts it and applies D33's EIRP
  check again, and a host test holds the default group equal to `kPhy`.

**Choices a reviewer should check against the specification:**

- **Any authenticated frame confirms a node**, so from the first `CONFIG` to the last
  confirming `GET` no command, roll or other `CONFIG` is admitted
  (`PhyChange::blocks_traffic()`). Polls still go out.
- **An empty fleet is refused `phy_fleet_incomplete`.** §12.4.1 step 2 names only an
  offline node. A bridge that moved alone would strand every node it has not heard.
- **The bridge's PHY rows stay `READ_ONLY` without a usable NVS store**, by the
  reasoning of §12.4.2 step 2, which the specification states for nodes only.
- **A failed commit write has no §16.7.5 reason.** The change reverts, `config/ack`
  reads `reverted`, and no `phy_reverted` event is published.
- **The trial marker lives in the PHY blob**, so the commit that writes the new group
  clears it in the same NVS write. A boot that finds it set publishes
  `phy_reverted` with `reason` `restart` once.
- **After an abandon the machine stays busy until the last node's window has closed**,
  so a second change cannot reach a node still counting down the first. A node that
  never answered step 4 is read back with `GET_ALL` at that point, which is step 4's
  deferred readback.

## 2026-09-24 — BF-33 slice 3: the simnode takes a PHY change, and no change can start

**The simnode's half of spec §12.4.2 is built and host-tested, and none of it has moved a
radio.** `phy_trial.{h,cpp}` holds the board's PHY group in `lran-config`'s `Store`, which
tells the radio when to retune and runs the trial window. `nvs_blob.cpp` keeps the group in
NVS. The native suite has 128 cases, 13 of them new. Both simnode targets and the bridge
were flashed. The bench then showed that **the bridge refuses every PHY change**, so
nothing reached a simnode on air. The last section below explains why.

**The simnode did not do what slice 2's entry says it did.** That entry says every simnode
answers a PHY `SET` `READ_ONLY`. In fact only `ROLE_GATELINK` answered `CONFIG`, and its
generic RAM store took a PHY id as an ordinary parameter. It answered `OK` and
`APPLIED_NOT_PERSISTED` and never retuned. The bridge would have gone on to step 5 and
retuned alone, then reverted when step 6 heard nobody.

**The operator decided three questions before the build:**

- **The simnode persists the PHY group in NVS**, one blob per board, in the bridge's
  layout. `PhyBlob` moved from the bridge into `lran-config` (`phy_blob.h`), so both
  firmwares write one format. `phy reset` erases the blob and retunes to D1's group, so a
  stranded board recovers without a reflash. Nothing else on a simnode persists.
- **A board retunes once every member identity has accepted the same group.** Up to four
  identities share one SX1262, and the bridge sends each its own `SET` on the old settings.
  A board that retuned after its first identity's ACK would not hear the next `SET`. A
  member is any enabled identity whose role is not `ROLE_FAULT`. So an enabled identity the
  bridge does not watch keeps the board on its old settings, and the change ends
  `not_heard`. Any authenticated frame to any identity confirms the whole board.
- **`ROLE_RANGE` and `ROLE_HEALTH` answer `CONFIG`** for the PHY group, and answer any other
  row `UNKNOWN_PARAM`. `ROLE_FAULT` still answers nothing.

**Choices a reviewer should check against the specification:**

- **A `SET` naming PHY rows during a trial answers `READ_ONLY`.** §12.4.2 does not say what
  a node does with a second group before the first is confirmed. The bridge never sends one
  (`blocks_traffic()`), and stacking one trial on another would leave nothing coherent to
  revert to.
- **A group some members accepted, but the board never retuned to, is dropped after
  `phy_trial_s`.** The radio never moved, so there is no `PHY_REVERTED` to send.
- **Confirmation is a `CONFIG` or `COMMAND` whose gate verdict is `Execute`, or a roll.** A
  roll skips the gate, and §12.4.1 counts the roll after a bridge restart as confirmation.
  A `DUPLICATE_CACHED` answer does not confirm.
- **`PHY_REVERTED` goes out with the next frame from the bridge, of any type**, before that
  frame's own answer, and from every `ROLE_GATELINK` identity. The other roles have no
  event schema and report nothing, as step 8 allows.
- **§12.3's backoff window is not recomputed on a retune**, as on the bridge: `backoff_max_ms`
  is a lever. This is the fourth spec question from slice 2's entry.
- **A generic-store `GET_ALL` now carries six more rows**, so a `ROLE_GATELINK` identity holding
  more than 16 `u32` overrides cuts its answer: the six rows cost 42 of 196 bytes. The cut is logged. §7.4.1's split is not
  built here.

**What slice 2 owed a board, read on this flash:**

- **The bridge's radio comes up on 917.4 MHz** from `g_boot_phy`: `LoRa: radio up -
  917400000 Hz, SF9, BW 125.0 kHz, CR 4/5, -4 dBm conducted`. `lora_task` has 6428 bytes
  free.
- **`sched_task` has 2312 bytes free**, of 5120, after a `get_all` to simnode1 completed.
  No PHY change ran, so `sched_phy()`'s own depth is not in that figure.
- **`mqtt_task`'s high-water mark is not read.** Nothing prints it.
- **The refusal is right on the broker.** `{"set":{"freq_hz":917000000}}` on
  `lran/bridge/config/set` answered `{"op":"set","persist":"not_applied","error":"phy_fleet_incomplete"}`.

**The simnode's `CONFIG` path works on air.** A `get_all` on `lran/simnode1/config/set`
drew six results from f1, `persisted`, and `config/state` filled in the PHY rows.

**No PHY change can start, on this bench or in production, until every provisioned
production node answers.** The fleet is every node the bridge watches.
`AvailabilityWatchdog` watches a production row always and a bench row once heard. So
0x01 and 0x02 are in the fleet from boot, both offline, and §12.4.1 step 2 refuses with
`phy_fleet_incomplete`. That holds until WellLink is deployed, which is not planned soon. The
refusal follows the text: the bridge polls both nodes. **Whether a provisioned node that
has never been deployed counts toward the fleet is a spec and registry question.** The
operator closed slice 3 host-only, and slice 4 cannot start without an answer.
`simnode_diag_enable` was set to 1 for the check and back to 0 afterwards.

## 2026-09-24 — D61: a node joins the PHY fleet once deployed or heard, and slice 4 is unblocked

**The operator chose a registry mark over the other two candidates**, and D61 closed the
same day as a bridge per-node lever, `deployed` (`0x0081`, default 0). Decision Register
§2.5 has the proposal and §3.10 the answers. The specification's §16.5 and §16.6 are to
say it at the next revision, with D60's two cases.

**What changed on the bridge.** The poll scheduler and the availability watchdog no longer
enrol a production row at construction. `sched_levers()` enrols a row the tick its
`deployed` lever reads 1, through `PollScheduler::enrol()`. Every other row, production or
bench, is enrolled on its first frame, as a bench row always was. The watchdog takes the
lever in place of `NodeInfo::is_bench`. Both latch: clearing the lever takes effect at the
next restart, so a node cannot leave §12.4.1's fleet mid-boot while a change might still
move it.

**Two choices a reviewer should check:**

- **The lever applies to bench rows too.** D61 names no exception, and a bench identity with
  `deployed` set is polled from boot. That is harmless and sometimes useful.
- **`deployed` is a `bool`, not the `u8` §2.5 first drafted.** `simnode_diag_enable` set the
  table's convention for a 0-or-1 row. The register's item 1 was corrected on the branch
  before merge.

**What it costs on a bench.** With every lever at 0 and no simnode transmitting, the fleet
is empty and a PHY change is still refused `phy_fleet_incomplete`, as slice 2 chose. Make
each simnode transmit once, or set its `deployed` lever.

**Not run on a board.** The bridge's native suite is 409 cases, all passing, and `heltec`
builds. Nothing was flashed.

## 2026-09-24 — BF-33 slice 4 on air: B4b's criteria hold, and a poll can clash with a PHY `CONFIG`

**Every criterion in B4b's row held on the bench.** The bridge was flashed with D61
(`2066fc9`), and both simnodes ran `0a0d6c9`, slice 3's build. Three identities made the
fleet: f0 and f2 on the Heltec, and f1 on the XIAO. Each change moved `freq_hz` between
917.4 and 917.0 MHz. Every `CONFIG` carried all six PHY rows. `phy_trial_s`,
`config_ack_timeout_ms` and `poll_reply_timeout_ms` kept their defaults of 120 s, 8 s and
10 s. The bench was set up this way:

- `simnode_diag_enable` was 1, and `deployed` was 1 on `simnode0` to `simnode2`. Both were
  set back to 0 afterwards.
- **D61 held on air.** After the flash, the bridge sent no boot poll to 0x01 or 0x02. Each
  `deployed` set enrolled its node within a second. After each later bridge reboot, the
  bridge polled all three bench nodes at once.

| Run | What was done | What happened |
|---|---|---|
| D33's clamp | `{"tx_power_dbm":10}` | The set was answered `clamped` at −4 dBm, the current value, so no trial opened and no `CONFIG` went out |
| The fleet moves | 917.4 → 917.0, then back | Each run passed §12.4.1 steps 3 to 7: the fan-out took 3 s, the bridge heard all three nodes 5 s after it retuned, and then it committed. Each board committed on its first `GET`. A Heltec reset after the second run's commit came back on the committed group |
| A node reboots in its trial | The Heltec was reset as soon as it retuned | The Heltec came back on 917.4, `REVERTED (reboot during trial)`. The bridge polled f0 and f2 on 917.0 and heard neither, so it committed nothing. It reverted 96 s after the first `CONFIG_ACK`, which is step 8's deadline for three nodes, and published `phy_reverted` `not_heard`. f1 answered polls on 917.0 and still reverted when its 120 s window closed, because a `POLL` confirms nothing. It then sent `PHY_REVERTED` detail `0x0001` in the first frame after the revert |
| The bridge reboots before its commit | The bridge was reset 150 ms after it retuned | The bridge came back on 917.4 and published `phy_reverted` `restart`. All three nodes reverted when their windows closed, and f1 sent `PHY_REVERTED` |
| The bridge reboots after its commit | The bridge was reset 150 ms after its commit, before any step 7 `GET` | The bridge came back on 917.0. Its §10.6 roll reached each node inside its window, and both boards committed 917.0 with no `GET` sent |
| Back to Envelope A | 917.0 → 917.4 | The change committed on the bridge and on both boards |

**A node's `PHY_REVERTED` from a bench identity never reaches the broker.** Spec §16.6
withholds bench data from `event/` topics, and the bridge counted every such frame in
`bench_withheld`. The bridge's own `phy_reverted` did publish, and §12.4.2 step 8 names it
as what reaches Home Assistant. GateLink's event will be the first to publish.

**A poll and a PHY `CONFIG` went to f2 211 ms apart, and the change was abandoned.** In
one tick, `sched_polls()` sent f2 its `POLL`, and then `sched_phy()` sent f2 its `SET`.
The bridge received no answer to either frame, and f2 logged no `CONFIG`. After
`config_ack_timeout_ms`, the change was abandoned `not_accepted`, which is §12.4.1 step 4.
f0 and f1 had already accepted. f1's board retuned alone and reverted at 120 s. The Heltec
never retuned, because f2 had not accepted. The bridge read f2 back with `GET_ALL` once
`phy_trial_s` had passed. The abandon worked as specified. The cause is that nothing in
`sched_task` stops a second frame from going to a node whose poll answer is still due. The
*BF-34 on air* entry saw the same 210 ms gap before a roll. By operator decision, the fix
goes on a branch of its own. The later runs here were started at 36 s past the minute,
between poll cycles.

**Three smaller findings:**

- **The `config/ack` for a committed change can be lost.** The reset 150 ms after the
  commit beat `mqtt_task` to the broker, so that set got no answer. The `config/state`
  published after the reboot showed the new value.
- **`sched_task` had 1352 of 5120 bytes free** when the deferred readback completed. That
  figure includes two completed PHY changes and one abandoned change, against 2312 after an
  ordinary `CONFIG`. `lora_task`'s lowest figure was 6168 bytes. `mqtt_task`'s is still
  unread, because nothing prints it.
- **The bridge's `event_id` for `phy_reverted` restarts at 1 after a reboot**, so ids 1 and
  2 were followed by id 1. Spec §7.3's deduplication covers node events, not the bridge's
  topic, so this breaks no rule. An automation keyed on `event_id` alone would miss the
  repeat.

## 2026-09-24 — The poll clash fixed: one exchange on the air at a time, and the bench shows the clash before and none after

**On the bench, the old image sent five frames while a `POLL`'s answer was still due, and
the fixed image sent none.** The fix is `air_turn.h` (`6a8b76d`). An outstanding scheduled
`POLL` holds a command, a roll, a `CONFIG` and the start of a PHY change. In turn, no
scheduled `POLL` starts while one of those waits for its answer, while a PHY change blocks
traffic, or while a command or configuration job waits in its queue. The operator chose this
over the narrower fix, which held other frames behind a poll but let polls run on. Under the
narrower fix, a missed poll could hold a step-7 `GET` for 10 s, more than step 8's 8 s
reserve for that node. A `POLL` could also still follow a `COMMAND` whose ACK was due. The
full trace is [`data/poll-clash-bench-2026-09-24.log`](./data/poll-clash-bench-2026-09-24.log).

**The bench.** The control arm ran on the image the bridge already held, `2066fc9`. The fix
arm ran on `30c295f`, flashed from a clean tree; its firmware is `6a8b76d`'s. The fleet was
f0 and f2 on the Heltec, and f1 on the XIAO. `poll_interval_s` was 10 on `simnode0` to
`simnode2`, which put a `POLL` on the air about every 3 s. The harness published each fix-arm
change 40–50 ms after the bridge's frame log showed a `POLL` going out. Each change then
started only once that `POLL` was answered, 0.4–2.1 s later.

**How a clash was counted.** The bridge's serial frame log (BF-27) records every frame it
sends and receives, on its own millisecond clock. A `POLL` to a node counts as open from its
`tx` record until the next `rx` from that node, or for 10 s. Every other `tx` inside that
interval is a frame sent while an answer was due.

| | Control, `2066fc9` | Fix, `30c295f` |
|---|---|---|
| `POLL`s sent | 95 | 77 |
| Command, roll or `CONFIG` sent while a `POLL` was open | **5**: three rolls and two step-7 `GET`s, each 229 ms after a `POLL` to another node | **0** |
| A scheduled `POLL` beside a step-6 `POLL` to one node | once, to f0, 229 ms apart | 0 |
| Rolls after the boot | f0 took 1 attempt; f2 and f1 took 2, each retried 3003 ms later, after a timeout | f0 and f2 took 1 attempt; f1 took 2 (see below) |
| PHY changes committed | 1 of 1 | **5 of 5**, each started beside a `POLL` |
| `CONFIG` frames sent | 7 for one change: f2's `GET` went unanswered and was sent again 7.4 s later | 30 for five changes, six each, none sent again |
| From the set to the commit | 14.3 s | 12.8–20.5 s |

**Five `OPEN`s went to f1 under 10 s polling on the fixed image.** Each was acknowledged at
the first attempt, 1.7–2.9 s after the publish on `lran/simnode1/cmd/open/set`, measured at the
broker. None waited out a missed poll, because no poll was missed.

**Two frames can still go out while an answer is due, and neither is this fix's to close:**

- **A PHY change's step-6 `POLL`s go 229 ms apart to different nodes**, in two of the five
  changes. They belong to the change itself, which `air_turn.h` leaves alone. Both changes
  committed.
- **f1's roll after the fix arm's reflash took two attempts with no `POLL` open.** The
  bridge heard f1's `ACCEPTED` at 14815 ms and sent the retry 52 ms later, 527 ms after the
  first attempt went on air. f1 answered it `REJECTED_CTX`, which completed the roll (spec
  §10.6 step 4). A 3000 ms window ending at 14867 ms would have opened at about 11867 ms. That
  fits a window opened when `sched_task` queued the frame, and a frame that then waited about
  2.5 s for media access. The trace does not show when the frame was queued, so this is not
  shown.

**Opening the three ports reset all three boards**, `rst:0x1 (POWERON)` on both Heltecs and
`USB_UART_CHIP_RESET` on the XIAO. The handoff said that pyserial reset none of them earlier
the same day. The first attempt published its setup while the bridge was booting, so only
`simnode2`'s set arrived and a one-node change ran. That attempt was stopped. The run above
waited for the bridge's `availability` before its setup.

**The control arm's return change was refused `phy_change_in_progress`**, because the
harness sent it 15 s after the commit, while the step-7 `GET`s were still running. That is
the harness's timing, not a defect. The fix arm's first change therefore went from 917.0 MHz
back to 917.4 MHz.

**The bench was left as it was found.** The fleet is committed on 917.4 MHz.
`simnode_diag_enable` is 0. `deployed` is 0 and `poll_interval_s` is 60 on `simnode0` to
`simnode2`, both as overrides. `lora_task` read 6344 bytes free at its lowest. No
configuration resolution ran on a node's own topic, so `sched_task` printed no high-water
figure.

## 2026-09-25 — BF-35: the configuration table's controls, in the sandbox HA

**Home Assistant registered every configuration entity the bridge published, and a write
from HA came back through the bridge's `config/state`.** The bridge was flashed with
the BF-35 change, before its commit, over USB on `/dev/cu.usbserial-0001`. esptool read MAC
`44:1b:f6:f9:70:14`, the bridge board's. The image uses 60.2 % of RAM and 28.1 % of flash.
The host suite passed 423 of 423.

**Before anything was published, the operator chose the names and the layout**: the table
name as the `object_id`, the bridge's PHY rows as box-mode controls, each node's PHY rows as
sensors, and the bridge's per-node rows on the bridge's availability. Impl Plan §4.4.3 has
the reasons.

**What HA registered, read over its REST API about 20 s after the boot:**

| Device | Entities | State |
|---|---|---|
| LoRa Bridge | 19 `number`, 1 `select`, 1 `switch` (`simnode_diag_enable`) | Every value the table's default. `tx_power_dbm` shows `-4.0`, because HA renders a number as a float |
| GateLink, WellLink | `poll_interval_s` 60, `deployed` off | Available, on the bridge's availability |
| GateLink, WellLink | 4 node-common `number`s, 6 PHY `sensor`s | `unavailable`, because neither node is online. R-3.3d, as intended |

**Three writes went through HA's services and came back through the bridge**, and each was
restored afterwards. `number.lora_bridge_diag_interval_s` went to 120 and back to 60.
`switch.lora_bridge_simnode_diag_enable` went on and back off.
`number.gatelink_poll_interval_s` went to 90 and back to 60. Every read came from the state
HA took from `config/state`, since an MQTT `number` or `switch` with a state topic is not
optimistic. No PHY row was written, because a write there starts a change across the fleet.
`test_discovery` covers the `select`'s template instead, by parsing the rendered command
through the bridge's own parser.

**What this left behind.** The bridge runs the BF-35 image, and every lever holds the value
it held before. The sandbox registry now has 45 new entities, all under `lran_bridge_`,
`lran_gatelink_` and `lran_welllink_`. The bench nodes got none.

**One gap this run exposed, not closed.** The `select` limits HA to 125, 250 and 500 kHz,
but `lran/bridge/config/set` still takes any `bandwidth_khz` from 125 to 500, 300 among them.
The table has a range, and the SX1262 has discrete points. Recorded under the handoff's
*Open*.

## 2026-09-25 — `spec_citation_version.py` reads role header lines, and finds six stale citations

**The check now reads every versioned `**<Role>:**` header line** that links an `LRAN-`
document, and compares the version after the link with that document's own **Version:**
header. Before this, it read citations of the protocol specification only. The number of
citations it checks went from 26 to 32, and every one of the 26 is still checked.

**The handoff expected two stale citations and the check found six.** Firmware Tasks
cites the Bridge PRD at v0.14 (now v0.15), the Impl Plan at v0.54 (now v0.58) and the
Library Plan at v0.12 (now v0.19). The Impl Plan cites the Bridge PRD at v0.14 (now v0.15)
and the Library Plan at v0.14 (now v0.19). The GateLink Impl Plan cites the GateLink PRD at
v0.9 (now v0.10). Each needs its document read against the intervening revisions before
the number moves, so none was bumped here.

**`KNOWN_STALE` holds the six, so `main` stays green.** An entry is keyed by citing file
and cited document, and holds the version cited. The check fails on a stale citation not
in the table, and on an entry whose citation has moved. The second failure is the one
that stops the table from outliving the debt. Both failures were exercised by editing a
citation and running the check.

## 2026-09-25 — The six stale role header citations reconciled

**Only Firmware Tasks needed changes to its body.** Each citing document was read against
the cited document's changelog since the version it cited. `KNOWN_STALE` is now empty.

| Citing document | Cited document, from → to | What the reading found |
|---|---|---|
| Firmware Tasks | Bridge PRD v0.14 → v0.15 | Nothing. v0.15 is D59 with no requirement change, and BF-33's row already cites D59 |
| Firmware Tasks | Impl Plan v0.54 → v0.59 | §8 did not say B4b was accepted (v0.56), and nothing named D61's `deployed` lever (v0.55) or the poll-clash fix (v0.57). §8 now does. v0.58's BF-35 was already in its row |
| Firmware Tasks | Library Plan v0.12 → v0.19 | BF-33's row still said the PHY rows answer `READ_ONLY` until it lands. Library Plan v0.19 built BF-33's library half, so the row now says it is built. v0.13–v0.18 were already in BF-32's, BF-34's and BF-24's rows |
| Impl Plan | Bridge PRD v0.14 → v0.15 | Nothing. The plan took D59 in its own v0.54 |
| Impl Plan | Library Plan v0.14 → v0.19 | Nothing. §6.2.2, §6.3.1 and B4b's row already describe what BF-34, BF-24 and BF-33 built |
| GateLink Impl Plan | GateLink PRD v0.9 → v0.10 | Nothing. The same D59 sweep wrote both, and §6.4 has the PRD's three points. **W17** asks nothing of the build |

**Four of the six were number-only drift.** A document took a revision's content and
never moved the header number. The check catches the number and cannot tell which kind of
drift it has found, so each one still needs reading.

## 2026-09-25 — The pre-GateLink survey: B5 has no simulator, and two event gaps had no task

**The survey sorted the handoff's open list by one question: can the bridge board, the two
simnodes and the sandbox HA close it before GateLink exists?** Most items can. The handoff
now keeps them in *Work before GateLink*, and keeps the rest in *Waits on GateLink or the
operator*.

**B5 cannot run on the bench yet.** Impl Plan §2.1 lists every milestone from B2 to B5 as
reachable on two boards, and §8 lets B5 use *"a real MPPT reachable via GateLink or a
simulator"*. The simnode has no such simulator. §10.2's `ROLE_GATELINK` answers `POLL`,
`COMMAND` and `CONFIG`, and nothing in `firmware/simnode/` handles `HEX_REQ`. Firmware
Tasks v0.46 adds **BF-36** for the responder. §10.2 is corrected in BF-36's commit, when
the claim becomes true, not here.

**Two event-delivery gaps had no task row.** The QoS 0 transport (Impl Plan §6.3.2) and
the queue that refuses a new event while the broker is down (§5.2.1) were open in the
handoff since BF-25. §5.2.1 gave the per-class refinement to BF-24 and BF-25, and neither
built it. They are now **BF-37** and **BF-38**. Both matter before GateLink deploys,
because its events drive email and SMS.

**The specification's owed text is in one list for the first time.** Items waiting on the
next revision were spread across the handoff, the Decision Register (§3.9, §3.10) and the
Library Plan (§4). The handoff's *Specification v0.15* table collects ten, and marks which
need only text and which need an operator decision first.

**Two stale lines surfaced and were fixed.** Firmware Tasks' BF-13 row said V-B9 had not
run, and BF-14's said the status page had not been seen. Both happened at B2's bench
session on 2026-09-13, in this log's earlier file. Two more are listed rather than fixed:
`firmware/bridge/CLAUDE.md` cites three documents at old versions in prose that
`spec_citation_version.py` does not read, and System PRD §12 gives the register's range as
D1–D58.

## 2026-09-25 — Spec v0.15 accepted, and the citations moved with the header

**The operator accepted spec v0.15 on 2026-09-25**, with D62–D69 as the register records
them. Bumping the header alone failed `spec_citation_version.py` at 26 citations, so the
bump waited for the sweep and merged with it. Each citing document was read against v0.15
before its citation moved.

**Two documents disagreed with v0.15, not only with its number.**

- **GateLink PRD R-5.3e** said a restore-defaults *"SHALL clear all overrides"*. D60 keeps
  the committed PHY group, so the requirement now says so, and names D68's `OVERRIDE` bit
  as the marking it asks for.
- **Bridge Impl Plan §4.2a, §4.4.3, §6.3.2 and §6.6.1** each carried a question for the
  specification. v0.15 answers all four, so each now records the answer, and §6.6.1 the
  rename D66 owes.

**The two stale citations the pre-GateLink survey entry listed are fixed**: `firmware/bridge/CLAUDE.md`
now cites the Bridge PRD, Impl Plan and Firmware Tasks at their current versions, and
System PRD §12 gives the register's range as D1–D69.

**The W4 vectors were regenerated, and every vector kept its bytes.** Only each file's
`spec` field changed. No vector exercises D68's `OVERRIDE` bit or D64's `INVALID_VALUE`
yet; both come with the `lran-protocol` change, which the handoff lists with the rest of
the code v0.15 owes.

## 2026-09-25 — The code spec v0.15 owed, built on host

**Every code line the handoff's group 1 listed is built, and none of it has run on air.**
The libraries, the simnode and the bridge pass their native suites (137, 29, 130 and 428
cases), and the `heltec` target builds. Library Plan v0.21 and Impl Plan v0.61 (§6.7.2a)
say what changed.

**A round trip does not witness the `OVERRIDE` bit.** A codec that read `status` whole
would decode `0x80` as an unknown `ParamStatus` and write the same byte back, so
`test_vectors`' new schema round trip passes it. The two new vectors are therefore also
checked by name, field by field. The round trip catches the other defect: a codec that
drops bit 7.

**After the first committed PHY change, every PHY row reads `override`.**
`commit_phy_trial()` writes the whole group, rows the set did not name included, so each
becomes a held override. D68 says an override equal to its default is still an override,
so the marking is honest. It will surprise an operator who changed only `spreading_factor`
and then sees `freq_hz` marked. Recorded rather than changed: the fix belongs to the store's
design, not to this marking.

**Two gaps found while wiring D69, both out of scope:**

- **No simnode sends `CONFIG_CHANGE` on its own.** Spec §8.7 has a node send it after a PHY
  revert. The bridge path can be exercised today only by setting the reason by hand.
- **The readback mirror skips a `CLAMPED` result**, though it carries the effective value
  (spec §7.4). `note_set_results()` records `OK` alone, so a clamped set leaves
  `config/state` showing the value from before the set. This predates v0.15.

## 2026-09-25 — Spec v0.15's code on air: four checks pass, and the bench found a wrong `persist`

**All four bench checks the handoff listed pass, after one bridge fix.** The bridge on
`/dev/cu.usbserial-0001` (MAC `44:1b:f6:f9:70:14`) first ran `5176d1a`. The simnode Heltec
on `/dev/cu.usbserial-3` (MAC `44:1b:f6:fa:bc:2c`) holds f0 `ROLE_RANGE` and f2
`ROLE_HEALTH`. The XIAO on `/dev/cu.usbmodem2101` (MAC `68:ee:8f:4b:85:f4`) holds f1
`ROLE_GATELINK`. Every board was identified by MAC before it was flashed. A paho client
recorded `lran/#` with UTC timestamps; the times below come from it.

**D68 — `source` follows the node's bit, not the value.** On the bridge's topic,
`cmd_retries` set to 3, its default, reads `override`. On f1, the PHY rows first read
`override` at Envelope A's values, because the XIAO's NVS held a committed group from an
earlier change. After `phy reset` and a reboot, the same values read `default` (13:34:06).
**The simnode's `phy reset` does not clear the marking until a reboot.**
`PhyTrial::reset_to_defaults()` writes each default through `Store::restore()`, which
holds it as an override, so a readback between the reset and the next boot still reads
`override`.

**D64 — a bandwidth of 300 is refused, and the ack's `persist` was wrong.** On the bridge's
topic the entry read `invalid_value` with value 125, but `persist` read `persisted`
(13:34:16). Spec §8.11 says `NOT_APPLIED` when every entry was refused. The unchanged-group
path in `handle_config_set()` set `persisted` for every PHY-only set, on the reasoning that
the group in force is the committed one. That holds for a row equal to the group, and not
for a refused one. `phy_unchanged_persist()` now decides it, host-tested, and the reflashed
bridge answered `not_applied` (13:36:52). With `phy_trial_s` 121 in the same set, the
bandwidth read `invalid_value` and the change committed with `persist` `persisted`
(13:37:12).

**On a node's topic the answer is `read_only`, and that is correct.** Spec §16.7.1 answers
any PHY row named on `lran/<node>/config/set` `read_only` with the last value read back,
so a bandwidth of 300 there never reaches D64's check. The handoff expected
`invalid_value` on both topics; the expectation was wrong, not the code.

**D67 — `phy_reverted` carries `boot`.** The first flash booted as 1 and the reflash as 2.
An SF 10 change, with the XIAO rebooted 1.5 s after the set and before it accepted, gave
`{"boot":2,"event_id":1,"reason":"not_accepted","node":"simnode1"}` (13:37:37).

**D66 — the frame log is on its new leaf.** `lran/bridge/diag/rxlog/log` carried 82
publications by 13:38, none retained, and `rxlog/state` carried none.
`tools/simctl/rxlog.py --seconds 90` read 12 records, two `STATUS` per bench node, with no
losses.

**D69 ran end to end once the simnode sent `CONFIG_CHANGE`.** The simnode now owes one
after its own PHY revert and carries it in the next poll's answer (spec §8.7). An SF 10 set
at 13:43:06 put the XIAO into its trial, and a reboot at 13:43:08 reverted it with detail
`0x0002`. The bridge abandoned the change at 13:44:44 (`reason` `not_heard`, `event_id` 2).
f1's next poll answer at 13:44:56 carried `PHY_REVERTED` and a `STATUS`. The bridge's next
poll to f1, at 13:45:06, drew a `CONFIG_ACK` (schema `0x12`), and `simnode1/config/state`
was republished at 13:45:08 with no `config/set` and no `config/ack`. The frame log does
not record `status_reason`, so the `CONFIG_CHANGE` itself is inferred: nothing else in that
window starts a readback.

**The `CLAMPED` mirror fix is host-tested only.** `note_set_results()` now mirrors `OK`,
`CLAMPED` and `INVALID_VALUE`, the three results spec §8.12 says carry the effective value.
No simnode node-held row clamps on the bench: f1's readback carries its PHY rows and the
bridge's per-node rows alone, so the fix waits for GateLink's table to reach the air.

**Opening either simnode's port appeared to reboot the board**, although `con.py` set DTR
and RTS low before `open()`, as `simctl.py` does. The boot banner followed each open, and
f1's `ctx_id` changed each time. Every reboot is a new context and a context roll at the
bridge, so a bench step that opens a console mid-scenario is also a reboot.

Suites: bridge 430 of 430, simnode 130 of 130; the `heltec` target builds. The run left
`simnode_diag_enable` and each bench row's `deployed` cleared, and `phy_trial_s` back at
120.

## 2026-09-25 — events at QoS 1 on espMqttClient, from a queue of their own (BF-37, BF-38)

**The bridge publishes events at QoS 1 now, and state can no longer take an event's queue
slot.** BF-37 replaced PubSubClient 2.8 with espMqttClient 1.7.3, D5's designated fallback.
BF-38 gave events a queue of their own. Impl Plan §4.3.3 records the choices. The bridge
board ran `e08f4b1`, flashed from a clean tree.

**What the bench showed**, on the sandbox broker:

| Check | Result |
|---|---|
| A dummy `vehicle_while_held_open`, read by a subscriber at QoS 1 | Arrived at QoS 1, not retained. The broker delivers at the lower of the two QoS values, so the bridge published at QoS 1 |
| The Mosquitto add-on restarted; three dummy `STATUS` frames and a `fire_asserted` with its follow-up raised during the outage | Both events arrived within 0.1 s of the bridge's `online`, each once |
| `lran/bridge/diag/radio/state` afterwards | `q_event_high_water` 2 and `q_event_dropped` 0, beside the six older queues |
| Heap, printed by the bridge on the reconnect | 72,580 bytes free, 60,188 at the lowest |

**The run did not force a QoS 1 retransmission.** An event the broker has not acknowledged
when the connection drops is sent again after the CONNACK. That behaviour comes from reading
espMqttClient's `_clearQueue()` and `_onConnack()`, and no bench run has exercised it. The
events in the outage row waited in the bridge's own event queue, which BF-38 built, and
never reached the library before the reconnect.

**Static RAM rose about 42 KB**, from 197,328 bytes on `main` to 239,260. The event queue
takes 13 KB and the library's packet pool about 25 KB. There is no heap figure from before
the change to compare with, because nothing printed one.

**Three things in the library cost the most reading:**

- **By default, espMqttClient allocates each outgoing packet on the heap.** Root rule 3
  forbids that. `EMC_USE_MEMPOOL` switches it to a static pool, and nothing in its README
  says so. With a pool, the library's `publish()` queues a packet and writes it in `loop()`,
  so the drain now waits for `pending()` to fall below eight.
- **An inbound payload arrives in pieces cut at the library's read buffer**, each with its
  offset and the total. A `config/set` cut in two would have been parsed as two broken
  ones. `InboundAssembler` rebuilds it, and `test_net` covers the cut points.
- **Its `library.json` lists AsyncTCP, which is LGPL-3.0, as a dependency on every ESP32
  build.** PlatformIO compiles it, and the linker map shows no AsyncTCP member in the image.
  `THIRD_PARTY_NOTICES.md` has the `grep` that would show otherwise.

**The synthetic filter for Home Assistant** is `ha/automations/lran_event_notify.yaml`. It
was loaded into the sandbox HA as committed. A dummy event from the bridge reached the
broker and did not trigger it. A hand-published event marked `synthetic: false` did, which
left one persistent notification in the sandbox. The automation was deleted after the run.

Suites: bridge 436 of 436; the `heltec` target builds.

## 2026-09-25 — B5's HEX code against osh-labs/VE.Direct_mppt_arduino: one register moved

B5 built `lib/vedirect/`, `charge_readback` and `sim_mppt` from Victron's "BlueSolar HEX
protocol" PDF. `osh-labs/VE.Direct_mppt_arduino` is now the VE.Direct reference of record
(GateLink Impl Plan §4.2.4), so the code was compared against the library's
`src/VeDirectHexProtocol.{h,cpp}` and `src/VeDirectRegisters.h` at its `main`. That
register file says it was itself verified against the same PDF, Rev 18.

**Framing agrees throughout.** The command and response nibbles, the Get/Set reply flags
(`0x01`, `0x02`, `0x04`), the `0x55` checksum, little-endian register and value, and
uppercase output all match.

**One register disagreed, and it moved.** The bridge read "System voltage setting" at
`0xEDEF`, and `sim_mppt` held it there. The library does not name `0xEDEF`; its
`SYSTEM_VOLTAGE` is `0xEDEA`, un8, volts. Both now use `0xEDEA`. The HA `object_id`,
`charge_system_voltage_v`, is unchanged. Which register carries the configured setting on
the MPPT 75/15 is still unobserved; B6's readback against the real MPPT confirms it.

**The other nine charge registers match** in ID, width, sign and scale: `0xEDF7`, `0xEDF6`,
`0xEDF4` at 0.01 V; `0xEDFD` and `0xEDF1`, un8; `0xEDF2`, sn16 at 0.01 mV/K; `0xEDF0` at
0.1 A; `0xEDFB` at 0.01 h. So do `sim_mppt`'s `0x0201` device state and `0xEDDA` error
code.

**Two gaps where the library is silent**, so the PDF still stands:

- `0xEDE0`, battery low-temperature level, sn16 at 0.01 °C. The library has no such
  register.
- **Lowercase hex.** The library's receive parser accepts it; `lib/vedirect` refuses it.
  This difference is kept on purpose: the library reads only MPPT output, while
  `lib/vedirect` also checks requests typed into Home Assistant, and Victron requires
  uppercase.

`HexRsp` has no `Async` (`0xA`) member. The bridge never sees an unsolicited frame, but
GateLink's UART will, and the library's async queue is the model for it there.

Suites: `lib/vedirect` 12 of 12, simnode 140 of 140, bridge 466 of 466.

## 2026-09-25 — B5's spec readings: four points where the code chose, raised for v0.16

BF-36 and BF-28 to BF-30 met four places where spec v0.15 is silent or reads two ways. The
operator chose each reading on 2026-09-25, and the code builds it. Each is raised for
spec v0.16 here, one line each. Until v0.16 settles them, the code is a reading of the
specification, not a statement of it.

- **(a) A refused write-class `HEX_REQ`.** §8.13 names `HEX_RSP(REJECTED_UNAUTHENTICATED)`
  for a request with no valid MAC, and §9.4 step 3 names `COMMAND_ACK(REJECTED_MAC)` for
  every authenticated frame. The simnode answers a bad MAC with the `HEX_RSP`, and a
  context, deduplication or `seq` failure with the `COMMAND_ACK` §9.4 names. The bridge
  claims either answer. Raise: §7.6 should say which frame answers at each step.
- **(b) `HEX_RSP`'s `seq`.** §9.2's table correlates a `HEX_RSP` to its request by `seq`,
  and §7.6 does not say the node repeats it. The simnode repeats the request's `seq`, and
  the bridge matches on the node and that `seq`. Raise: §7.6 should state it.
- **(c) A retained `write_enable/set`.** §16.2 marks `write_enable/{state,set}` retained
  together. A retained `ON` on `set` would re-arm writes on every broker reconnect, which
  defeats gate 2. The bridge ignores a retained `set` and publishes an empty retained
  message to clear it. Raise: §16.2 should mark `set` not retained.
- **(d) Two VE.Direct topics the spec does not list.** `vedirect/charge/state`, the
  readback R-3.5d asks for, is not in §16.2; it follows §16.1's grammar with `charge` as
  the item. §16.6's list of a bench node's answers names `config/ack`, `config/state` and
  `cmd/ack`, and not `hex/response`, `hex/audit` or `write_enable/state`. The bridge
  publishes those three for a bench node whatever `simnode_diag_enable` says, for D65's
  reason, and gates the bench readback on the flag. Raise: §16.2 and §16.6 should list
  them.

Impl Plan §6.4.1 records the code, and none of it has been on air.

## 2026-09-25 — V-B6 on the bench: the three gates, the expiry and the audit, against f1's simulated MPPT

**V-B6 passed on the bench.** The bridge board and the XIAO were flashed from `7e7b92a`, a
clean tree, and f1 ran `ROLE_GATELINK` with BF-36's simulated MPPT. A script held both
serial ports open for the whole run and published to `lran/simnode1/vedirect/...` on the
sandbox broker. `simnode_diag_enable` was 1 for the run, so the bench readback published,
and 0 afterwards.

| Step | Result |
|---|---|
| Get `0xEDF7`, `:7F7ED006A` | `answered`, `ok`, `:7F7ED008C05D9`, 14.20 V. `seq` 11, in the read space |
| Set `0xEDF7` to 14.00 V while disarmed | `refused_disarmed`, no frame on air; `hex/audit` with `authorization` `disarmed` |
| `ON` on `write_enable/set` | `write_enable/state` `ON` 0.8 s later |
| The same Set while armed | `answered`, `ok`; `hex/audit` with `authorization` `armed`. The XIAO logged the write under command `seq` 2 |
| Get `0xEDF7` again | `:7F7ED007805ED`, 14.00 V. The readback pass after the write had already published it on `charge/state` |
| `mppt f1 timeout 1`, then a Get | `answered` with `status` `timeout` and a `null` response |
| `hello` on `hex/request` | `malformed`, `seq` `null`, nothing transmitted |
| Arm, then wait | `write arm expired` and `write_enable/state` `OFF` **301.0 s** after the arm. The Set that followed was `refused_disarmed` |
| A fresh subscriber | Received the retained `hex/audit` (the last refusal), `write_enable/state` `OFF` and `charge/state` |

**The first readback pass ran as the bridge first heard f1**, after the roll, one register
every 2 s, and all ten answered. The XIAO counted 43 frames in and 44 out, with no drop and
no rejection.

**A retained `write_enable/set` is refused on a reconnect, and only then.** The first
attempt published a retained `ON` while the bridge was subscribed, and the bridge armed. The
test was wrong, not the code: a broker delivers a message to a subscriber already
connected with the retain flag clear (MQTT 3.1.1 §3.3.1.3), so that `ON` is
indistinguishable from an operator's. The case the rule guards is a reconnect. With that
`ON` still retained, a reset of the bridge logged `retained write_enable/set ignored and
cleared`, published an empty retained message on `write_enable/set`, and stayed disarmed.
A fresh subscriber then found no retained `set`.

**A refused write takes a command `seq`.** The two `refused_disarmed` Sets took `seq` 1 and
4, and neither reached the air. The node accepts any `seq` above its high-water mark (spec
§9.4), so the gap costs nothing. It does mean `seq` on `hex/audit` is not a count of writes
the node saw.

**`charge/state` does not follow a change made behind the bridge's back.** After `mppt f1
reset` put 14.20 V back, `charge/state` still read 14.00 V, because a pass runs only on
first hearing and after a write the bridge sent. On GateLink the same happens when a
setting is changed with VictronConnect. The next bridge boot corrects it.

Not covered: gate 1 on air. No bench tool sends a write-class `HEX_REQ` with a bad MAC;
`test_gatelink` and `test_hex_proxy` cover it on the host.

## 2026-09-25 — The air-timing defects: exchanges exclude each other, windows open on air, step-6 POLLs take turns

**Three defects from B4b's bench runs are fixed and host-tested, and none is yet shown on
air.** All three sat in `sched_task`'s decision about when a frame may go or when its answer
is late. Each is one commit on `b-defects-air-timing`.

**A command and a `CONFIG` could be in flight to one node together** (`30d3ae1`). Every
pair of exchanges excluded each other except two: `sched_config()` did not ask whether a
command or a roll was busy, and `sched_commands()` and `sched_roll()` did not ask about the
`CONFIG`. Each sender carried its own list, so the gap was one missing term in three places.
`exchange_may_start()` in `air_turn.h` now answers for every exchange, and each sender asks it
alone plus its own extra condition. A gate command can now wait behind a `CONFIG`, for its ACK
timeout and then its readback, where before it would have gone beside it.

**A reply window opened at the queue, not on air** (`2b55681`). This is the *poll clash
fixed* entry's f1 roll, retried 527 ms after it went on air. The code confirms the
mechanism that entry could only infer: every path called `on_sent(now_ms)` straight after
`send_tx()`, and `lora_task` can hold a frame through several CAD rounds of up to
`backoff_max_ms` each. The trace still does not show when that frame was queued, so the
2.5 s wait remains an inference from the arithmetic. The command, roll, `CONFIG`, HEX and
PHY-change paths now queue with a ticket. `lora_task` records when each ticketed frame
leaves it, whether sent, timed out, refused by the radio or dropped with the radio down.
`sched_task` holds that path's `next()` until then, and `on_aired()` moves the window's
start. **A HEX write was the sharpest case**: it is never retried, so a window closed
early reported `unknown` for a write that happened.

**The scheduled poll keeps its window at the queue.** `scheduler.h` says its answer time
includes the queue and media access on purpose, B3a records it against
`poll_reply_timeout_ms` (Impl Plan §6.1.1), and 10 s is sized for that wait. Moving it would
change a measure that other entries cite. Polls a `CONFIG` readback or a PHY change sends
belong to their path, and they do wait for the air.

**The hold has a 10 s backstop.** If `lora_task` never reported a frame, the path would
hold for good, and on the command path that means a gate that stops answering. After 10 s the
window opens from then, and the serial log prints `air: <path> frame not reported`. Nothing
should print that line; if it prints, look for an exit from `lora_task`'s transmit path that
does not call `note_tx_done()`.

**A PHY change's step-6 `POLL`s go one at a time** (`3c53e7f`). Each node's `POLL` waited
only for that node's previous one, so two went 229 ms apart to different nodes. Now a
`POLL` whose answer is still due holds the next, to any node, for one
`config_ack_timeout_ms`. **The first version of this fix starved a node**: after a silent
node's timeout, the loop picked the first unheard node again, which was the silent one.
`test_a_silent_node_holds_the_next_poll_for_one_timeout` caught it, and the nodes now take
turns from the one after the last polled. Worst case, with every node silent, each node is
polled once per `nfleet × config_ack_timeout_ms` rather than once per timeout. Step 8's
deadline is unchanged.

**What would show these on air**, on a bench run with the frame log (BF-27):

- No `CONFIG` `tx` record while a `COMMAND` or `ROLL_CONTEXT` to any node awaits its answer,
  and the reverse.
- No roll or command retry within `cmd_ack_timeout_ms` of the previous attempt's `tx`
  record. The `tx` record is written when `start_transmit()` starts, so the window, which
  now opens at `TX_DONE`, closes at least that long after it.
- Every step-6 `POLL` `tx` record follows the previous one's answer, or comes at least
  `config_ack_timeout_ms` after it.

Native suites: 475 cases pass. `heltec` builds.

## 2026-09-25 — The air-timing fixes on air: no two exchanges overlapped, and every one took one attempt

**The bridge ran `6db6771` for a 5-minute bench run, and none of the three defects
appeared.** The fleet was f0 and f2 on the Heltec and f1 on the XIAO, with `deployed` set and
`poll_interval_s` 10. The trace is
[`data/air-timing-bench-2026-09-25.log`](./data/air-timing-bench-2026-09-25.log).

| Check | Result |
|---|---|
| Rolls after the boot | f0, f1 and f2, **1 attempt each** |
| A `CONFIG` and an `OPEN` to f1 published 50 ms apart, four times, both orders | **Serial every time.** The `COMMAND` went, its ACK came back, and only then the `CONFIG`, 0.2–1.7 s later |
| Five more `OPEN`s to f1 | All acknowledged at the first attempt, 1.5–2.7 s from the publish to `cmd/ack` |
| PHY change to 917.0 MHz and back | **Both committed**, in 11.6 s and 13.8 s |
| Step-6 `POLL`s | Six. After each change's first, each went 0.2–2.1 s after the previous node's answer |
| A frame sent while another path's answer was due | **0 of 111** |
| `air:` backstop lines | 0 |

**How an overlap was counted.** A `tx` in the bridge's frame log opens its peer's answer
until the next `rx` from that peer, or until that path's own window has passed: 10 s for a
`POLL`, 3.5 s for a `COMMAND`, 8 s for a `CONFIG` and 3 s for a `HEX_REQ`, each plus 300 ms
of airtime. **A flat 10 s cap for every type, the poll-clash entry's rule, flags five
frames.** All five follow a BF-30 `HEX_REQ` to f0 or f2 by 5.0–9.2 s. Neither identity has
an MPPT behind it, so neither answers, and the HEX path's 3 s window had closed.

**Media access was busy, so the window fix had something to act on.** The bridge counted 44
CAD backoffs and 2 forced transmissions over 105 frames. No exchange was retried. **The
2026-09-24 roll retry was not reproduced**, so this run shows the fix does no harm under
that load, not that it closes that case. The host test
`test_the_window_counts_from_when_the_frame_aired` in `test_context_roll` replays it.

`sched_task` read 1932 bytes free at its lowest, on a `CONFIG` resolution; `lora_task` read
6416. **The bench was restored**: `deployed` 0 and `poll_interval_s` 60 on `simnode0` to
`simnode2`, and `simnode_diag_enable` 0, all acknowledged. The fleet is on 917.4 MHz. f1 holds
`dedup_cache_depth` 8, its default, in RAM until its next reboot.

## 2026-09-25 — The mirror readback: a node reboot is read from its STATUS, and config/state follows it

**`config/state` no longer reports overrides a node reboot cleared.** The bridge now reads a
reboot from a node's `STATUS` and asks for a readback, through the pending bit D69's
`CONFIG_CHANGE` already sets. Impl Plan §6.7.7 records the design.

**The handoff proposed a readback when a node's `ctx_id` changes. Spec §10.1 rules that
out.** Since v0.13 a new context "no longer means the node rebooted", because a roll makes
one, and the same section names `boot_count` and `uptime_s` as the fields that report a
reboot. Keyed on the context, the readback would fire after every bridge restart, once per
roll. It would also race the roll: a node whose `ACCEPTED` is lost is learned at its next
frame, before the roll resolves on `REJECTED_CTX`. `RebootWatch` in `config_path.h` reads
three signs instead: `status_reason` `BOOT`, a change in a non-zero `boot_count`, and an
`uptime_s` below the last reading plus the time since. A roll moves none of them.

**On the bench**, the bridge ran `3534e0b`. f1 was on the XIAO, running `7e7b92a`. Each
case first set `dedup_cache_depth` 16 on `lran/simnode1/config/set`, and `config/state`
showed it as an override within 3 s. The trace is
[`data/mirror-readback-bench-2026-09-25.log`](./data/mirror-readback-bench-2026-09-25.log).

| Case | Sign that caught it | `config: f1 rebooted` | `config/state` back to `null`, `default` |
|---|---|---|---|
| `REBOOT` on `lran/simnode1/cmd/reboot/set`, payload `165` | `BOOT`, in the simnode's first `STATUS` | On that `STATUS` | 8 s after the publish |
| Board reset, by reopening the XIAO's port, then `push f1` | **`uptime_s` alone.** The push carried `DEBUG_SYNTHETIC`, and a real boot reports `boot_count` 0 | On the push | 4 s after the push |

**The board reset abandoned a BF-30 charge readback mid-pass**, at `0xEDFB`, because the
reset landed on a `HEX_REQ` in flight. That is the pass's own timeout doing its job; the
next first hearing starts a new pass.

**The node's readback marks `freq_hz` and its PHY neighbours `override`.** That is the
simnode's `phy reset` defect, already under group 2 of the handoff, and not new.

`sched_task` read 1900 bytes free at its lowest, on a `HEX_REQ`. The previous entry's run
read 1932. **The bench was restored:** `simnode_diag_enable` 0, acknowledged. f1's override
went with its reboot. Native suites: 483 cases pass. `heltec` builds.

## 2026-09-25 — A node reset between a command and its ACK made §10.3's resync a second execution

**The question was what the spec provides to reboot a node.** `REBOOT` (`0x7F`, guard
`0xA5`) was the whole answer, and §8.1's table row was all the spec said about it. Tracing
a lost ACK through it found the defect. The rebooted node answers the bridge's retry with
`REJECTED_CTX`. §10.3 step 2 adopted the new context and resent the `REBOOT` with `seq` 1,
and the node accepted it, because its dedup cache and `rx_high_water` went with the reset.
**One lost ACK, two reboots.**

**The same path runs without any directed reboot.** A watchdog, a panic or a brownout
between an `OPEN` and its ACK draws the same answer, and the resync became a second relay
pulse. The bridge cannot tell that answer from a node that reset before the request
arrived. `command.cpp` and `hex_proxy.cpp` both resynced, and `hex_proxy.cpp`'s comment
argued the retry could not write twice. That holds only for a node that has not reset.

**The operator chose D70–D72 the same day**, and spec v0.16's new §10.7 lists every reset
case. An actuation command, a `REBOOT` or a VE.Direct Restart that draws `REJECTED_CTX`
now ends `unconfirmed`, and the bridge adopts the context without a retry. `resync_may_retry()`
reads spec §8.1's split by range. `test_command` gains three cases and `test_hex_proxy`
one; the three resync cases that used `OPEN` now use `REQUEST_STATUS`. Native suites:
487 bridge cases, 137 library and 140 simnode, all pass. `heltec` and the simnode build.

**Two more gaps turned up in the trace.** §10.1 said only "random" for `ctx_id`, and a
node without WiFi or Bluetooth gets a pseudo-random `esp_random()`. A repeated `ctx_id`
would have let the retried `REBOOT` pass §9.4 step 2 and loop. It would also have reopened
replay, and the bridge would have withheld the new boot's events as repeats. And an
`EVENT` queued at a reset is lost, because events have no ACK. v0.16 requires true
entropy, and it has a node send `FIRE_ASSERTED` and `HARD_SHUTDOWN` again at boot.

**Not run on the bench.** The 2026-09-25 mirror-readback run showed `REBOOT` with its ACK
delivered: `ACCEPTED`, a new `ctx_id`, and an authenticated `CONFIG` accepted afterwards.
The simnode's reboot is simulated, and no run has lost the ACK. The handoff holds both.

## 2026-09-25 — D70 on the bench: a reset between a command and its ACK ends `unconfirmed`, with one execution

**The bridge no longer resyncs an actuation or a `REBOOT` that drew `REJECTED_CTX`.** It
publishes `unconfirmed`, adopts the node's new context, and sends nothing more. The bridge
ran `99d5be4`, flashed over USB for this run. f1 was on the XIAO, whose build still names
spec v0.15 in its banner; nothing it does on this path changed in v0.16. The trace is
[`data/d70-bench-2026-09-25.log`](./data/d70-bench-2026-09-25.log).

**The handoff's recipe could not test `OPEN`.** An `OPEN` whose ACK is suppressed, with no
reset, is answered from the dedup cache and ends `acked`, which is correct. The actuation
case needs a reset between the execution and the retry. `ctx f1 new` on the console
supplies it: it calls the same `new_context()` as the simulated `REBOOT`, and it went in
10 ms after the simnode logged `OPEN ACCEPTED`, well inside `command_ack_timeout_ms`
3000. Case C is the recipe as written, kept as the control.

Each case armed `ack f1 suppress 1` first.

| Case | Simnode | `cmd/ack` | Executions, actuations after |
|---|---|---|---|
| A: `REBOOT`, payload `165` | `REBOOT ACCEPTED`, `rebooted`, then `REJECTED_CTX` on the retry | `unconfirmed`, attempts 2, result 3, 5.7 s after the publish | 1, 0 |
| B: `OPEN`, then `ctx f1 new` | `OPEN ACCEPTED`, then `REJECTED_CTX` on the retry | `unconfirmed`, attempts 2, result 3, 8.4 s after the publish | 2, 1 |
| C: `OPEN`, no reset | `OPEN ACCEPTED`, then `DUPLICATE_CACHED (ACCEPTED), not executed` | `acked`, attempts 2, result 7 | 3, 2 |

**One reboot and one actuation per command, and no third frame.** In A and B the retry
carried the old `ctx_id` and the same `seq`, and the bridge's next command reached
the new context at `seq` 1 with no roll. `diag/cmd/state` read `cmd_unconfirmed` 2 and
`cmd_resyncs` 0 after B.

**This is still a simulated reset.** The simnode's `REBOOT` and `ctx new` clear what a
reset clears without an `esp_restart()`. Group 2 of the handoff holds that gap. The bench
was restored: `ack f1 normal`, and `simnode_diag_enable` was 0 throughout.

## 2026-09-26 — The three restart edges: each now survives the restart in NVS, host-tested

Group 1's last item. Impl Plan §6.7.8 has the design. This entry records what reading the
code found.

**The trial marker's loss had a second cost.** `Store::restore_defaults()` called
`clear_all()`, which on the bridge is `Preferences::clear()` on the whole `cfg`
namespace, and then `save_group()`, which writes the blob with the marker clear. Between
the two writes, flash held no PHY group at all. A reset in that moment would bring the
bridge up on the table's defaults, off a fleet that had moved. `clear_all()` now removes
the scope's table keys one at a time and never touches the blob. `Store::restore_defaults()`
no longer rewrites the group, and lran-config's `Persist` contract says so.

**The owed `config/ack` clears only after it has left the bridge.** Clearing the record when
`sched_task` queued the answer would not have fixed the 2026-09-24 case, because that
answer was queued and then lost in the queue. `drain_publish_queue()` clears the record
once the publish queue is empty and the transport's `pending()` is 0. A refused
publication sets the answer back to owed, so a disconnect at the wrong moment can publish
it twice.

**The bench `online` needed a record, not a rule.** The first BF-26 build published
`offline` at every boot with the flag clear, which spec §16.6 forbids (the *BF-26 on air*
entry). With every state `Unknown` at boot, the bridge has no other way to tell a topic
holding a retained `online` from one never published. The mask is written on the tick a
bench row's topic changes between `online` and `offline`, not on every publication.

**Verified:** lran-config 29 and bridge 490 host tests, simnode 140, the `heltec` and
`simnode-heltec` builds, and `run_ci_local.py`. No bench run: each edge needs a reset
timed within a fraction of a second, or a flag set while NVS refuses the write.

## 2026-09-26 — Group 2's housekeeping: the leveled log, the watchdog and `phy reset`, host-tested

Four of group 2's five items, scoped with the operator at the start: BF-11a, BF-11b,
`mqtt_task`'s high-water mark and the simnode's `phy reset`. The simnode's spec v0.16
reset obligations and BF-27's bridge-side simulators became groups of their own in the
handoff. Impl Plan §5.2.2 has the design.

**The first line length cut the longest line.** `LogMessage` held 160 bytes, the frame
log's line length. The test that formats the `levers:` line with every field at its widest
failed: that line reaches 182 characters. It is 192 now. A cut line ends in `...`, but a cut
`levers:` line hides the value a bench run set out to confirm.

**The watchdog timeout is compile-time, against root rule 8's letter.** Impl Plan §5.2.2
argues it. The TWDT was already running under Arduino-ESP32 at 5 s, watching only core
0's idle task. `esp_task_wdt_init()` reconfigures it to 10 s rather than failing, which the
IDF 4.4 header states.

**`phy reset` had to leave the table's defaults unmarked, and no call on `Store` could do
that.** `restore()` holds any value it is given as an override, a default included, and
`restore_defaults()` deliberately keeps the PHY group (D52). lran-config gains
`Store::forget_phy_group()`, documented as a bench tool that nothing on the air reaches.

**Verified:** bridge 497, lran-config 30 and simnode 140 host tests; the `heltec`,
`simnode-heltec` and `simnode-xiao-wio` builds; `run_ci_local.py`. **Nothing was flashed.**
Owed on the bench: the `Reset:` banner line, one `mqtt: stack high-water` line after
connect, `sched_task`'s high-water mark on the next configuration resolution against
2026-09-24's 1352 bytes, and `phy reset` on a simnode followed by a readback with no PHY
row marked as an override.

## 2026-09-26 — Group 2's housekeeping on the bench: all four owed lines read

The bridge ran `4ef3f2c` on `/dev/cu.usbserial-0001` (MAC `44:1b:f6:f9:70:14`), flashed
over USB. Both simnodes ran the same commit: the Heltec on `/dev/cu.usbserial-4` (MAC
`44:1b:f6:fa:bc:2c`) holds f0 and f2, and the XIAO on `/dev/cu.usbmodem2101` (MAC
`68:ee:8f:4b:85:f4`) holds f1. The trace is
[`data/housekeeping-bench-2026-09-26.log`](./data/housekeeping-bench-2026-09-26.log).

- **The `Reset:` banner line reads `Reset: power_on`** after an RTS reset. The ROM line
  above it reads `rst:0x1 (POWERON)`, so the two agree: the CP2102 pulls EN, and the S3
  reports that as a power-on reset.
- **`mqtt_task`'s high-water mark fell three times**: 4616 bytes free of 6144 before the
  broker connected, 2568 after it, and 2124 once the `get_all` readbacks had run. That
  leaves 2124 bytes as the lowest reading so far, not a settled figure.
- **`sched_task` had 2296 bytes free** after f1's `get_all` resolved. The comparable
  2026-09-24 figure is 2312, after an ordinary `CONFIG`, so `log_printf()` costs about 16
  bytes on this path rather than the 130 expected. The 1352 bytes of 2026-09-24 came after
  two PHY changes and an abandoned one. No PHY change ran here, so that low is not
  re-measured.
- **`phy reset` leaves no PHY row marked `override`.** After `phy reset` on the XIAO, a
  `get_all` on `lran/simnode1/config/set` drew six PHY results, and `config/state` showed
  `freq_hz` to `phy_trial_s` as `default`. `poll_interval_s` 60 and `deployed` 0 stayed
  overrides, as the previous bench left them.

**The first `get_all` was refused with `context_roll_pending`.** The reflash gave f1 a new
`ctx_id`, and the bridge had not heard it. `push f1` rolled the context in one attempt, and
the retry succeeded. That is spec §10.1 working as written, not a defect.

No watchdog reset appeared on any board during the run. The bench was left as it was
found: `simnode_diag_enable` 0, and no row's `deployed` changed.
