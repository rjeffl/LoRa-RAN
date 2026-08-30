# Decision register — entries to add

Paste into `LRAN-Decision-Register`. Renumber `D32`/`D33` if the register has moved past
`D31` since I last saw it. `D1`'s existing entry is amended rather than replaced.

---

## D32 — SX1262 driver library: RadioLib

**Decided:** 2026-08-30
**Status:** Settled
**Affects:** every firmware in the repo — bridge, GateLink, WellLink, simnode, range test

RadioLib is the SX1262 driver for all LRAN firmware.

**Why.** It covers both target modules — the Heltec V3's internal SX1262 and the
Wio-SX1262 on the XIAO and GateLink carriers — from one API, which is what §12.2's
injected pin map requires in order to mean anything. It exposes Channel Activity
Detection directly, which §12.3 depends on. And it does not drag in a vendor board
package, so the same driver serves a board whose pins are fixed by the vendor and one
whose pins are chosen on a carrier PCB.

**Rejected:** Heltec's own library — convenient on one board, useless on the StamPLC
carrier, and it would have forced a second driver for GateLink. A raw Semtech HAL —
more control than this project needs and CAD, calibration and TCXO handling to write
from scratch.

**Consequences.** Pin the RadioLib version in every `platformio.ini`; a driver shared by
four firmwares is not a thing to let float. The two settings that fail silently on the
Heltec V3 — TCXO reference voltage and DIO2-as-RF-switch — belong in the injected board
config from the first commit, not discovered per firmware.

---

## D33 — Fixed channel at Part 15.249 power

**Decided:** 2026-08-30
**Status:** Settled, with standing conditions
**Closes:** W5
**Affects:** §12.1, §12.3, and it bounds D1

A single fixed channel, no frequency hopping, transmitting at or below the Part 15.249
power provisions.

**Why.** Full reasoning and the link budget are in Protocol Specification v0.6 §18.1.
In short: 15.247 digital-modulation operation wants ≥500 kHz occupied bandwidth, which
LoRa at BW 125 kHz on a fixed channel does not meet — hence LoRaWAN's hopping. The
15.249 provisions permit roughly 0.75 mW EIRP (~−1 dBm), and at that ceiling the link
has about **48 dB of margin at SF7 over 150 m**, before spending anything on spreading
factor. Vegetation and terrain cannot plausibly consume that. Hopping would buy
robustness this link does not need at the price of a materially more complex bridge.

**Standing conditions — losing any one reopens this:**

1. TX power stays at or below the 15.249 ceiling. This is **EIRP**: conducted power plus
   antenna gain. With a 2 dBi antenna the conducted figure is around −3 dBm. Record
   conducted power and antenna gain separately or the number cannot be audited.
2. Aggregate channel occupancy stays low — a handful of nodes at status cadence.
3. The ambient survey finds no co-channel occupant on the chosen frequency.

**Not a compliance determination.** Confirm the radio modules' own FCC grant conditions
— antenna type and gain, and what each grant assumes about power and hopping — before D1
fixes a number.

**On the site's existing 915 MHz equipment** (four YoLink temperature sensors and a
switch, on a YoLink hub, all inside the dwelling): this has **no bearing on the operating
mode**. Part 15 compliance is per device; a certified product nearby establishes that
*some* compliant mode exists, not that this one is it. It bears on channel selection and
CAD tuning instead — see D1 and §12.3.

---

## D1 — amendment (channel, SF, BW, CR, TX power)

**Status:** still open, now bounded

D1 remains open pending range test results, but is no longer unbounded:

- **TX power is capped** by D33 at the 15.249 EIRP ceiling. The range test sweep starts
  at the bottom of the SX1262's range and climbs only if the link fails. A working point
  chosen at a power that cannot be used is a result that has to be thrown away.
- **The frequency requires an ambient survey first.** Per §12.1, D1 shall not fix a
  frequency until an RSSI sweep of 902–928 MHz has been run at both the bridge location
  and the most distant node location. The two do not see the same picture, and it is the
  node's noise floor that sets its margin.
- **The site has known occupants.** Four YoLink temperature sensors plus a switch talk to
  a YoLink hub inside the dwelling. YoLink uses LoRa at 915 MHz and a hub teardown found
  a Semtech SX1276, so this is real CSS modulation and CAD will see it. Whether it is
  LoRaWAN-band-plan or proprietary is **unconfirmed** — coverage describes it as
  LoRaWAN, but a consumer star network to a vendor hub is more likely proprietary. The
  survey settles it empirically, which is better evidence than a datasheet either way.
- **W7 still follows.** The airtime table regenerates once SF/BW/CR are fixed.

**Do not let range test results close D1 on their own.** The frequency needs the survey,
and the power needs the grant conditions from D33.
