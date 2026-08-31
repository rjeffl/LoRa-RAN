# `firmware/range-test/` — engineering log

Dated entries. Measurements, surprises, and things that cost an hour.

Binding specification: [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md)
v0.7 (`ver = 2`). Tasks: [`LRAN-Range-Test-Firmware-Pass1-Tasks`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md).

## What this firmware is answering

| Item | Where it is tracked |
|---|---|
| **D1** — SF / BW / CR / TX power | Decision Register §2.1. **Bounded, not free:** TX power is capped by D33 and the frequency waits on M20 |
| **M6** — range and RSSI at ~500 ft **on both bearings** | Decision Register §5.1 |
| **M20** — ambient RSSI sweep of 902–928 MHz at **both** the bridge and the far node | Decision Register §5.1. Blocks D1's frequency and D33's third standing condition |
| **W9** — full-size (222 B) and fragmented `PING` over the air | Protocol Spec §18, §6.6.1, §6.6.2 |
| **§14 stage 1** — PHY CRC failures | The one discard path that cannot be produced at a desk; observe it at the far edge of the walk |

## Things to record every time, or the number is not reusable

**Conducted TX power and antenna gain separately** — D33's ceiling is EIRP, and a single
combined figure cannot be audited later. Also: antenna height at both ends, bearing,
weather, foliage state, and the date. A dry-February path and a wet-July path are not the
same path.

---

<!-- Entries below, newest last. -->

## 2026-08-31 — R1–R3, `range/skeleton`: project stands up and builds

`firmware/range-test/` created as a standalone PlatformIO project. Target builds clean
under `-Wall -Wextra -Werror`; 17 host tests pass in `native`. **No hardware yet** — every
acceptance criterion that needs two boards is untested. See "what is not verified" below.

### The Heltec V3 pin map is now *confirmed*, not *derived*

R2 requires the pin numbers come from the board schematic or the vendor board definition,
not from memory or a forum post. Transcribed from

```
~/.platformio/packages/framework-arduinoespressif32/
    variants/heltec_wifi_lora_32_V3/pins_arduino.h
```

at framework version `3.20017.241212+sha.dcc1105b`, which is what `espressif32@6.13.0`
resolves:

| Function | GPIO | Vendor variant symbol |
|---|---|---|
| NSS | 8 | `SS` |
| SCK | 9 | `SCK` |
| MISO | 11 | `MISO` |
| MOSI | 10 | `MOSI` |
| RST | 12 | `RST_LoRa` |
| BUSY | 13 | `BUSY_LoRa` |
| DIO1 | 14 | **`DIO0`** — see below |

**These agree, value for value, with Bridge Impl Plan §10.8.1's `LRAN_PROFILE_HELTEC`
entry.** That section describes both its maps as "derived, not transcribed from a vendor
pin table", and the Heltec column specifically as "the community-standard V3 assignment".
The Heltec column can now be described as **confirmed against the vendor variant**; the
XIAO column still carries §10.8.1's own instruction to ring it out against the module on
arrival.

**A naming trap worth an hour of somebody's time.** The vendor variant calls GPIO 14
`DIO0`. That is the SX127x name; on the SX1262 the line is DIO1, and DIO1 is what RadioLib
wants as its IRQ pin. The *number* is right and the *label* is legacy. Anyone who trusts
the symbol over the number will go looking for a different GPIO and not find one.

### R1's "hold PRG at boot" cannot work on this board

R1 specifies role selection by holding the PRG button at boot. PRG on the V3 is GPIO 0 —
the ESP32-S3 BOOT strapping pin. **GPIO 0 held low through reset puts the chip into the
ROM serial downloader**, so the application never runs and never reads the button. As
written, the instruction selects "download mode", not "responder".

Implemented instead as a **3-second selection window immediately after the application
starts**: OLED countdown, press PRG for `RESPONDER`, no press for `INITIATOR`. Every
property R1 actually asked for survives — one binary, two roles, not persisted, role shown
on the OLED, no laptop needed at the walking end.

Arguably better in the field: the walking unit is chosen by a deliberate press with the
display confirming it, rather than by a hold whose effect is invisible until the radio
does or does not start. `INITIATOR` is the no-press default because it is the tethered
end, so a wrong default is visible on the laptop that is already there.

**The GPIO 0 assignment itself is not from the vendor variant** — `pins_arduino.h` stops
at the LoRa and OLED pins and declares no button. It is the ESP32-S3 BOOT strapping pin
and the button wired to it on this board. `TODO(R2)` in `src/role.h`: confirm against the
V3 schematic at first bring-up and record it here.

### The D33 clamp is integer arithmetic, and host-tested

Task guardrail 3 puts the power cap in code rather than in operator discipline. Three
decisions worth recording:

- **Tenths of a dB as `int16_t`, no floats.** The clamp is compared and printed; a value
  that displays as `-3.0` and compares unequal to `-3.0f` is a debugging session nobody
  needs.
- **Rounds toward −∞, not toward zero.** C++ integer division truncates: `-35 / 10` is
  `-3` where the floor is `-4`. With a 2.5 dBi antenna the conducted ceiling is −3.5 dBm,
  and truncation would authorise −3 dBm — **0.5 dB above the D33 ceiling**. There is a
  host test named for exactly this.
- **A ceiling below the SX1262's −9 dBm minimum is a refusal, not a floor.** Past about
  8 dBi of antenna gain the EIRP ceiling drops under what the radio can emit. The firmware
  reports `BelowRadioFloor` and does not transmit, rather than quietly using −9 dBm and
  producing a log that cannot be audited.

Conducted power and antenna gain travel together in one `PowerPoint` struct so they cannot
drift apart between the clamp and the CSV (D33 standing condition 1).

Spec §18.1's worked example — 2 dBi antenna, roughly −3 dBm conducted — is a host test
rather than a comment.

### Provisional, and labelled as such in the code

`915.0 MHz / SF7 / CR 4:5` are **placeholders for getting two boards to talk**, not a
choice. D1 is open and bounded; §12.1 forbids fixing a frequency before M20's ambient
survey has run at both ends, which is R8 and has not happened.

### What is *not* verified

Everything requiring hardware. Specifically **R2's acceptance criterion is unmet**: no
packet has crossed the bench, and no RSSI has been observed at 1 m. R3's settings dump is
host-tested for content but has never been printed by a real boot. The radio bring-up path
— TCXO, `setDio2AsRfSwitch`, `begin()` return codes — is written and compiled but has
never run on an SX1262.

`range/skeleton`'s gate is "both boards flash, radio inits, link established at one test
point". **That gate is not passed.**

## 2026-08-31 — R2 gate PASSED on hardware, and the bug it caught

Both Heltec V3 boards flashed and run against each other on the build-machine bench.
**R2's acceptance criterion is met:** both boards initialise and packets cross with
plausible RSSI.

| | |
|---|---|
| Test point | 915.000 MHz, SF7, BW 125 kHz, CR 4/5 — **provisional, not D1** |
| TX | −9 dBm conducted (sweep floor) + 2.0 dBi → **−7 dBm EIRP**, inside the D33 ceiling |
| Payload | 17 B (`LRAN-RANGETEST-R2`) |
| Separation | bench, both boards on the build machine, ~1 m, stock whips |
| Round trips | **11 complete**, 0 tx errors, **0 PHY CRC errors** |
| Initiator RX | −48.4 dBm mean (−48 / −49), SNR 12.77 dB mean |
| Responder RX | −49.0 dBm mean (−49 / −49), SNR 12.15 dB mean |
| Echo latency | 50 / 76 / 100 ms (min / mean / max) |

Both directions are within 1 dB of each other, which is what identical hardware at each
end should give and is a cheap check that neither PA is misbehaving. Free-space at 1 m
and 915 MHz is ~31.7 dB, so ideal received power would be about −36.7 dBm; the observed
−49 sits ~12 dB under that, which is unremarkable for stock whips at arbitrary
orientation on a desk. **Nothing here is a range figure** — it is a bench link.

`--- settings (R3) ---` printed correctly on both boards at boot, with `conducted_dbm`
and `antenna_gain_dbi` as separate fields (D33 standing condition 1). Neither board
logged an OLED warning, so the panel ACKed at 0x3C on both and the Vext sequence lifted
from wattcycle-reader is right on this board too.

### The bug the gate caught: `poll()` re-read one packet forever

**First bench run looked wrong immediately.** The initiator transmits every 2 s; the
responder was reporting a receive roughly **15 times a second**, every one with
byte-identical RSSI and SNR, and echoing each.

`RadioLink::poll()` was asking `getPacketLength()` whether a frame had arrived. That
register holds the length of the **last** packet received and **is not cleared by
reading it**. With no new traffic it keeps returning the same non-zero value, so `poll()`
kept re-reading the same buffered frame and reporting it as a fresh arrival.

**This would have been quietly fatal to R4.** Round-trip PER is echoes-received over
probes-sent, and both counters were being inflated by re-reads of a single packet — by a
factor that depends on loop timing, not on the link. It would have produced a PER of
approximately zero at every test point, including the ones where the link was failing,
and the number would have looked plausible enough to walk 500 ft on.

Fixed by gating on the **DIO1 interrupt** (`setPacketReceivedAction`), with the flag
cleared inside `start_receive()`. That clear is load bearing: DIO1 is shared, and
RadioLib's blocking `transmit()` lets TxDone fire through the same line and the same
action, which would set the flag with no packet to read. Every transmit path calls
`start_receive()` afterwards, so the spurious set is discarded at the one point it can be
identified as spurious. `getPacketLength()` is now only asked *how big* a frame is, never
*whether* one came.

After the fix: one receive per board per 2 s probe, alternating initiator → responder →
echo → initiator, exactly as designed.

**Worth generalising.** Any firmware in this repo that polls RadioLib for arrival has
this trap available to it. The bridge and simnode should use the interrupt from the
start rather than rediscovering this.

### Both CP2102 bridges report the same USB serial number

```
/dev/cu.usbserial-0001   USB VID:PID=10C4:EA60 SER=0001 LOCATION=0-1
/dev/cu.usbserial-3      USB VID:PID=10C4:EA60 SER=0001 LOCATION=2-1
```

`SER=0001` on both. **The boards cannot be told apart by USB serial number** — only by
device node, which depends on enumeration order and is not stable across replug. Two
consequences:

- Scripted flashing must address boards by the port it just enumerated, not by a
  remembered name. Do not write `/dev/cu.usbserial-3` into anything durable.
- The OLED role badge is the only reliable way to tell which physical board is which
  once they are off the bench. That is now an inverted `INIT` / `RESP` tag drawn in a
  fixed position on every screen, not just at startup.

### A serial role selector, alongside PRG

R2's gate is worked with **both boards tethered to one machine**, where a thumb cannot
reach two PRG buttons in two 3-second windows. Sending `i` or `r` during the same
selection window now picks the role directly.

The button remains the primary selector — the walking end is untethered by definition and
PRG is all it has. This is additive, still inside the window, and still not persisted: a
power cycle re-asks (R1). Bridge Impl Plan §11.2's original design selected modes by
serial keypress, so the mechanism is not foreign to this firmware.

### Still not verified

- **Nothing about range.** M6 is untouched; this is a 1 m bench link.
- **D1 remains open.** The frequency needs M20 (R8); the power figure needs M21.
- **GPIO 0 as PRG is still inferred**, not confirmed against the V3 schematic — the
  serial selector was used for both boards on this run, so the button path has **not**
  been exercised on hardware. `TODO(R2)` in `src/role.h` stands.
- **The OLED badge has not been read by a human.** Both panels initialise and the code
  path runs; that the tag renders legibly is unconfirmed.

## 2026-08-31 — bench observations: PRG confirmed, display truncation fixed

Operator observations from the R2 bench, and the fixes they produced.

### Both role selectors confirmed on hardware — `TODO(R2)` closed

- Reset, then PRG inside the 3 s window → `RESPONDER`, on **both** boards.
- No press → `INITIATOR`, on both boards.
- Reboot of the second board alone, PRG inside the window → that board `RESP`, the
  other still `INIT`.

**GPIO 0 is confirmed as the PRG button.** Behaviourally rather than off the schematic,
which is the stronger of the two checks: what matters is that the button reaches that
GPIO under the post-boot window, and it does. The `TODO(R2)` in `src/role.h` is closed.

This also confirms the workaround for the strapping-pin problem works in the operator's
hands, not just in the code — the whole reason R1's "hold PRG at boot" had to be
reinterpreted.

### The "locked" display was not a fault

Both boards sat on the role screen until traffic appeared. **That is correct behaviour
with no initiator present** — `show_link()` is only called on a receive, so the role
screen is what a board shows while nothing is arriving. Worth stating plainly because it
looks like a hang, and during the walk a responder that has gone out of range will do
exactly the same thing.

**A refinement worth making in R6:** with no traffic there is currently no way to tell
"out of range" from "crashed". A last-heard age on the responder screen would separate
them and costs nothing. Noted for R6 rather than done here.

### Two silent truncations on the OLED, both fixed

The panel is 128 px and neither string fit:

| Rendered | Should read | Fix |
|---|---|---|
| `RESPONDE` | `RESPONDER` | role word `ArialMT_Plain_24` → `_16` |
| `heltec_wifi_lora_32_3` | `heltec_wifi_lora_32_V3` | new `short_name` field, `"Heltec V3"` |

**Both role words are nine characters**, so `INITIATOR` was clipping too — it just reads
as a plausible word when it loses its last letter, which is worse. A label that silently
drops a character is worse than a smaller one, and the inverted badge already carries the
at-a-glance version.

`short_name` is a field on `BoardRadioConfig` rather than a trim at the draw site, so the
128 px limit lives with the board it belongs to and **pass 2's XIAO entry has to answer
the same question**. The full `name` is unchanged and still what the R3 settings dump
prints — that is what a CSV is correlated against, and there width does not matter.

Two host tests added (19 now): `short_name` is within a 16-character budget, and the full
name is still the unambiguous one. A character count is a crude proxy for a rendered
width and the only one a host test can check; its job is to stop pass 2 pasting another
22-character name into the XIAO entry and rediscovering this on a bench.

### SNR was missing its units — not truncated, just absent

Operator asked whether `SNR 12.2` was being cut off. It was not: the format string was
`"SNR %.1f"` into a 24-byte buffer for an 8-character result. **The units were simply
never written.** Now `"SNR %.1f dB"`.

Worth separating from the two truncations above, because the symptom looked identical
and the cause was not. One is a panel too narrow for the string; the other is a string
that was never complete. The tell is the inconsistency: RSSI has an explicit `dBm` label
drawn beside it, so SNR having no unit was an oversight rather than a width decision.

Right-aligned at 128 px, `SNR -20.5 dB` is the widest realistic case at roughly 66 px, so
it starts near x=62 and clears the 30 px role badge.

### Link re-verified after the changes

Reflashed both boards after the truncation fixes, and again after the SNR units: 13
initiator receives, 15 responder receives, **0 tx errors, 0 PHY CRC errors** on both
runs, RSSI −49/−50 dBm, SNR ~12 dB. Unchanged from the gate run. The display edits
touched nothing on the radio path, and this confirms it.

## 2026-08-31 — R4: the sweep runs, and three methodology bugs hardware found

Branch `range/sweep`. The sweep enumerates 24 test points and emits one row each.
Bench run: **all 24 points 8/8 echoes, 0% PER**, both legs symmetric within 1.6 dB.

New modules, all Arduino-free and host tested: `airtime`, `bench_frame`, `sweep`,
`sentinels`. 76 host tests.

### The airtime formula reproduces spec §15.1 exactly — W7 has its instrument

All **twenty-one** figures in §15.1 are asserted as tests and all pass: 19/24/30/34/38/
96/222 bytes at SF7/8/9. §15.1 says the table "should be regenerated once **D1** fixes
SF", which is **W7**; an implementation that reproduces the existing table value for
value is the thing to regenerate it with, not merely a convenience for the sweep.

The 4.25-symbol sync interval §15.1 records v0.2 having omitted is pinned by its own
test, so that ~4% understatement cannot come back.

Two derived uses: the echo timeout is `2 × airtime + margin` per test point (a fixed
timeout is either absurd at SF7 or scores every SF12 probe lost), and the sweep reports
its own duration — **nominal 368 s, worst case 416 s** per position on the default plan.
Both figures, because the spread is what the operator needs: worst case is what a dead
position costs, and that is exactly where the data is wanted.

### Bug 1 — the responder never followed the initiator

First sweep run: **all eight SF7 points at 0% PER, all sixteen SF9 and SF12 points at
100%.** Not a link result. The initiator retunes per test point; the responder stayed
where it booted, and two radios on different spreading factors cannot hear each other
at all.

**A sweep in that state can only ever measure its first radio configuration and reports
every other one as a dead link** — which at 500 ft is indistinguishable from a real
result, and would have gone straight into D1.

The responder cannot be told to retune out of band: the only channel is the one whose
configuration is changing. So it hunts — dwell on a configuration, and on silence step
to the next one **cyclically**. Stepping cyclically from the last configuration heard
is both the fast path and the recovery path, so there is no separate scan mode: in plan
order the next configuration is almost always right.

Only freq/SF/CR affect reception, so it tracks **six** configurations, not 24 points.
Dwell is two worst-case probe periods **at that configuration** — one probe period is
0.5 s at SF7 and 8.4 s at SF12, so a fixed dwell abandons SF12 mid-probe.

### Bug 2 — the echo ran at the wrong power, flattering PER

With bug 1 fixed the data showed a systematic gap: at −9 dBm test points the initiator
measured ~−41 dBm while the responder measured ~−48. **Exactly the 6 dB between −9 and
−3.** The responder was echoing at its clamped ceiling regardless of test point, so the
return leg was 6 dB stronger than the outbound one.

Round-trip PER stops being a measurement of the link when its two legs run at different
powers, and it biases *optimistic* at precisely the low-power points the D33 ceiling
forces this sweep to care about. The probe names its test point, so the echo now
transmits at the probe's own power — still via the clamp; nothing bypasses D33.

After the fix both legs agree within 1.6 dB across all 24 points.

### Bug 3 — reacquisition time was charged to the link as packet loss

Losses then appeared **only on the first test point after a configuration change**:
2 of 8 entering SF9, 1 of 8 entering SF12. The counts matched
`dwell(previous config) / probe_period(new config)` exactly — the responder's hunt time,
scored as lost packets, on 5 of 24 points, biasing them *pessimistic*.

The initiator knows the plan, so it knows how long the responder needs. It now sends
**uncounted warmup probes** on a configuration change — transmitted normally, excluded
from the statistics — sized from the previous configuration's dwell and capped at 6. A
warmup longer than the measurement it protects would be a worse trade than the bias.

Zero when the configuration is unchanged, which is three points in four.

### Worth noting about all three

None of these is visible in review. Each produces a plausible-looking CSV: bug 1 gives
a clean 0% at SF7 and an honest-looking total failure elsewhere; bug 2 gives slightly
better numbers than the truth; bug 3 slightly worse, only at boundaries. **Two of the
three bias PER in opposite directions**, so an average would have hidden both.

They were found by reading a bench sweep of a link known to be good — a 1 m desk link
where every point *must* read 0% PER. That is the value of running the sweep somewhere
the answer is already known before walking anywhere.

### Not done in R4

- **R5** position marking — `position_id` is plumbed through the frame and the CSV but
  is still 0; the responder's PRG button does not yet increment it.
- **R6** the responder's own local summary, and the real CSV.
- **R7** committing traces to `docs/rangetest/data/`. The serial format is a first cut
  and R7 owns the committed schema.
- **Frequency is a single-entry axis.** §12.1 forbids fixing one before M20, so
  sweeping frequencies now would produce numbers nobody can interpret. R8 fills it.
- **Nothing about range.** Still a 1 m bench link. M6 untouched, D1 open.

## 2026-08-31 — R5: one sweep per position, PRG starts the next

The initiator is now a two-state machine. It sweeps once, **ARMS**, and waits; the
operator walks, presses PRG on the responder, and the next sweep begins. Verified end
to end on the bench across two positions, 0% PER throughout both.

A free-running loop was the wrong shape: it re-measures a position the operator has
already left, and each wrap costs the responder a reacquisition it need not pay.

### The button is on the walking end, so its press has to travel in band

There is no second channel — the only link is the one whose configuration the sweep
keeps changing. So the **responder owns `position_id`**, increments it on a press, and
stamps it into every echo; the initiator learns it from there and never writes it. One
writer, so the two ends cannot disagree about where the operator is standing.

That has a consequence worth stating: **an armed initiator must keep talking.** If it
went silent between sweeps there would be no echo to carry the new position, and the
press would never arrive. It beacons on configuration 0 once a second — a header-only
frame at the sweep floor, counted in nothing.

The R4 draft had this backwards: the responder took `position_id` from the probe, which
made the *initiator* authoritative about a fact only the walking end knows.

### Press-to-start latency: 15 s, then 0.67 s

First end-to-end walk measured **15 seconds** between the press and the next sweep
starting. Cause: a sweep ends on the slowest configuration (SF12, CR 4/8) whose dwell is
~17 s, and the responder was waiting that out before cycling to find the beacon.

Nothing needs discovering there. Both ends already know a new sweep starts on
configuration 0 and that an armed initiator beacons on it, so the press now tunes the
responder straight to configuration 0 rather than letting a timer expire.

| | Press → sweep start |
|---|---|
| Before | 15.0 s |
| After | **0.67 s** |

The operator stands still once per position and again per sweep; fifteen seconds of
each of those, across a walk with a dozen positions, is several minutes of standing in
a field for nothing.

### Bench aids, clearly not the field flow

`p` on the responder's console increments the position exactly as PRG does; `s` on the
initiator's forces a sweep; `n` advances the position locally. They exist so the walk
can be driven with both boards on a desk — which is how the 15 s latency was found
before anyone walked anywhere. The physical button is the field mechanism and is
confirmed working (2026-08-31 entry above).

### R5 acceptance

| Criterion | Status |
|---|---|
| Position ID appears correctly in the CSV | **Met** — `pos=1` rows after the transition |
| Increments once per press | **Met** — debounced falling edge, non-blocking |
| Survives the walk out and back | **Not tested** — needs an actual walk |

The debounce is deliberately non-blocking: the responder has to keep echoing while the
operator is pressing, and a blocking debounce would drop probes at exactly the moment a
new position begins.

### Still open

- **R6** — the responder's own local summary of what *it* received, and the real CSV.
- **R7** — committing traces to `docs/rangetest/data/`.
- Nothing about range. M6 untouched, D1 open pending M20 and M21.

## 2026-08-31 — R6: the real CSV, the responder's own log, and last-heard age

Initiator CSV is now a shared formatter; the responder keeps and persists its own
record; both displays say something useful when nothing is arriving. 99 host tests.

### The CSV header and its columns cannot drift apart

One schema string produces both, and a host test asserts they have the same field
count. A CSV whose header stops matching its columns **parses, plots, and misattributes
every value**, and nothing downstream can detect it. 27 columns; R7 commits these
traces to `docs/rangetest/data/` as D1's evidence, so a reader in eighteen months has
to be able to interpret them.

Everything is integer tenths with a `10` suffix in the column name — no decimal points
and no floats, so there is no rounding step between the measurement and the file.
Conducted power and antenna gain stay separate columns (D33 standing condition 1).

**The refuse-to-truncate rule caught its own bug.** `kCsvMaxLine` was first sized for
the widest row (~200 chars) at 320. The *header* is the long line — 27 column names run
to 349 — so `csv_header()` correctly returned 0 rather than emitting a short header, and
the test failed immediately. Now 448, with a test pinning the header against it.

### What the responder's local log adds, given the echo already carries its readings

The echo carries `resp_rssi`/`resp_snr`, so the initiator already has the downlink
signal level — **but only when the echo arrives.** A probe the responder *heard* whose
*echo* was lost is invisible to the initiator: it sees a missing echo and cannot tell
which leg failed. That is exactly the direction R4 gives up by making round-trip PER
primary.

Three numbers separate them completely:

```
probes_sent  (initiator)  vs  probes_heard (responder)  ->  DOWNLINK loss
probes_heard (responder)  vs  echoes_recv  (initiator)  ->  UPLINK loss
```

So the echo also carries `resp_heard`, the responder's tally for that test point. That
makes **the initiator's CSV self-sufficient** — the disambiguation is in the row itself,
rather than requiring the responder's NVS log to be recovered and merged afterwards.
The log remains the record for probes whose echo never made it back at all.

Per-position ring of 16, persisted to NVS on each position change, dumped over serial
at boot — R6's "small NVS ring of per-position summaries dumped over serial on
reconnect", and nothing larger. Ring overflow is **reported**, because a dumped log
that has quietly dropped its earliest positions is worse than one that says so.

### Two counting bugs the bench found, both in the same place

`resp_heard` must be comparable to `probes_sent` or the arithmetic above is meaningless.
Twice it was not:

| Symptom | Cause |
|---|---|
| `resp_heard=12` vs `sent=8` | The responder counted the **warmup probes** the initiator excludes |
| `resp_heard=9` vs `sent=8`, first point of position 1 only | The **armed beacon** used `kind=Probe` with `tp_index=0`, so beacons were tallied against real test point 0 |

Both fixed by making the distinction explicit **on the wire** rather than locally:
`BenchKind::WarmupProbe`. It is echoed exactly like a probe — that is how the responder
proves it has found the configuration — and counted by neither end. Carried in the
existing `kind` byte, so the frame layout is unchanged.

Warmup was already excluded locally by the initiator; the lesson is that a local
exclusion is not enough when **both** ends are counting. After the fix, all 38 rows of a
two-position run show `resp_heard == probes_sent` exactly.

### Last-heard age — "out of range" is not "crashed"

Flagged as a refinement during the R2 bench and built now. A display frozen on its last
good reading makes a responder that has walked out of range look identical to one that
has locked up, and on a walk that is the difference between carrying on and turning
back.

After 12 s of silence the responder shows a **counting** age instead — visibly alive —
with the last RSSI it did hear, or "no contact yet" if it never heard anything, which is
a different situation worth distinguishing. The threshold is above one SF12 probe period
(~8.4 s) so it does not flicker between probes at the slowest configuration.

### R6 acceptance

| Criterion | Status |
|---|---|
| Responder OLED: live RSSI, SNR, position, echo count, large text | **Met** |
| Responder keeps a local running summary of what it received | **Met**, persisted and dumped |
| Initiator CSV: one row per test point per position, all listed columns | **Met**, 27 columns |
| Initiator OLED echoes similar data | **Met** |
| Readable outdoors at arm's length in sunlight | **Not tested** — needs daylight |

### Still open

- **R7** — committing traces to `docs/rangetest/data/`.
- Nothing about range. M6 untouched, D1 open pending M20 and M21.
