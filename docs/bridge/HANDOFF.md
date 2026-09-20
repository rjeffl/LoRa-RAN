# Bridge Node — session handoff

**Written 2026-09-20 at about 19:00 UTC, by the session that read day 1 of the D1 brief's §5
run and built BF-32's library half.** That session committed both 24-hour captures, took the
reading to the operator, and **recorded the operator's acceptance of 917.4 MHz**. The capture
exercise is over: no board is holding a serial port, nothing is armed, and no measurement is
pending. This file replaces the previous one wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**D1 is closed and the next work is B4, which needs no board and no measurement.** *Git
state* below has the commands that show where every branch stands.

**First actions, in order:**

1. **Reflash the three bench boards from their own projects.** All three still run
   `chan-capture` from the capture exercise. `firmware/bridge -e heltec` for the bridge
   board, `firmware/simnode` for the other two. *Hardware state* below has the per-board
   detail, and tells the two Heltecs apart by enclosure.
2. **Continue BF-32's bridge half**, on `b4-lran-config`. The library half is in. What
   remains: NVS persistence behind the `Persist` interface, the `config/set` subscriber, the
   split between bridge-held and node-held parameters, **reassembly of a split readback**
   (accept more than one `CONFIG_ACK` per `seq`, publish `config/state` only on completion,
   `config_readback_timeout_ms`), and `config/ack` and `config/state` publication.
3. **Then the interleaved sweep, then BF-24**, in that order and for the reason below.

### D1 is closed, and what that settles

**The operator accepted 917.4 MHz on 2026-09-20**, declining the frequency-change brief's
proposed move to 917.2 MHz. **Decision Register §3.4.1 is the record.** Read it before
reopening anything about the channel.

**Nothing moves.** Brief §6's list is not executed: no constant, no boot banner, no test
assertion, no specification revision, no board reflashed for frequency and **no citation
sweep**. `ver` stays 2, and `kPhy.freq_hz` and Library Plan §4's `freq_hz` default both stand
at 917.4 MHz.

**Day 1's numbers**, from the bridge engineering log's 2026-09-20 entry and
[`LRAN-D1-Parallel-Capture-Analysis`](../shared/LRAN-D1-Parallel-Capture-Analysis.md):
917.2 MHz carried 0.3050 % occupancy against 917.4 MHz's 0.2095 %, **more in 24 of 24
hours**, with 713 buckets in the −90 to −80 dBm band against 68. Hour-by-hour occupancy
correlated at **r = 0.978**, which is how the excess was attributed to the channel rather
than to a schedule on the property.

**D33 is not reopened, and its standing condition 3 changed.** It read "no co-channel
occupant on the chosen frequency", which is false for 917.4 MHz — the Davis transmits there.
§3.4.1 restates it around **characterising** occupants and records the Davis as accepted:
6.7 ms every 130.6882 s, a 0.005 % duty, about 0.85 % overlap with a maximum-length SF9
frame. **No collision mitigation beyond the existing retry is indicated.** GateLink sits
nearer the Davis transmitter and nothing has been measured there.

**Day 2 at 917.6 MHz is not needed.** It was to run only if day 1 ruled out 917.2 MHz, and
day 1 ruled 917.2 MHz out instead. Its own open finding — 18 bursts at −45 to −47 dBm in
the 2026-09-19 capture — stays open and needs no capture to sit there.

**Two sources are unidentified and neither reopens anything**: the aperiodic −89 dBm source
on 917.2 MHz, a frequency LRAN does not use, and a single wideband event at
2026-09-20T13:25:22Z that read −42 dBm at 917.4 MHz and −39 dBm at 917.2 MHz in the same
second. **M26** is the open row that still matters, for §5.4's unconfirmed attribution of the
915.8–916.4 MHz cluster.

**`rssi_report.py`'s periodicity verdict is not to be trusted on a long capture**, and this
is the one trap the exercise leaves behind. It called the Davis "not periodic" on the very
day that confirmed its clock to half a second. Left unfixed by operator direction, since no
capture is planned. `firmware/chan-capture/CLAUDE.md` and the `periodicity()` docstring carry
the mechanism; fix it before any future capture, because that verdict is what the documents
cite when they attribute an occupant.

**The captures are committed and the arming script is gone.** The two stopped bench files
without `office` in their names were never committed and stay in `.git/info/exclude`.

**`firmware/chan-capture/` changed twice on the bench.** The engineering log's 2026-09-19
entries have the detail:

- **It restarts receive every 100 ms.** Without that, each bridge poll 0.2 MHz away left the
  simnode Heltec reading a flat −74 dBm until a restart. The IRQ status read 0x0000 throughout,
  so no flag shows the state. **Never remove the restart.**
- **It switches the OLED off at boot**, because the XIAO's panel kept simnode's last frame.
- **The entry blaming the XIAO for a carrier is superseded** by the one after it. There was no
  carrier.

**Every bench board runs `chan-capture`.** Nothing on the bench transmits. **Reflash each board
from its own project before any bridge or simnode work**, the interleaved sweep included:
`firmware/bridge -e heltec` for the bridge board, and `firmware/simnode` for the other two. The
XIAO is unplugged.

**One question from the bench has no owner.** Can the bridge's own receiver stick between polls
after a strong burst on another channel? At the bench it would cost nothing. At the gate, where
frames arrive near −100 dBm, a receiver stuck 40 dB high would miss them until its next
transmission. The 2026-09-19 log entry names the test.

**Reflash the boards, then run the interleaved sweep, then BF-24.** None of it waits on D1, which is closed. The ten frame-log bursts of
2026-09-17 put a 250 ms gap at **2.50 %** and a 2000 ms gap at **1.90 %**, where the same arms
had read 5.6 % and 0 % that morning. Every loss fell in four of the ten runs whatever the
spacing, so the losses look clustered in **time**. **Every sweep on record ran one spacing to
completion before starting the next**, so a slow change in the environment is indistinguishable
from an effect of spacing. Alternating the two arms inside one session separates them, at 45 s
a burst. **Both simnodes are powered down from the M25 capture**; power up the one the sweep
floods from and rebuild its identities first:

```bash
~/.platformio/penv/bin/python tools/simctl/rxlog.py --seconds 45 --json burst.json
~/.platformio/penv/bin/python tools/simctl/rxlog.py --read docs/bridge/data/bf27-framelog-session-2026-09-17.json
```

**The sweep goes before BF-24 because BF-24 decodes a fragmented `STATUS`**, the traffic pattern
that loses frames on this firmware, and decoding into it first means debugging two problems at
once. M25 found nothing on the channel loud enough to cause losses at one metre, in the hours it
covered, which leaves the sweep as the next measurement for them.

**B4 is the milestone, and BF-23 does not wait on either measurement.** B4 is MQTT,
discovery and publication policy. Every B4 criterion but one is reachable with **no node
hardware** (**V-B11**): BF-23, BF-24, BF-25, BF-32, with BF-26 behind BF-32.

**BF-23's discovery half was built on 2026-09-17**, as `json_writer.h`, discovery
generation, 26 generated `/ha/` example payloads and `tools/checks/ha_examples.py`. Ask git
whether it has reached `main`:

```bash
git fetch origin -p
git log --oneline -1 origin/main -- tools/checks/ha_examples.py   # empty: not on main yet
```

**Runtime configuration from Home Assistant is decided: D43–D56, accepted 2026-09-19.** The
operator chose the general `config/set` route with `/lib/lran-config/` behind it, accepted every
recommendation of `LRAN-Config-Set-Brief` — which is committed on `spec-v0.13`, not here, so
it is named rather than linked — and of the v0.13
read-through, and then decided two more: **D55**, `len` is a byte count and a multiple of the
`ptype`'s width, so an entry can carry an array and a string is `u8` bytes; and **D56**, the PHY
parameters become runtime-configurable under new spec §12.4's commit-and-revert. Decision
Register §3.6 is the record. The work sits on three branches that do not touch this file:

- **`spec-v0.13`** carries the Protocol Spec v0.13 draft: §16.7's `config/*` payloads, §12.4's
  PHY scheme, §7.4's `len` rule, the register entries, Library Plan §4's parameter table,
  **BF-32** and **BF-33** in the tasks, and corrections to five passages v0.12 left stale.
  **The spec header stays at v0.12** until the citation sweep, which the operator wants run
  once, at the end of this pass.
- **`b4-lran-config`**, stacked on it, carries the code that conforms: the `NodeConfigV1`
  rename, the simnode's D52/D53 behaviour, and the codec skipping an over-wide value rather
  than dropping the frame. **BF-32's library half is in** — `/lib/lran-config/`'s table and
  store, host-tested in `native`, plus `MORE_FOLLOWS` on `CONFIG_ACK` (**D57**, spec §7.4.1).
  The bridge half is step 2 above.
- **`docs/prose-review-policy`** changes root `CLAUDE.md`: a whole-document prose review now
  happens **when the operator asks**, not because a document was opened. New prose still meets
  the skill, and stale facts are still raised whenever seen. It is small, independent and ready
  to merge.

**BF-32 is unblocked.** The operator accepted Library Plan §4's names and ranges on 2026-09-19.
Start with the library half, host-tested in `native`, then the bridge's table, NVS persistence,
the `config/set` subscriber, the bridge and node halves of a set, and the `config/ack` and
`config/state` publication.

**Three things are open for the operator, and none blocks BF-32:**

1. **The PHY rows' ranges**, which the session chose rather than measured: `spreading_factor`
   7–12, `bandwidth_khz` 125–500, `tx_power_dbm` −9 to −4, `phy_trial_s` 30–900 s. The defaults
   are D1's. `tx_power_dbm`'s maximum **is** D33's ceiling, and `bandwidth_khz` stays at 125
   until an envelope decision. All six PHY rows are `READ_ONLY` until **BF-33** builds §12.4.
2. **Whether BF-33 belongs in B4** or in a milestone of its own. It consumes BF-32's table, but
   it is radio work with its own bench cost.
3. **When to run the citation sweep.** The operator's rule: once, when the spec edits for this
   pass are done. `spec_citation_version.py` passes at v0.12 meanwhile.

**Also still owed**: the whole-document style passes. The fact reviews are done, and they
corrected stale facts in five documents. The spec's style pass goes on its own branch, as
agreed on 2026-09-16 for spec revisions; the Bridge and GateLink Implementation Plans' are
owed on the same terms. **W15** (`CONFIG_ACK` has no default-or-override flag)
and **W16** (nothing says when a node sends `CONFIG_CHANGE`) are open, both GateLink's.

**Once BF-32 lands, BF-23's lever half and BF-26 reduce to reading its table.** Every
`TODO(BF-23)` timing constant becomes one row: `g_diag_interval_s`, `poll_interval_s`,
`poll_reply_timeout_ms`, `missed_poll_threshold`, `error_min_interval_ms` and the media-access
config.

**The catalogue runs in one command**, and every row it drives is green:

```bash
export LRAN_MQTT_HOST=$(sed -n 's/^#define MQTT_HOST[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_USER=$(sed -n 's/^#define MQTT_USER[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_PASSWORD=$(sed -n 's/^#define MQTT_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
~/.platformio/penv/bin/python tools/simctl/simctl.py --port /dev/cu.usbmodem2101
```

**Command substitution, not `echo`**, so the values land in the environment and nothing
prints. *Bench credentials* below says why this is safe today and will not be.

**Set the bench up like this.** One simnode board is enough; a second is needed only for
`PING` between nodes. `simctl` enrols its own identity. For a hand-run session, use
`push f1` for the GateLink role, or `fault <id> hdr_rsv` for any other. **A fault's `dst`
defaults to the bridge.** **Drive a command from the broker**: publish to
`lran/<node>/cmd/<action>/set`, and read `lran/<node>/cmd/ack`.

## What 2026-09-19 established

The engineering log's four 2026-09-19 capture entries have the numbers.

- **The office's external monitor raises every receiver's floor, at every frequency.** It cost
  the simnode Heltec 3.7 dB at 0.3 m and the bridge board 1.2 dB at 1 m. The bridge board is at
  its production position, so **that 1.2 dB is what the monitor costs the bridge's receiver**
  whenever both are on. No other spacing has been measured.
- **With the monitor off, the two Heltecs agree on floor and occupancy at the office**: 0.1 dB
  and 5.4 % apart. Their Davis peaks sit 8 dB apart.
- **Runtime configuration from HA has a design**: D43–D54, spec v0.13 §16.7, BF-32.

- **917.6 MHz has no Davis, and a source no other capture has shown.** No band was periodic, so
  the receiver rejects the Davis by at least 33 dB at 0.166 MHz offset. Occupancy read
  0.1467 % against M25's 0.0912 %, over the same UTC hours a day later, almost all of it in the
  −110 to −101 dBm band and present before any other board was powered. 18 bursts at −45 to
  −47 dBm, 10 to 60 ms each, ran from 04:33 to 12:16 UTC. The first came 9 minutes after the
  simnode Heltec went on the bench, and neither M25 nor the 917.2 MHz capture, both run with no
  other board powered, shows anything above −71 dBm. **Whether the board was their source is
  not established.**
- **The calibration hour passes on floor and occupancy**: −115.2 against −114.9 dBm, and
  0.1228 % against 0.1186 %. The Davis medians, −72 and −73 dBm, sit at the 1 dB limit, but the
  bridge board's reading stepped up about 8 dB at 14:09 UTC while its reading of the episodic
  source fell. **Gain toward a source depends on direction and placement**, so no single offset
  corrects one receiver to the other. What changed at 14:09 is not recorded.
- **Z-Wave stays below −110 dBm at 917.4 MHz**, on the evidence of the ZEN17's reports every
  30 s, which leave no 30 s grid in any capture. That holds if the ZEN17, a 700-series device
  reporting to an Aeotec Gen5 (500-series) stick, runs its link at 100 kbps on 916.00 MHz,
  which is inferred and not confirmed.
- **The Trane TCONT624 thermostat is not a test of 916.00 MHz.** A non-Plus device from about
  2014 transmits on 908.4 MHz. Its two runs of three requests matched nothing above −110 dBm.
- **The −93 dBm episodic source appears at all three frequencies** at similar levels, so it does
  not separate them.
- **The Davis overlaps a maximum-length SF9 frame 0.85 % of the time**, and the calibration
  hour's non-Davis events at −100 dBm or stronger bound overlap at about 0.8 %. Both are bench
  figures.

## What 2026-09-18 established

The engineering log's 2026-09-18 entries have the numbers, the 917.2 MHz capture's included.
[`LRAN-Site-RF-Inventory`](../shared/LRAN-Site-RF-Inventory.md) has the research, with sources.

- **917.4 MHz is not empty.** Over M25's ten hours it was occupied 0.0912 % of the time above
  −110 dBm, by a source near −75 dBm every 130.69 s and a source near −93 dBm arriving in
  episodes. Part of the weakest band, −110 to −101 dBm, is associated with the bridge's own
  transmissions, by a mechanism not established.
- **None of it explains the bench losses.** Nothing came within 34 dB of the −37 dBm wanted
  signal, and the simnode's media access never drops a frame on a busy channel. The capture did
  not cover the hours of the 2026-09-17 losses, so the channel is a weaker candidate, not a
  closed one.
- **The 130.69 s source is the Davis Vantage Pro2**, identified by prediction: its hop cycle
  predicted 130.6875 s at transmitter ID 1, and the operator then read ID 1. One of its 51 hops
  is 917.434 MHz, inside LRAN's receive bandwidth, for 6.7 ms a visit. Its transmitter sits at
  `weather-island`, on the path between the bridge and the gate. No Davis packet has been
  decoded.
- **The Davis hops 0.5 MHz either side of 917.4 MHz left no trace**, so the receiver rejects a
  Davis burst at that offset by at least 35 dB. Rejection at 0.25 MHz, where 917.2 MHz's
  neighbours sit, has not been measured.
- **The property's other radios are researched, not measured.** Z-Wave is at 916.00 and
  908.4 MHz, Insteon at 915.0 MHz (dual-band means powerline plus RF), and YoLink at 923.3 MHz.
  The Dakota Alert DAPT-4000 driveway sensor at the gate is 433.92 MHz, outside the band.
- **M20's −54 dBm at 916.0 MHz in the house sits on Z-Wave's channel, not YoLink's**, against
  Decision Register §5.4's candidate attribution.
- **One published Davis unit runs 33 kHz below nominal.** If this one does too, its
  "917.434 MHz" hop sits almost exactly on 917.4 MHz.
- **At 917.2 MHz no band is periodic over ten hours**, about 275 Davis hop cycles, so the Davis
  leaves no trace 0.2 MHz from its nearest hop. The −80 dBm band holds one bucket, against 201 at
  917.4 MHz.
- **Occupancy at 917.2 MHz read 0.3260 %, against M25's 0.0912 %, over hours that do not
  overlap.** Hours 18 to 22 UTC read 0.365 % to 0.668 %. The other hours read 0.064 % to 0.197 %,
  inside M25's hourly range of 0.048 % to 0.228 %.
- **A source at −90 and −89 dBm ran at 917.2 MHz from 17:57 to 23:49 UTC** and nowhere else in
  the capture: 328 buckets, 166 events, no period. Nothing like it appears in M25's capture, which
  never covered those hours. It is unidentified.
- **Episodes at −94 to −96 dBm clustered between 18:01 and 18:26 UTC**, matching M25's −93 dBm
  episodic source by level and shape.
- **The weakest band's association with the bridge's own transmissions is stronger at
  917.2 MHz**: 21.0 % of its buckets fell near one, against 14.2 % at 917.4 MHz and 8.3 % by
  chance. The mechanism is still not established.
- **The unattended handover worked, and so did `--reset-on-open` on its first hardware run.**
  `rssi_capture.py` gained `--reset-on-open`, with host tests in CI.

## What the 2026-09-17 sessions established

The engineering log's eight 2026-09-17 entries have every number below. **Read the last
entry first.** It supersedes the earlier ones on the receive path's mechanism, and on
whether spacing is the variable at all, and leaves their measurements standing.

- **B3b is accepted.** Its tasks were confirmed on air on 2026-09-16, and the milestone
  closed on 2026-09-17 once its last criterion had a home.
- **V-B12 moved to B4 rather than gaining a `BF-*` number**, with the operator. Its
  saturated arm cannot run on this firmware, and both things it needs, BF-23's lever and
  BF-26's bench diagnostics, are already B4 tasks. Impl Plan §8.1 records the move.
- **M22's idle arm measured five spacings that morning**: 5.6 % PER at 250 ms, 5.0 % at
  400, 1.0 % at 700, and 0 % at 1100 and at 2000. The endpoints reproduced on an
  instrumented build the same day.
- **That curve did not reproduce the same afternoon**, as *The next job* describes.
  **Both readings stand as taken.** What is in doubt is whether spacing is the variable,
  because no sweep on record interleaved its arms.
- **Three mechanisms inside the bridge are ruled out.** `rx_no_interrupt` read zero across
  190 frames spanning both arms, which rules out `kIrqReadMs`. `rx_deaf_ms` held at
  **639–659 ms per 60 s window across five bursts** whose PER ran 6 %, 2 %, 2 %, 0 %, 0 %,
  which rules out the transmit path in aggregate. Across eight `--gap 250` bursts, bridge
  transmissions against frames lost ran 3/5, 0/4, 7/1, 3/1, 6/3, 4/2, 3/1, 3/1, so the
  bridge's media access does not predict the losses either.
- **BF-27's frame log ruled out the transmit path per frame.** Of nine frames lost across
  ten bursts, the bridge's own deafness can account for **at most 0.52**. That figure is a
  ceiling, because `rx_deaf_ms` is a total and never says where in a gap it fell. Four of
  the gaps contain a bridge transmission and still cannot account for their losses.
- **376 receptions were logged one by one, and every one was clean.** Each was announced by
  its own interrupt, none arrived corrupt, and the ladder discarded none. RSSI ran −39 to
  −36 dBm throughout, and the RX queue never went deeper than 1. **Every loss is a frame
  the radio never delivered**, which at one metre is not an RF story. **The cause is not
  known.**
- **`cad_free` closed a blind spot in every earlier run.** The control shows `cad_backoffs`
  0 against `cad_free` 3 per burst. On the old instrument, that window read as a bridge that
  never contended for the channel, while the radio left receive three times for 643 ms.
- **Two of the three radio families on the property cannot move `cad_backoffs` at any
  signal level.** A LoRa CAD detects a LoRa preamble at the configured SF, so Z-Wave and
  Insteon FSK are invisible to it. Decision Register §3.4 names CAD as the instrument
  keeping the channel under observation for **D33 standing condition 3**. **M25** builds one
  that can see them, and **M26** completes the §3.1 inventory it would be read against.
  **D33's status is unchanged**, and neither item reopens it.
- **The instruments are built and tested on the host.** `tools/simctl/per_measure.py` gives
  a PER figure and `rxlog.py` says *which* frames were lost, each with its arithmetic
  separated from its I/O. `rx_wake.h`, `rx_deaf.h`, `frame_log.h` and `chan_monitor.h` are
  the bridge-side half.

**Do not re-derive the closed candidates.** The engineering log's 2026-09-17 entries have
all of it, with the numbers.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-19 entries **last one first**, then 2026-09-18, then the 2026-09-17 entries **last one first**. Entries from 2026-09-10 to 2026-09-16 are in [`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md); read them only when a document cites one |
| 3 | [`traps.md`](./traps.md) | the section for the work you are about to do. Bench work needs *Measuring frame loss* and *Bench boards and serial ports* at least |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§8** owns the milestones; **§8.1** is V-B12's move; **§6.6.1** BF-27's frame log; **§10.5** the fault catalogue; **§7.2.1** BF-21's `simctl`; **§6.2.1** BF-18; **§6.1.1–§6.1.2** BF-17 and BF-20; **§4.3.2** BF-19; **§10** the simnode |
| 5 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §7 is B4, where the work goes next; §6's B3b tasks are all built |
| 6 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 7 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has, what it does not, and its traps |
| 8 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9–§12, §14, §16 | keys, context, reassembly, radio, the discard ladder, MQTT. **§18.2, never §18.1 alone** |
| 9 | [`LRAN-D1-Frequency-Change-Brief`](../shared/LRAN-D1-Frequency-Change-Brief.md) | the proposed move to 917.2 MHz, the two captures that decide it (§5), and what moves with it (§6) |
| 10 | [`LRAN-Site-RF-Inventory`](../shared/LRAN-Site-RF-Inventory.md) | the property's Z-Wave, Insteon, YoLink, Davis and Dakota Alert radios, and what M20 saw at each frequency |
| 11 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) | **§3.1** the site's 900 MHz equipment; **§3.4** D33's standing conditions and the note against its own instrument; **§5.4** M20's channel evidence; **M25**, **M26** |
| 12 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

[`briefs/2026-09-17-session-brief.md`](./briefs/2026-09-17-session-brief.md) is optional. It
is a dated reading of the documents above and adds recommendations, not facts.

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**, **B3b**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **V-B3**, **V-B9**, **V-B10**, **W9**. **BF-2**–**BF-9**, **BF-15**–**BF-22**. BF-27's frame log |
| Not done | **M26** — researched from published sources on 2026-09-18; the Z-Wave hardware is now named (Aeotec Gen5 stick, ZEN17, Trane TCONT624) but no link's data rate is confirmed, and Insteon model numbers are not. It no longer gates D1 or D33; what still needs it is §5.4's attribution of the 915.8–916.4 MHz cluster. **The 917.6 MHz −46 dBm source** — unidentified, and no capture is planned. **The 2026-09-20T13:25:22Z wideband event** — unidentified. **`periodicity()`'s verdict on a long capture** — a known defect, left unfixed by operator direction. **B4**: BF-23's lever half, BF-24, BF-25, BF-26 and **BF-32**, which the other two wait on. **Spec v0.13**: drafted through D56, citation sweep and style passes owed. **BF-33** is new and unstarted. **BF-27's other three tools**. **V-B12**, a B4 criterion with its idle arm measured. **M22** open. **BF-11a**, **BF-11b** |
| Queue | Reflash the three bench boards from their own projects, then BF-32's bridge half, then the interleaved sweep, then BF-24. BF-23's discovery half waits on none of it. **No measurement is queued**: D1 is closed at 917.4 MHz, M25 is done and day 2 is not needed |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d lib/lran-link -e native             # spec 12.3 media access, both firmwares
pio test -d lib/lran-sim -e native              # BF-7's FramePatch, against the W4 negatives
pio test -d firmware/bridge -e native           # bridge host suites
pio test -d firmware/simnode -e native          # simnode host suites
pio run  -d firmware/bridge -e heltec           # bridge target - NEEDS secrets.h
pio run  -d firmware/simnode -e simnode-heltec  # simnode target - NEEDS secrets.h (key only)
pio run  -d firmware/simnode -e simnode-xiao-wio
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing
python3 tools/checks/no_mbedtls_hkdf.py         # HKDF built from HMAC, spec 9.1
python3 tools/checks/bridge_partitions.py       # A/B table; add --firmware/--elf after a build
python3 tools/checks/spec_citation_version.py   # binding citations vs. the spec header
python3 tools/simctl/test_simctl.py             # simctl's verdict logic, no board
python3 tools/simctl/test_per_measure.py        # M22 PER arithmetic and its guards, no board
python3 tools/simctl/test_rxlog_analyze.py      # BF-27 frame-log arithmetic and its guards
python3 tools/simctl/test_rssi_analyze.py       # M25 channel-capture arithmetic and its guards
python3 tools/simctl/test_rssi_capture.py       # M25 capture tool's --reset-on-open, no board
python3 tools/checks/simctl_catalogue.py        # simctl's rows vs. fault.cpp
python3 tools/vectors/check.py                  # W4 vectors, self-check
```

**CI runs all of these on every pull request**, and builds both V-B9 bad images. Run them
locally when you are about to spend bench time on the result. **Call `pio` and the bench
tools' Python by path from a script**; [`traps.md`](./traps.md#bench-boards-and-serial-ports)
says why.

## Git state — ask git, do not read it here

> **Where `main` points, what merged last, which branches exist and whether a PR is open
> are deliberately not written in this file.** A written SHA is wrong the moment the branch
> carrying it merges, and it is wrong in the worst direction — confidently, in a file whose
> whole value is being trustable cold.

```bash
git fetch origin -p                             # prune deleted remote branches first
git log --oneline -1 origin/main                # where main actually is
gh pr list --state open                         # what is open, if anything
git log --branches --not --remotes --oneline    # local-only work; empty is good
git branch -vv | grep ': gone]'                 # local branches whose remote was deleted
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository.

**Permanent history is citable; moving state is not.** `4250e00` (six document defects),
`ebdcf0d` (the `LoRaBridge` retirement and the D33 bench-power reconciliation), `8253085`
(**P8**, with D34's amendment and spec v0.11), `28ffd82` (the poll-to-answer instrument),
`65508bf` (the 2026-09-16 catalogue and W9 record) and `76e6d11` (M25's capture).

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it. Merge each PR without it, retarget the next to
`main` while it is still open, then delete branches by hand. **This worked as written on
2026-09-16** for #59 → #60 → #61.

**A push touching `.github/workflows/` needs workflow token scope.** It was refused once, on
2026-09-08, and accepted since. Try the push; if it is refused, the operator refreshes auth.

## Bench credentials — the broker is a sandbox, and that changes what is safe

**The broker at the address in `secrets.h` is a disposable sandbox Home Assistant install
with its own Mosquitto.** Its credentials are **not** the production ones, so they may be
put into the environment directly rather than prompted for. **That changes at the production
cutover**, after which a password must not reach argv, a log or a committed file.

**`simctl` and `per_measure` read `LRAN_MQTT_HOST`, `LRAN_MQTT_USER` and
`LRAN_MQTT_PASSWORD` from the environment and never take them as arguments.** That is right
either way: an argument reaches argv, and argv reaches the process table and the shell
history. Source them out of `secrets.h` without echoing them; `secrets.h` is gitignored and
stays where it is.

**The OTA password is read from `LRAN_OTA_PASSWORD`** in the shell that runs the upload.

## Hardware state

**This table names the devices in this subproject's terms.** The range-test handoff owns
them in its own roles, and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | **`chan-capture` / `heltec` at `35c8471` since 2026-09-19T13:43:41Z**, flashed by the arming script. It runs no bridge firmware at all: no WiFi, no MQTT, no polls. MAC `44:1b:f6:f9:70:14`. **Reflash `firmware/bridge -e heltec` from the branch being worked on before any bridge work.** **The capture file's `CHAN-BOOT` line names the running image**; check it rather than trusting this row | NVS: `chan-capture`'s frequency, **917.4 MHz** | **At its target location in the office, NW wall, desk height, since 2026-09-19 about 15:35 UTC.** On USB as `/dev/cu.usbserial-0001`. **The port is free — day 1 ended at 2026-09-20T17:56:58Z and nothing is capturing.** At the bench, **its Davis reading moved about 8 dB at 14:09 UTC on 2026-09-19** with nothing recorded as moved, so a peak level from this board measures direction as much as the receiver. **Reflash it for bridge work**; its position no longer needs preserving |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | **`chan-capture` / `heltec` since 2026-09-19, not simnode firmware.** Last flashed from `6bf9a38`, the build with the 100 ms receive restart. MAC `44:1b:f6:fa:bc:2c`. **Reflash `firmware/simnode -e simnode-heltec` before any simnode work**; it was BEHIND on `ctx_reject` and `set_displaced` (BF-21) before this anyway | NVS: `chan-capture`'s frequency, left at **917.2 MHz** by day 1. A reflash to simnode firmware makes it irrelevant | **In the office, about 1.5 m from the bridge board, since 2026-09-19 about 15:35 UTC.** On USB as `/dev/cu.usbserial-3`. **The port is free — day 1 ended at 2026-09-20T17:56:58Z.** At the bench **its floor read −114.9 dBm**, within 0.3 dB of the bridge board's. Its port name moves across replug |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | **`chan-capture` / `xiao-wio` since 2026-09-19, not simnode firmware.** Last flashed from `01f4332`, which has the 100 ms receive restart. MAC `68:ee:8f:4b:85:f4`. Native USB, so it enumerates as `/dev/cu.usbmodem*`. **Reflash `firmware/simnode -e simnode-xiao-wio` before any simnode work** | NVS: `chan-capture`'s frequency, **917.4 MHz**; the B1b position log, dumped and committed | On USB as `/dev/cu.usbmodem2101`, sampling 917.4 MHz and never transmitting. **Its floor reads −110 dBm, 4 dB above the Heltec's**, so every sample counts as occupied against `chan-capture`'s fixed −110 dBm threshold. **Its OLED is switched off by the image**, so a dark panel no longer means it is unpowered |

**A USB flash puts the bridge board back in a known state.** Flash from a committed tree: a
`-dirty` git field on the banner means the running image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure**: both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug. The XIAO is unambiguous, because
it is the only `usbmodem` port.

**To tell the two Heltec ports apart without opening an enclosure, read the boot banner.**
Opening the port reboots the board, which prints it; the bridge's first line is `LRAN Bridge
Node - node 0x00`. Open with `dtr` and `rts` low before `open()`, as `simctl`'s `Console`
does, or the open presses PRG. **Confirmed on 2026-09-17**, when the bridge answered on
`/dev/cu.usbserial-0001` and the simnode Heltec on `/dev/cu.usbserial-3`.

**No stored state on any of these boards is the only copy.** Every survey site and every B1b
position is committed under `docs/rangetest/data/`, and every M22 run and the M25 capture
under `docs/bridge/data/`.

**Its antenna stays on the bridge board.** The bridge uses the range test's 3.0 dBi 19 cm
stick (Bridge PRD **R-4.3a.1**). The gain is a term in D1's EIRP arithmetic, and
`radio_config.h` asserts the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

**An older artifact is correct for when it was made.** Changes before 2026-09-16 are in
[`traps.md`](./traps.md#behaviour-that-changed-before-2026-09-16).

- **The bridge board runs `firmware/chan-capture/` since 13:43:41 UTC on 2026-09-19**, when
  the arming script flashed it, and no bridge firmware at all. Anything it records until it is
  reflashed from `firmware/bridge/` is listen-only channel data. Before that:
- **The bridge board runs capture-only images since 2026-09-18 15:14 UTC**: 917.2 MHz
  (`5e752e9`) until 01:14 UTC on 2026-09-19, then 917.6 MHz (`91e63dd`). Anything it records
  until it is reflashed from the branch being worked on is data from one of those frequencies. `CHAN-BOOT` and the banner's `PHY:` line say which; a
  record without either cannot be placed.
- **`rssi_capture.py` can reset the board on open since 2026-09-18** (`--reset-on-open`) and
  then writes a `#RESET` line. A capture file without one was started on a running board.
- **V-B12 is a B4 criterion since 2026-09-17**, not a B3b one. Text saying B3b is blocked on
  it, or that no task owns it, is correct for before that.
- **`lran/bridge/diag/radio/state` carries `rx_no_interrupt` and `rx_wake_empty` since
  2026-09-17** (`rx_wake.h`), and **`cad_free` and `rx_deaf_ms` since later the same day**
  (`rx_deaf.h`). Text saying no counter sits between `RX_DONE` and the ladder is correct for
  before the first pair. Text treating `cad_backoffs` as the bridge's media-access
  instrument is correct for before the second. **All four are bridge-local and deliberately
  not spec §14.1 counters**, so they are absent from `lran/bridge/diag/state` and from
  schema `0xF0`.
- **`enter_mode()` is the one place `lora_link.cpp` changes `g_mode` since 2026-09-17.** A
  transition that assigns it directly escapes the deaf-time accounting.
- **`lran/bridge/diag/rxlog/state` exists since 2026-09-17** (BF-27), carrying one record
  per frame in and out. **It is the one bridge topic that is not retained on a `/state`
  leaf**, against spec §16.2's table, and the deviation is raised rather than settled. Impl
  Plan §6.6.1 says why. Text saying the bridge publishes only aggregate counters is correct
  for before it.
- **The bridge writes `CHAN` and `CHANSUM` lines to serial since 2026-09-17** (M25,
  `chan_monitor.h`). A tool reading that port for anything else has to tolerate them.
  **They are not on MQTT and are not meant to be**: a bucket a second for ten hours is a
  capture file, not a diagnostic topic.
- **`log_task` drains the frame-log ring and is no longer idle since 2026-09-17.** It was a
  `vTaskDelay` loop with a `TODO(BF-11a)` in it from BF-11 until then. **The leveled log
  still has no queue**; only the frame log uses the task.
- **The bridge accepts protocol version N *and* N−1 since BF-22**, and addresses each node
  in the version that node announced. Text saying it accepts only N, or that `bad_ver`
  moves `rx_bad_ver` twice, is correct for before it. `lran/<node>/diag/state` gained
  `unsupported_ver`.
- **The bridge subscribes to `lran/+/cmd/+/set` and publishes `lran/<node>/cmd/ack` and
  `lran/bridge/diag/cmd/state` since BF-18.** Text saying the bridge only publishes, or
  that `subscribe()` has no caller, is correct for before it.
- **`set_displaced` moves one counter since BF-21**, not two. A capture from before it also
  shows `rx_reassembly_timeout`.

## Traps that cost real time here

[`traps.md`](./traps.md) has the full set, grouped by the work they bite. These are the ones
the next job meets first:

- **Do not treat "space frames above 1 s" as a safe rule**, and give every frame-counting
  measurement a control arm in the same session.
  [Measuring frame loss](./traps.md#measuring-frame-loss)
- **A capture with a simnode powered up is not a channel measurement**, and a `CHAN` line is
  not the denominator. [Measuring the channel](./traps.md#measuring-the-channel-m25)
- **Opening either board's serial port reboots it**, which resets its identities and its
  `ctx_id`. [Bench boards](./traps.md#bench-boards-and-serial-ports)
- **Check the bridge's frequency before any bench run.** Its boot banner's `PHY:` line and
  `CHAN-BOOT` both state it. A bridge on a capture image and a simnode on `main` share no
  channel, and the symptom is a bridge that hears nothing.
- **A reboot of the bridge board zeroes every counter.**
  [Measuring frame loss](./traps.md#measuring-frame-loss)
- **A command takes 4-9 s from the MQTT publish to the node**, not ~1 s.
  [Commands](./traps.md#commands-the-broker-and-mqtt)
- **HA's entity registry remembers every `unique_id`**, and a retained discovery config
  survives a reflash. Develop discovery against the dev HA VM and dev broker until **B6**.
  [Commands](./traps.md#commands-the-broker-and-mqtt)

## Open, and not closable from here

- **The bridge loses frames at one metre, and the cause is not known.** The open question
  is whether spacing is the variable at all. M25's capture found nothing loud enough to
  matter at one metre in the ten hours it covered, which were not the hours of the losses.
  The interleaved sweep in *The next job* is the measurement that can answer it. **The margin inverts between
  here and the gate.** At one metre the wanted signal is −37 dBm and the loudest neighbour
  −54 dBm. At the gate the wanted signal is about −100 dBm, the neighbours −80 to
  −89 dBm, and M25's periodic source about −75 dBm. **A loss rate measured at one metre is not evidence about 87 m**, and it is
  optimistic in the wrong direction. **BF-24's decode work meets the same traffic pattern
  first.**
- **Nothing a node sends the bridge carries a MAC the bridge verifies.** §9.2 makes every
  authenticated type bridge → node, so the bridge's own `rx_rejected_seq` and
  `rx_dup_command` stay at zero by construction. BF-18 proved the *sending* half.
- **The `radio_ok` half of the OTA verdict is untested on hardware.** V-B9's images fail by
  network or by panic; no image with a dead radio exists.
- **`poll_reply_timeout_ms` = 10 000 has bench evidence** (522–1686 ms at 1 m, no contention)
  but no measurement under contention or at range.

#### Spec v0.12 — landed 2026-09-16, and what it left open

**All nine questions are answered.** Eight became **D35–D42** in the Decision Register;
the ninth was a fact, verified against the datasheet as **M24**. `LRAN-Spec-v0.12-Brief`
holds the reasoning and is superseded. **`ver` stays `2` and no vector regenerates.**

- **W14** — §17.1's duty-cycling design assumed a silicon address filter that LoRa does
  not have, so its power model is unquantified. Owed before WellLink is built on that
  profile, moot if **D19** makes WellLink mains-powered.
- **W10** is now a counting question: `CONFIG` and `CONFIG_ACK` are single-frame, so a
  configuration larger than one frame is several messages with no atomicity across them.
  Count GateLink's real parameters against 24 entries before GateLink's block of the
  `/lib/lran-config/` table is written. Spec v0.13 adds that nothing says how a node splits a
  `GET_ALL` answer larger than 21 results.
- **§14.2's bound 1 is what v0.12 left untested on air** — no unregistered source has been
  produced on the bench.

#### Work no task owns

- **The receive path's losses** — unassigned, and **three hypotheses shorter**. All three
  instrument steps are **built**, all bridge-local rather than spec §14.1, and each came back
  clean. **A fourth candidate is not in this firmware at all**: the channel. **M25**
  measured it on 2026-09-18 and made it weaker without closing it. **M26** completes the
  equipment inventory; neither is a `BF-*` task.
  **A poll-free burst is not available** and does not need to be. The flooding node enrols
  itself on its first frame (`scheduler.cpp`), so it is polled regardless of what else is
  quiet, and the poll load is measured rather than avoided.
- **Spec §16.2's retention rule against a streaming diagnostic.** The frame log publishes
  unretained where §16.2's table says a `/state` leaf is retained, and Impl Plan §6.6.1
  raises the deviation. No spec amendment has been drafted.
- **The Implementation Plan cites PRD v0.6; the PRD is at v0.12.** This was found on
  2026-09-17 and deliberately not bumped. **Reconcile §§2.1, 2.2, 3.2, 7.1 and 8 against the
  PRD's v0.7–v0.12 changelog first, then correct the citation**; a bare number bump is the
  failure the citation rule exists to expose. **Nothing caught it** because
  `tools/checks/spec_citation_version.py` covers protocol-spec citations only, and no check
  reads a PRD-to-plan or plan-to-tasks citation. Extending it will likely surface other
  stale ones; report those rather than fixing everything on one branch.
- **No link has ever been measured at 917.4 MHz.** Every walk, every bench trace and B1b's
  gate run were captured at **915.0 MHz**, the range test's provisional frequency, which
  Decision Register §3.4 already flags as `weather-island`'s own peak. The operating channel
  has a survey behind it and **no link measurement at all**. Found on 2026-09-17 by reading
  every `freq_hz` column under `docs/rangetest/data/`.
- **R-3.2c is unmet and no task names it.** The PRD says *"WiFi RSSI SHALL be published as
  a diagnostic."* `wifi_rssi_dbm()` exists (`wifi_link.cpp`) and is read into the OLED
  status page and **nowhere else**: `task_runtime.cpp` puts it on the screen, and no MQTT
  topic carries it. Verified on 2026-09-17 by reading every caller. **It belongs in BF-24's
  table**, which is the task that owns what the bridge publishes. It is recorded here
  because BF-24's row does not mention it and nothing else would surface it.
- **A PING responder on the bridge** — spec §17.3 requires RF loopback of every node build.
- **Decoding per schema has no task** — Impl Plan §5.3's `decode/`; `app_task`'s `TODO`
  gives it to BF-24. Nothing a simnode sends is decoded or published today.
- **Six task stacks unmeasured** — every size but `lora` is still BF-11's figure.
- **The first-attempt `AUTH_FAIL` at every boot** — reproducible, unexplained, unassigned.
- **A serial log line per network state change** — proposed, not assigned to a task.
- **The OTA first-data timeout** — one failure in two uploads on 2026-09-13; it did not
  recur on 2026-09-15's three uploads.
- **R-5.3d on hardware** — an OTA upload deferred during a LoRa transaction.
- **BF-11a** (log queue drain) and **BF-11b** (hardware watchdog from `sched_task`).
- **What GateLink's `COMMAND_ACK` waits for** — pulse complete, or gate confirmed. GateLink
  **M3** needs the answer.
- **GateLink M0's LDO margin** — sized for Envelope B's 19.6 dBm, tested only at −4 dBm.
- **The range-test firmware still transmits on the provisional 915.0 MHz.**

### Closed, and not to be reopened by habit

- **B3b — accepted 2026-09-17.** Its tasks were on air the day before; the milestone closed
  once V-B12 had a home. Do not reopen whether V-B12 should have had a `BF-*` number.
- **`kIrqReadMs`, the bridge's media access and its transmit path — ruled out 2026-09-17**
  as the cause of the receive losses, the last per frame. Engineering log, 2026-09-17.
- **BF-22 — built and on air 2026-09-16.** V-B10 met. Do not widen the accepted
  version range past N−1 for convenience; §13.2 is why.
- **BF-21 — built and run 2026-09-16.** Both its decisions are made; do not reopen
  `set_displaced`'s second counter or ask again whether `ctx_reject` should exist.
- **BF-18 — built and on air 2026-09-16**, `ResyncFailed` included, via `ctx_reject`.
- **B3a — accepted 2026-09-16.** The catalogue, W9 and the clause-by-clause record are in the
  engineering log and in #61.
- **W9 — closed 2026-09-16** on the bench, board to board.
- **B0 — accepted 2026-09-15.** Bench record in the engineering log and in #60.
- **V-B9 — re-run and passed 2026-09-15**, with the `radio_ok` gap noted above.
- **V-B3 — passed 2026-09-15**, both directions.
- **B2 — accepted and merged, 2026-09-13.**
- **D1 and D33 — CLOSED 2026-09-10.** 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted,
  Envelope A. Reopening needs a new fact, not a re-reading of B1b.
- **M19 — DONE 2026-09-10.** Do not regenerate §15.1's table expecting different numbers.
- **M6 — CLOSED 2026-09-09.** Gate **~87 m**, well **~100 m**. No well walk is owed.
- **M20 — CLOSED 2026-09-05.** The tool ranks channels from the committed trace.
- **M21 — CLOSED 2026-09-06.** §15.23 home-built.
- **W4, W5, W12 — closed.** §18.1 is annotated, not rewritten, and must not be read alone.
- **D31 — closed 2026-09-08.** Copyright holder is Robert J. Lee.
- **D32 — closed.** RadioLib, pinned. Run `tools/rangetest/check_pa_table.py` after any
  version bump.
