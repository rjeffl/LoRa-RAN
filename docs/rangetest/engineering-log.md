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

## 2026-08-31 — sunlight legibility: passes with a hand, not without

Checked outdoors. **The OLED does not power through direct sunlight**; with minimal
shading from a hand it is comfortably readable.

That closes R6's last acceptance criterion, but as a **qualified pass, not a clean
one**. R6 asked for "text large enough to read outdoors at arm's length in sunlight"
and the honest answer is that font size was never the binding constraint — a 128×64
monochrome OLED at maximum contrast is simply outmatched by direct sun, and no layout
change fixes that. Contrast is already at 255.

### What follows from it

- **It is an operating procedure, not a defect.** Shade the display with a hand at each
  position. Recorded in R10's fieldwork notes so it reaches whoever walks the bearing.
- **The glance is brief, so the layout matters more than it did.** RSSI is already the
  largest element and stays that way; the role badge, position and counts are secondary
  and small. Nothing to change, but worth stating as a constraint on future edits: a
  hand-shaded glance is not the moment to add a fourth line.
- **The display is not the record.** The CSV over serial is, and the responder's NVS log
  covers the untethered end. Nothing about the measurement depends on reading a screen
  in a field.

### One cheap thing worth trying, untested

The SSD1306 can invert — mostly-lit field with dark glyphs instead of the reverse. On an
emissive panel that raises total emitted light and *may* read better against bright
ambient, at some cost in power and possible bloom. **Not implemented and not
recommended on evidence** — it is a five-minute experiment for whoever is next outside
with both boards, and if it helps it is a one-line change. Recording it so the idea is
not rediscovered from scratch.

## 2026-08-31 — R7: traces land in the repo

`tools/rangetest/capture.py` writes a committed trace; `docs/rangetest/data/README.md`
documents the 27 columns; `2026-08-31-bench.csv` is the first one.

**The committed trace is a FORMAT PROOF, not range data.** Both boards ~1 m apart on the
desk. Every point reads 0% PER, and that is the assertion: on a link that good, anything
else is a firmware fault rather than a link finding. It exists so the schema, the
tooling and the README's reading guide are exercised end to end before anyone walks a
bearing with them. **M6 is untouched.**

Validated after capture: 27 columns, 24 rows, `tp_index` 0–23 with no gap, no ragged
rows, 0% PER throughout, `resp_heard == probes_sent` on every row, and zero
`phy_crc_err` / `foreign` / `filler_err`. Legs agree within 0.9 dB.

The README's column table is checked against the firmware's own schema string rather
than by eye — 27 columns, none missing.

### Two bugs in the capture tool, both found by using it

**A stray row from the previous sweep.** The first capture wrote **25 rows for a
24-point plan**: a `tp_index=8` row was still in the serial buffer from an earlier run
when capture started, and it landed at the top of the file ahead of `tp_index=0`. A
trace with a duplicated point and a row belonging to a different sweep would have been
believed. Fixed by discarding any data row seen before the CSV header — the header is
printed once per boot, so anything earlier belongs to a previous run.

**Then that fix broke the settings block.** Clearing accumulated state at the header
also cleared the settings dump, which the firmware prints *before* it — so the second
capture produced a trace with an **empty configuration block**, which is precisely what
this tool exists to prevent. R3 prints that dump so a CSV can be correlated with the
configuration that produced it; a trace without it is a table of numbers with no idea
what radio made them. Fixed by resetting the settings on the `--- settings` marker and
the rows on the header, which are different events.

Worth noting the shape: the second bug was **caused by the fix for the first**, and it
was silent — 24 correct rows, a clean validation, and a missing header block that no
row-level check would ever notice. It was caught by reading the file.

A third, smaller one on the way past: the settings filter was "contains `=` and no
comma", which swallowed an ESP-IDF log line (`i2cInit(): ... sda=17 scl=18`) into the
configuration block. Now a strict `key=value` pattern.

### R7 acceptance

| Criterion | Status |
|---|---|
| Sweep output goes to a versioned directory, not a scratch file | **Met** — `docs/rangetest/data/` |
| Traces are committed | **Met** — one, labelled as a format proof |
| Usable as D1 evidence | **Not yet** — this is a bench link. Needs the walk, plus M20 and M21 |

---

## 2026-09-03 — the capture tool could not survive the walk it was built for

R7's capture tool was written and proved against a **bench** run: one sweep, six
minutes, an operator watching the terminal the whole time. R10's position walk is a
different shape — a dozen sweeps over an hour or more, with the operator at the *far*
end holding the responder and nobody looking at the laptop — and reading `capture.py`
against that shape before walking found three ways it loses the afternoon.

**The trace only existed in memory until the tool exited.** Rows accumulated in a list
and the file was written in one go at the end. Every failure mode of a long unattended
capture — a closed terminal, a laptop asleep, a nudged USB cable, and most of all
**Ctrl-C, which is the natural way to end a walk** — landed between the first row and
the last, and wrote nothing at all. The bench never exposed this because a bench run
ends by reaching `--sweeps 1` on its own.

Rows are now appended and flushed as they arrive. A capture that dies at position 9
keeps positions 1–8.

**`--timeout` was wall-clock, and 30 minutes by default.** It bounded the whole session,
not the silence. A walk spends most of its time with the initiator saying nothing at all
— it is ARMED, waiting for a PRG press that is several hundred feet away — so the only
correct reading of "nothing is happening" is **idle time since the last byte**. Renamed
to `--idle-timeout` with the same 1800 s default, now measured from the last serial data.
A wall-clock deadline would have ended the capture mid-walk and the operator would have
found out on the way back.

**`--sweeps` had no "until I say stop".** It was required to be a positive count, so a
walk needed the number of positions known in advance — which is exactly the thing a walk
discovers rather than plans. Default is now 0, meaning run until Ctrl-C.

### What a mid-walk reboot does now

The old code discarded every captured row when it saw a second CSV header. That is
defensible on a bench and indefensible after an hour of walking, so the rows are kept and
a `# board rebooted here` comment marks the seam. The honest caveat goes in the file and
in `data/README.md`: **`position` is owned by the responder** (R5), so a *responder*
reset restarts numbering at 0 and the positions after the seam collide with earlier ones.
The tool cannot fix that; it can refuse to hide it.

A header whose column list differs from the one already written stops the capture
outright. Appending would produce a file whose columns mean two different things halfway
down, which is worse than a short trace.

Every trace now ends with a `# capture ended: <reason> - N rows` line. Its absence is the
signal that a file was truncated by something that never got to finish.

### Verified without hardware

Exercised against a simulated initiator on a pty — recorded boot banner, settings dump,
header and rows — in three modes: SIGINT mid-capture, `--sweeps 2` self-terminating, and
a reboot mid-capture. All three write a well-formed trace. The two R7 regressions are
still covered by the same fixture: the pre-header row from a previous run is discarded
(6 rows, not 7), and the `i2cInit(): ... sda=17 scl=18` line stays out of the
configuration block.

**No firmware change.** The initiator's serial output is what it always was; only the
tool reading it changed.

---

## 2026-09-03 — R8: the ambient survey, and the go signal the walk was missing

Two pieces of work, both driven by the field trip they are for.

### R8 — the survey runs, and it found something in fifteen seconds

`SURVEY` is the third mode on the initiator binary, selected with `v` in the boot window.
It scans 902.0–927.8 MHz in 200 kHz steps — 130 bins — reading **instantaneous** RSSI, and
holds a peak, mean and floor per bin across repeated passes. One pass takes ~4.2 s at the
default 30 ms dwell.

**Nothing in this mode transmits.** There is deliberately no path from the survey loop to
`transmit()` or `set_power()`, and the mode returns from `loop()` before the frame handling
runs at all.

**The measurement is `getRSSI(false)`, not `getRSSI()`.** The default reads the packet
status register, which holds the *last received frame's* RSSI and is not cleared — the
same trap `poll()` fell into with `getPacketLength()`. On an empty bin there is no frame,
so the survey would have been a picture of its own memory: a stale reading from a bin
scanned minutes ago, repeated across the band. This is the third time that register family
has offered the same bug in this firmware.

First run, indoors, 15 seconds, 6 passes: floor a flat −113 to −114 dBm across the band,
and **bin 64 (914.8 MHz) peaking at −90.0 dBm against a −110.1 dBm mean**, with 913.8–915.8
MHz consistently hotter than its neighbours. That is a real occupant sitting on top of the
provisional 915.0 MHz starting point, found before the boards left the desk. M20 is the
task that settles what it is; this says the question was worth asking.

**The settle time is not decoration.** The SX1262's RSSI reads low and climbs while its AGC
settles after a retune, so the first samples after each of 130 retunes per pass are
measurements of the receiver waking up rather than of the band. Left in, they drag every
bin's mean down by the same amount — the worst kind of error, because it looks like a
clean, quiet band. Four milliseconds are discarded per bin.

### Seven sites, not two, and why that changed the design

R8 as written asks for the bridge location and "the most distant node location". The
property has **seven** places that matter: the bridge, GateLink at the gate, the existing
front-island weather station, WellLink, the irrigation pump, the lower hop yard and the
propane tank.

That is not a bigger version of the same job. With one stored run, every site costs a walk
back to the laptop to read it out before the next one overwrites it — seven walks instead
of one loop. So the survey stores **one run per site**, keyed by site in NVS and **named**,
because a trace saying "site 3" and nothing else is a trace nobody can place in eighteen
months. The site travels in every CSV row, so one file holds the whole campaign.

Seven blobs of 1580 bytes in a 20 kB NVS partition is close enough to the limit to be
worth checking rather than assuming, so there is a host test asserting the arithmetic and
a hardware run that filled all seven slots and read them back after a reboot: **910 rows,
seven sites, no write failures.** A short write is reported loudly and does **not** advance
the site — advancing over a run that was not stored would lose it silently, and that is the
one failure that costs a second trip to that site.

### The walking operator had no way to know a sweep had finished

Found by writing the field procedure, not by testing. R5 has the operator press PRG, stand
still for a sweep, then move on — but **the operator is at the walking end, several hundred
feet from the initiator's console and its OLED**, and nothing on the responder said the
sweep was over. The procedure would have read "wait about seven minutes", and a guess that
is early puts half a sweep at one position under the label of another, undetectably.

The initiator already beacons once a second while ARMED. The problem was that the beacon
was a `WarmupProbe`, which is **also** what it sends five times *during* a sweep at each
configuration change — so the responder could not say "done" without saying it five times
too early.

The beacon now has its own kind, `BenchKind::ArmedBeacon`: echoed like any probe so the
position still travels back, counted by neither end, and distinguishable. The responder
shows an inverted `DONE` bar with the position number and `PRG = next`. Inverted rather
than merely different text, because the glance that reads it is at a hand-shaded panel in
sunlight after standing still for seven minutes, and it has to survive not reading any
words at all.

**Measured on hardware, both boards, one full sweep:** the sweep took **424 s** (against a
382 s nominal / 430 s worst-case estimate — the estimate is good), and the go signal
reached the responder **17.2 s** after the initiator went ARMED. The lag is real and is
the responder cycling from the SF12 configuration back round to the beacon's. It is in the
field procedure as an expected 15–20 s rather than treated as a defect.

### Two things hardware found in the tooling

**The campaign dump looked like seven reboots.** Each site's dump reprinted the CSV header,
and to anything reading the port a repeated header is exactly what a board reboot looks
like — `capture.py` duly marked six false "board rebooted" seams and, worse, stopped after
the first site because it counted the first site's completion marker as the whole job. Now
a campaign dump prints one header for all seven sites and one completion marker at the end,
and `capture.py` only calls a repeated header a reboot when a fresh settings dump preceded
it. Verified: 910 rows, one header, zero seams.

**The survey trace was describing a sweep that never ran.** Boot printed the R4 sweep plan
— test point count, probe counts, estimated duration — in survey mode too, and it landed in
the trace's comment block. That is precisely the failure the settings dump exists to
prevent, pointed the other way: a trace correlated with a configuration that was not used.
The sweep plan is now printed only when a sweep will actually happen.

### R8 acceptance

| Criterion | Status |
|---|---|
| Third mode on the initiator binary | **Met** |
| 902-928 MHz, 200 kHz steps, 130 bins | **Met**, host tested |
| Peak and mean per bin over repeated passes | **Met** — peak, mean and floor |
| Runs at both required locations | **Exceeded in capability, not yet done** — seven sites supported and bench-proven; no site trace captured yet |
| Two committed traces | **Not met** — fieldwork |
| A chosen frequency justified against them | **Not met** — needs the field campaign, and D1 also needs M21 |

---

## 2026-09-03 — field-prep: five questions, three of them defects

All five came from reading `FIELD-PROCEDURE.md` against the actual kit before walking. Three
were real problems in what had been handed over.

### The antenna is a build setting, and it was buried

The production 3.0 dBi antennas replace the stock 2.0 dBi whips everything was bench-tested
on. That is not a documentation change: **the gain is an input to the D33 clamp**, because
the 15.249 ceiling is on EIRP. At 2.0 dBi the conducted ceiling is −3 dBm; at 3.0 dBi it is
−4 dBm. Fitting the production antenna against a firmware still holding 2.0 dBi is wrong
twice over — the trace records an antenna that was not fitted, and the clamp permits an
EIRP 1 dB over the ceiling.

It was `constexpr int16_t kAntennaGainDbi10 = 20;` on line 46 of a 1200-line `main.cpp`.
It is now `-DLRAN_ANTENNA_GAIN_DBI10=30` in `platformio.ini`, under a comment block that
gives both antennas and both ceilings, and **there is no default in the code** — a missing
flag is a `#error`. A default is precisely what silently survives an antenna swap.

No test needed re-running and nothing was invalidated: the committed bench traces are
format proofs, not range data.

### `capture.py` only listened, so the procedure told the operator to run two things at once

The procedure said "press `z` on the console" and also "run `capture.py`". On one port.
macOS opens a serial device **without an exclusive lock**, so both processes succeed and
then split the incoming bytes between them at random — which is how the bench scripts
appeared to work, and how a field trace would have come home with holes in it and no
indication anything was wrong.

`capture.py` now drives the board: `--reset` pulses the reset line, `--role` selects
INITIATOR / RESPONDER / SURVEY, `--key` sends console keys and `--key-after` waits first.
One command, one process, one port. It also removes the old "start the capture BEFORE
resetting the board" trap entirely, because the tool now owns both events.

**The role key has to be sent repeatedly, not once.** The first version wrote it 0.8 s
after reset and the board came up INITIATOR every time — the boot is ROM bootloader, then
`Serial.begin()`, then the OLED bring-up's own delays, and a byte landing before the UART
is configured is simply gone. On the walking end that is a board that will not echo; in
survey mode it is a board that **transmits**. It is now sent every 150 ms across the whole
window. `select_role()` drains everything available per pass and returns on the first
match, and none of `i`, `r`, `v` is a key in any mode, so the repeats are inert.

### `pyserial required` was true and the advice was wrong

`python3` on the build machine is one of five framework installs on `PATH`, none of which
has pyserial; PlatformIO's venv has it. The documented command said `python3`, and
`pip3 show pyserial` reporting it installed was a *different* interpreter again.

Installing it into whichever `python3` happens to be first would have worked here and
broken on the Kubuntu field machine. The docs now say
`~/.platformio/penv/bin/python` everywhere, and the error message names both the
interpreter known to have it and the one actually running.

### Height above ground is the wrong quantity on this property

The procedure repeated the standard flat-path advice: record antenna height, it matters at
915 MHz. It does — over flat ground, where height buys Fresnel clearance. **This property
has 60–80 ft of relief between the bridge and the gate**, and 1.2 m versus 2.0 m of mast is
noise against that.

The guidance now asks for three things instead: height above local ground (still governs
near-field clearance, cheap to note), **approximate true elevation** from phone GPS or a
topo source at ±10 ft, and **whether the path has line of sight** — the last being the most
predictive thing available and free. The fixed installations are a one-time note rather than
a per-run measurement, since those heights are already set.

### Two things that were not defects

Reflashing is needed once, for the antenna flag. And the boards were already flashed — but
from the wrong branch, see below.

### PR #15 merged to the wrong base

**R8 never reached `main`.** #15 was opened against `range/capture-walk` while #14 was still
open; #14 merged to `main` first, then #15 merged into `range/capture-walk`, leaving the
survey mode on a branch nobody was building from. It was caught by flashing both boards and
finding a role prompt that read `send 'i'/'r'` with no `'v'` — while the same board's
settings dump correctly showed the new `antenna_gain_dbi=3.0`, which is what made the
mismatch obvious rather than merely confusing.

`range/field-prep` is branched from `range/capture-walk` and carries R8 forward. **Check
what `main` actually contains before flashing a board from it**; a stacked PR whose base
merges first does not follow it.

### Verified on hardware after all of the above

One command, both paths, on boards flashed from this branch:

- Survey: reset → `SURV` → 25 s scan → dump → **130 rows**, `antenna_gain_dbi=3.0`,
  `role=SURVEY`, clean `# capture ended`.
- Walk: reset → INITIATOR → sweep header and rows, `antenna_gain_dbi=3.0`.

### A postscript on the erase, and on trusting a separate check

The erase looked broken: `# all stored surveys erased from NVS` on both boards, and a
follow-up boot reporting `survey campaign complete - 1 site(s)`. Twenty minutes went into
looking for a spurious store — a stray `prg_edge()`, a role-key repeat landing on a mode
key, anything.

There was none. Driven in **one process on one open port** — erase, reboot, check, then
twenty seconds of scanning untouched and check again — both boards go empty and stay
empty. The fault was in the checking: **every `capture.py --reset` re-enumerates the USB
bridge**, and with two identical CP2102s both reporting `SER=0001` the two device nodes
can swap between invocations. The erase and the check were landing on different boards.

This is the `SER=0001` hazard that is already in the notes, showing up in a shape nobody
had written down: not "you flash the wrong board" but "you verify the wrong board, and
conclude the firmware is broken". Recorded in the field procedure as: trust the
confirmation line from the command that did the work, not a separate check afterwards.

`capture.py --key` now echoes the board's `#` lines for exactly this reason — a setup
command whose confirmation is filtered out is a command you have to verify some other way,
and the other way is what was wrong.

---

## 2026-09-04 — the survey campaign could not actually be run

Three questions about the field procedure, and the second one was a firmware defect that
would have stopped the campaign at the first site.

### The survey was unreachable on battery, and the failure mode was transmitting

R1 says the role is **not persisted**: a power cycle re-asks. R8 then made `SURVEY`
selectable **only** by the serial character `v`, on the reasoning that PRG already means
RESPONDER and overloading it puts the survey one mistimed thumb away from the walk. Select
it at the house, where the laptop is, and walk out with the board still running.

That silently assumed the board stays powered. **It has no battery fitted**, so moving from
the laptop to a power bank *is* a power cycle. The board came back up as `INITIATOR` — the
no-press default, and **the one mode that transmits**. The documented campaign would have
put a board on the air at every site while measuring nothing.

Fixed with a gesture, not a flag: **tap PRG for RESPONDER, hold ~1.5 s for SURVEY.** The
OLED names the role a release would choose while the button is still down, so the word
flips from `RESPONDER` to `SURVEY` under the thumb and the operator releases on the one
they wanted — a hold whose effect is invisible until the radio does or does not start is
exactly what R1's selection window exists to avoid. R1's "not persisted, a power cycle
re-asks" is untouched.

### Then the workaround for it was itself destructive

The first correction told the operator to press PRG past the sites already stored to get
the cursor back where it belonged. That is `survey_store_and_advance()`, which **stores the
run in progress into each slot on the way** — an empty run written over every completed
site. The campaign destroying itself to get back to where it was.

The site cursor is now persisted in NVS next to the runs. **The role is not campaign
progress**: R1 governs the first, and the second has to survive a power cycle for the same
reason the stored surveys do. On boot the survey mode prints
`# resuming campaign at site N <name>` and picks up exactly where it stopped.

Verified on hardware: erase, store two sites, power cycle, and the board resumes at site 2
`weather-island` with sites 0 and 1 intact.

`n` and `b` move the cursor **without storing**, for a skipped site or a mis-set cursor.
Correcting a cursor must never go through the store path.

### `capture.py` could not capture the responder's log at all

Asked for the exact command to read the responder's position log after a walk, there
wasn't one. `capture.py` recognised two header shapes and required data rows to begin with
a digit; the responder's log is `RESP,position,...` with rows starting `RESP,`. **Every row
was being discarded**, silently, as not-a-data-row.

This is the reverse-direction data — the only record that a probe was heard whose echo was
lost — and it is the thing R6 exists to produce. `RESP,position,` is now a recognised
header, `RESP,` rows are accepted, and the firmware prints `# responder log complete` so a
capture stops on it like any other unit of work. The `--- end responder log ---` banner is
for a human; the tool needed a `#` marker it already knew.

Verified: 1 row, `RESP,0,104,104,-232,-270,-190,123,105,135` — 104 probes heard, 104 echoes
sent, on a 1 m bench link.

### The battery module: nothing to do in firmware

Asked whether charging and USB transition still work off stock Meshtastic. **This firmware
touches nothing battery-related** — no ADC read, no `VBAT`, no charge control, and the
vendor variant does not declare a battery pin. Charging and the USB/battery power path are
onboard hardware and run regardless of firmware; Meshtastic only *reads* the voltage.

What is lost is the readout, and there is none here to lose. What is gained is real: with a
battery fitted, moving between the laptop and the power bank stops being a power cycle, so
the role survives and the hold-PRG step goes away. Recorded as a ten-minute bench check to
do before relying on it, since no code here can tell you.

### And a reference that should have existed

`capture.py`'s behaviour was spread across three documents, its own docstring and the
argparse help. [`CAPTURE-PY.md`](./CAPTURE-PY.md) is now the single man-page-style
reference, with a complete command for each field job.

---

## 2026-09-04 — first real position walk: the data is good, and it found two bugs

Two positions on the gate bearing, 3.0 dBi antennas both ends at 1.2 m AGL, dry, foliage
full. `2026-09-04-walk-gatelink.csv` and `-resplog.csv`.

### What the trace says

| | Position 0 | Position 1 |
|---|---|---|
| Test points | 24 | 24 |
| Probes sent / echoes | 192 / 169 | **192 / 192** |
| PER | 0% except tp 0-2 | **0% throughout** |
| Initiator RSSI, median | −44.1 dBm | −72.3 dBm |
| `phy_crc_err` / `foreign` / `filler_err` | 0 / 0 / 0 | 0 / 0 / 0 |

**The two ends agree to within 0.6 dB on average** (2.0 dB worst) between
`init_rssi_mean10` and `resp_rssi_mean10` across all 48 rows — the link is symmetric and
both radios are measuring the same thing.

**The cross-check closes exactly.** Position 0 lost 23 probes by the initiator's count
(8 + 8 + 7 on test points 0, 1 and 2). 192 − 23 = 169, and the responder's log reports
**169 probes heard, 169 echoes sent**. Two independent counters, two files, no
disagreement. That is the strongest evidence yet that the instrumentation is right, and it
is what made both of the bugs below visible rather than plausible.

28 dB of path loss between the two positions, and still 0% PER at the far one — this link
has margin in hand at the D33 ceiling.

### Bug 1 — the last position of every walk was never saved

Two positions were walked. **The responder log contains one row.**

`resp_log_save()` was called only when the position ADVANCED — persisting the position
being left. The final position is never left: the operator walks home and powers the board
down. Its data, often the most distant point and the entire reason for the walk, was gone.

Now saved on the **armed beacon**, the same signal that raises `DONE`: the initiator has
finished the sweep for this position, so the tally is complete. Saving on the transition
writes once per position, not once per beacon.

### Bug 2 — the initiator swept before the responder existed

Position 0 lost its first 23 probes: 100% PER on test points 0 and 1, 87.5% on test point
2, then clean for the remaining 21. Position 1 was 192 of 192.

That is not the link. The initiator booted straight into `Sweeping`, and the field flow is
*start the capture* (which resets the initiator) → *walk over* → *boot the responder*. The
initiator is always several test points ahead of a responder that is not yet listening.
Every first sweep would have carried the same fake loss, at the position where the boards
are closest together and the numbers look most trustworthy.

The initiator now boots **ARMED** and runs nothing until the first PRG press — which is
what R5's "one press, one sweep, one position" already describes. Positions run 1..N and
every one of them is clean.

Worth naming the shape: **both bugs produced plausible data.** A missing position looks
like a walk that covered fewer stops, and 100% PER on the first two test points looks like
a radio warming up. Neither would have been caught by looking at one file. The cross-check
between the two files is what made them undeniable, and it is now written into
`data/README.md` as the thing to do with every walk.

### Bug 3, found while verifying bug 1

The responder's armed-transition line read `# far end ARMED - sweep complete at position
N`. **`sweep complete` is the exact substring `capture.py` stops a capture on**, so reading
the responder's log while the initiator was still powered and beaconing ended the capture
on that line instead of on `# responder log complete`. The bench showed it as two
completion units counted for a two-row log.

It now reads `# far end ARMED - position N measured and saved`. The responder does not run
sweeps, so saying it did was wrong as well as ambiguous.

Worth noting for anything that adds a marker later: the completion markers are matched as
**substrings anywhere in a `#` line**, so a new message that happens to contain one silently
truncates a capture. The markers are listed in `capture.py`'s `COMPLETION_MARKERS`.

### Verified on hardware, both fixes, a full two-position walk

| | Before | After |
|---|---|---|
| Rows before the first PRG press | test points 0-2 of position 0 | **0** |
| Position 1 / 2 probes, echoes | 192 / 169 at position 0 | **192 / 192 and 192 / 192** |
| Positions in the responder log | 1 of 2 | **2 of 2**, both 192 heard, 192 echoed |

The responder log now closes against the sweep trace on both positions with nothing left
over, which is the check `data/README.md` asks for.

---

## 2026-09-04 — auditing the docs against the firmware, and the recipe that never ended

Both field documents were brought back in line with the firmware after six behaviour
changes in two days. The audit was mechanical rather than by eye: extract every
`capture.py` invocation from both documents, check every flag against `argparse`, check
every quoted `#` message against the firmware source, and then **run all eleven of them
against real boards**.

### One documented recipe could never terminate

```
capture.py --port ... --reset --role responder --key x --out ... --idle-timeout 20
```

`--idle-timeout` ends a run on **silence**. A responder hunting for an initiator prints
`# resp tuned to config N` on every dwell expiry, so it is never silent, so the deadline
never fires. **The documented "erase the position log" command runs forever.** Confirmed by
running it: still alive after four minutes, no output past the confirmation line.

Silence is the right rule for a walk and the wrong rule for a command. Added `--run-for`, a
wall-clock deadline that does not care what the board is saying, and switched both erase
recipes to it. It is rejected if it does not exceed `--key-after`, or the keys would never
be sent. Verified: 25.5 s for a `--run-for 25`, with `# position log cleared` echoed.

### What the audit found in the documents

- The responder's armed line was still documented as `sweep complete at position N`. It
  changed to `position N measured and saved` when that substring turned out to stop a
  capture, and the document had not followed.
- The survey key table in `CAPTURE-PY.md` was missing `n` and `b` entirely — they were
  added with the site cursor and only reached `FIELD-PROCEDURE.md`.
- The erase confirmation gained `, site cursor reset` and neither document said so.
- Neither document mentioned that **the initiator now boots ARMED and positions start at
  1**, which is the single most visible change to what a trace looks like.

None of these would have stopped a walk. Together they are how a document stops being
trusted: each individually small divergence teaches the reader to check the source instead.

`CAPTURE-PY.md` now carries a **"firmware behaviour this tool depends on"** section, so the
board-side facts a command depends on live next to the commands rather than only in the
procedure. It also records the trap that produced the marker bug: **completion markers
match as substrings anywhere in a `#` line**, so wording a new firmware message carelessly
truncates captures.

### Worth keeping as a habit

Extracting the commands from the documentation and executing them is the only check that
catches a recipe which is *syntactically* fine and *semantically* endless. Reading it would
not have. Eleven recipes, eleven runs, one of them exposed as unusable.

---

## 2026-09-04 — the erase that "did not stick": opening a serial port pressed the button

Reported from the bench: erase the survey log with the documented recipe, start the Job B
capture, and the board has already advanced to `gatelink-gate` before PRG is ever touched.

It looked like a synchronisation problem between the two commands. It was not. **GPIO 0 is
both the PRG button and IO0, and IO0 is driven by the USB bridge's DTR.**
`serial.Serial(port, ...)` asserts DTR as part of opening, so **merely opening the port
held the button down**, and the firmware read it as a press. In survey mode a press is
store-and-advance, so a tethered session stored a bogus run and stepped the campaign
cursor by one.

The symptom is exactly "the erase does not stick", because it does stick — and then a
phantom press immediately re-stores site 0 and advances to site 1. Three consecutive
sessions reproduced it perfectly: 1 site then cursor 1, 2 sites then cursor 2, each open
adding one.

This is the same GPIO 0 that R1's role selection is built around, and its dual life is
already documented — PRG cannot be held through reset because IO0 is a strapping pin. What
had not been noticed is that the *host* can drive that line at any time, not just during
boot, and that every tool touching the port therefore presses the button.

### Fixed on both sides, because either alone is insufficient

**Host.** `capture.py` now constructs the port unopened, sets `dtr = False`, and only then
opens — the only ordering that applies the setting *as* the port opens rather than after
the pulse. It also deasserts on the way out.

**Firmware.** `prg_edge()` was never a debounce. It rate-limited *changes* — accepted the
first LOW sample it saw, then refused another for 40 ms — so any glitch narrower than the
poll interval still reported a press. It now requires the line to be **continuously low
for 50 ms** before reporting, once per press. A human press is over 100 ms; a line glitch
is far shorter.

The host fix alone left one store in five reset cycles, from the pulse on *close* rather
than open. The firmware fix is what closes it: **eight consecutive tethered survey
sessions, opened, reset into SURVEY, left scanning and closed — nothing stored, cursor
still at `bridge-house`.**

### And a documentation cause, underneath the electrical one

The Job B site-0 recipe was `--key-after 300 --key p`. **`p` *is* the PRG press** — the
tool stores and advances on its own, five minutes in, while the surrounding procedure tells
the operator to press PRG. Two mechanisms doing the same thing and no way to tell which
acted. That is its own contribution to "it advanced before I ever pressed PRG", and it
would have remained true after the electrical fix.

The recipe no longer automates it; `--key p` is documented as the opt-in variant, with the
warning that the site name will change with nobody touching the board.

### The shape worth remembering

Two independent causes producing one symptom, one electrical and one editorial, and the
electrical one was invisible to every test that drove the board *within* a single open
port. It only appears across sessions — which is precisely what the operator does and what
none of the bench scripts did.

### Confirmed on hardware, 2026-09-04

The debounce rewrite risked making the button unresponsive, since it now demands 50 ms of
continuous low rather than accepting the first sample. Checked on the boards by hand:
**a short PRG press still selects RESPONDER and still advances the position; a long press
still selects SURVEY.** 50 ms is comfortably below a real thumb and comfortably above a
line glitch, and both gestures are unaffected.

That closes every verification item that could only be settled by a person at the bench.

---

## 2026-09-04 — re-running a documented recipe destroys the trace it names

Caught while checking what `main` held after PR #17: the two traces from the 2026-09-04
capture were missing. `git log --name-status` put the deletion in `66081bd`, a commit whose
subject is about the DTR fix and which had no business touching them.

**No evidence was lost** — that capture turned out to be an indoor process check rather
than a measurement, and is not committed. But the mechanism is real and would have taken a
genuine trace just as quietly.

### The mechanism, which is a defect in the tool

`Trace.open()` opens the output with `"w"`, and it does so **the moment a CSV header
arrives** — before a single data row is known to be coming. So aiming a capture at a path
that already holds a trace truncates it immediately, and if that capture then fails and
exits with "no data rows captured", the old trace is gone and nothing says so.

The recipes in `CAPTURE-PY.md` and `FIELD-PROCEDURE.md` **name real trace paths**, because
that is what makes them copy-pasteable. Auditing those recipes by running them therefore
pointed live captures at committed evidence. The audit was the right idea — it found the
recipe that could never terminate — but it needed the tool to be safe against exactly this.

`capture.py` now refuses to write to an existing `--out` unless `--force` is given. The
files under `docs/rangetest/data/` are the evidence for D1; a tool that quietly replaces
one with an empty file is not fit to be pointed at them.

### Worth stating plainly

This one was recoverable only because the file had been committed. A trace captured and
then audited in the same session, before any commit, would have been unrecoverable — and
the loss would have been **silent**, because "no data rows captured" reads like the *new*
run failing rather than like the old one being destroyed.

Two habits follow, both now in the docs: **commit a trace as soon as it comes off the
board**, and never point a capture at a path that already holds one.

---

## 2026-09-05 — the field data lands, and the tool ate a third of the survey

R10 fieldwork ran: a seven-position walk on the gate bearing (2026-09-04) and the
seven-site ambient survey campaign (2026-09-05). **M6 has real data for the first time
and M20's campaign is complete.** Both traces are committed. Getting the survey out of
the board took three attempts and the reason is a defect in `capture.py`, not the radio.

### The walk: six positions, and the link has margin everywhere

`2026-09-04-walk-gatelink.csv` with `-resplog.csv`. P0 is the fixed initiator at the
house; the responder walked P1-P6. The full position map and GPS fixes are in the
trace's own header.

| pos | probes | DL loss | UL loss | init RSSI med | resp RSSI med |
|---|---|---|---|---|---|
| 1 | 192 | 1 | 3 | −98.4 | −98.1 |
| 2 | 192 | 0 | 0 | −77.8 | −77.5 |
| 3 | 192 | 1 | 3 | −98.5 | −98.1 |
| 4 | 192 | 0 | 0 | −81.9 | −81.1 |
| 5 | 192 | 0 | 0 | −93.0 | −92.4 |
| 6 | 192 | 0 | 1 | −93.8 | −93.0 |

1152 probes over six positions, **2 lost downlink and 7 lost uplink** — 0.6% round trip
at worst. Every one of the 144 test points returned a reading; there is not a single
dead test point in the trace. `filler_err` is zero throughout, `foreign` zero,
`phy_crc_err` 2 (both at position 3). The two ends agree to within 0.8 dB at every
position, so the link is symmetric and both radios are measuring the same thing. The
responder log closes against the sweep trace at all six positions, which is the
cross-check `data/README.md` asks for.

**The link closes with margin at every place a node will live, at the D33 ceiling.**

### Position 7 is not a location

A PRG press after position 6 advanced the cursor and started a seventh sweep; the
responder was then powered down. Two test points at position 6's spot, then 22 test
points of 100% PER against a switched-off far end.

It is worth writing down *why* that is provably not a link result rather than trusting
the operator's memory: **SF12 at −4 dBm reads 100% PER there while SF7 at −9 dBm was
clean at −80.8 dBm.** SF12 is ~15 dB more sensitive at higher power and cannot fail
where SF7 succeeded. The responder log has six rows and no seventh. The row is kept and
annotated in place rather than deleted — a trace that silently loses a position is worse
than one that explains an odd one.

**The press should not have been necessary.** The 2026-09-04 fix already saves each
position on the armed beacon, so position 6 was in NVS before the button was touched.
`FIELD-PROCEDURE.md` never says so, so the operator did the safe-looking thing and it
cost a phantom position. The procedure now states it.

### P3 is the one number in the walk that cannot be defended

*(Superseded 2026-09-05 — see "the walk's geometry holds up" below. The claim that
nothing can obstruct a 6 m path is simply wrong, and the reading is sound.)*

P3's arcsecond fix is identical to P0's, which would make position 3 a ~6 m link reading
−98.5 dBm with a barn in the path. Nothing can obstruct a 6 m path. At 35N one
arcsecond is ~31 m of latitude and ~25 m of longitude, so **two spots up to ~30 m apart
share a fix** and the true P0-P3 separation is simply unresolved.

The general point matters more than this one position: **arcsecond GPS cannot support an
RSSI-vs-distance curve on a property whose positions are 40-105 m apart.** The fixes
identify locations; they do not measure paths. M6 as written asks whether the link closes
where nodes will live, and it does — but a path-loss model needs better position data
than this walk carries.

### The survey: 325 rows and a whole site vanished, three times, at the same byte

The first campaign capture wrote 585 rows, not 910. `welllink-well` was absent entirely,
`weather-island` held bins 0-28 and `irrigation-pump` bins 94-129 — one contiguous hole.

Two hypotheses died on the way to the answer, and both were reasonable:

- **"The site was never surveyed."** Killed by the firmware: `survey_store_and_advance()`
  advances the cursor *only* when the NVS write succeeds, so sites 4, 5 and 6 existing
  proves site 3 stored.
- **"A receive-buffer overrun."** Killed by re-dumping: a second capture cut at the
  *identical* bin, and a random overrun does not repeat to the byte.

What settled it was making the tool record more, then reading the board raw. Adding the
board's per-site preamble to the trace showed **`bins_sampled=130 of 130` for every
site** — the radio had done its job — and `# survey campaign complete - 7 site(s)` said
all seven were loaded and dumped. A minimal raw logger then caught 7 sites x 130 rows
on the wire, complete. The board was never at fault.

**`capture.py` sent role-selection keys for 3.5 s in a `time.sleep` loop without ever
reading the port.** In survey mode the board is not quiet during that window: it prints
the settings dump and then a full seven-site campaign at boot, ~55 kB. The tty buffer
holds ~17.9 kB and silently discards the rest until something drains it.

| | |
|---|---|
| Bytes buffered before the cut | 17936 |
| Bytes lost, contiguous | 19184 |
| Resumes at | site 4 bin 94, 37120 bytes in — ~3.2 s of wire time |
| Blind window | 3.5 s |

Deterministic because buffer size and boot timing are both constant. That determinism is
exactly what made it look like a firmware bug.

The raw logger only produced clean data because its role loop happened to call
`ser.read()`. That accident is the whole diagnosis.

### What changed in `capture.py`

1. **Drain the port during the reset/role window** and parse those bytes with the rest.
   Both branches — with `--role` and without — were blind; both now read.
2. Parse buffered lines even when a read returns empty, so pre-buffered output is not
   stranded behind a board that has gone quiet. In survey mode it scans in silence.
3. Read 64 kB per call, not 4 kB.
4. **Discards are counted.** A data row whose field count does not match the header used
   to vanish without trace; it now increments a counter, writes
   `# WARNING: N malformed data line(s) DISCARDED` into the trace, and exits 1.
   Repo rule 4 applied to the tool: a discard that increments no counter is how 325 rows
   went missing without the file saying anything was wrong.
5. **The board's `#` lines go into the trace**, so `bins_sampled` travels with the data.
   That is the field that distinguishes a site the radio never finished from one the
   link dropped, and its absence is why the first two diagnoses were guesses.
6. Flush throttled to 0.25 s and the progress print to every 25 rows.

The re-dump is a strict superset of the first capture: all 585 overlapping rows are
byte-identical, which is the check that says we recovered the same campaign rather than
a new one.

### The recovered sites are the ones that mattered

The first analysis concluded "915.0 MHz is clean at every site." That was drawn from the
five sites that survived, and **the two that were lost are the two with occupants in the
LRAN channel.** A textbook survivorship error, and worth recording as one.

| site | floor | in-channel peak (914.6-915.4) | over floor |
|---|---|---|---|
| bridge-house | −116.0 | −113.0 | 3 dB |
| gatelink-gate | −118.0 | −113.0 | 5 dB |
| **weather-island** | −118.0 | **−81.0** at 915.0 | **37 dB** |
| welllink-well | −116.0 | −112.0 | 4 dB |
| **irrigation-pump** | −116.0 | **−77.0** at 915.2 | **39 dB** |
| hopyard-lower | −116.0 | −113.0 | 3 dB |
| propane-tank | −116.0 | −106.0 | 10 dB |

The noise floor is uniform across the property at −116 to −118 dBm, and the mean in
every in-channel bin sits at −114.5, i.e. **at the floor**. So these are rare strong
bursts, not carriers: one hit caught in 600-800 samples. At −77 dBm an interferer is
comparable to or stronger than our own received level on the walk (−78 to −98 dBm), so
this is a collision risk at two sites, not a blocked channel — precisely the thing
Protocol Spec §12.1 says will reopen the channel choice through `cad_backoffs`.

Loudest occupants overall: 906.4 MHz at −39 and 911.4 at −40 (weather-island, almost
certainly the weather station and the YoLink sensors beside it), 916.0 at −51
(bridge-house), 920.0 at −74 (gate).

### The peak column is not site-attributable, and that is a methodology defect

The scan was left running while walking between sites: `survey_store_and_advance()`
calls `g_survey.reset()` and resumes immediately, with no paused state. Peak-hold never
forgets, so **a burst picked up in transit is attributed to the destination site.**

That undercuts exactly what `data/README.md` says a peak is for. Floor and mean are
unaffected in practice — the floor is stationary and min-held, and a few minutes of
walking against a 5-minute dwell barely moves a mean that is sitting on the floor anyway
— so the channel conclusions above stand. The occupant *inventory* does not: it may
credit a site with an emitter that was heard 50 m away. The caveat is recorded in the
trace's own header.

**M20 needs a hold state before its occupant list is evidence.** Filed as the next
firmware task.

### Standing lessons

- **A tool that turns hardware output into evidence must count what it throws away.**
  Every one of the three wrong diagnoses here came from a file that could not say
  whether it was complete.
- **Record the instrument's own view of its work.** `bins_sampled=130 of 130` collapsed
  the search space from "board or wire" to "wire" in one line.
- **When a loss repeats to the byte, stop suspecting the radio.** Determinism is a
  fingerprint of software, not of RF.
- **Beware concluding from the survivors.** The clean-channel finding was drawn from
  exactly the sites whose data made it through.

---

## 2026-09-05 — R11: the survey stops measuring the walk

The M20 campaign measured the walk between sites and filed it under the destination.
This is the fix. Branch `range/r11-survey-hold`.

### The defect, stated precisely

`survey_store_and_advance()` called `g_survey.reset()` and returned, and the scan loop
carried straight on sampling. So from the moment the operator pressed PRG at one site to
the moment they arrived at the next, the radio was folding the *path between them* into
the next site's run.

For the floor and the mean that barely matters — the floor is stationary and min-held,
and a few minutes of walking against a five-minute dwell hardly moves a mean that is
sitting on the floor anyway. For `peak_dbm10` it is fatal, because **a peak hold never
forgets**: one burst heard while walking past an emitter is credited permanently to a
site the operator had not yet reached.

**There is no press pattern that avoids it.** Pressing on arrival rather than on
departure only moves the contamination from the destination to the site just left; the
accumulator is running either way. That is worth writing down because it was the first
thing tried, and it is the kind of fix that looks right until you draw the timeline.

### The shape of the fix

A phase, not a rule. `SurveyCampaign` in `survey.h` holds the cursor and a `Held` /
`Running` phase, and the scan loop returns early while held.

**Two presses per site**: one on arrival to start the dwell, one when it is done to store
and advance, which returns to `Held`. The walk happens in `Held` and is not measured.

Three decisions inside that are worth their reasoning:

- **The accumulator is cleared when the dwell STARTS**, not when the previous site was
  stored. Otherwise a long hold — a rest, a gate to open, a conversation — accumulates
  into the next site's run and the hold state buys nothing.
- **`SurveyCampaign` does not own NVS.** A store fails by short write on a 20 kB
  partition, and a cursor that advanced over a site that was not written is a site
  silently lost. So the caller performs the store and reports the outcome through
  `note_stored(ok)`; on failure **nothing moves** — cursor put, phase still `Running`, run
  still in memory and still accumulating, so the operator can press again or read it out.
  Dropping to `Held` on a failure would quietly stop measuring a site the operator
  believes is live, which is the worse of the two failures.
- **Boot and power cycle come up `Held`.** The board has no battery and every move between
  laptop and power bank is a power cycle; coming back `Running` would measure the walk
  from wherever it was switched on.

The OLED gets an inverted `HELD` bar, for the same reason `show_sweep_done` has one: at
arm's length through a shading hand, "walking, not measuring" versus "measuring" has to
survive a glance that reads no words. Getting it wrong in the scanning direction
contaminates the run; getting it wrong in the held direction wastes a five-minute dwell
that measured nothing.

### `# hold_discipline=1`

A reader cannot tell a clean run from a transit-contaminated one from the numbers. So the
per-site preamble says which firmware produced it. `2026-09-05-survey-campaign.csv` is the
one committed trace without the line, and its caveat stays.

This is the same lesson as `bins_sampled`, one entry earlier: **record the instrument's
own view of its work.** Both times the missing line was the one that would have collapsed
the diagnosis.

### 12 host tests, and one that would have caught the last bug

`SurveyCampaign` is Arduino-free and radio-free like the rest of `survey.h`, so every
transition is host-testable: the failed store that moves nothing, the retry after it, the
last site that stores without advancing, the cursor corrections that drop to `Held`, the
stale cursor that is clamped rather than trusted, and the power cycle that resumes held.
133 tests to 145.

### The tool had no tests at all

Worth stating on its own. The defect that destroyed a third of the 2026-09-05 campaign was
in `capture.py`, and **nothing in this repo tested `capture.py`**. Every test covered the
firmware; the tool that turns the firmware's output into committed evidence was untested,
and it is the component with the most direct path from a small mistake to lost data.

`tools/rangetest/test_capture.py` now exists. The regression under test is blunt — **the
boot window must read the port** — and the boot window was extracted into
`drive_boot_window()` with injected time so a 3.5 s window costs nothing to test.

Verified the test has teeth by reintroducing the bug: four assertions fail, naming the
drain. A regression test that has never been seen to fail is a comment.

It also pins something easy to break by accident: **none of R11's new `#` lines may
contain a completion marker.** `capture.py` matches those as substrings anywhere in a `#`
line, so a carelessly worded firmware message silently truncates a capture — that trap
cost a bench run on 2026-09-04 and is now asserted rather than remembered.

### Not done here

The campaign has not been re-walked. The trace in the repo is the contaminated one, its
caveat stands, and **M20's occupant inventory is still not evidence** — R11 makes the next
campaign clean, it does not retroactively clean this one. D1 continues to wait on that
walk and on M21.

---

## 2026-09-05 — flashing R11 caught R11's own provenance bug within the hour

Both boards flashed from `main` at 5a4c463 (PR #20). Verified on hardware, and the
verification immediately found a defect in the thing it was verifying.

### What the flash confirmed

- Boot comes up **HELD**: `# resuming campaign at site 6 propane-tank - HELD, press PRG
  to start the dwell`.
- **NVS survived the flash.** All seven sites still stored, 910 rows dumped, cursor at 6.
  Worth knowing explicitly: a PlatformIO upload writes the app partition and leaves NVS
  alone, so campaign data is not at risk from a reflash. It *is* at risk from `z`.
- `capture.py` clean: 910 rows, zero malformed.

### The defect: the flag was printed by the wrong firmware

The dump said **`# hold_discipline=1` seven times** — for seven runs collected days
earlier by pre-R11 firmware, which is exactly the contaminated campaign the flag exists
to warn about.

The line was a constant in `survey_dump()`. So it asserted a property of **this**
firmware, while claiming to describe how **that** data was collected. Those are different
firmwares whenever anything is read back out of NVS, which is the only way survey data is
ever read.

**A provenance marker that the reader emits is not provenance.** It has to travel with the
data, because the whole point is that the reader and the writer are not the same.

This is the second time in two days that a "record the instrument's view of its work"
line was the thing that mattered, and the first time one of them was wrong.

### The fix, and the constraint on it

Blob **version 2**: a flags byte at offset 20, bit 0 = hold discipline, three reserved
bytes written zero and ignored on receive (repo rule 5). Header 20 → 24 bytes, blob
1580 → 1584, still comfortably inside the 20 kB partition — the fit assertion was updated,
not deleted.

**v1 blobs are still read**, and report `hold_discipline=0`. That was the binding
constraint: the pre-R11 campaign was sitting in NVS on a board with no battery, and
rejecting v1 to add one bit would have destroyed the only copy of the data that motivated
the bit. `deserialize()` picks the header length from the version and clears the flag on
the v1 path — `reset()` sets it true for a live run, so forgetting to clear it would have
left a stale `true` behind. That has its own test.

### Verified on hardware, same boards, same NVS

```
# hold_discipline=0     x7
910 rows, 0 malformed
# resuming campaign at site 6 propane-tank - HELD
```

Seven honest zeroes, and the v1 data still fully readable. That is the whole change
working: old runs readable and correctly labelled, new runs labelled by what they are.

### 7 more tests, 145 → 152

The live run's flag, the round trip, a hand-built v1 blob that still reads, the v1 blob
that reports `0`, the stale-`true` path, an unknown version still rejected, and the
reserved bits written zero and ignored rather than validated.

The hand-built v1 blob is worth the twenty lines it costs: it is the only way to test
backward compatibility once the code that wrote v1 no longer exists.

### Standing lesson

**Provenance belongs in the artefact, not in the tool that reads it.** Both this and
`bins_sampled` one entry earlier are the same shape of problem, and the difference between
them is that `bins_sampled` was already coming from the instrument and this one was not.

---

## 2026-09-05 — boards erased and staged for the M20 re-walk

Both boards flashed from `main` at 19e605f (PR #21) and survey-erased, ready for the
re-walk that gives M20 an occupant inventory.

The old campaign was dumped one last time before erasing — it is committed, so this was
belt-and-braces rather than necessary, but a `z` is not undoable and a 910-row dump costs
five seconds.

### Both boards were erased, and the second one is why

The two CP2102 bridges both report `SER=0001` and their port names swap between
invocations, so there is no reliable way to say which physical board is which from the
host. Erasing only "the survey board" is therefore a guess. Both were erased.

That turned out to matter. **The second board was holding two stored sites** —
`bridge-house` and `gatelink-gate`, at **4 and 3 passes** against 71–91 for a real run.
Seconds of scanning each: the signature of the **DTR-presses-PRG trap**, where opening a
serial port asserts DTR, which is IO0, which is PRG, which in survey mode is
store-and-advance. Every tethered session before that trap was fixed stored a junk run
and stepped the cursor.

Neither was ever committed and nothing is lost. But it is the first direct sighting of
what that trap actually left behind on a board, rather than the inference from "the erase
does not stick", and it is a good argument for erasing both ends rather than the one you
believe is the survey board.

### State

Both boards: empty survey NVS, cursor at site 0, `HELD` at boot, `# hold_discipline`
reported from the blob. The first PRG press starts the dwell at `bridge-house`.

---

## 2026-09-05 — the walk's geometry holds up, and the height was wrong

Two corrections to the 2026-09-04 walk, both from operator questions. One retracts a
concern I raised; the other fixes a number in a committed trace.

### Retraction: P3 is fine, and "nothing can obstruct a 6 m path" was wrong

I flagged position 3 as indefensible because its arcsecond fix rounds onto P0's, which
would make it a ~6 m link reading −98.5 dBm with a barn in the path.

**The geometry is perfectly ordinary.** Two points 6 m apart with a barn between them —
one standing a few metres from each side — puts the structure squarely in the path. That
is a normal way to end up with an obstructed short link, and I should not have called it
impossible.

### The excess-loss table, which is the actual check

Comparing positions on their *median* RSSI was a mistake: the 24 test points mix two
conducted powers, so the medians are not comparable across positions. Comparing one
fixed configuration — SF7, CR 4/5, −4 dBm, 16-byte payload — against free space at
915 MHz gives this:

| pos | measured | GPS dist | free-space | excess | obstruction noted |
|---|---|---|---|---|---|
| 4 | −80.6 | 106 m | −70.2 | **10.4 dB** | LOS |
| 2 | −77.8 | 40 m | −61.7 | **16.1 dB** | LOS, small shrub |
| 5 | −92.0 | 80 m | −67.7 | **24.3 dB** | LOS to back of house |
| 1 | −96.1 | 85 m | −68.3 | **27.8 dB** | LOS, tree and shrub |
| 6 | −90.8 | 40 m | −61.7 | **29.1 dB** | LOS to opposite side of house |
| 3 | −94.8 | 0–30 m | — | ~35–49 dB | **barn in path** |

**The excess loss tracks the obstruction notes.** Clear LOS is cheapest at 10 dB;
vegetation and houses cost 16–29 dB; the barn costs most. That ordering was not designed
in — the notes were written in the field and the arithmetic done a day later — and it is
the strongest evidence yet that the walk is internally consistent.

P3 at ~49 dB (or ~36 dB if the fix is off by the full quantisation) is a heavily
obstructed path, which is exactly what a metal-clad barn between two nearby points looks
like. **No retake needed.** What remains unknown is its *distance*, and that is true of
every position, not just this one.

### The height was recorded wrong

The capture note said **"both ends 1.2m AGL"**. The operator's actual figure is
**2–4 ft (0.6–1.2 m)**, varying between positions and not recorded per position. 1.2 m
was the top of the range, not the value.

This is not pedantry. Over ground at 915 MHz the two-ray reflection makes received power
scale with the **product of the two antenna heights**, so a height that varied by 2×
across the walk is worth several dB of the scatter *between* positions. Absolute levels
at any one position are unaffected; cross-position comparisons carry that uncertainty on
top of the ±15 m position uncertainty already recorded.

I first guessed this explained part of the P2/P6 gap. **It does not** — see the next
entry. Two-ray caps the height contribution at ~12 dB even at the extremes of the range,
and the measured gap is 18.2 dB. Height is real but it is not the driver.

The trace header is corrected in place rather than the number quietly changed: a
committed trace that carried a wrong figure should say so.

### What would actually be needed for a path-loss model

Not a P3 retake. **Better position data for all seven points**, plus per-position height:

- distances to ~1 m (measuring wheel, laser, or a phone GPS logging decimal degrees
  rather than arcseconds), and
- antenna height recorded at each position rather than as a range for the walk.

`LRAN-Range-Test-Firmware-Pass1-Tasks.md` R10 already says "height matters more than you
expect... record it" and "two runs at different heights are worth more than one careful
run at an unrecorded one." That guidance was right and was not followed closely enough —
worth saying plainly rather than filing as a lesson for someone else.

**M6 is unaffected by all of this.** It asks whether the link closes where nodes will
live, and at all six positions it closed with margin at the D33 ceiling. The path-loss
model is a different, unscheduled question.

---

## 2026-09-05 — the initiator was indoors, and it changes how the walk reads

Operator clarification, and it is the most important piece of site context in the whole
walk. It arrived last and should have been in the capture note on the day.

**The initiator sat at the bridge node's target location: inside the house, in the office
on the NW side.** Every path in the trace crosses at least one 2x4 framed exterior wall at
the initiator end. **There is no free-space leg anywhere in this data.**

| positions | path |
|---|---|
| P1, P2, P3, P4 | single wall penetration, the NW exterior wall |
| P5 | faces the SW side — the path crosses the structure |
| P6 | faces the SE side — the path crosses the structure |

### It is visible in the trace, cleanly

P2 and P6 are both ~40 m from P0. **P2 is stronger by 18.2 dB, consistently across all 24
matched test points** (13.0 to 19.5 dB, every configuration). P2 leaves by the NW wall;
P6's path crosses the house.

That is too large and far too uniform to be terrain or height. The two-ray model caps the
height contribution at about 12 dB even taking the extremes of the 0.6–1.2 m range at both
ends, and this is 18.2. **The structure is the driver, and the previous entry's guess that
height explained the P2/P6 gap is withdrawn.**

Matching test points pairwise rather than comparing medians is what made it clean — the
same mistake, and the same fix, as the excess-loss table.

### What it does to the excess-loss table

Every figure in it — the 10.4 dB at P4 included — **bundles at least one wall**. So the
outdoor portion of those paths is *better* than the table suggested: P4's outdoor leg is
close to free space once a wall's 4–10 dB is taken out of its 10.4 dB.

None of that wall loss is measured here and none of it can be separated out after the
fact. The table stays useful for ranking the positions against each other and useless as
an absolute propagation figure.

### The important part: this is the right geometry, and the wrong data

Both are true and neither cancels the other.

- **Right for M6.** The bridge really will be in that office. These numbers are what the
  deployed link will actually see, walls and all, which is exactly what M6 asks. The
  result stands: the link closed with margin at all six positions at the D33 ceiling.
- **Wrong for a path-loss model.** The readings cannot be compared to outdoor propagation
  curves and cannot be extrapolated to another node location by distance alone. A node
  sited on the SE face starts ~18 dB down on one sited on the NW face at the same range,
  and no distance-based estimate will tell you that.

### The survey's site 0 was indoors too — answered, and the answer is useful

Confirmed by the operator: **site 0 `bridge-house` was measured indoors**, survey node at
the bridge's approximate target location in the office. The other six are at their node
target locations; whether each was strictly outdoors was not recorded at capture time.

**The noise floor does not care.** −116.0 dBm median indoors, against −116 to −118 across
the outdoor sites — the indoor site is not an outlier by even 2 dB.

| site | floor med | floor min | mean med |
|---|---|---|---|
| **bridge-house (indoors)** | **−116.0** | −125.0 | −114.7 |
| gatelink-gate | −118.0 | −119.0 | −114.9 |
| weather-island | −118.0 | −119.0 | −114.9 |
| welllink-well | −116.0 | −123.0 | −114.9 |
| irrigation-pump | −116.0 | −125.0 | −114.8 |
| hopyard-lower | −116.0 | −118.0 | −114.8 |
| propane-tank | −116.0 | −118.0 | −114.8 |

That is a result, not a null: **the floor across this property is receiver-thermal-limited,
not environment-limited.** Walls attenuate external noise but the SX1262's own noise floor
dominates either way, so being indoors neither helped nor hurt. It also means site 0's
floor is directly usable as the bridge's own margin figure — which is exactly the number
§12.1 asks for, measured in exactly the right place.

**The peaks are a different story.** An emitter heard through a wall is 4–10 dB stronger
outside, so 916.0 MHz at −51 dBm is a strong external source, and weak external emitters
may be masked at site 0 and not at the others. Site 0's occupant list is not like-for-like
with the rest, and the trace header says so.

### Lesson

**Site conditions are part of the measurement.** The capture note carried bearing, height,
antenna gain, weather and foliage — and omitted that one end was inside a building, which
turned out to be worth 18 dB between two positions at the same range. The note template in
`FIELD-PROCEDURE.md` now asks for indoor/outdoor and wall penetrations at each end.
