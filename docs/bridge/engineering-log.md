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
- The broker is another IoT host, at 192.168.4.52:1883. The credentials in `secrets.h`
  authenticate against it.
- The bridge runs `v_b12_blaster`, flashed over USB from `9bd01b3`. Its `secrets.h` points
  at the IoT network. **Restore the house values and reflash `heltec` when V-B12 is done.**

**The first flash could not join the network.** `secrets.h` named the SSID `McLeeNetIoT`,
and the bridge logged `NO_AP_FOUND` on every attempt. The operator corrected it to
`McLeeIoT`. After that the bridge logged one `AUTH_FAIL` and one `ASSOC_FAIL` in its first
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
