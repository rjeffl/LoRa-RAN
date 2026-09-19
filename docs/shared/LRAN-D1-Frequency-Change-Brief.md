# LRAN D1 — frequency change brief, 917.4 to 917.2 MHz

**Document:** `LRAN-D1-Frequency-Change-Brief`
**Version:** 0.2
**Status:** **Draft for decision.** Nothing here is decided; the operator decides, and the
Decision Register records it
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) **v0.12** (`ver = 2`)
**Decision status:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) — **the only
place D1's and D33's status is recorded**
**Last updated:** 2026-09-19

> **This document decides nothing.** It sets out why D1's frequency should be reopened, the
> candidates, and the one measurement that separates the two best ones. **When the operator
> decides, the register is the file that changes**, and this brief is then marked superseded
> rather than edited to agree with the outcome. [`LRAN-D1-PHY-Decision-Brief`](./LRAN-D1-PHY-Decision-Brief.md)
> is the precedent.

## Recommendation

**Move D1's frequency from 917.4 MHz to 917.2 MHz, and change nothing else.** SF9, BW 125 kHz,
CR 4/5, −4 dBm conducted, §15.249 Envelope A and `backoff_max_ms` = 1500 all stay.

**Before committing, capture each candidate beside 917.4 MHz over the same day.** The
two candidates differ on one untested question: whether Z-Wave's 916.0 MHz traffic reaches a
receiver 1.2 MHz away. The first 917.2 MHz capture added a second: an evening source near
−89 dBm that a capture at 917.4 MHz over the same hours would place. §5 gives the method and
the acceptance test.

**Decide before GateLink is built.** Today the change touches one constant, three bench boards
flashed over USB or OTA, and the documents. Once GateLink is at the gate, it also means a USB
reflash there, because nodes have no OTA.

## 1. Why D1 is reopened

**A Davis Vantage Pro2 weather station transmits inside 917.4 MHz's receive bandwidth.** This
is a new fact; it was not in the evidence D1 closed on 2026-09-10.

- **One of the Davis's 51 hop channels is 917.434 MHz**, 34 kHz from LRAN's centre. The SX1262
  receives 917.3375–917.4625 MHz at BW 125 kHz.
- **M25 identified it by prediction.** The hop cycle at transmitter ID 1 predicted a period of
  51 × 2.5625 s = 130.6875 s. M25 measured 130.69 s, and the operator then read ID 1 on the
  console. No Davis packet has been decoded.
- **Its transmitter sits at `weather-island`, on the line between the bridge and the gate.**
  Both ends of the link hear it.

**At 917.4 MHz it costs about 6.7 ms every 130.69 s.** At the bench, where the wanted signal is
−37 dBm, a burst near −75 dBm does not matter. At the gate, where the wanted signal is about
−100 dBm, a burst that overlaps an uplink frame likely loses it. The bridge engineering log
estimates that at about 0.85 % of maximum-length SF9 frames, from timing alone; no loss rate
has been measured at the gate.

**It also bears on D33.** Standing condition 3 requires that the survey find no co-channel
occupant on the chosen frequency, and the Davis is one. Moving off its hop is the direct answer.
Whether D33 reopens is the operator's decision, recorded in the register.

**The detail is in three places:** the bridge engineering log's 2026-09-18 entries, and
[`LRAN-Site-RF-Inventory`](./LRAN-Site-RF-Inventory.md) §6 and §9.

## 2. What stays fixed

**Only the frequency moves.** Everything else D1 fixed was chosen for reasons the Davis does not
touch, so this brief does not reopen it:

| Parameter | Stays | Why it is unaffected |
|---|---|---|
| Rule section and bandwidth | §15.249 Envelope A, BW 125 kHz | The §15.249 limit is the same across 902–928 MHz |
| SF / CR | SF9, CR 4/5 | Chosen on B1b's link margin, which does not change over 0.2 MHz |
| TX power | −4 dBm conducted, 3.0 dBi antenna | The EIRP ceiling does not depend on frequency within the band |
| Airtime and `backoff_max_ms` | §15.1's table; 1500 ms | Airtime depends on SF, BW, CR and length, not on frequency |
| Sync word | `0x12` | Unrelated |

**The frequency is a radio setting, not a frame field.** So `ver` stays 2, and the W4 vectors
do not regenerate.

## 3. The candidates

**Spec §12.1 already bounds the search.** Envelope B is confined to 903.0–914.2 MHz, and
923.3–927.5 MHz is LoRaWAN US915 downlink, which leaves roughly 915.2–923.0 MHz as uncommitted
under Envelope A. Within it, the Davis leaves a gap about 0.5 MHz wide between each pair of
hops. Around 917 MHz its hops are 916.934, 917.434 and 917.936 MHz.

**The Davis may run below its nominal frequencies.** One published measurement of a Davis
transmitter found it 33.1 kHz low at 22 °C and 26.7 kHz low at 11 °C, with ±9.9 kHz deviation
and about 39 kHz occupied. That is one unit, not this one. The table gives the clearance both
ways.

| Candidate | Clearance to nearest Davis hop, nominal / 33 kHz low | M20 worst peak, all seven / deployed sites | Offset from Z-Wave 916.0 MHz | Verdict |
|---|---|---|---|---|
| 917.4 MHz (today) | 34 kHz / 1 kHz | −110 / −111 dBm | 1.4 MHz | **On a Davis hop** |
| **917.2 MHz** | **234 kHz / 199 kHz** | **−109 / −111 dBm** | 1.2 MHz | **Recommended** |
| 917.6 MHz | 166 kHz / 199 kHz | −105 / −112 dBm | 1.6 MHz | **The alternative** |
| 927.7 MHz | 230 kHz / 263 kHz | −106 / −106 dBm | — | Rejected, §3.1 |
| 902.2 MHz | 182 kHz / 149 kHz | −100 / −100 dBm | — | Rejected, §3.1 |

**Both 917.2 and 917.6 MHz were in D1's original ranking.** Decision Register §5.4 recommended
917.2–917.6 MHz and picked 917.4 as the best of three close scores; the D1 brief called the
difference "inside the survey's resolution". The survey peaks here come from
`docs/rangetest/data/2026-09-05-survey-campaign-r11.csv`.

**Each candidate's 125 kHz channel coincides exactly with one survey bin.** The survey measured
125 kHz every 200 kHz on centres such as 917.2 and 917.6 MHz, so both candidates' peaks were
measured across their whole channel. A centre off that grid, such as the exact midpoint of the
Davis gap at 917.18 MHz, would put part of the channel in an unsurveyed gap.

### 3.1 Why not a band edge

**Clear of every Davis channel only means the two band edges, and both are worse.**

- **Above the Davis, 927.53–928.0 MHz**, is LoRaWAN downlink channel 7, 927.5 MHz at 500 kHz
  wide, which covers up to 927.75 MHz. A channel clear of it sits within 190 kHz of the 928 MHz
  band edge, which reopens §15.249(d)'s out-of-band limit and Protocol Spec §18.2's analysis.
  Licensed services, paging included, begin above 928 MHz, and the survey stopped at 927.8 MHz.
  **It also gains little against the Davis**: its nominal clearance, 230 kHz, is no wider than 917.2 MHz's.
- **Below the Davis, 902.0–902.38 MHz**, overlaps LoRaWAN uplink channel 0 at 902.3 MHz. The
  survey saw −100 dBm in-channel and −80 dBm at 902.8 MHz.

## 4. What separates 917.2 from 917.6

**The survey and the nominal Davis clearance favour 917.2 MHz.** Its worst peak across all seven
sites is 4 dB lower, and its nominal clearance is 68 kHz wider. If the Davis runs 33 kHz low, the
clearances become equal. On deployed sites alone, 917.6 is better by 1 dB, which is inside the
survey's resolution.

**Z-Wave favours 917.6 MHz, and that is the untested question.** Z-Wave's 100 kbps channel is
916.00 MHz. M20 measured −54 dBm there at the bridge's own location, the loudest signal in its
campaign; the site RF inventory, §8, attributes it to Z-Wave rather than YoLink. 917.2 MHz is
1.2 MHz from it, 917.4 MHz is 1.4 MHz and 917.6 MHz is 1.6 MHz.

**One hypothesis would make that matter, and it is not established.** M25 found a second source
at 917.4 MHz, near −93 dBm, arriving in episodes lasting up to tens of seconds; nothing in the
inventory accounts for it. Z-Wave mesh traffic could arrive in episodes like that, though no
source used here describes its traffic pattern. If Z-Wave energy at 916.0 MHz reaches the
917.4 MHz reading about 39 dB down, that source could be Z-Wave, and 917.2 MHz would see more
of it. Two facts bound the hypothesis without settling it:

- **The receiver's rejection has one data point.** The M25 capture shows no trace of the Davis
  hops 0.5 MHz either side of 917.4 MHz, so rejection at that offset is at least 35 dB. Rejection
  at 1.2–1.6 MHz has not been measured here.
- **The episodic source could equally be a device nobody has inventoried.**

**A capture at each candidate over the same hours tests it directly.** If the episodic source
is Z-Wave, it should read stronger at 917.2 MHz than at 917.4, and weaker at 917.6. The Z-Wave controller's own frame
log, time-aligned with a capture, would confirm it.

## 5. How to decide

**Capture each candidate beside 917.4 MHz over the same hours, one receiver per frequency.** Captures taken
one after another cannot separate frequency from time of day. M25's occupancy at 917.4 MHz
moved almost five-fold between hours. The first 917.2 MHz capture ran from 15:14 to 01:14 UTC
and M25 from 03:44 to 13:43 UTC, so the two shared no hour. That capture found a source near
−89 dBm from about 18:00 to 23:50 UTC, and M25 never listened at those hours. The bridge
engineering log's 2026-09-18 entry has the figures.

### 5.1 The receivers

**Run `firmware/chan-capture/` on every receiver.** It is a listen-only image: it runs the
bridge's sampler, writes the same `CHAN`, `CHANSUM` and `CHAN-BOOT` lines, and never
transmits, so receivers a metre apart stay out of each other's captures.
`tools/checks/chan_capture_never_transmits.py` holds it to that in CI. `freq <hz>` on its
serial console stores a frequency and reboots, and every boot states the frequency in
`CHAN-BOOT`.

**Two Heltec V3s run it: the bridge board and the simnode Heltec**, the operator's choice on
2026-09-19. The XIAO with the Wio-SX1262 Kit reads its floor at −110 dBm, 4 dB above the
Heltecs, so against the firmware's fixed −110 dBm threshold it counts every sample as
occupied. The bridge board runs this image for the run, not a capture-only bridge image,
because a bridge polls. **Power nothing else up that transmits on LRAN's PHY.** Record each receiver's board,
enclosure, antenna and position in the engineering log, because the capture file carries none
of them.

**Receivers do not read alike, and occupancy cannot be corrected afterwards.** The firmware
counts samples above a fixed −110 dBm and keeps only each second's peak. A receiver that reads
2 dB hot counts more occupancy than its neighbours, and no analysis of the file can take that
back. The calibration hour below measures that bias, and the rotation removes it.

### 5.2 The run

1. **Calibrate: one hour with both receivers on 917.4 MHz**, started together. Both hear the
   same Davis hop every 130.69 s, so the calibration has a common source as well as a common
   floor. For each receiver, record its median floor, its median Davis peak and its
   occupancy.
2. **Capture for 24 hours with one receiver on 917.4 MHz and the other on a candidate.**
   917.4 MHz is the same-run baseline both tests compare against. A full day puts every hour
   in every file, so the start time does not matter. **917.2 MHz goes first**, because the
   evening source was found there; **917.6 MHz follows** on a second day, after its own
   calibration hour.
3. **Swap the receivers if the calibration says to.** Put each receiver on the other's
   frequency for the next day if the calibration hour shows their floors or Davis peaks more
   than 1 dB apart, or their occupancies more than 20 % apart. Either spread is as large as the
   differences tests 2 and 3 judge.
4. **Read each file with `tools/simctl/rssi_report.py`**, and compare the three hour by hour.
   `rssi_report.py` does not yet apply a receiver's offset; until it does, state each
   receiver's calibration beside every figure compared across receivers.

### 5.3 The tests

1. **Accept 917.2 MHz if all three hold over the same hours:**
   - no periodic source at 130.69 s, or at any other period;
   - occupancy above −110 dBm no higher than 917.4 MHz's in the same run, hour by hour;
   - no episodic source stronger than at 917.4 MHz in the same hours. That covers both the
     −93 dBm source M25 found and the −89 dBm evening source the first 917.2 MHz capture found.
2. **Prefer 917.6 MHz instead if it passes the first two tests and its episodic sources read
   clearly weaker than at 917.2 MHz.** That is the result §4's hypothesis predicts. If neither
   candidate passes, D1 needs a wider search than this brief covers.

**A source that reads the same on all three frequencies does not separate them.** A broadband
or nearby source would do that, and it would bear on the gate link at every candidate. It
would still need identifying, and it would not decide D1.

**The single-channel captures stand as records.** The first 917.2 MHz capture and the
917.6 MHz capture started on 2026-09-19 are correct for their hours. The 917.6 MHz capture
covers M25's hours, so it can be read against M25 directly.

## 6. What moves with the decision

**If the operator accepts a new frequency, these change together:**

1. **Decision Register.** D1's frequency and §3.4's table. D33 standing condition 3's status.
   The M25 and M26 rows, §3.1's inventory, and §5.4's attribution of the 916.0 MHz cluster, all
   raised by PR #78.
2. **Protocol Specification §12.1**, the frequency row and its fixed-in-v0.10 note, as a
   revision to v0.13. **`ver` stays 2.** Every binding citation then moves to v0.13;
   `python3 tools/checks/spec_citation_version.py` finds them.
3. **One constant and its tests:**
   - `kPhy.freq_hz` in `lib/lran-link/include/lran/link/radio_config.h`;
   - the assertions in `firmware/bridge/test/test_lora/test_lora.cpp` and
     `firmware/simnode/test/test_identity/test_identity.cpp`;
   - the PHY line of the bridge's boot banner in `firmware/bridge/src/main.cpp`;
   - the comments naming 917.4 MHz in `lora_link.cpp` and `tools/simctl/rssi_analyze.py`.

   `firmware/chan-capture/` samples `kPhy.freq_hz` when nothing is stored, and needs no change.
4. **Three boards.** The bridge over USB or OTA, and both simnodes over USB. Every board must
   move together, because one SX1262 listens on one frequency.
5. **Documents that state the current PHY.** Seventeen files outside the archive and the dated
   records mentioned 917.4 MHz when this brief was written, not all of them as the current PHY;
   `grep -rln "917\.4" docs CLAUDE.md firmware/*/CLAUDE.md` lists them. **Dated records stay as written**: engineering-log entries, run data and M25's
   capture are correct for 917.4 MHz.
6. **This brief** is marked superseded.

**The range-test firmware is separate.** It still transmits on the provisional 915.0 MHz, which
the register already flags, and this change does not touch it.

## 7. What this does not fix

- **The bench losses at one metre.** M25 found nothing loud enough to cause them, and the Davis
  at −75 dBm is 38 dB below the bench's wanted signal. The interleaved spacing sweep is still
  the next measurement for those.
- **M25's episodic source**, unless §4's hypothesis holds and 917.6 MHz is chosen.
- **The missing link measurement.** No link has been measured at 917.4 MHz; every walk ran at
  915.0 MHz. A 0.2 MHz move does not change propagation, and it does not supply that
  measurement either.
- **Future neighbours.** A Z-Wave Long Range controller would add 920 MHz at up to +14 dBm,
  2.8 MHz from 917.2 MHz. A second Davis transmitter at another ID hops the same table, so the
  917.2 MHz gap stays clear of it.

## Changelog

| Version | Date | Change |
|---|---|---|
| **v0.1** | 2026-09-18 | Initial release. Written because M25 and the site RF inventory identified the Davis Vantage Pro2 on a hop channel inside 917.4 MHz's receive bandwidth |
| **v0.2** | 2026-09-19 | §5 rewritten for same-hour captures on listen-only receivers (`firmware/chan-capture/`): two Heltecs, one on 917.4 MHz as the baseline and one on a candidate each day, with a calibration hour and a conditional swap. The operator chose Heltecs only after the XIAO's floor read −110 dBm. The first 917.2 MHz capture shared no hour with M25 and found an evening source M25's hours could not show. §5.3's occupancy baseline is 917.4 MHz in the same run, not M25's 0.0912 %. The Recommendation and §4 name the same-hours condition; §6 drops `chan_monitor.h`, whose comment no longer names 917.4 MHz |
