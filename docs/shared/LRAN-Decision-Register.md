# LRAN Decision Register

**Document:** `LRAN-Decision-Register`
**Version:** 0.14
**Status:** Living document. Updated whenever a decision changes state.
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Last updated:** 2026-09-20

> **This is the only place a decision's status is recorded.** Every other document in
> the set references decisions by number and describes the *outcome* where it is
> relevant, never the status. When a decision closes, this file is the one that
> changes.

---

## Table of contents

1. [How to use this register](#1-how-to-use-this-register)
2. [Open decisions](#2-open-decisions)
3. [Resolved decisions](#3-resolved-decisions)
4. [Retired decisions](#4-retired-decisions)
5. [Measurement backlog](#5-measurement-backlog)
6. [Changelog](#6-changelog)

---

## 1. How to use this register

| Field | Meaning |
|---|---|
| **#** | Permanent identifier. **Numbers are never reused**, even after a decision is retired — a retired D-number appearing in an old log entry must still resolve to the right thing |
| **Owner** | The document where the outcome is written up |
| **Status** | `open`, `resolved`, or `retired` |
| **Gate** | What the decision blocks, or the bring-up phase by which it must close |

**Statuses.** `resolved` means a choice was made and the consequences are written into
the owning document. `retired` means the question stopped applying — usually because a
design change removed the thing it was about. A retired decision is not a decision that
was answered; it is one that no longer needs answering, and the distinction matters when
reading old material.

**Adding a decision.** New numbers continue from the highest issued, currently **D57** for
decisions and **M26** for measurement-backlog items.
A decision belongs here rather than in a node document when its answer would change more
than one section, or when it is blocking work.

---

## 2. Open decisions

| # | Decision | Owner | Notes | Gate |
|---|---|---|---|---|
| **D19** | **WellLink power source** | WellLink PRD | Mains vs. battery/solar. Determines whether the reserved RX duty-cycling design (Protocol Spec §17.1) is needed, and whether battery telemetry is required in the WellLink schema | Before WellLink design |
| **D25** | **VE.Direct TX translator** | GateLink Impl Plan | BSS138 retained by default but may fail against a weak symmetric 5 V driver. Settled by **one measurement**: 10 kΩ from the MPPT TX pin to GND with the port streaming, observe the low excursions. Fallback ADuM1201 or 74LVC1G17. **The BSS138 stays on the RX direction either way** | Before carrier build |
| **D28** | **BLE link margin from the StamPLC mounting position** | GateLink Impl Plan | The Stamp-S3A's 2.4 GHz antenna is internal to the DIN case with no external option, and the pack's own transmitter is weak (~−80 dBm from inches away, confirmed independently with a phone — this is the battery, not the test hardware). Measure RSSI from the intended mounting position. Fallbacks: SmartShunt, or the D30 co-processor. **Amended 2026-09-06 — see §2.2. Still open, but the margin looks considerably better than this row's premise** | Phase 5 |
| **D29** | **Enclosure thermal envelope** | GateLink Impl Plan | **Narrowed to the high end.** Cold exposure affects no functional dependency; summer solar gain in a closed box is cumulative and does. Instrument LM75 + MPPT + BMS, verify the existing screened vents, add shade, and fit a thermostatic fan **only if logged maxima justify it** | Phase 9 / ongoing |

### 2.1 D1 — what bounds it (2026-08-30)

> **D1 closed on 2026-09-10 and D33 closed with it. §3.4 records what was chosen.** This
> section and §2.2 are the dated account of what bounded the decision, and they keep their
> numbers because documents across the set cite them. They are left as written; read §3.4
> for the outcome.

D1 remains open pending range test results, but it is no longer unbounded. Three
constraints now apply, and **range test results do not close D1 on their own**:

- **TX power is capped** by **D33** at the §15.249 EIRP ceiling. The range test sweep
  starts at the bottom of the SX1262's range and climbs only if the link fails. A
  working point chosen at a power that cannot be used is a result that has to be
  thrown away.
- **The frequency requires an ambient survey first.** Per Protocol Spec §12.1, D1 shall
  not fix a frequency until an RSSI sweep of 902–928 MHz has been run at **both** the
  bridge location and the most distant node location (**M20**). The two do not see the
  same picture, and it is the node's noise floor that sets its margin.
- **The site has known occupants.** Four YoLink temperature sensors plus a switch talk
  to a YoLink hub inside the dwelling. YoLink uses LoRa at 915 MHz and a hub teardown
  found a Semtech SX1276, so this is real CSS modulation and CAD will see it. Whether
  it is LoRaWAN band-plan or proprietary is **unconfirmed** — a consumer star network
  to a vendor hub is more likely proprietary. M20 settles it empirically, which is
  better evidence than a datasheet either way.
- **The power figure also needs the grant conditions** recorded under D33.
- **The bandwidth and the rule section are one decision** *(fourth bound, added
  2026-09-06 by M21)*. A single fixed channel with no hopping exists inside the modules'
  grants **only at BW500**, within 903.0–914.2 MHz (§15.247 DTS). BW125 forces the
  §15.249 envelope and its ≈ −1.2 dBm EIRP ceiling — which the walk shows is sufficient,
  so **Envelope A is the plan of record**. **D1 shall not fix `BW` without fixing the
  envelope in the same motion.**

**W7 still follows.** The airtime table regenerates once SF/BW/CR are fixed (**M19**).

### 2.2 D1 and D28 — what M21 changed, 2026-09-06

Recorded here rather than by editing the rows above, because the rows are the *current*
state and this is the *dated* account of how they got there.

**The frequency now has a survey behind it, and one candidate is ruled out.** M20's field
work closed on 2026-09-05 (§5.1). `weather-island` shows the one confirmed in-channel
occupant at **−80 dBm on 915.0 MHz** against a −115 to −118 dBm floor; `gatelink-gate`
carries the strongest near-band neighbour at **−66 dBm on 914.0 MHz**, at the site GateLink
will occupy. Both bursty, no carrier anywhere in 902–928. **The range-test firmware's
provisional 915.0 MHz is the confirmed occupant's own peak and D1 must move off it.**
Note also that 923.3–927.5 MHz is LoRaWAN US915 *downlink*, so Envelope A's genuinely
uncommitted region is roughly **915.2–923.0 MHz**.

**The power figure is bounded, and it was measured at the ceiling rather than modelled.**
The fitted antenna is confirmed as a 19 cm stick claiming **3.0 dBi**, vertical, the same
part at both ends, so the conducted ceiling is **−4 dBm**. The 2026-09-04 walk ran at
exactly that and closed at 0 % PER across all six positions; the same walk at −9 dBm — the
SX1262's hard floor — showed 12.5–25 % PER at SF7. **A working point below −4 dBm is not
available**: it is both unnecessary and, at −9 dBm, measurably worse.

**The SF question the survey did not answer.** SF9 at −9 dBm was clean at all six positions
where SF7 was not, but SF9 costs a `backoff_max_ms` raise above its 1107 ms full-frame
airtime (Protocol Spec §12.3). SF7 at the −4 dBm ceiling was also clean. Both live.

**B1b added a constraint on 2026-09-09, and it pulls against W9's.** At the gate, all three
spreading factors closed at 0 % PER at both −9 and −4 dBm, so PER does not choose between
them. The **fade tail** does. Mean SNR margins at the gate were 17.1 dB at SF7, 20.5 at SF9
and 24.7 at SF12, but the **worst single SF7 probe reached −5.3 dB SNR — a 2.2 dB margin —
at −119.0 dBm**, against 14.8 dB for the worst SF9 probe. Per-test-point RSSI spread at the
gate is 15 dB. So SF7 is the SF that keeps §12.3's defaults valid as written *and* the SF
whose margin at the gate occasionally approaches zero; SF9 buys about 13 dB of tail margin
for a `backoff_max_ms` raise. **Neither fact settles it and both belong in the decision.**
Trace `2026-09-09-b1b-walk-gate.csv`, second sweep.

**D28's premise has improved, and its own recorded figure stays as written.** The GateLink
node sits in a plastic enclosure **inside** the steel gate-controller enclosure — which
also contains the pack and its BMS, 6–8 in away. The BLE link therefore never crosses a
metal wall; both ends are inside the same cavity. A reading of **−50 to −60 dBm** was taken
in that enclosure with a Heltec V3, 20–30 dB better than D28's row records. That
discrepancy is **not silently reconciled**: candidate explanations are a different
measurement position, a standing-wave null in the cavity, pack state, or the earlier
reading having been taken outside the box. **D28's −80 dBm is a dated observation and is
left standing**; this entry supersedes its *outlook*, not its record. D28 closes on
**M23**, which is now a cavity-position and antenna question rather than a survival one.

---

## 3. Resolved decisions

| # | Decision | Outcome | Owner |
|---|---|---|---|
| **D3** | Detector and movement-cause coverage: bus vs. discrete I/O | **Discrete inputs.** The detectors are tapped directly; movement cause is derived locally from input timing | GateLink PRD |
| **D4** | Poll scheduler location | **Bridge firmware**, per node, runtime-configurable from an HA `number` entity | Bridge PRD |
| **D5** | MQTT client library | **`MqttTransport` abstraction, PubSubClient first.** `MQTT_MAX_PACKET_SIZE` ≥ 1024 — the default 256 is smaller than a Discovery config and fails confusingly | Bridge Impl Plan |
| **D6** | Gate-node display trigger | **Button toggle + automatic on in any debug mode**, with an inactivity timeout on the manual path only | GateLink PRD |
| **D7** | BusT4 VCC handling | **Superseded** — the BusT4 port is not connected in v1. Its VCC pin carries 24 V and stays untouched | Research Archive |
| **D8** | HA entity modeling | **`cover` (device_class `gate`) as the primary control surface, plus a "Hold gate open" `switch`.** Position reporting deferred | GateLink PRD |
| **D11** | Project license | **MIT.** The remaining stack imposes no copyleft once BusT4 left v1. Holder name closed as **D31** | System PRD §11.2 |
| **D12** | VE.Direct isolation vs. level shifting | **No isolation needed.** Single enclosure, short leads, worst-case ground offset ~16–40 mV against a 5 V threshold. Level-shifter choice tracked separately as **D25** | GateLink Impl Plan |
| **D13** | BusT4 physical layer | **Differential.** Measured: open to ground on both data pins, 145–174 Ω between them — a terminated differential pair. **Research archive only** | Research Archive |
| **D14** | Decode placement | **Moot.** The node reads discrete inputs and drives discrete relays; there is nothing to decode | System PRD §3.3 |
| **D15** | Battery SOC source | **BLE BMS.** The pack is a **TDT** unit; the access sequence is documented and an independent client validated over 32 consecutive polls with zero CRC failures. SmartShunt demoted to a physical-layer contingency behind **D28** | GateLink PRD |
| **D16** | OTA policy | **Bridge yes, remote nodes no.** The bridge is on the LAN, mains powered and physically accessible; a bad flash ~87 m away is a walk with a laptop and there is no second radio path to recover through | Bridge PRD |
| **D17** | Naming | **LRAN umbrella; `lran/` MQTT root; GateLink / WellLink / Bridge Node.** *Amended 2026-09-10:* the bridge was recorded here as **LoRaBridge**, the name it carried before its own documents settled on **Bridge Node**; both ran side by side across the set until an audit found them. `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device name** — a user-visible string, not a second node name. The one instance deferred then, Protocol Spec §5.3's node-table gloss, was changed with the next substantive revision: **spec v0.10 reads `(Bridge Node)`** | System PRD §1.3 |
| **D18** | Auto-close observability | **Resolved, and better than expected.** `OUT = Moving` stays energized through the auto-close countdown, so auto-close state *is* observable — and hold state falls out of it for free | GateLink PRD |
| **D20** | Gate controller standby policy | **Standby retained**, timeout measured at **60 s**. Command relays wake the board on their own, so nothing is lost by keeping it | GateLink PRD |
| **D21** | Wake mechanism | **None needed.** Every command relay drives a command-class input, so the pulse that carries the command is also the pulse that wakes the board | GateLink PRD |
| **D22** | Hold-open mechanism | **OPEN+LOCK / UNLOCK**, confirmed on the bench. Replicates the mechanism the installation already uses | GateLink PRD |
| **D23** | OUT1/OUT2 sense polarity | **No inversion.** An energized OUT relay *prevents* the board entering standby, so an asserted output is always valid. Use normally-open contacts, OUT1 = OPEN, OUT2 = MOVING | GateLink PRD |
| **D24** | Manual UNLOCK path | **Two independent paths**: the handheld remote already programmed with UNLOCK, and the control-panel pushbutton rewired to AUX2 | GateLink PRD |
| **D26** | StamPLC 3.3 V rail | **No 3.3 V rail is exposed.** Bus power pins are VIN, GND and EXT_5V only, and EXT_5V sits near 4.76 V under load. The carrier LDO stays in the BOM, and an AMS1117 is excluded on dropout | GateLink Impl Plan |
| **D27** | Carrier board fabrication | **Perfboard populated with prefabricated modules**; regulator and discretes mounted directly. Preserves the "no hand-built discrete circuits" property. Remaining sub-item: pick a DIN-rail carrier and cut the board to it | GateLink Impl Plan |
| **D31** | Copyright holder name | **Robert J. Lee**, a personal name rather than a project or entity name. `LICENSE` now exists at the repo root carrying the MIT text and `Copyright (c) 2026 Robert J. Lee`, and the 87 source files that carried the `<holder>` placeholder carry the name. **The first public push is no longer blocked by this**; §11.3's separate `THIRD_PARTY_NOTICES.md` obligation was written the same day | System PRD §11.2 |
| **D30** | LoRa/BLE co-processor | **Not adopted.** A direct SX1262 on the carrier is the plan of record. The Heltec-class co-processor is retained as a documented fallback with three explicit triggers | GateLink Impl Plan |
| **D32** | SX1262 driver library | **RadioLib**, for every firmware in the repo — bridge, GateLink, WellLink, simnode, range test. One API across the Heltec V3's internal SX1262 and the Wio-SX1262 on the XIAO and GateLink carriers, direct CAD access, no vendor board package. See §3.1 | System PRD §11.1 |
| **D1** | **LoRa PHY parameters** — SF / BW / CR / frequency / TX power | **SF9, BW 125 kHz, CR 4/5, 917.4 MHz, −4 dBm conducted with the fitted 3.0 dBi antenna**, inside D33's Envelope A. Closed **2026-09-10** on the evidence assembled in `LRAN-D1-PHY-Decision-Brief`, which is superseded by this row. **`backoff_max_ms` rises to 1500** as the one configuration change SF9 forces. *Confirmed 2026-09-20:* the proposed move to 917.2 MHz is **declined** on 24 hours of parallel measurement, and every parameter in this row stands unchanged. `LRAN-D1-Frequency-Change-Brief` is superseded. See §3.4 and §3.4.1 | System PRD §5.1, Protocol Spec §12.1 |
| **D33** | FCC Part 15 operating mode (**closes W5**) | **Envelope A — §15.249, single fixed channel, no hopping, BW 125 kHz, −4 dBm conducted with a 3.0 dBi antenna.** Reopened 2026-09-06 by M21 and **closed again 2026-09-10 in the same motion as D1**, because §2.1's fourth bound makes `BW` and the rule section one decision. Envelope B (§15.247 DTS, BW500, 903.0–914.2 MHz) is retained as a documented fallback behind its three triggers. Permanently: the modules' grants **do not transfer**, so the operative frame is **§15.23 home-built** and **no node may be represented as FCC certified**. *Standing condition 3 answered 2026-09-20, and not as it was written:* 917.4 MHz **does** carry a co-channel occupant, the property's Davis station, and **D33 is not reopened** because that occupant is now characterised and its airtime is negligible. The condition is restated in §3.4.1 so it can be tested again. Reasoning in §3.1 (2026-08-30), §3.3 (reopened), §3.4 (closed) and §3.4.1 (condition 3) | Protocol Spec §18.1, §18.2 |
| **D34** | Home for Protocol Spec §9.4 steps 4–6 (**closes W12**) | **Split, not placed whole.** Steps 4, 5 and the state half of 6 become `lran::CommandGate` in `/lib/lran-protocol/` — one per peer, immediately after `Reassembler`. The **dispatch** half of step 6 stays in the application. The gate returns a verdict; the caller decides. See §3.2. *Amended 2026-09-11:* the high-water mark advances in `check()`, **before** dispatch, as spec §9.4 step 6 orders it; the single-threaded-receiver precondition is withdrawn; a retry inside the execution window is **in flight** — counted in `rx_dup_command`, not answered; cache storage is 32 entries, **128 B per peer**. Split and placement unchanged. See §3.2.1 | Protocol Library Impl Plan §3, §6 (**P8**) |
| **D35** | A frame from a source the receiver holds no key for (**Protocol Spec §14**) | **New stage 9a, counter `rx_unknown_src`, counted into `rx_dropped`, never answered.** Placed after authentication so a forged `src` is still rejected by the MAC check first, and separate from `rx_not_addressed` because *not for me* and *I do not know you* lead an operator to different places. See §3.5 | Protocol Spec §14, §14.1 (**BF-15a**) |
| **D36** | How a `DUPLICATE_CACHED` `COMMAND_ACK` carries the cached result | **In `detail`**, which §6.3 already defines as result-specific. No wire change. The original answer's own `detail` is not reproduced, and §6.3 now says so rather than leaving it to be discovered. Codifies what simnode BF-6 shipped | Protocol Spec §6.3, §9.4 (**BF-18**) |
| **D37** | What a node answers to a repeated `CONFIG` | **The cached `CONFIG_ACK`, from the dedup cache**, exactly as for any authenticated type (§9.4 steps 4–6). Because the ACK carries *effective* values it is also a correct readback. Re-applying the set was rejected: it is safe only while every §8.10 `op` is idempotent, which nothing commits to | Protocol Spec §7.4 (**BF-18**, GateLink M3) |
| **D38** | `CONFIG` / `CONFIG_ACK` fragmentation, against §3.1's reassembly cap | **Single-frame in v1.** §3.1's cap and the single-frame payload cap are both 196 B, so fragmenting these types could never carry one byte more — v0.4 through v0.11 described an encoding no conforming sender could produce. A larger configuration is **several messages**, with no atomicity across them. Batching on §8.10's `op` is recorded as the v2 path. **W10 is rewritten** and is now a counting question, not a fragmentation one | Protocol Spec §7.4, §11.4, §11.5 |
| **D39** | Which sequence space a bridge-originated unauthenticated `seq` belongs to | **Neither. It is local and advisory**, advances no high-water mark, and MUST NOT be used to reject. `POLL` carries one so an answer can be matched to its poll | Protocol Spec §10.2, §6.4 (**BF-17**) |
| **D40** | Whether a discard made before the MAC check may be attributed to the `src` it names | **No.** A counter raised before stage 9 is the **receiver's own**; only stage 9 onward may be published per node. Before authentication `src` is a claim, and attributing those discards would let any transmitter move another node's counters and another node's Home Assistant history | Protocol Spec §14.1 (**BF-19**) |
| **D41** | Whether the bridge sends §14's `ERROR` replies, and how they are addressed | **Registered sources only, rate-limited.** `error_min_interval_ms` default **1000**, runtime-settable; `src` the sender's own, `ctx_id` **`0`** (unknown, §5.5), `ref_seq` the offending frame's. A receiver MUST NOT adopt a zero `ctx_id`. Answering any `src` was rejected as a reflection vector: a spoofed frame would make the receiver transmit at an attacker's chosen rate. `BAD_CRC` and `BAD_VERSION` stay optional. New **§14.2** | Protocol Spec §14.2 (**BF-19a**) |
| **D42** | Which §16.2 topics get a normative payload now | **`lran/bridge/version` and `lran/<node>/diag/state` only**, in new §16.2.1, because both ship today and Home Assistant breaks on a rename. **`config/set` and `config/ack` stay undefined** until they have a caller — neither has an implementation, a configuration library or an inbound path, and a payload specified before its first caller is a guess with a version number. *Amended 2026-09-19:* the `config/*` payloads now have their caller, and **D48** defines them | Protocol Spec §16.2.1 (**BF-13**, **BF-19**; **BF-26**, **BF-23** deferred) |
| **D43** | Route for runtime configuration from Home Assistant | **The general `lran/<node>/config/set`, with `/lib/lran-config/` behind it**, for the bridge's parameters and every node's. A narrow single-purpose topic was rejected because it spends an HA-visible token that cannot be renamed; a serial-only lever was rejected because it leaves root rule 8 unmet on nodes that cannot be reflashed without a walk. See §3.6 | System PRD §9.4, Protocol Spec §16.7 (**BF-32**) |
| **D44** | Whether the parameter table is generated or hand-written | **A hand-written C++ table is the one source**, and every other copy is derived from it by code: firmware defaults and HA `number` discovery read it directly, and a host tool writes `/docs/gatelink-config.md` for a check to diff, as `tools/ha/dump_discovery.cpp` does for `/ha/`. No generator, no YAML, and nothing maintained by hand against the header | Protocol Library Plan §4, System PRD §9.4, Protocol Spec §7.4 |
| **D45** | What answers `POLL` `poll_flags` bit 1 and `REQUEST_CONFIG` | **An unsolicited `CONFIG_ACK` with `op` = `GET_ALL`, with a `seq` from the node's status sequence space** (§10.2) — what simnode BF-6 already sends. It carries no MAC, for the reason `STATUS` carries none: a spoofed readback misreports configuration as a spoofed `STATUS` misreports state (§9.5). `CONFIG` `GET` and `GET_ALL` stay as the authenticated read. Retiring bit 1 and `REQUEST_CONFIG` was rejected: it breaks working simnode code and a published HA button, and costs an authenticated frame per readback | Protocol Spec §6.4, §7.4, §8.1, §9.2 |
| **D46** | Whether `param_id` is one namespace, and whose schema `0x12` is | **One namespace for the fleet, allocated in blocks per owner**: `0x0000`–`0x00FF` bridge, `0x0100`–`0x01FF` every node, `0x1000`–`0x1FFF` GateLink, `0x2000`–`0x2FFF` WellLink. Schema `0x12` keeps its value and becomes **node config v1**, carried by any node. No byte changes | Protocol Spec §7.1, §7.4 |
| **D47** | Where a parameter about a node is held | **Each parameter declares its owner**: the bridge, globally; the bridge, per node; or the node. The bridge applies its own half of a `config/set`, sends the node's half as `CONFIG`, and publishes one `config/ack` when both have an outcome. `config/state` shows the node's and the bridge's per-node values together, so HA sees one device with one configuration | Protocol Spec §16.7, Protocol Library Plan §4 |
| **D48** | The `config/set`, `config/ack` and `config/state` payloads (**closes D42's deferral**) | **One JSON topic per node, keyed by parameter name**, not `param_id`. HA `number` entities publish into it through a `command_template`. `config/ack` carries per-entry status and the **effective** value, and a new persistence outcome, **`unknown`**, for a `CONFIG` that got no `CONFIG_ACK`; the bridge resolves it by readback and publishes a second `config/ack` | Protocol Spec §16.7 (**BF-32**) |
| **D49** | What `persist_status` means on a node with no microSD slot | **The bridge persists to NVS.** §8.11's `APPLIED_NOT_PERSISTED` means *no usable nonvolatile store* — microSD on GateLink, NVS on the bridge — and the bridge reports it only when an NVS write fails | Protocol Spec §8.11, §16.6 |
| **D50** | How a `GET` entry, a `GET_ALL` and a `RESTORE_DEFAULTS` are encoded | **A `GET` entry carries the expected `ptype` and `len` = 0. `GET_ALL` and `RESTORE_DEFAULTS` carry `count` = 0**, and a receiver ignores any entries it finds in one. What simnode BF-6 already reads | Protocol Spec §7.4, §8.10 |
| **D51** | An entry whose `len` does not match its `ptype` | **Rejected with `TYPE_MISMATCH`; the rest of the set applies.** `len` delimits the entry, so the frame stays parseable. Discarding the frame was rejected: it would break §7.4's per-entry results. What simnode BF-6 already does | Protocol Spec §7.4 |
| **D52** | What answers `RESTORE_DEFAULTS` | **The full effective configuration, as for `GET_ALL`**, so the bridge can republish `config/state` without a second readback. Simnode BF-6 answers with no results and changes to match | Protocol Spec §7.4, §8.10 |
| **D53** | What `persist_status` means for a read, and when `NOT_APPLIED` applies | **After a write, what was applied; after a read, whether the current overrides are persisted** (`PERSISTED` when there are none). `NOT_APPLIED` means nothing was applied: an unknown `op`, or a `SET` whose every entry was rejected | Protocol Spec §7.4, §8.11 |
| **D54** | Whether a dedup hit repeats `REQUEST_STATUS`'s or `REQUEST_CONFIG`'s follow-up frame | **No. A dedup hit repeats the ACK and nothing else**; the bridge recovers a missing follow-up with `POLL` bit 0 or bit 1. What simnode BF-6 already does | Protocol Spec §10.4 |
| **D55** | What `len` means in a `CONFIG` entry | **The total number of value bytes, and a multiple of the `ptype`'s unit width** (1, 2 or 4). `len / width` is the number of units: 0 for a `GET`, 1 for a scalar, N for an array. **A string is `ptype` = `u8`** with `len` its length. An entry the receiver cannot take — a `len` that is not a multiple, a value too wide to store, an array where the parameter is a scalar — is rejected alone with `TYPE_MISMATCH` (**D51**) and never discards the frame, so a node built before a type existed still reads a set that uses it | Protocol Spec §7.4 (**BF-32**) |
| **D56** | Whether the LoRa PHY parameters are runtime-configurable | **Yes, under new §12.4's commit-and-revert**, reversing §12.1's *"out of scope for v1"*. Frequency, SF, BW, CR and TX power become `/lib/lran-config/` parameters, held per node; the sync word, header mode and CRC stay contractual. One atomic `CONFIG`, last known-good persisted first, `phy_trial_s` (default 120) from apply, confirmation is a frame **received** on the new settings, and both ends revert on silence. **TX power stays clamped by D33** and BW by the envelope coupling; widening either is a decision, not a configuration change. A node that has not built the path answers `READ_ONLY`. The operator's reasoning, 2026-09-19: *"within reason configurability (aka ability to adapt on the fly) proves more successful in the long run and minimizes recompile changes"* | Protocol Spec §12.1, §12.4, §8.12; Protocol Library Plan §4 (**BF-33**) |
| **D57** | How a node answers a `GET_ALL` too large for one frame | **Several `CONFIG_ACK` messages, every one but the last marked `MORE_FOLLOWS`** — bit 7 of `count`, whose top two bits are unreachable because 193 bytes of payload hold at most 32 results. Schema `0x12` keeps its layout and offsets. The node walks its table in ascending `param_id` across the answer, repeats `op` and `persist_status` on every message, and sends at most **4** messages. A solicited answer repeats the request's `seq`, so **the bridge accepts more than one `CONFIG_ACK` per `seq`** and closes on the message with `MORE_FOLLOWS` clear. A repeated `GET_ALL` is answered by walking the table again rather than from the dedup cache, because a read applies nothing. The bridge never publishes `config/state` from an answer that did not complete; it abandons one on `config_readback_timeout_ms` and requests another | Protocol Spec §7.4.1, §11.4, §16.7.4; Protocol Library Plan §4 (**BF-32**) |


### 3.1 Notes on D32 and D33

Both were settled on **2026-08-30**. Their outcomes are written up in the owning
documents; what follows is the part that has no other home.

> **D33 was reopened on 2026-09-06.** This subsection is left as written — it is the record
> of what was decided and why on 2026-08-30, and rewriting it would destroy the thing that
> makes it useful. **§3.3 records what changed.** Read them in that order.

**D32 — RadioLib.** Rejected alternatives: Heltec's own library, convenient on one
board, useless on the StamPLC carrier, and it would have forced a second driver for
GateLink; a raw Semtech HAL, more control than this project needs plus CAD, calibration
and TCXO handling to write from scratch.

*Consequences, and they are standing ones.* **Pin the RadioLib version in every
`platformio.ini`** — a driver shared by four firmwares is not a thing to let float. The
two settings that fail silently on the Heltec V3, **TCXO reference voltage** and
**DIO2-as-RF-switch**, belong in the injected board config (Protocol Spec §12.2) from
the first commit, not discovered per firmware.

**D33 — fixed channel at §15.249 power.** Status is *settled, with standing conditions*.
Losing any one of the three reopens it:

1. TX power stays at or below the 15.249 ceiling. This is **EIRP**: conducted power plus
   antenna gain. With a 2 dBi antenna the conducted figure is around −3 dBm. **Record
   conducted power and antenna gain separately** or the number cannot be audited.
2. Aggregate channel occupancy stays low — a handful of nodes at status cadence.
3. The ambient survey (**M20**) finds no co-channel occupant on the chosen frequency.

**This is not a compliance determination.** Confirm the radio modules' own FCC grant
conditions — antenna type and gain, and what each grant assumes about power and hopping
— **before D1 fixes a number**.

**On the site's existing 915 MHz equipment** (four YoLink temperature sensors and a
switch, on a YoLink hub, all inside the dwelling): this has **no bearing on the
operating mode**. Part 15 compliance is per device; a certified product nearby
establishes that *some* compliant mode exists, not that this one is it. It bears on
channel selection and CAD tuning instead — see §2.1 and Protocol Spec §12.3.

### 3.2 D34 — why a split, and why it lands in the protocol library

Settled **2026-08-31**, closing **W12**. The specification asked "where do steps 4–6
live" as one question; it is two, and asking it as one is why it stayed open.

**Steps 4, 5 and the high-water update in step 6 are validation against receiver
state** — the same logic on every side, no allocation, no I/O, clock injected.
**Dispatch is node behaviour** and stays in the application. `CommandGate::check()`
returns `Execute` / `ReturnCached` / `Reject`; it never executes anything.

*Why `/lib/lran-protocol/` rather than a new library or four applications.*

1. **`Counters` already owns `rx_rejected_seq` and `rx_dup_command`**, and
   `total_dropped()` already sums the first. Until the incrementer shares that
   `Counters` instance, schema `0xF0`'s `rx_dropped` under-reports on exactly the
   frames that move a gate. An incrementer in a different library from the counter
   struct recreates the split that produced the v0.5 naming drift.
2. **`Reassembler` is the precedent and settles the scope question**: per-peer,
   stateful, `now_ms` injected, no allocation, returns a `Status`. `CommandGate` is
   the same shape. If reassembly is in scope, this is.
3. **`Status` must gain `DuplicateCached` and `RejectedSeq`** so `Counters::bump()`
   stays the single mapping point — its `-Werror=switch` guard only works if the
   values are in the enum, which they cannot be from outside the library.
4. A separate `/lib/lran-rx/` would need `Frame`, `Status`, `Counters`, `AckResult`
   and `seq_newer`. A library whose entire surface is another library's types is a
   header with a build system attached.

**Rejected:** leaving it to each application. §10.4 is not advisory — a relay pulse is
not idempotent — and four independent implementations of a replay check is three too
many.

**Consequences.**

> **Three of these consequences were amended on 2026-09-11** — the precondition, the
> cost, and where the high-water mark advances. The list is left as written: it is the
> record of what was decided on 2026-08-31. **§3.2.1 records what changed.** Read them in
> that order.

- **`check` and `record` are two calls.** The cached value is the *result of
  execution*, so one call cannot produce it, and caching before execution would
  return a success ACK for a command that then failed.
- **`check → execute → record` must be atomic with respect to frame arrival**, and is
  recorded as a documented precondition in the manner of `Reassembler`'s monotonic
  clock rather than engineered around. A retry landing inside that window finds no
  cache entry *and* fails the `seq` check, answering `REJECTED_SEQ` where §10.4
  requires the cached ACK. Unreachable on a single-threaded receive loop, which is
  what both sides use. **§10.4 is silent on this window** — a specification gap
  recorded rather than patched locally.
- `dedup_cache_depth` (§10.4, default 8) becomes a `/lib/lran-config/` parameter,
  runtime-settable.
- Cost is **32 B per peer** — the gate holds one `ctx_id` and entries store
  `(seq, result, detail)`. 32 B on a node, 160 B on a five-node bridge.
  `reset_context()` clearing the cache then falls out for free, which is exactly
  §10.4's "lost on reboot, which is correct."

**The deadline is later than the specification implied, and this is the useful part.**
§9.2 makes **every authenticated type bridge → node** — `COMMAND`, `CONFIG` and
write-class `HEX_REQ`. Nodes emit only unauthenticated frames, so **on the bridge
steps 4–6 apply to an empty set today**. The deadline is therefore not "before the
second firmware is written" but **before the first firmware that accepts a
`COMMAND`**: simnode `ROLE_GATELINK` at **B0**, and GateLink **M3**. `ROLE_RANGE`
echoes unauthenticated `PING`, so **W12 does not block the range test firmware**.
Land `CommandGate` as library milestone **P8**, before B0.

### 3.2.1 D34 amended — the execution window, 2026-09-11

**D34 is amended, not reopened.** The split and the placement stand. Three consequences
change, on the recommendations of
[`LRAN-P8-CommandGate-Brief`](./LRAN-P8-CommandGate-Brief.md), which the operator
accepted in full on 2026-09-11 and which is now superseded.

**What was wrong.** §3.2's second consequence rested on a premise: no frame arrives
between `check()` and `record()`, because every receiver runs a single-threaded receive
loop. GateLink Impl Plan §5.2 contradicts it — frames arrive in `lora_task`, commands
dispatch in `app_task`, the relay pulse runs in `io_task`. The library plan compounded
it by having `record()` advance the high-water mark after execution, where spec §9.4
step 6 advances it before dispatch. Together they let a bridge retry arriving mid-pulse
pass step 4 (no cache entry yet) and step 5 (mark not moved) and **execute a second
time**. Spec §9.4 had named the falsifier — *revisit if a receiver ever dispatches
asynchronously* — and nothing tracked it.

**What was decided.**

| # | Question | Decision |
|---|---|---|
| 1 | Where the high-water mark advances | **In `check()`, when it returns `Execute`** — spec §9.4's own order. A command whose execution fails has still consumed its `seq`, and the failure is its cached result |
| 2 | What a retry inside the window receives | **Nothing.** `check()` holds the entry **in flight**; a retry finding it gets the new verdict `InFlight`, is counted in `rx_dup_command` and is not answered. The bridge's next retry receives the real result. Rejected: answering `REJECTED_SEQ`, which has the bridge report "rejected" for a command being carried out; and synchronous execution, which blocks `lora_task` for a pulse |
| 3 | Cache capacity against the 1–32 runtime depth | **32 entries of static storage, depth runtime-settable 1–32.** 128 B per peer — 640 B on a five-peer bridge. §3.2's "32 B per peer" was the default depth's cost, not the range's |
| 4 | Where the amendment is recorded | Here, in Protocol Spec **v0.11** §9.4 and §10.4 (**no wire change**), and in the library plan's §3.10 — in the same branch as P8's code |
| 5 | Sequencing | P8 on its own branch from `main`, independent of B2 |

**Consequences.**

- **`Status` gains three values, not two.** `RejectedSeq` and `DuplicateCached` as §3.2
  said, and **`DuplicateInFlight`**, which also bumps `rx_dup_command`. It is held apart
  so a field log does not read "DuplicateCached" for a frame that received no answer.
- **The precondition is withdrawn**, not restated. Library P8's test suite carries the
  falsifier it never had: a second `check()` before `record()` must return `InFlight`,
  never `Execute`, and that test fails against the API as first specified.
- **An in-flight entry can be evicted** if `dedup_cache_depth` newer commands are
  accepted before it completes. Its retry then falls to step 5 and is refused — never
  executed twice — and `record()` reports the loss so the caller can log it.
- **Still open, and not part of this amendment:** what GateLink's `COMMAND_ACK` waits
  for — pulse complete, or gate confirmed. It sets how often the window is hit, not what
  happens in it. GateLink **M3** needs the answer.

---

### 3.3 D33 reopened — what M21 found, 2026-09-06

**The mechanism worked as designed.** §3.1's standing condition 1 required the modules' own
FCC grant conditions to be confirmed *before D1 fixes a number*. M21 did that, and the
confirmation did not support the premise. Full record in
[`LRAN-M21-FCC-Grant-Findings`](./LRAN-M21-FCC-Grant-Findings.md) and its implementation
companion [`LRAN-M21-Handoff`](./LRAN-M21-Handoff.md).

**What the grants say.** Heltec V3 — `2A2GJ-HTIT`, **finished-product certification, not
modular**, LoRa DSS 125 kHz at ≈13.5 dBm and DTS 500 kHz at ≈13.9 dBm over 902.3–914.9 and
903.0–914.2 MHz respectively, with an **internal 3.0 dBi antenna declared and fixed**.
Seeed Wio-SX1262 — `Z4T-WIO-SX1262`, **single modular approval**, 92 mW (≈19.6 dBm), with
a grant condition that the antenna **must not be co-located or operating in conjunction
with any other antenna or transmitter**.

**Three findings, in order of consequence.**

1. **Neither module is certified under §15.249.** Both are §15.247. D33's premise named a
   different rule section from the one either module was tested against.
2. **D33's operating mode exists inside those grants only at BW500.** Both manufacturers
   split LoRa the same way — 500 kHz as DTS, 125 kHz as DSS, i.e. as frequency hopping. A
   single fixed 125 kHz channel is too narrow for DTS and is not hopping, so it is neither.
   **This is what makes `BW` and the rule section one decision**, and it is D1's fourth
   bound (§2.1).
3. **The grants do not transfer, permanently.** Four independent grounds: LRAN runs custom
   firmware and drives the PHY arbitrarily through RadioLib (D32); the Heltec grant is not
   modular; the Heltec antenna declaration excludes the external antenna every deployment
   and range test uses; and **co-located LoRa, WiFi and BLE are a hard project requirement**,
   which the Wio's modular grant forbids outright. The fourth is recorded as **closed, not
   deferred**, so it is not re-opened later as an option.

**The operative frame is §15.23 (home-built devices)** — not more than five units, personal
use, not marketed, builder expected to follow good engineering practice and aim at the
applicable technical standards. **Corollary, and it is a standing one: no LRAN node may be
represented as FCC certified**, and no "Contains transmitter module FCC ID: …" label may
appear in the README, a LICENSE header, an enclosure label, or HA device metadata.

**D33's chosen ceiling survives its reasoning.** §15.249 was the only rule section
permitting a single fixed narrow channel, and it happens also to be sufficient — the
2026-09-04 walk closed at 0 % PER at all six positions at that ceiling. What is replaced is
the *justification*, not the number.

**One arithmetic correction to §3.1's condition 1.** It states a conducted figure of "around
−3 dBm" from a 2 dBi antenna. The fitted antenna claims **3.0 dBi**, so the conducted
ceiling is **−4 dBm**. The requirement to record conducted power and antenna gain
**separately** is unchanged and is now more important, not less: the findings note uses
*different* gain assumptions for compliance and for link budget, and a single combined EIRP
figure cannot be re-derived into either.

**What reopening D33 does and does not block.** It does not block the range test firmware,
which already clamps to this ceiling and logs both terms. It blocks **D1 fixing a number**,
which was always the gate.

### The clamp is now verified over the air — 2026-09-07

**Until this date, "the number the firmware computed is the number the PA emitted" was
assumed.** `clamp_conducted()` is host tested and the ceiling arithmetic is host tested, but
nothing had ever confirmed the clamp *over RF*. The §7.6 EIRP sanity check closed that gap.

The sweep plan requests `kSx1262MaxDbm` (**+22 dBm**) for its high point and relies entirely
on the clamp to bring it to the ceiling. **A failed clamp would present as a 26 dB step, not
a 5 dB one** — a compliance fault rather than a measurement error.

**Result: the highest conducted power reaching the air was −4 dBm against a −4 dBm ceiling
for the fitted 3.0 dBi antenna, at three tape-measured distances, in three runs, on both
board profiles.** Traces `2026-09-07-eirp-sanity{,-xiao,-swap}.csv` in `docs/rangetest/data/`;
procedure in `docs/rangetest/EIRP-SANITY-CHECK.md`; full account in the range-test
engineering log for 2026-09-07.

**This is evidence for the §15.23 good-engineering-practice record, and it is not a
compliance determination** — that needs a calibrated field-strength meter at 3 m, per
§7.6 §6. What it establishes is that the ceiling this register specifies is the ceiling the
hardware applies.

**A module difference was found and does not disturb the ceiling.** The Wio-SX1262's
`(TX − RX)` sits **3.37 dB below** the Heltec's, reproduced across two runs. Reciprocal RSSI
cannot say whether that is a weaker PA or an optimistic RSSI reading, and **it does not need
to**: a weak PA sits further under the ceiling, and an optimistic RSSI implies the Wio's
transmit equals the Heltec's, which passed the clamp check directly. **Neither reading puts
either module over.** Findings §7.5 had budgeted ~3 dB for the requested-to-connector gap
and left the Wio's share unmeasured; it now has a bound.

**Confirmed in the field 2026-09-09, and it is not a bench artifact.** B1b measured
`init_rssi − resp_rssi` at **−2.91 dB** at G1 (−84 dBm) and **−3.27 dB** at the gate
(−100 dBm), against **−0.15 and −0.43 dB** on two Heltec-Heltec sweeps in the same capture
and −0.51 to −0.85 dB across all six positions of the 2026-09-04 walk. The instrument's own
bias is therefore about 0.5 dB and the module figure sits well outside it, at two geometries
16 dB apart in received power. **The compliance argument above is unchanged** — it already
covered both readings of the asymmetry.

**B1b's Heltec A/B separated the transmit term from the receive term — a first, and the
degeneracy is broken by substitution rather than permutation.** All three of B1b's gate sweeps
put the responder in one place, resting on top of the gate controller enclosure with the
antenna vertical, with one initiator untouched in the office throughout; only the responder
board changed, so path loss cancels. From `sum/2` of **−98.94 dBm** (Wio) against **−89.69**
(Heltec), and the `(init − resp)` figures of −3.27 and −0.43 dB:

| term | value |
|---|---|
| `(TX+RX)_wio − (TX+RX)_heltec` | **−9.25 dB** |
| `(TX−RX)_wio − (TX−RX)_heltec` | **−2.84 dB** |
| **TX_wio − TX_heltec** | **≈ −6.0 dB** |
| **RX_wio − RX_heltec** | **≈ −3.2 dB** |

**Carry the caveats with the numbers.** The sum term rests on **one pair of sweeps**, and
placement at that mount is worth 2.10 dB (sweep 3, still being placed, read −91.79), so each
split term carries roughly **±1 dB**. The difference term is measured twice, at two geometries
16 dB apart, and matches the bench — **split approximate, difference firm.** One repeat,
alternating Wio–Heltec–Wio at the same mount, would settle it.

**The ruling against role permutation is unchanged and still applies.** For any pair,
`P_ij − P_ji = (TX_i − RX_i) − (TX_j − RX_j)`; swapping roles cannot reach the sum term. What
§7 lacked was an absolute reference, and a second node at the same mount turns out to be one.
**Do not run further role permutations.**

**The compliance conclusion above is strengthened, not disturbed.** §7.6 check 3 backed every
measurement out **below** its calculated figure on both boards, so neither transmits above its
setpoint, and a Wio delivering ~6 dB less at the same commanded power sits **further under**
the ceiling. **Check 2's pass on both boards is not in tension with a 6 dB inter-board
difference** — it is a step-size test (5 dB expected against 26 dB for a broken clamp) and is
blind to a common-mode offset in delivered power by design.

**The consequence for GateLink, which is a Wio node.** B1b's gate sweep was taken with the
Wio, so its 0 % PER and every margin figure already carry this penalty — **those are the
deployed numbers.** The new option is that **a Heltec at the gate would see ~9 dB more
margin**, which belongs to GateLink's module choice alongside the SF question in §2.2.

### 3.4 D1 and D33 — what was chosen, 2026-09-10

**Closed on the evidence already in hand. No new measurement was taken, and none was
owed** — §2.1's four bounds had all reported (M6 2026-09-09, M20 2026-09-05, M21
2026-09-06). The options were assembled in
[`LRAN-D1-PHY-Decision-Brief`](./LRAN-D1-PHY-Decision-Brief.md), which is **superseded by
this section** and is kept as the dated record of how the choice was framed.

| Parameter | Fixed at | Why |
|---|---|---|
| Rule section | **§15.249, Envelope A** | Closed 0 % PER at its own ceiling on both bearings. Nothing measured asks for Envelope B |
| `BW` | **125 kHz** | Forced by Envelope A, and fixed in the same motion per §2.1's fourth bound |
| Frequency | **917.4 MHz** | §5.4's two independent scorings agree, and 917.4 wins on the one using all seven sites |
| `SF` | **9** | About 13 dB of tail margin, bought with a runtime-configurable number |
| `CR` | **4/5** | Nothing measured constrains it, and extra FEC does not repair a packet that never arrived |
| TX power | **−4 dBm conducted**, 3.0 dBi antenna | The D33 ceiling. The SX1262's −9 dBm floor measured *worse* — 12.5–25 % PER at SF7 on 2026-09-04 |

**The SF choice is the only contested one, and it was decided on the cost of being
wrong.** W9 wants SF7, which keeps Protocol Spec §12.3's media-access defaults valid as
written. B1b wants SF9, because the worst single SF7 probe at the gate reached a **2.2 dB
margin at −119.0 dBm** against 15 dB of per-test-point RSSI spread, and both node sites are
obstruction-limited rather than distance-limited — seasonal foliage moves a tail that is
already near zero. **SF9's cost is `backoff_max_ms`, a number changed from an HA dashboard.
SF7's risk is a USB reflash at a gate with no OTA**, because PHY parameters are deliberately
not runtime-configurable (Protocol Spec §12.1). Spend the cheap knob to protect the
expensive one.

**A hardware route to the same margin existed and was not taken.** A Heltec at the gate
would see about 9 dB more margin than the Wio-based node. It is a board change against a
configuration change, and the configuration change is cheaper. Recorded because it stays
available if GateLink's radio is ever revisited for another reason.

**What moved with the decision.** Protocol Spec **v0.10** carries all of it: §12.1 states
the fixed parameters, §12.3 raises `backoff_max_ms` to **1500** above SF9's 1107 ms
full-frame airtime, and §15.1's table is confirmed to be on the BW125 / CR 4/5 basis this
decision fixes, closing **M19** and **W7**.

**Two things stay under observation, and neither reopens D1 on its own.**

- **`cad_backoffs` is the instrument for the channel.** M20 measured 125 kHz every 200 kHz,
  so **37.5 % of the band was never looked at** and a transmitter sitting entirely in a gap
  is invisible at any level. §12.3's defaults were also chosen against an empty channel.

  > **That instrument cannot do this job, found 2026-09-17 — see M25.** `cad_backoffs`
  > counts a **busy CAD only**, which the bridge answered the same day with `cad_free`;
  > and a LoRa CAD detects a LoRa preamble **at the configured spreading factor**, so it
  > is blind to the property's Z-Wave and Insteon FSK at any signal level, and to LoRa at
  > another SF. **D33's status is unchanged here** and this is not a reopening — it is the
  > record that the check named against standing condition 3 cannot fire. M25 builds one
  > that can; M26 completes the §3.1 inventory it would be read against.
- **The range-test firmware still transmits on the provisional 915.0 MHz**, which is
  `weather-island`'s own peak. It is a bench instrument and this is not urgent, but a re-run
  on the old channel produces data that will be distrusted later.

---

### 3.4.1 D1's frequency confirmed, and D33's standing condition 3 answered, 2026-09-20

**The operator accepted 917.4 MHz on 2026-09-20, and every parameter in §3.4 stands
unchanged.** [`LRAN-D1-Frequency-Change-Brief`](./LRAN-D1-Frequency-Change-Brief.md)
proposed moving to 917.2 MHz; the brief is **superseded by this section** and is kept as the
dated record of why the question was asked. Its §6 list of what would move is **not
executed**: no constant changes, no boot banner, no test assertion, no specification
revision and no board is reflashed. **`ver` stays 2 and no citation sweep is triggered.**

**The measurement is brief §5's parallel run**, and it is the first direct look at the
chosen channel over a full day. Both receivers ran `firmware/chan-capture/` over the same
24 hours, 2026-09-19T17:56:58Z to 2026-09-20T17:56:58Z, 8,634,999 samples each with one
skipped sample each and no gap. Full reading in
[`LRAN-D1-Parallel-Capture-Analysis`](./LRAN-D1-Parallel-Capture-Analysis.md); the bridge
engineering log's 2026-09-20 entry has the same figures as a dated record.

| | 917.4 MHz | 917.2 MHz |
|---|---|---|
| Occupancy above −110 dBm | **0.2095 %** | **0.3050 %** |
| Hours carrying more occupancy | 0 of 24 | **24 of 24** |
| −90 to −80 dBm buckets | **68** | **713** |

**917.2 MHz fails brief §5.3 test 1** on its occupancy and episodic-source conditions.
Hour-by-hour occupancy on the two channels correlates at **r = 0.978**, so the property's
own activity moves both receivers together and the 917.2 MHz excess is attributable to the
channel rather than to a schedule. The excess is not the receivers' gain difference either:
a source reaching 917.4 MHz 8 dB down would appear in the −100 to −90 dBm band, and that
band tracks across the two files at 838 buckets against 1043.

**The choice is between one characterised occupant and two unidentified ones.** 917.2 MHz
carries an aperiodic source near −89 dBm in **all 24 hours**, not the six the first capture
suggested, and it is unidentified. 917.6 MHz has its own unexplained bursts at −45 to
−47 dBm. 917.4 MHz's only structured occupant is the Davis, whose airtime is now measured.
**Day 2 at 917.6 MHz is not needed for D1**: it was to run only if day 1 ruled out
917.2 MHz, and day 1 ruled 917.2 MHz out instead.

#### D33 standing condition 3 is not met as written, and D33 is not reopened

**§3.1's condition 3 reads "the ambient survey (M20) finds no co-channel occupant on the
chosen frequency."** That is false for 917.4 MHz and has been since 2026-09-18: the
property's **Davis Vantage Pro2** transmits there. §3.1 says losing any one of the three
conditions reopens D33, so this has to be settled rather than noted.

**It is settled by characterising the occupant instead of denying it.** Day 1 measured the
Davis over 660 occurrences: a fitted period of **130.6882 s** with a median residual of
**0.54 s**, a burst of about 6.7 ms, a duty cycle of **0.005 %**, and an overlap with a
maximum-length SF9 frame of about **0.85 %** before any retry. A retry reuses `seq` and goes
out within `backoff_max_ms` = 1500 ms, so it cannot land on the same hop. **That is not a
threat to the link, and no collision mitigation beyond the existing retry is indicated.**

**Condition 3 is therefore restated, and this is the version to test against:**

> **3.** Every co-channel occupant on the chosen frequency is **identified or
> characterised**, and their aggregate airtime leaves the link's measured margin intact. An
> occupant that is neither identified nor characterised reopens D33; a characterised one
> with negligible airtime is recorded here and accepted.

**The Davis is recorded as an accepted occupant under it.** So is the −93 dBm episodic
source, which appears at all three candidate frequencies and therefore separates none of
them — characterised as to airtime, still unidentified as to source.

**Two occupants stay open under this condition and neither reopens D33 on its own**, because
neither is co-channel at a level that touches the margin: the aperiodic −89 dBm source on
917.2 MHz, which is not a frequency LRAN uses; and a single wideband event at
2026-09-20T13:25:22Z that read −42 dBm at 917.4 MHz and −39 dBm at 917.2 MHz in the same
second, one second wide, once in 24 hours.

**What replaced the instrument, and what did not.** §3.4 recorded that `cad_backoffs` cannot
test this condition, and v0.12 recorded that its named check cannot fire. The instrument is
now `firmware/chan-capture/` plus `tools/simctl/rssi_analyze.py`, which sample the channel
directly at about 100 samples a second regardless of modulation or spreading factor.
**M20's own limit is not lifted**: it measured 125 kHz every 200 kHz, so 37.5 % of the band
is still unlooked-at, and every "clear" verdict in §5.4 still carries that bound. What day 1
establishes is the **chosen channel**, continuously, not the band.

**§3.1 and §5.4 are left as written.** They are dated records of what was decided and
measured on 2026-08-30 and 2026-09-06, and this section is where the change lives.
**§5.4's attribution of the 915.8–916.4 MHz cluster is unchanged and still unconfirmed**:
both day-1 receivers were 125 kHz wide and at least 1.2 MHz away, so this measurement says
nothing about that cluster. **M26** still owns it.

**One caveat on the tooling, and it does not affect the figures above.** `periodicity()` in
`tools/simctl/rssi_analyze.py` reports this same Davis as "not periodic" once a capture runs
a full day, through two defects that only appear at that length. It is **unfixed by operator
direction**, since no further capture is planned. Every period and residual quoted here was
computed by seeding a fit with a known period and reading the residuals directly.
`firmware/chan-capture/CLAUDE.md` and the function's own docstring carry the warning.

---

### 3.5 D35–D42 — the spec v0.12 set, 2026-09-16

**Eight questions raised during bridge B3a and simnode B0, decided together because they
were holding B3b.** Each had been recorded in the engineering-log entry that raised it and
left unpatched; `LRAN-Spec-v0.12-Brief` collected them with options, the operator accepted
every recommendation on 2026-09-16, and the brief is **superseded**.

**One property was checked across the whole set, and it holds.** No answer changes a frame
layout, a header field, an enumeration value, a schema or the authentication scope, so
`ver` stays `2` and no test vector regenerates. On a fleet with no OTA that is the
difference between a document change and a walk to every node. Two of the rejected options
would have broken it: spending a `reserved` bit in `COMMAND_ACK` (D36) and raising §3.1's
reassembly cap (D38).

**Three answers are worth reading for their reasoning rather than their outcome:**

- **D41 rejected the obvious answer.** Answering any `src` with an `ERROR` before the
  sender is authenticated makes the receiver a reflection: one spoofed frame, one
  transmission, at a rate the attacker picks, on a channel the whole fleet shares. Silence
  was the other candidate and costs the field diagnosis the counters exist to give. The
  rate-limited middle keeps the diagnosis and bounds the exposure to addresses already in
  the registry.
- **D38 is a contradiction, not a gap.** §11.4 called `CONFIG` fragmentable and §3.1 capped
  a reassembled set at exactly what one frame already carries. Both statements had been
  read many times; neither had been read against the other until BF-6 asked what a repeated
  `CONFIG` does.
- **D40 is the specification catching up to an implementation that was right.** BF-19 could
  not build §14.1's "per node by the bridge" as written and published the aggregate
  instead, recording why. The rule now says what the bridge does.

**Not decisions, and recorded elsewhere.** The ninth question — whether the SX126x can
filter node addresses in LoRa mode — was a fact, not a choice: it is **M24**, verified
against the datasheet, and it costs Protocol Spec §17.1 a mechanism it relied on, now
**W14**.

---

### 3.6 D43–D57 — runtime configuration from Home Assistant, 2026-09-19 and 2026-09-20

**The operator chose the general route on 2026-09-19 (D43) and accepted all eight
recommendations of `LRAN-Config-Set-Brief` the same day**, which is superseded. Six became
D44–D49. The other two were not choices: the bridge's parameter list is written into
Protocol Library Plan §4 for review, and **BF-32** owns the build.

**The property §3.5 checked holds again.** No answer changes a frame layout, a header
field, an enumeration value or the authentication scope. D46 renames schema `0x12`
without changing its value, and D45 defines a reply the specification had left
unwritten, so `ver` stays `2` and no test vector regenerates.

**Two answers are worth reading for their reasoning:**

- **D45 writes down what an implementation already did.** The specification offered three
  ways to request a readback and defined a reply for one. Simnode BF-6 answered the other
  two with an unsolicited `CONFIG_ACK`, which contradicted §9.2's reason for leaving
  `CONFIG_ACK` unauthenticated. The rule now says what the simnode does, and why that is
  safe, as D40 did for BF-19.
- **D44 resolves a disagreement BF-23 had already made moot.** System PRD §9.4 said
  *generated*; Protocol Library Plan §4 said *hand-written, maintained by hand*. BF-23
  generated `/ha/` from the firmware's own `discovery.cpp`, which showed that a
  hand-written table and code-derived outputs are compatible.

**D50–D54 came from the v0.13 read-through the same day**, and the operator accepted each
recommendation. Each fills a gap in §7.4, §8.10, §8.11 or §10.4 that simnode BF-6 had
already filled locally. Four adopt what the simnode does. D52 does not: the simnode answers
`RESTORE_DEFAULTS` with no results, which would leave the bridge unable to republish
`config/state` without a second readback. The read-through also opened **W16**, on when a
node sends `CONFIG_CHANGE`, and left it to GateLink.

**D55 and D56 came from the operator's reading of the brief**, later the same day, and
neither adopts an implementation's behaviour the way D45 and D50–D54 do.

- **D55 generalises `len`.** The operator read it as a unit count and asked which it was.
  Defining it as a byte count that is a multiple of the `ptype`'s width answers both: a
  scalar is one unit, a string is `u8` bytes, and an array needs no new `ptype`. Nothing on
  the wire changes, because every value defined today is one unit wide.
- **D56 reverses a v1 scope decision, and the specification had already named the
  mechanism.** §12.1 said PHY parameters were not runtime-configurable, *"if this is ever
  wanted it needs a commit-and-revert scheme — apply, require a confirmation frame within N
  seconds, otherwise revert."* The operator judged field adaptability worth more than the
  simplicity of a fixed PHY. The scheme is §12.4, and the parameters are declared
  `READ_ONLY` until **BF-33** builds it, so Home Assistant can read the working point before
  it can change it.

**D57 answers W10, on 2026-09-20, and the count is why.** W10 had asked for GateLink's real
parameters to be counted against §7.4's ceilings before `/lib/lran-config/` was designed. The
count: **25 named rows at 171 bytes** of the 193 a `CONFIG_ACK` has for results, which fits one
frame with three `uint16` rows to spare. The rows GateLink PRD R-5.3a and R-5.4 imply but do not
name — the dry-run switch, the buzzer, injection spacing, VE.Direct staleness,
`mppt_write_arm_timeout_s`, `republish_interval_s` — take it to 211 bytes. **The operator decided
on the margin rather than the overflow**: a requirement that every interval, window, threshold and
debounce be configurable will cross three rows during GateLink's implementation, and enumerating
the list now does not bound it. Of the three routes considered — a marked sequence, a paged
`GET_ALL` carrying an offset, and a rule that a node's set must fit one frame — the operator chose
the sequence because it asks the requester to know nothing about how many parameters exist.

**Found in the same pass, and not decisions.** Five passages still described `CONFIG` and
`CONFIG_ACK` as fragmented after D38. Protocol Spec v0.13 corrects them; the brief's §2
lists them.

**Two GateLink documents disagree with D56 and with Library Plan §4**, found while counting
for W10 and not fixed here. They are tracked in
[`docs/gatelink/doc-findings.md`](../gatelink/doc-findings.md) and fixed at the GateLink
milestone, with whatever else its implementation surfaces (operator, 2026-09-20):

- **GateLink PRD §5.3.1 still lists the LoRa PHY parameters as not runtime-configurable**,
  with the reasoning *"changing these from HA means changing the link you are changing them
  over."* **D56 decided the opposite** and §12.4's commit-and-revert is the answer to that
  objection. Those six rows are also the whole of the margin the count above found.
- **`tx_conducted_dbm` and `tx_power_dbm` are one parameter under two names**, the first in
  the GateLink PRD and the second in Library Plan §4, where it becomes a permanent Home
  Assistant `object_id`.

---

## 4. Retired decisions

| # | Decision | Why retired |
|---|---|---|
| **D2** | RX duty-cycle period and preamble length | Retired for GateLink when the night/low-PV profile was dropped — the saving was ~0.36 Ah/day against 100 Ah usable. **The design is preserved** as a reserved feature in Protocol Spec §17.1 and this decision **reopens only if WellLink is battery powered (D19)** |
| **D9** | PV-aware profile thresholds | Same — went with the night profile |
| **D10** | Rx-boosted gain on or off | **Retired as a power question.** The difference is 1.1 mA, or 0.026 Ah/day. It remains a **link** question: if the range test shows any benefit from the +3 dB, take it (**D1**) |

> **Why retired decisions stay listed.** Each of these appears in older material and in
> the research archive. A reader following a reference from that material needs to land
> on an explanation, not a gap — and in D2's case, on the fact that the design still
> exists and where it went.

---

## 5. Measurement backlog

Ordered by consequence. Every `TBM` in the document set has a row here.

### 5.1 Blocking or high-consequence

| # | Measurement | Blocks | Owner |
|---|---|---|---|
| M1 | **Gate controller branch current in all four gate states**, by inline meter or shunt on a mA range, or by low-range clamp with ten turns through the jaw and the reading divided by ten. LED lights separately accounted | **The top item — power-budget credibility.** First clamp figures fail their own sanity check | GateLink Impl Plan |
| M2 | **`MOVING` behaviour at the open limit and through the auto-close countdown** | Hold detection and the `hold_confirm_ms` default. If MOVING dips at the open limit, the confirmation timer must absorb it | GateLink Impl Plan |
| M3 | **IN5 (FIRE) and IN6 (alarm) idle and asserted voltages** | Sense polarity and idle state. Not a damage risk — the inputs are rated 5–36 V — but wiring them the wrong way round inverts an emergency alert | GateLink Impl Plan |
| M4 | **MPPT VE.Direct TX low excursion under a 10 kΩ load to GND**, preferably on a scope | **D25**, carrier BOM | GateLink Impl Plan |
| M5 | ~~**BLE RSSI to the BMS from the final StamPLC mounting position**~~ | **Superseded by M23 (2026-09-06)**, which asks the question the confirmed enclosure stack actually poses: multiple positions and orientations inside a reverberant steel cavity, with the Stamp-S3A's own antenna. M5's single-position wording predates that | GateLink Impl Plan |
| ~~M6~~ | ~~**Range and RSSI at ~500 ft on both bearings.**~~ **CLOSED 2026-09-09. Both bearings measured, and "~500 ft" was a guess that the measurements retire — see *M6's distances* below.** Procedure for the §7.6 precondition is `docs/rangetest/EIRP-SANITY-CHECK.md`; the reader is `tools/rangetest/eirp_check.py`. **Record conducted TX power in dBm**, not a RadioLib power index — the Heltec (≈13.9 dBm) and Wio (≈19.6 dBm) certified powers differ by ~6 dB and an index does not carry between them. ~~**Run the M21 findings note §7.6 short-range RSSI/EIRP sanity check first**~~ — **DONE 2026-09-07, all four checks PASS.** Heltec pair plus §7's Wio repeat, three tape-measured distances, traces `2026-09-07-eirp-sanity{,-xiao,-swap}.csv`. **The precondition is met and M6 is unblocked.** It also discharges §7.4's antenna-counterpoise uncertainty to the extent that check can — it rules out a *gross* gain error, not the 3.0 dBi vendor claim itself. **Partly answered:** the 2026-09-04 walk closed at six positions to 106 m along the gate bearing at the −4 dBm ceiling. ~~**The last ~46 m is unwalked — B1b**~~ — **B1b RAN 2026-09-09 and the gate bearing is answered.** The gate closed **192/192, 0 % PER at all 24 configurations** on the deployed pairing (Heltec V3 indoors at the bridge's target location, XIAO+Wio at the gate controller), at both −9 and −4 dBm conducted, zero error counters. Trace `2026-09-09-b1b-walk-gate.csv`. **Margin on the mean is 17–25 dB of SNR; the SF7 tail is not — one probe reached a 2.2 dB margin at −119.0 dBm**, which is a new constraint on D1's SF choice and pulls against W9's. **G2 is the 2026-09-04 walk's P1, and the deployed GateLink antenna lands within 6 in of it** — so B1b measured the link where the node will radiate rather than near it, which is a stronger answer to M6's question than any position on the earlier walk. **THE WELL BEARING IS ALSO ANSWERED, AND WAS ALL ALONG.** Operator confirmation 2026-09-09: **the 2026-09-04 walk's P3 is survey site 3, `welllink-well`** — a **Heltec pair**, initiator indoors at the bridge's real target location, through the NW wall plus the barn, **2.08 % PER over 192 probes** at the D33 ceiling (init −99.5 / resp −98.7 dBm). The trace's position map names no site, so the only well-bearing measurement this project has sat in a committed file under a number. **No well walk is owed.** Two corrections came with it, both in the range-test log's 2026-09-09 entry: **P3 is ~100 m out and its recorded fix is wrong rather than coarse** (two points sharing one arcsecond cell at 35N are at most 40.0 m apart), and **"the barn costs most" is false** — recomputed at 100 m its excess loss is 25.1 dB, fourth of six. **Carry into WellLink:** P3 is the thinnest margin any node site showed and the only position in that walk with any `phy_crc_err`. **M6's own wording overstates one bearing** — the gate is ~500 ft, the well is ~100 m and is obstruction-limited rather than distance-limited, so a 500 ft walk there would measure a place no node occupies. **CLOSED 2026-09-09**, on the operator's decision that the "~500 ft" in its own wording was a guess and never a measurement. Distances below. First positions recorded in decimal degrees; still not a path-loss model, because the house end is behind a wall on an arcsecond-era fix | **D1**, bridge antenna siting | Range Test Tasks |
| M20 | ~~**Ambient RSSI sweep of 902–928 MHz**, run at the bridge location **and** at the most distant node location~~ | **Field work done (2026-09-05).** R11 re-walk, seven sites, 902.0–927.8 MHz in 200 kHz bins, 130/130 bins, `dropped = 0`; committed as `docs/rangetest/data/2026-09-05-survey-campaign-r11.csv`. One confirmed in-channel occupant (`weather-island`, −80 dBm at 915.0), strongest near-band neighbour `gatelink-gate` at −66 dBm on 914.0, floor −115 to −118 dBm and uniform, every occupant bursty, no carrier anywhere in the band. **Residual done (2026-09-06)** by `tools/rangetest/survey_reintegrate.py`, output committed as `docs/rangetest/data/2026-09-06-m20-reintegration.csv`. **M20 is closed.** Results in §5.4 | **D1's frequency** (Protocol Spec §12.1) and **D33 standing condition 3** | Range Test Tasks |
| M7 | **BMS pack-current sign convention**, captured once under charge and once under load | Last open item in the BMS protocol (Protocol Spec §18, W6). Bit `0x4000` is believed to be the discharge flag but has only been observed at 0.0 A | GateLink Impl Plan |

### 5.1.1 M6's distances — the "~500 ft" was a guess, and both nodes are closer

**Closed 2026-09-09.** M6 asked for "range and RSSI at **~500 ft** on both bearings." That
figure was **estimated before any node was walked to** and never measured. The walks put both
node sites at **roughly 85–100 m**, so the requirement overstated the range it was written to
test by nearly a factor of two.

| Site | Distance from the bridge | How it is known |
|---|---|---|
| **GateLink**, the gate controller | **~87 m (285 ft)** | Two independent derivations agreeing: the bridge's arcsecond fix against G2's decimal fix gives 87.6 m, and the 2026-09-04 excess-loss table lists P1 — the same spot — at 85 m. The bridge fix carries about ±20 m of arcsecond quantisation |
| **WellLink**, the well | **~100 m (330 ft)** | Operator figure, 2026-09-09. **Its recorded fix is wrong**, not merely coarse — see the range-test log's 2026-09-09 entry |

**Neither node is at 500 ft and the requirement is retired rather than reworded**, because
what M6 was really asking — *does the link close where the nodes will live* — is answered at
the distances that exist:

| Bearing | Result |
|---|---|
| **Gate** | **B1b, 2026-09-09.** 192/192, **0 % PER** at all 24 configurations on the deployed pairing, within 6 in of where the antenna will sit |
| **Well** | **Walk P3, 2026-09-04.** 2.08 % PER over 192 probes, Heltec pair, at the D33 ceiling |

**Read the margins as distances only with care.** Both sites are obstruction-limited rather
than distance-limited: the gate path carries a 24 in trunk, the well path a barn, and every
path crosses the bridge's exterior wall. **The excess loss over free space is 25–29 dB at the
obstructed positions and 10 dB at the clear one** — which is the term that matters here, not
the range.

**What a corrected distance does not change.** Nothing in D1, D33 or the link budget was
derived from the 500 ft figure; the decisions rest on measured RSSI, SNR and PER at the actual
sites. The figure appeared in scene-setting prose — the System PRD, the Bridge Implementation
Plan's B1a row, the repository README and root `CLAUDE.md` — all corrected in the same change.
**Archived document revisions keep it**, as dated records of what was believed.

### 5.2 Informative

| # | Measurement | Informs | Owner |
|---|---|---|---|
| M8 | **Whether the 12 V lamp output also tracks the auto-close countdown** | Viability of the lamp as a backup MOVING source if OUT2 is ever wanted elsewhere. If it does not track, it is not a drop-in | GateLink Impl Plan |
| M9 | Real EXIT→SAFETY gap times, by driving the vehicle | `detect_sequence_window_ms` default | GateLink Impl Plan |
| M10 | Exit wand hold / de-assert behaviour with a stationary vehicle | Debounce and re-trigger lockout. Some wands de-assert after a hold time, producing repeated edges from an idling vehicle | GateLink Impl Plan |
| M11 | Which loop input the loop detector occupies; whether EDGE is in use | Spare-capacity record | GateLink Impl Plan |
| M12 | Node supply current via the onboard INA226 | Budget confidence — the node measures itself, so this closes several rows by construction | GateLink Impl Plan |
| M13 | Enclosure temperature, seasonal, across LM75 + MPPT + BMS | **D29** | GateLink Impl Plan |
| M14 | One week of MPPT yield (H19/H20/H21) and battery Vmin baseline before install | Install go/no-go, and the *actual* present margin for free | GateLink Impl Plan |
| M15 | DIN-rail carrier selection, then cut the perfboard to it | Carrier build (**D27** sub-item) | GateLink Impl Plan |
| M16 | StamPLC IO schematic, netlist-level 3.3 V check | Confirms **D26**; cannot change it | GateLink Impl Plan |

### 5.3 Administrative

| # | Item | Blocks |
|---|---|---|
| M17 | ~~**Copyright holder name for the LICENSE file**~~ | **Done (2026-09-08).** **Robert J. Lee.** `LICENSE` written at the repo root; placeholder replaced in every source file. **D31 resolved.** `THIRD_PARTY_NOTICES.md` (System PRD §11.3) written the same day |
| M21 | ~~**Confirm the SX1262 modules' own FCC grant conditions**~~ | **Done (2026-09-06).** Both grants recorded in `LRAN-M21-FCC-Grant-Findings`. Heltec `2A2GJ-HTIT` — finished-product, **not modular**, ≈13.9 dBm DTS, internal 3.0 dBi antenna declared and fixed. Seeed `Z4T-WIO-SX1262` — single modular approval, 92 mW, **no-co-location condition**. **Neither is §15.249**, and the fixed-channel no-hopping mode exists in both grants only at BW500. **D33 reopened** (§3.3); **D1 gains a fourth bound** (§2.1). Backlog gains M22 and M23 |
| M18 | ~~Protocol test vectors — fixed key, known frames, expected MACs and CRCs~~ | **Done.** `/tools/vectors/` holds 72 vectors from an independent Python generator, passing on host and on target with zero divergence. Protocol Spec **W4 is closed**; §13.2's standing requirement to regenerate on every protocol change continues to apply |
| M19 | ~~Airtime table regeneration once D1 fixes SF/BW/CR~~ | **Done (2026-09-10).** D1 fixed SF9 / BW125 / CR 4/5, and §15.1's table was already computed on that basis — the SF9 column needed confirming against §12.3's backoff window rather than recomputing. Protocol Spec v0.10 marks SF9 the operating point and raises `backoff_max_ms` to 1500. **W7 closed with it** |
| M24 | ~~**Whether the SX126x can filter node addresses in LoRa mode**~~ | **Done (2026-09-16).** **It cannot.** In SX1261/2 Rev 1.1 (`DS.SX1261-2.W.APP`, December 2017) `AddrComp` is GFSK `PacketParam5` (Table 13-56) with `NodeAddrReg` `0x06CD` and `BroadcastReg` `0x06CE` (Tables 13-57, 13-58), all under §13.4.6.1 **GFSK Packet Parameters**; the LoRa packet parameters in §13.4.6.2 are preamble length, header type, payload length, CRC type and invert-IQ (Tables 13-66 to 13-70) — **no address parameter, no address register**. Raised by **BF-16** and carried unverified through two revisions. **Protocol Spec v0.12 withdraws the §12.1 requirement**; addressing is §14 stage 5, in software. **§17.1 loses the silicon discard it assumed for duty-cycled nodes — now W14**, owed before WellLink is built on that profile and moot if **D19** makes WellLink mains-powered |
| M22 | **Bridge LoRa packet error rate with WiFi idle vs. saturated.** Run a sustained MQTT or iperf flood while the bridge receives a known `PING` sequence; compare PER and RSSI against the WiFi-idle baseline | Confirms the deliberate "**no** mutual exclusion on the bridge" policy (Bridge PRD). If PER degrades, the fallback is **physical antenna separation via the IPEX pigtail**, not firmware arbitration — ESP-IDF's coexistence arbitration has no visibility into an SPI-attached SX1262, so there is no hook to build on | Bridge Impl Plan |
| M23 | **BLE RSSI to the BMS from the Stamp-S3A at its final mounting position**, inside the plastic enclosure inside the closed **steel** gate-controller enclosure, ~6–8 in from the pack. Sample **at least three positions and two orientations** — both ends share one reverberant cavity, so the risk is a standing-wave null, not attenuation. In the same session, measure **LoRa-to-BLE isolation** by logging BLE RSSI with the LoRa transmitter keyed and unkeyed | **D28**, superseding **M5**. Prior figures (−80 dBm, and −50 to −60 dBm) both used a Heltec V3 rather than the Stamp-S3A's internal antenna. Run before committing the mounting hardware; it does **not** gate M6 or B1b. A poor reading is a cable, connector and null question before it is an antenna verdict | GateLink Impl Plan |
| M25 | ~~**Long-duration channel occupancy at 917.4 MHz, measured at the bridge**~~ | **Done (2026-09-20).** **The channel carries one periodic occupant, the property's Davis Vantage Pro2**, at about 6.7 ms every 130.6882 s — a 0.005 % duty and about 0.85 % overlap with a maximum-length SF9 frame. `lora_task`'s sampler was not the instrument in the end: it was moved into `lib/lran-link/` as `ChanMonitor` and given a listen-only firmware, `firmware/chan-capture/`, so several receivers could sample different frequencies over the same hours without the bridge's own polls in the data. Measured first on 2026-09-18 over M25's own hours, then over **24 hours on 2026-09-19 to 2026-09-20** beside 917.2 MHz. **D33 standing condition 3 is answered and restated in §3.4.1**, and the Davis is recorded there as an accepted occupant. The 917.2 MHz and 917.6 MHz captures and the −93 dBm episodic source are in the same section; the bridge engineering log's 2026-09-18 to 2026-09-20 entries have every number | Bridge Impl Plan |
| M26 | **Confirm the frequencies and power classes of the property's Z-Wave and Insteon equipment**, and add them to the §3.1 inventory. Z-Wave matters most: the classic US band is near 908.4 MHz at low power, while Z-Wave Long Range uses 912 MHz and 920 MHz and permits far higher power | **Still open, and no longer gating D1 or D33.** §3.1's inventory records YoLink only — *"four YoLink temperature sensors and a switch, on a YoLink hub, all inside the dwelling"*. The operator identified Z-Wave and Insteon on 2026-09-17 and neither appears anywhere in this repository. **What changed on 2026-09-20:** D1's channel and D33's condition 3 were settled by measuring 917.4 MHz directly for 24 hours rather than by reasoning from the inventory, which is the stronger evidence and does not depend on this row. What still needs it: **§5.4's attribution of the 915.8–916.4 MHz cluster**, the loudest thing in the M20 campaign and still only guessed at as the YoLink hub — both day-1 receivers were 125 kHz wide and at least 1.2 MHz away, so that capture says nothing about it. Z-Wave stayed below −110 dBm at 917.4 MHz across all 24 hours, and the ZEN17's 30 s reporting leaves no 30 s grid in any capture | Decision Register |

---

### 5.4 M20's re-integration — the channel evidence for D1, 2026-09-06

Derived from `2026-09-05-survey-campaign-r11.csv` by
`tools/rangetest/survey_reintegrate.py`; the tool is host-tested and refuses a trace
without hold discipline, because it ranks channels by peak. **No new field work was
required and none should be done** — re-walking to answer this would have been an
expensive, invisible mistake.

**The finding that changes D1, and it was not in the occupant inventory.** There is an
occupant cluster at **915.8–916.4 MHz**, seen at six of seven sites and by a wide margin
the loudest thing in the campaign: **−54 dBm at `bridge-house` on 916.0**, 62 dB above the
floor, with −86 and −80 dBm at `gatelink-gate` on 915.8 and 916.4. `bridge-house` was
measured indoors at the bridge's target location and the YoLink hub is inside the dwelling,
which is the obvious candidate — **unconfirmed, and it does not need confirming to be
avoided.**

This matters because it sits exactly where a reasonable person moves *to*. The inventory
recorded `weather-island` at −80 dBm on 915.0 as the one confirmed in-channel occupant, and
915.0 is the range test's provisional frequency; **nudging a few hundred kHz off it lands
in something 26 dB stronger.** By contrast the 915.0 signal appears at `weather-island`
only — every other site reads floor there — so it is local to that site, while the
915.8–916.4 cluster is property-wide.

**Envelope A — 125 kHz candidates in 915.2–923.0 MHz.** Scored on the worst peak across
sites, then on the strongest neighbour within ±600 kHz, since a candidate reading floor
next to a −54 dBm burst is an untested channel rather than a quiet one.

| Candidate | Worst peak, all seven | Worst peak, deployed sites | Nearest strong neighbour |
|---|---|---|---|
| **917.4 MHz** | −110.0 dBm | −111.0 dBm | −104 dBm at 917.0 |
| **917.2 MHz** | −109.0 dBm | −111.0 dBm | −104 dBm at 917.0 |
| **917.6 MHz** | −105.0 dBm (`propane-tank`) | **−112.0 dBm** | −104 dBm at 917.0 |
| 918.2 MHz | −109.0 dBm | −109.0 dBm | −103 dBm at 918.8 |
| *915.8 / 916.0 / 916.4* | *−78 / −54 / −80 dBm* | — | **the cluster above — avoid** |

**917.2–917.6 MHz is the recommendation**, and the two scorings agree on it. It is ~1.2 MHz
clear of the 915.8–916.4 cluster, its floor is the campaign-wide −116 dBm, and its nearest
neighbour of any strength is −104 dBm.

**Envelope B — the eight US915 500 kHz channels.** Their centres are exactly the DTS grant
range's endpoints, 903.0 + 1.6 MHz × k. Integrated floor is ≈ **−110 dBm**, i.e. 6.0 dB
above the per-bin −116 — the bandwidth penalty a BW500 receiver pays before any occupant.

| Channel | Integrated floor | Worst peak | |
|---|---|---|---|
| **909.4 MHz** | −109.6 dBm | **−107.0 dBm** | **clear — the pick if Envelope B is ever triggered** |
| 911.0 MHz | −110.3 dBm | −102.0 dBm | clear, second choice |
| 906.2 / 907.8 | −110.0 dBm | −96 / −89 dBm | occupied |
| 903.0 / 912.6 / 914.2 / 904.6 | ≈ −110 dBm | −79 / −77 / −66 / −64 dBm | **strong occupants — do not use** |

**Half the Envelope B grid is unusable**, including 914.2 MHz, where `gatelink-gate` — the
site GateLink will occupy — sees −66 dBm. That is worth carrying into any future Envelope B
trigger: the fallback is real but it is not a free eight-channel choice.

**One limit that post-processing cannot lift.** The survey measured 125 kHz every 200 kHz,
so **37.5 % of the band was never looked at** and a transmitter sitting entirely in a gap
is invisible at any level. Absence of a peak was already weak evidence (`data/README.md`);
the gaps make it weaker. This bounds every "clear" verdict above and is a reason to keep
`cad_backoffs` under observation after D1 rather than treating the channel as settled.

---

## 6. Changelog

- **v0.14** — **D17's row no longer says spec §5.3 reads `(LoRaBridge)`**; spec v0.10 changed it on 2026-09-10 and the row was not updated. **D43–D56 resolved: runtime configuration from Home Assistant.** The
  operator chose the general `config/set` route and accepted every recommendation of
  `LRAN-Config-Set-Brief` on 2026-09-19; the brief is **superseded**. New **§3.6** carries
  the reasoning. **D50–D54** followed from the v0.13 read-through the same day, filling five
  gaps in `CONFIG` semantics. **D55 and D56** followed from the operator's reading: `len`
  becomes a byte count that is a multiple of the `ptype`'s width, and the PHY parameters
  become runtime-configurable under §12.4's commit-and-revert. **D42 is amended**: its deferred `config/*` payloads are now D48's. No
  frame layout changes, so `ver` stays `2`. **D57 closes W10 on 2026-09-20**: a `GET_ALL`
  answer too large for one frame becomes several `CONFIG_ACK` messages marked
  `MORE_FOLLOWS`, and §3.6 carries the count that settled it — GateLink's named parameters
  reach 171 of 193 bytes, three rows short of the ceiling. §1's highest issued numbers are
  corrected to **D57** and **M26**; v0.12 added M25 and M26 without updating them.

- **v0.13** — **D1's frequency confirmed on 24 hours of measurement, and D33's standing
  condition 3 answered.** The operator accepted **917.4 MHz** on 2026-09-20 and declined
  `LRAN-D1-Frequency-Change-Brief`'s proposed move to 917.2 MHz, which measured busier in
  **24 of 24 hours** in brief §5's parallel run. **No parameter changes**, so brief §6's
  list is not executed: no constant, no boot banner, no test assertion, no specification
  revision, no board and no citation sweep. **D33 is not reopened**, but condition 3 was
  false as written — 917.4 MHz does carry a co-channel occupant — so new **§3.4.1**
  restates it around characterising occupants rather than finding none, and records the
  Davis as accepted at a 0.005 % duty. **M25 is done**: the instrument became
  `firmware/chan-capture/` rather than `lora_task`'s sampler. **M26 stays open and stops
  gating D1 and D33**; what still needs it is §5.4's unconfirmed attribution of the
  915.8–916.4 MHz cluster. §3.1 and §5.4 are left as written, being dated records. The
  brief is superseded. Highest issued numbers are unchanged at **D42** and **M26** — this
  revision mints no decision. Bridge engineering log, 2026-09-20; full reading in
  `LRAN-D1-Parallel-Capture-Analysis`.

- **v0.12** — **M25 and M26 added; §3.4 gains a note against its own instrument.** The
  operator identified Z-Wave and Insteon on the property on 2026-09-17, neither of which
  §3.1's inventory records, and neither of which a LoRa CAD can detect. **No decision
  changes status**: the entries record that D33 standing condition 3's named check cannot
  fire, and schedule the measurement that would let it. Bridge engineering log,
  2026-09-17.

- **v0.11** — **D35–D42 resolved: the spec v0.12 set.** Eight questions raised during
  bridge B3a and simnode B0, collected in `LRAN-Spec-v0.12-Brief` and accepted by the
  operator on 2026-09-16; the brief is **superseded**. New **§3.5** carries the reasoning,
  including the property checked across the whole set — no answer changes a frame layout,
  so `ver` stays `2` and no vector regenerates. **M24 is done**: the SX126x cannot filter
  node addresses in LoRa mode, verified against the datasheet, which withdraws a §12.1
  requirement and opens **W14** against §17.1. Protocol Spec **v0.12** carries every
  outcome. Highest issued numbers are now **D42** and **M24**.

- **v0.10** — **D34 amended, not reopened.** A review found that `CommandGate` as the
  library plan specified it would execute a bridge retry twice on a receiver that
  dispatches on another task, which GateLink's plan does. The operator accepted
  `LRAN-P8-CommandGate-Brief`'s recommendations on 2026-09-11: the high-water mark
  advances in `check()`, a retry inside the execution window is in flight and not
  answered, and cache storage is sized for the 1–32 range at 128 B per peer. **New
  §3.2.1** records it; §3.2 is left as written with a pointer, and the D34 row carries the
  amendment inline in the form D17's took. **No decision changes state.** Protocol Spec
  **v0.11** carries the outcome; library milestone **P8** implements it. The brief is
  **superseded**. What GateLink's ACK waits for stays open and is recorded in §3.2.1.

- **v0.9** — **D1 is closed, and D33 closed with it.** SF9, BW 125 kHz, CR 4/5, 917.4 MHz,
  −4 dBm conducted with the fitted 3.0 dBi antenna, inside Envelope A. Both rows moved from
  §2 to §3 and **§3.4 records what was chosen and why**; §2.1 and §2.2 keep their numbers and
  their wording, with a pointer added, because 22 documents cite them. The two decisions
  close together because §2.1's fourth bound makes `BW` and the rule section one decision.
  **The SF tie-break is the substance:** W9 wants SF7 and B1b's 2.2 dB fade tail wants SF9,
  and SF9's cost is a runtime-configurable `backoff_max_ms` while SF7's risk is a USB reflash
  at a gate with no OTA. `LRAN-D1-PHY-Decision-Brief` is **superseded**, not edited to agree.
  **M19 done and W7 closed** — §15.1's table was already on the BW125 / CR 4/5 basis, so it
  needed confirming rather than recomputing. Protocol Spec **v0.10** carries §12.1, §12.3's
  1500 ms window and §15.1. **The range-test firmware still sits on the provisional
  915.0 MHz**, recorded in §3.4 as the one loose end this decision creates.

- **v0.8** — **D1's row gains a pointer, and its status does not change.** All four bounds
  named in §2.1 have reported — M6 2026-09-09, M20 2026-09-05, M21 2026-09-06 — so D1 is now
  a decision to make rather than a measurement to run, and nothing in the register said that
  in one place. The evidence was spread across §2.1, §2.2, §5.4, the M21 findings, Protocol
  Spec §12 and §15, and the range-test log's 2026-09-09 entry;
  [`LRAN-D1-PHY-Decision-Brief`](./LRAN-D1-PHY-Decision-Brief.md) assembles it and
  recommends a working point. **The brief decides nothing and this file remains the only
  place D1's status is recorded** — when D1 closes, this row changes and the brief is marked
  superseded rather than edited to agree.

- **v0.7** — **D17 amended: the bridge is `Bridge Node`, not `LoRaBridge`.** The register
  recorded the name the project used before its own document set settled on **Bridge
  Node**, and because this is the only place a decision's status lives, the two names ran
  side by side across every document until an audit found them. `lran-bridge` remains the
  firmware target and **"LoRa Bridge" remains the HA device name**, a user-visible string
  rather than a second node name. Retired across the live document set in System PRD
  v0.11. **No decision changed state**, and no other decision is affected.
  **One live instance is left on purpose:** Protocol Spec §5.3's node-table gloss reads
  `(LoRaBridge)`. Changing it bumps the specification to v0.10 and makes all 21 binding
  citations stale, which is a large mechanical change to buy a name gloss, so it is
  **deferred to the next substantive specification revision** and recorded in D17 so it is
  not lost. Archived documents keep the old name throughout, as superseded records should.

- **v0.6** — **D31 closed: the copyright holder is Robert J. Lee.** `LICENSE` written at
  the repo root with the MIT text and `Copyright (c) 2026 Robert J. Lee`; the `<holder>`
  placeholder replaced in the 87 source files that carried it, and in `secrets.h.example`
  and `tools/vectors/embed.py`'s emitted header. **M17 is marked done** and **D11's row
  updated** — D11 itself has been resolved since v0.1 and only its dangling "holder name
  outstanding" note needed changing. **The first public push is no longer blocked**, with
  one caveat that was recorded rather than glossed and then discharged: System PRD §11.3
  also requires `THIRD_PARTY_NOTICES.md`, an attribution obligation of the MIT and BSD
  components rather than a D31 residual. It is now written, and it records what is actually
  in a build rather than restating §11.1 — auditing the two against each other turned up
  **two components in the build that §11.1 lists nowhere** (Unity, and the ThingPulse
  SSD1306 driver) and an **LCD row that names the wrong package** (M5GFX, MIT with
  LovyanGFX BSD-2-Clause inside it, resolved through M5StamPLC). Those are findings about
  the build, not decisions, so they live in the notices file's §3.
  **Header correction:** this file's header still read **v0.4** while §6 already carried a
  **v0.5** entry — the v0.5 edit bumped the changelog and not the header. Corrected here by
  numbering this revision **v0.6**, not by renumbering v0.5.
- **v0.5** — **M20 closed.** Its M21 residual is done as post-processing on the committed
  R11 trace — `tools/rangetest/survey_reintegrate.py`, host-tested, output committed as
  `2026-09-06-m20-reintegration.csv` — and the results are in new **§5.4**. The
  re-integration found an occupant cluster at **915.8–916.4 MHz** that the original
  inventory missed, including a **−54 dBm** peak at `bridge-house` on 916.0, 62 dB above
  the floor and the loudest signal in the campaign; it sits exactly where a small move off
  the provisional 915.0 MHz would land. **917.2–917.6 MHz is the Envelope A
  recommendation** on both the all-sites and deployed-sites scorings, and **909.4 MHz** is
  the Envelope B pick — with the finding that **half the US915 500 kHz grid is unusable
  here**, 914.2 MHz included, where the GateLink site sees −66 dBm. D1's frequency now has
  a ranked, reproducible answer rather than a survey to read.
- **v0.4** — **M21 closed, D33 reopened, D1 amended, D28's outlook improved.** Neither
  radio module is certified under §15.249 — both carry §15.247 DTS **and** DSS grants — and
  D33's fixed-channel, no-hopping mode exists inside those grants **only at BW500** within
  903.0–914.2 MHz. Two envelopes are documented in `LRAN-M21-FCC-Grant-Findings`;
  **Envelope A (§15.249) is the plan of record**, and the reason is measured rather than
  modelled: the 2026-09-04 gate-bearing walk ran at its **−4 dBm conducted ceiling** with
  the confirmed 3.0 dBi antenna and closed at 0 % PER across all six positions, while the
  same walk at −9 dBm — the SX1262's hard floor — lost 12.5–25 % at SF7. Envelope B is
  retained behind three explicit triggers. **D1 gains a fourth bound** tying `BW` to the
  rule section (§2.1), and **§2.2** records what M21 changed for D1 and D28 as a dated
  entry rather than by editing their rows. Recorded separately in **§3.3**: the grants
  **do not transfer** — custom firmware, a non-modular Heltec grant, an external antenna
  outside the Heltec declaration, and a co-location requirement the Wio's modular grant
  forbids — so the operative frame is **§15.23 home-built**, permanently, and no node may
  be represented as FCC certified. §3.1 is left as written and annotated, per the rule that
  a dated record is corrected by a new entry rather than rewritten. Backlog: **M20's field
  work is marked done** (the 2026-09-05 R11 re-walk) with its M21 amendment reduced to
  post-processing of the committed trace; **M6 gains the conducted-power and sanity-check
  requirements** and records how far the walk actually reached; **M5 is superseded by M23**;
  **M21 closes**; and **M22** (bridge LoRa PER under WiFi load) and **M23** (BLE RSSI and
  LoRa isolation inside the steel enclosure) are added.
- **v0.3** — **D34 added, closing Protocol Spec W12**: §9.4 steps 4–6 are **split**
  rather than placed whole — steps 4, 5 and the state half of 6 become
  `lran::CommandGate` in `/lib/lran-protocol/`, dispatch stays in the application.
  Reasoning in **§3.2**, including the finding that changes the schedule: §9.2 makes
  every authenticated type bridge → node, so **steps 4–6 apply to nothing on the
  bridge today** and the real deadline is the first firmware that accepts a `COMMAND`
  — simnode B0 and GateLink M3, **not** the range test. Two consequences worth
  carrying: `dedup_cache_depth` becomes a `lran-config` parameter, and §10.4 is
  **silent on the check/execute/record window**, recorded here as a specification gap
  rather than patched locally.
- **v0.2** — **D32 and D33 merged in** from a standalone entries file, which is now
  deleted; a second file holding decisions is exactly the split this register exists to
  prevent. **D32** fixes RadioLib as the SX1262 driver for every firmware in the repo.
  **D33** closes Protocol Spec **W5** — a single fixed channel, no hopping, at or below
  the §15.249 power provisions — and is recorded as *settled with standing conditions*
  rather than as a bare outcome, because the power ceiling is **EIRP** and a later move
  to higher conducted power, a larger fleet, or a co-channel occupant each reopens it.
  Their rationale beyond a table cell is in **§3.1**. **D1 is amended, not closed**: new
  **§2.1** records the three things that now bound it, the most consequential being that
  **range test results do not close D1 on their own** — the frequency needs the ambient
  survey and the power needs D33's grant conditions. Backlog gains **M20** (ambient RSSI
  sweep at both ends, blocking D1's frequency and D33's third standing condition) and
  **M21** (module FCC grant conditions); **M18 is marked done** with W4's closure; M6's
  owner moves to the range test tasks document. Parent link repaired for the `docs/`
  reorganization.
- **v0.1** — Initial release. Extracted from `lran-prd-v0_8` §13 and §13.1 so that
  decision churn no longer requires editing the system overview, and so that a single
  status is authoritative rather than restated across six documents. Content carried
  forward unchanged in substance; **D1–D31 retain their numbers**. Reorganized into
  open / resolved / retired sections, since a flat table of thirty-one rows in which
  twenty-two are done buries the six that need attention. Added an owner column mapping
  each decision to the document where its outcome is written up. The measurement backlog
  is renumbered **M1–M19** and split by consequence — previously it was an unordered
  list whose "top item" was identified only in prose.
