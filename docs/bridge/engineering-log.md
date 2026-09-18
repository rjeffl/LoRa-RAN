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
