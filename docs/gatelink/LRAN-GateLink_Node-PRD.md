# LRAN GateLink Node PRD

**Document:** `LRAN-GateLink_Node-PRD`
**Version:** 0.6
**Node:** `GateLink`, node ID `0x01`
**Status:** Requirements settled. Several field measurements outstanding.
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.10**
**Companion:** [`LRAN-GateLink_Node-Implementation-Plan`](./LRAN-GateLink_Node-Implementation-Plan.md)
**Last updated:** 2026-09-10

> **This document states goals and requirements only.** Part numbers, pin maps, wiring
> detail, firmware architecture and bring-up procedure live in the implementation plan.
> Frame layouts and enumerations live in the protocol specification and are never
> restated here.

---

## Table of contents

1. [Overview](#1-overview)
2. [Goals and non-goals](#2-goals-and-non-goals)
3. [Interfaces and protocols](#3-interfaces-and-protocols)
4. [Hardware requirements](#4-hardware-requirements)
5. [Firmware requirements](#5-firmware-requirements)
6. [Power requirements](#6-power-requirements)
7. [Home Assistant entity requirements](#7-home-assistant-entity-requirements)
8. [Safety requirements](#8-safety-requirements)
9. [Test and verification requirements](#9-test-and-verification-requirements)
10. [Changelog](#10-changelog)

---

## 1. Overview

### 1.1 What GateLink is

A remote LRAN node roughly **87 m (285 ft)** from the house, controlling and monitoring a
solar-powered driveway gate installation. It presents the gate to Home Assistant as a
`cover` with a hold-open mode, reports gate and solar and battery state, transports
charge-controller configuration in both directions, and detects and classifies vehicle
traffic through the gate.

It is, architecturally, a **discrete-I/O device plus two serial transports**: it reads
debounced contacts, drives momentary relays, parses a VE.Direct stream, and polls a BLE
battery BMS. It decodes no proprietary bus.

### 1.2 Installation context

The **gate controller, charge controller, battery and GateLink all share a single
existing enclosure.** The gate motors are mounted externally on the gate itself and are
driven through dedicated motor ports on the controller — **no motor resides inside the
enclosure.**

Four consequences shape requirements elsewhere in this document:

- All signal runs — accessory I/O, VE.Direct — are short, inches to a couple of feet.
- Motor current enters the enclosure only via the controller's own supply and motor
  terminals, so worst-case ground offset on signal lines is negligible.
- The BLE battery BMS sits within inches of the node.
- **The enclosure is outdoors.** It sees sub-freezing winter mornings and summer solar
  gain, and the host platform is rated 0–40 °C (§4.5, **D29**).

### 1.3 Installed equipment GateLink interfaces to

| Equipment | Role |
|---|---|
| Nice/Apollo **1050** control board | Gate operator. Retains all safety authority (§8) |
| Victron **MPPT 75/15** charge controller | PV charging; telemetry and configuration source |
| **12 V 100 Ah LiFePO4** pack with BLE BMS | Node supply, and the only good SOC source |
| **Diablo DSP-7LP** loop detector | Two safety loops wired **in series** into one channel, producing a **single** dry contact |
| Self-contained **exit wand** sensor | At least 20 ft further inside the gate than the inside safety loop |
| Remote keypad, panel keyswitch + pushbutton, handheld remote | Existing human controls, all preserved (§3.1) |
| 2 × 2 W LED lights | Night-only lighting. **Not** controlled by GateLink, but dominant in the power budget (§6) |

---

## 2. Goals and non-goals

### 2.1 Goals — gate control

- **G-1. Momentary open.** Open the gate; the controller's auto-close returns it.
  Analogous to pressing open on a remote.
- **G-2. Open and hold.** Open the gate and hold it open until instructed to close.
  **The common case** — deliveries, contractors, guests.
- **G-3. Close.** Release the hold, or close immediately.
- **G-4.** All commands SHALL be issued through the controller's **documented accessory
  inputs**, replicating what a human control panel does.

### 2.2 Goals — monitoring

- **G-5. Gate state** — open, closed, moving — from the controller's programmable relay
  outputs.
- **G-6. Hold state observed, not assumed.** An open gate with nothing counting down to
  auto-close is a held gate, **whoever held it**.
- **G-7. Vehicle detection and direction-of-travel classification** from the existing
  safety-loop and exit-wand contacts, tapped directly.
- **G-8. Alert on vehicle detection while the gate is being held open.** This is the
  **primary operational requirement** behind G-7.
- **G-9. Immediate, separately routable alert on any FIRE assertion.**
- **G-10. Hard-shutdown / entrapment alarm state** reported.
- **G-11. Full charge-controller status**, plus **read/write access to all its
  configuration registers**, with GateLink acting as transport only.
- **G-12. Battery health and state of charge** from the pack BMS.

### 2.3 Goals — configurability and development

- **G-13. Every timing interval, window and threshold GateLink uses SHALL be settable
  at runtime from HA**, without reflashing and without a walk to the gate.
- **G-14.** Built-in bench debug tooling sufficient to exercise every code path
  **without moving a large motorized gate**.

### 2.4 Non-goals (v1)

- **BusT4.** The controller's protocol bus is not used. See
  [`LRAN-Research-Archive`](../archive/LRAN-Research-Archive.md).
- **OTA.** GateLink is USB-only. This constrains the protocol (Protocol Spec §13.1) and
  is the reason G-13 exists.
- **Gate position reporting** (percentage open). Discrete states only — §7.3.
- **Stop, partial open, step-by-step and block/release** as HA commands. Terminals for
  some of these remain free.
- **Controlling more than one gate operator or charge controller.**
- **Replacing or modifying the controller's own safety logic.** §8.
- **Firmware power optimization as a design driver.** §6.3.

---

## 3. Interfaces and protocols

### 3.1 Gate controller — documented accessory I/O

GateLink drives and reads the controller **exclusively through terminals the
manufacturer documented for third-party use.** Nothing is reverse-engineered, nothing
touches a 24 V rail, and nothing depends on a bus being awake.

#### 3.1.1 Design principle — present as a control panel

Every command is a **momentary dry-contact closure** on an input the controller already
expects a human device to drive; every status read is a dry contact the controller
already provides for signalling accessories.

Two consequences are requirements in their own right:

- **R-3.1.1a. All GateLink outputs SHALL be momentary pulses on normally-open
  contacts.** No maintained assertions anywhere. There is no relay state that, if
  welded or stuck, holds the gate in an abnormal condition. Hold-open is a **latched
  state inside the controller**, set and cleared by pulses, not a wire held down.
- **R-3.1.1b. All existing human controls SHALL be paralleled, never replaced.** Adding
  a dry contact in parallel with an existing one costs nothing and preserves every
  manual path — keypad, keyswitch, remote, front panel.

#### 3.1.2 Output requirements — four momentary relays

| Relay | Function | Serves |
|---|---|---|
| K1 | **OPEN and LOCK** | G-2 |
| K2 | **UNLOCK** | G-3 |
| K3 | **Momentary open** | G-1 |
| K4 | **Immediate close** | G-3 |

- **R-3.1.2a.** Pulse width SHALL be `relay_pulse_ms`, default **500**, runtime
  configurable. The controller's internal input debounce is undocumented and nothing in
  the design is sensitive to a longer closure; 500 ms buys actuation margin for free.
- **R-3.1.2b.** GateLink SHALL never assert two conflicting relays simultaneously, and
  SHALL enforce a minimum inter-pulse spacing.
- **R-3.1.2c.** K2 SHALL precede K4 whenever the gate is held — a locked controller
  ignores a close command. GateLink sequences this automatically; HA sees a single
  "close" operation.

#### 3.1.3 Wake is solved by construction

The controller wakes on **command-class** inputs — those that cause an action or state
change — and not on conditioning inputs that merely qualify motion already in progress.
Every relay in §3.1.2 drives a command-class input, so **the pulse that carries the
command is also the pulse that wakes the board** (**D21**).

- **R-3.1.3a.** No dedicated wake mechanism, wake relay, or fail-safe-STOP wiring SHALL
  be required, and nothing SHALL depend on the controller's standby configuration.
- **R-3.1.3b.** **Standby SHALL remain enabled** on the controller (**D20**). It is the
  largest single lever in the power budget (§6.2).
- **R-3.1.3c.** GateLink SHALL allow `post_wake_settle_ms` (default **500**) after a
  pulse before evaluating state, and SHALL NOT conclude a command failed until at least
  `command_confirm_timeout_s` (default **5**) has elapsed.

#### 3.1.4 Input requirements — six isolated inputs

| Input | Source | Reads |
|---|---|---|
| IN1 | OUT1 relay, programmed **OPEN** | Gate is at the fully-open position |
| IN2 | OUT2 relay, programmed **MOVING** | Gate is in motion **or** open with the auto-close timer running |
| IN3 | Loop detector contact, paralleled | Safety loops (either loop) |
| IN4 | Exit wand contact, paralleled | Exit wand |
| IN5 | FIRE terminal, sensed | Someone used the keypad FIRE code |
| IN6 | Alarm output, sensed | Hard shutdown / entrapment latch |

**R-3.1.4a. Gate and hold state SHALL be derived from IN1 and IN2 as follows:**

| IN1 (OPEN) | IN2 (MOVING) | State |
|---|---|---|
| 0 | 0 | **Closed and idle** — the only state in which the controller can sleep |
| 0 | 1 | **Moving** — opening from closed, or closing once past the open limit |
| 1 | 1 | **Open, auto-close countdown running** — the gate will close on its own |
| 1 | 0 | **Open and held** — nothing is counting down, so a lock is in force |

> **The bottom row is the one that carries this design.** `OUT = Moving` stays
> energized through the auto-close countdown, not merely during motion. An open gate
> with no countdown is therefore a gate somebody locked open — and **it does not matter
> who**: LRAN's own OPEN+LOCK, the programmed handheld remote, the keyswitch, or the
> keypad FIRE code all land in the same observable state. This makes hold state a
> *measurement* rather than a flag the firmware hopes is still true, and it is what
> lets G-8's alert cover manual holds (**D18**).

- **R-3.1.4b.** A hold SHALL NOT be declared until a stable 1/0 reading has persisted
  for `hold_confirm_ms` (default **2000**). The 1/1 state is briefly re-entered at the
  start of a close cycle before the gate leaves the open limit, and whether MOVING dips
  momentarily at the open limit is **`TBM`** (M2). The same timer absorbs both.
- **R-3.1.4c.** Direction of travel during motion SHALL be inferred from the previous
  stable state, which GateLink tracks. This is sufficient for the `cover` entity.
- **R-3.1.4d.** **Acquisition SHALL be by polling, not interrupt**, at `input_poll_ms`
  (default **100**, configurable 20–500), with debounce expressed as
  `input_debounce_samples` (default **2**) — two consecutive agreeing reads.

> **Why 100 ms is generous, not marginal.** Direction discrimination (§3.2) operates on
> windows of seconds to tens of seconds; the closest pair of events of interest is
> separated by at least 20 ft of driveway; and the controller's own inputs are debounced
> in hardware. 100 ms leaves three orders of magnitude of margin and gives the I/O task
> room to be preempted by BLE or VE.Direct work without missing an edge. The interval
> may also be **stretched at runtime** if the timing budget ever gets tight — a
> configuration change, not a reflash.

- **R-3.1.4e.** GateLink SHALL publish the raw debounced input bits alongside every
  derived state, so that a derivation bug is diagnosable from a logged frame without a
  firmware change or a walk to the gate.

### 3.2 Vehicle detection — **REQUIRED**

#### 3.2.1 Installed detection hardware

| Signal | Source | Position | Semantics |
|---|---|---|---|
| **SAFETY** (IN3) | Loop detector | Two loops, one outside and one inside the gate, wired **in series** into one channel | **Single** dry contact. Asserts when a vehicle is over **either** loop. The two loops are *not* separately observable |
| **EXIT** (IN4) | Self-contained wand sensor | **At least 20 ft** further inside the gate than the inside safety loop | Separate dry contact |

Both contacts are tapped **in parallel** with their existing connections to the
controller.

#### 3.2.2 Direction classification

| First rising edge | Then | Classification |
|---|---|---|
| `EXIT` | `SAFETY` | **EXIT** — vehicle departing |
| `SAFETY` | `EXIT` | **ENTRY** — vehicle arriving |
| either | *(no second edge within window)* | `UNDETERMINED` |
| both within one debounce period | — | `UNDETERMINED` |

Four properties of the installed topology shape the logic:

1. **The window must be large.** A departing vehicle trips EXIT, then waits for the
   gate to open before reaching SAFETY. Gaps of 10–30 s are normal.
2. **SAFETY cannot distinguish inside from outside.** A traversal produces one
   continuous SAFETY assertion spanning both loops, not two separable pulses. **The
   classifier must not attempt to read structure inside a single SAFETY assertion.**
3. **EXIT is also an actuator.** The wand's assertion causes the controller to open the
   gate, so an open transition shortly after an EXIT rising edge is self-evidently
   exit-triggered.
4. **Wand hold behaviour must be characterized** (**M10**). Some self-contained wands
   de-assert after a hold time even with a vehicle stationary above them, which would
   produce repeated edges from an idling vehicle.

#### 3.2.3 Requirements

- **R-3.2.3a.** GateLink SHALL represent the instantaneous state of SAFETY and EXIT as
  discrete booleans in the status payload.
- **R-3.2.3b.** GateLink SHALL derive direction of travel from the rising-edge ordering
  in §3.2.2.
- **R-3.2.3c.** A detection event SHALL trigger an **immediate unsolicited push**,
  independent of the poll schedule.
- **R-3.2.3d.** Classification SHALL be implemented as a state machine with an explicit
  idle-reset condition, so that partial traversals — a vehicle pulls up to the wand and
  reverses; a vehicle stops on the loops and waits — resolve to `UNDETERMINED` rather
  than corrupting the next real traversal.
- **R-3.2.3e.** Configurable parameters: `detect_sequence_window_ms` (default
  **60000**), `detect_sequence_idle_ms` (default 10000), `input_debounce_samples`
  (default 2), `held_open_alert_repeat_s` (default 0 = one alert per detection).

#### 3.2.4 Held-open alert — the primary operational requirement

The need: *the gate is sometimes deliberately left standing open — deliveries,
contractors, guests — and in that state any vehicle movement through it should notify by
email and SMS from HA.*

- **R-3.2.4a.** The gate is **HELD_OPEN** whenever §3.1.4 reports a confirmed hold,
  **whatever the source** — LRAN's own command, the handheld remote, the keyswitch, the
  keypad FIRE code, or a hold observed across a reboot and therefore unattributable.
- **R-3.2.4b.** While HELD_OPEN, any rising edge on SAFETY or EXIT SHALL:
  1. Raise a `vehicle_while_held_open` alert in an **immediate event push**, carrying
     the hold source.
  2. Include the direction classification if available at push time, and **send a
     follow-up refining the same event** when classification completes.
- **R-3.2.4c. There SHALL be no arming delay.** Alerts fire on the first edge, with no
  dead window.

> **Why no arming delay.** With hold state *observed* rather than inferred, there is no
> risk of firing on a normal traversal: a routine pass leaves the auto-close countdown
> running, which asserts IN2, which is not a hold. An earlier revision carried a
> 30-second arming delay that existed solely to suppress false positives from an
> inferred "statically open" condition. Measuring the condition removes the need for it.

- **R-3.2.4d.** Because this drives email and SMS, the event SHALL be delivered as a
  **non-retained** message (Protocol Spec §16.3), not solely as a retained
  `binary_sensor` state. Retained sensors replay on HA restart and on discovery
  refresh, which would produce spurious 2 AM notifications. **The binary sensor is for
  dashboard visibility; the event is the automation trigger.**

#### 3.2.5 FIRE is an emergency, not a mode

The keypad's FIRE code is used **only for testing and in a real fire emergency.** It is
not a routine hold-open mechanism, and treating an assertion as merely another way to
hold the gate open understates it.

- **R-3.2.5a.** Any rising edge on IN5 SHALL generate an **immediate, independent event
  push on its own topic**, non-retained, regardless of gate state — in addition to
  recording the hold source when the gate is open.
- **R-3.2.5b.** GateLink **senses** FIRE and **never asserts it** (§8).

> Keeping FIRE separate from the held-open event lets HA route it differently. This is
> the one signal in the system that should be allowed to wake someone up.

### 3.3 Charge controller — VE.Direct

- **R-3.3a.** GateLink SHALL continuously parse the ~1 Hz **text protocol** stream,
  cache the latest complete snapshot, and watch the charger error field.
- **R-3.3b.** GateLink SHALL support the bidirectional **HEX protocol** for read and
  write of all charge-controller configuration and status registers, acting as
  **transport only** — it SHALL NOT interpret register semantics, hold a register
  cache, or replay writes.
- **R-3.3c.** Text and HEX SHALL be multiplexed on one UART. **The text protocol SHALL
  NOT be disabled**; the 1 Hz stream is the primary telemetry source.
- **R-3.3d.** One outstanding HEX transaction at a time. On timeout
  (`hex_timeout_ms`, default 1000) GateLink SHALL return an explicit timeout status
  rather than silence.
- **R-3.3e.** GateLink SHALL enforce the write-authentication rule: HEX Set and Restart
  commands require a valid MAC (Protocol Spec §7.6). This is one of three independent
  gates on charge-controller writes; the other two are enforced on the bridge.
- **R-3.3f.** GateLink SHALL publish an explicit **staleness flag** when no complete
  text frame has arrived within timeout.

> **Why write protection is a safety requirement and not a convenience.** Writing charge
> parameters under LiFePO4 is a **battery-damage path**. Re-enabling temperature
> compensation or equalization on a lithium pack is exactly the kind of one-character
> mistake that is invisible until the battery is harmed.

### 3.4 Battery BMS — BLE

- **R-3.4a.** On `bms_poll_s` (default **300**), GateLink SHALL connect over BLE, read
  state of charge, pack voltage, pack current, cell voltages, temperatures and
  alarm/protection flags, then **disconnect**.
- **R-3.4b.** GateLink SHALL de-initialize the BLE controller between polls rather than
  leaving the stack resident.
- **R-3.4c.** BMS alarm or protection flags SHALL trigger an immediate unsolicited push.
- **R-3.4d.** A failed BLE connection SHALL NOT block or delay any other node function.
  BMS data is published with a **staleness age**; consumers treat absence as unknown,
  **not as zero**.
- **R-3.4e.** The connect/read/disconnect cadence SHALL leave the BMS reachable from a
  phone between polls. The pack accepts **one connection at a time**, so a persistent
  connection would lock the vendor app out entirely. At the default interval the BMS is
  free ~98% of wall-clock time, and an HA switch SHALL allow polling to be suspended.
- **R-3.4f.** GateLink SHALL publish the **BLE link RSSI** as ongoing evidence for
  **D28**.

> **Why the BMS and not the charge controller.** LiFePO4 has a famously flat discharge
> curve — roughly 13.2–13.3 V across 20–80% SOC — so voltage-derived SOC is close to
> meaningless through the middle of the range. The charge controller compounds this: it
> measures **charge** current only, so coulomb counting on discharge is unavailable from
> it. **The BLE BMS is the only good SOC source in the base BOM** (**D15**).

- **R-3.4g. SOC provenance SHALL be published.** The fallback chain is `bms_ble` →
  `smartshunt` → `voltage_coarse`, and which one is in use is a runtime condition. An
  SOC figure whose provenance is invisible is worse than no SOC figure.

### 3.5 LoRa and the bridge

GateLink is a LoRa endpoint. All framing, addressing, authentication, sequencing,
fragmentation and media-access behaviour is defined in
[`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) and is **not restated
here**. Node-specific obligations:

- **R-3.5a.** GateLink is node `0x01` and emits status schema `0x10`, event schema
  `0x11`, config schema `0x12` and health schema `0xF0`.
- **R-3.5b.** GateLink SHALL transmit on: a poll request; gate or hold state change;
  non-zero charger error; vehicle detection; FIRE assertion; hard-shutdown alarm; BMS
  alarm; configuration change; boot.
- **R-3.5c.** GateLink SHALL verify MAC, context and sequence before pulsing any relay,
  before applying a configuration change, and before passing a HEX write.
- **R-3.5d.** GateLink SHALL deduplicate commands on `(ctx_id, seq)` and return the
  cached ACK rather than pulsing twice. **A relay pulse is not idempotent**; the bridge
  retrying an ACK it never received must not move the gate again.

---

## 4. Hardware requirements

Part numbers, pin assignments and wiring detail are in the implementation plan. These
are the requirements those choices must satisfy.

### 4.1 I/O requirement

**Four momentary dry-contact relay outputs and six inputs, two of which are 12 V
voltage-sense.** This is the requirement that drives platform selection.

### 4.2 Host platform

- **R-4.2a.** The host SHALL be an **M5Stack StamPLC** (Stamp-S3A / ESP32-S3FN8).

> **Why this is a requirement rather than an implementation choice.** The I/O profile in
> §4.1 is met **natively** by an industrial controller — four SPDT relays on screw
> terminals, eight opto-isolated 5–36 V inputs, 6–36 V supply input, DIN mount — and met
> by a bare radio module only with an external relay module, per-channel resistive
> dividers with clamp diodes, and a 12 V→USB-C adapter bolted on. The platform choice
> **removes three subassemblies and every hand-built discrete circuit from the design**,
> which is a long-standing project preference, and it aligns GateLink with AquaLink
> (System PRD §3.5). The cost is that the platform has no radio, which §4.3 addresses.

- **R-4.2b.** Relays, inputs, buttons and indicators sit behind I²C expanders and
  therefore consume **no general-purpose GPIO**. The design SHALL preserve that
  property; anything that spends GPIO on I/O breaks the radio pin budget.
- **R-4.2c.** Nonvolatile state SHALL live on **microSD**, not onboard flash, so a node
  rebooting repeatedly under a marginal supply does not wear internal flash.
- **R-4.2d.** The microSD SHALL be **optional by design**: absent, the node runs on
  firmware defaults and accepts runtime configuration into RAM only.

### 4.3 Radio

- **R-4.3a.** LoRa SHALL be an **external SX1262 module, 915 MHz, SPI**, on a carrier
  board.
- **R-4.3b.** The module SHALL use **DIO2 for RF switching and SHALL NOT require
  separate TXEN/RXEN lines.** The pin budget has no room for RF-path control, which
  excludes most PA/LNA "long range" variants.
- **R-4.3c.** The module SHALL run from 3.3 V and SHALL state its **TCXO voltage** —
  the radio driver must be given the correct value or the radio will not calibrate.
- **R-4.3d.** The carrier SHALL provide a **local 3.3 V regulator** with dropout
  ≤300 mV. The host exposes **no 3.3 V rail** and its 5 V expansion rail sits near
  4.76 V under load (**D26**).
- **R-4.3e.** The LoRa antenna SHALL be **outside the steel gate-controller enclosure**,
  via an SMA bulkhead on that enclosure. See §4.3.1 for the full RF path, which crosses
  two bulkheads, and for what depends on it.
- **R-4.3f.** The carrier SHALL be **DIN-mounted**, not free-floating on its header. The
  SMA bulkhead and the VE.Direct cable both pull on the board, and an outdoor enclosure
  that sees a seasonal thermal cycle is no place for something hanging on a connector.
- **R-4.3g.** The SX1262 reset SHALL NOT be wired to the host's shared peripheral reset
  line. A radio driver pulsing that line would also reset the display and an I/O
  expander — **and therefore risks disturbing relay state.**
- **R-4.3h.** The node SHALL enforce **strict mutual exclusion between LoRa transmit and
  BLE activity**, via one shared interlock rather than two independent schedulers. The BMS
  poll and the LoRa TX path are inches apart inside a metal cavity (§4.3.1); this is cheap
  here and, unlike on the bridge, there is nothing that requires a continuously held radio
  link. **See the Bridge PRD for why the bridge deliberately does not do this** — the
  asymmetry is a decision, not an oversight.
- **R-4.3i.** TX power SHALL be configured as **two runtime parameters**,
  `tx_conducted_dbm` and `antenna_gain_dbi`, never one combined EIRP figure, and the
  firmware SHALL compute EIRP from them and **refuse to transmit above the configured
  envelope ceiling**. The envelope itself (Protocol Spec §18.2) is a runtime parameter, and
  a bandwidth/power combination valid in one envelope and not the other SHALL be rejected.
  GateLink has no OTA: a compile-time ceiling is a walk to the gate.

### 4.3.1 The enclosure stack and the RF path

Confirmed **2026-09-06**. Recorded as requirements because two separate analyses now depend
on this arrangement, and a mechanical change made for unrelated reasons would silently
invalidate them.

```
steel gate-controller enclosure  (outdoors, at the gate)
├── 1050 gate controller, MPPT charge controller, Reno loop detector
├── 100 Ah LiFePO4 pack + BMS              ← the BLE target, 6-8 in away
└── plastic GateLink enclosure             ← RF-transparent
    └── StamPLC + carrier
        └── Wio-SX1262 IPEX
              -> SMA-female bulkhead on the PLASTIC enclosure
              -> SMA male-to-female jumper
              -> SMA-female bulkhead on the STEEL enclosure
              -> 19 cm stick antenna, claimed 3.0 dBi, vertical, OUTSIDE
```

- **R-4.3.1a.** The LoRa antenna SHALL be a **vertically mounted 19 cm stick of nominally
  3.0 dBi**, and that gain figure SHALL be what is written into `antenna_gain_dbi`
  (R-4.3i). It is the direct term in the EIRP calculation and the conducted ceiling that
  follows from it is **−4 dBm** (Protocol Spec §18.2).
- **R-4.3.1b.** The **steel** enclosure SHALL remain conductive and substantially closed.
  **What would falsify the analyses that rest on it:** replacing it with a non-conductive
  enclosure, cutting a large aperture, or moving the LoRa antenna inboard of it. Any of
  those invalidates the LoRa-to-BLE isolation argument (M21 handoff §4) and the premise
  that makes the EIRP figure meaningful at all. **The tracked check is M23**, which
  measures the isolation term directly.
- **R-4.3.1c.** The **plastic** inner enclosure carries no RF analysis and may be changed
  freely. It is transparent at both 915 MHz and 2.4 GHz. Recorded so that a later reader
  does not treat the two enclosures as interchangeable.
- **R-4.3.1d.** The internal LoRa run SHALL use **properly shielded assemblies with sound
  connectors**, kept short and routed away from the motor harnesses. It is the one radiator
  that sits *inside* the cavity with the BLE receiver and the BMS, and leakage there has
  nowhere to go. **A poor BLE reading at M23 is a cable and connector question before it is
  an antenna verdict.**
- **R-4.3.1e.** Feedline and connector loss across the two bulkheads (0.5–1.5 dB) SHALL be
  assumed **0 dB for compliance** and **1.5 dB for link budget**. Never one figure for
  both, and never the flattering one for compliance.

### 4.4 Supply

- **R-4.4a.** GateLink SHALL be powered **directly from the 12 V LiFePO4 pack** through
  an inline fuse to the host's VIN terminal block. No intermediate 12 V→5 V adapter.
- **R-4.4b.** The node SHALL measure and publish **its own supply voltage and current**
  from the onboard INA226. Node consumption becomes a measured telemetry value rather
  than a budget assumption.
- **R-4.4c. Nothing SHALL depend on a clean shutdown or on RAM surviving a power
  event.** Unlike a lead-acid pack, which sags, a LiFePO4 pack goes to **zero volts at
  the terminals** when the BMS opens. GateLink will not brown out gracefully; it will
  drop dead and reboot when the BMS re-closes.

> This is benign for the gate interface: all relays de-energize and assert nothing, and
> the gate's true state — including whether it is being held open — is readable from
> IN1/IN2 the moment the node returns. Short of a wiring fault there are exactly two
> ways this node loses power: the pack BMS opens, or the inline fuse blows. The first
> announces itself days ahead through BMS telemetry (§3.4); the second is
> catastrophic-only.

### 4.5 Environment

- **R-4.5a.** The host is specified **0–40 °C** and the enclosure is outdoors. **This
  is a deliberate, instrumented exceedance, not an oversight** (**D29**).
- **R-4.5b.** GateLink SHALL publish **all three available temperature sensors** — the
  host's own, the charge controller's, and the pack's — and log them across a full
  season before any thermal mitigation is fitted.

> **The concern is the high end.** At the cold end the exposed parts are electrolytics,
> the LCD (which ghosts badly when cold and is a debug aid, not a functional dependency)
> and RTC crystal accuracy — none of them things the system depends on, and the exposure
> is a few hours of a winter morning. At the hot end the concern is real and cumulative:
> a sealed box in summer sun is the case that shortens component life. Mitigation is a
> ladder — verify the existing screened vents, then shade, then a thermostatic fan —
> and a decision to climb it should rest on **logged maxima, not a datasheet number**.
> The pack's own low-temperature charge inhibition (§6.4) makes enclosure temperature a
> value the system wants regardless, so instrumenting D29 costs nothing extra.

### 4.6 Detector power

- **R-4.6a.** The loop detector SHALL be powered from an **ungated** supply terminal, so
  that it remains live during controller standby.

> Without this, the held-open alert is unimplementable: the gate standing open is
> precisely when the controller sleeps, and a detector on gated power would be dark —
> and it would not matter how GateLink acquired the contact. Cost is ~1 mA continuous,
> 0.02 Ah/day. **Secondary benefit, independent of LRAN:** inductive loop detectors
> need time to tune after power-up, and continuous power removes the window where the
> detector was powering up cold at the moment a gate cycle began. *(Already done in the
> field — see the implementation plan's integration observations.)*

### 4.7 Display

- **R-4.7a.** The display SHALL be **off by default in all normal operating modes**.
- **R-4.7b.** A user button SHALL toggle it, starting an inactivity timer
  (`display_timeout_s`, default 60); a press while on SHALL blank immediately.
- **R-4.7c.** The display SHALL power on and **remain on, ignoring the inactivity
  timer, whenever any debug mode is active**, returning to manual behaviour when the
  last debug mode exits (**D6**).
- **R-4.7d.** Only the **backlight** is switched; the panel stays initialised.

> An earlier design switched display *rail power*, which required full re-initialisation
> after every cycle — a known way to build a display that works exactly once. Switching
> the backlight retires that failure mode entirely.

---

## 5. Firmware requirements

### 5.1 Responsibilities

1. **Command driver** — pulse K1–K4 on authenticated command; enforce pulse width and
   inter-pulse spacing; never assert conflicting relays.
2. **State reader** — debounce IN1–IN6; derive gate **and hold** state; maintain the
   hold source.
3. **Detection and direction classification** (§3.2).
4. **VE.Direct text reader** — parse continuously, cache, watch the error field.
5. **VE.Direct HEX transport** — multiplex with the text stream; enforce the write
   rule. **No local interpretation of register semantics.**
6. **BLE BMS client** — duty-cycled connect / read / disconnect.
7. **LoRa endpoint** — receive commands, transmit status/ACK/event; CAD and backoff
   before transmit; fragmentation for oversized payloads.
8. **Trigger and cache logic** (R-3.5b).
9. **Authentication** (R-3.5c, R-3.5d).
10. **Configuration management** (§5.3).
11. **Power management** — WiFi **off**; BLE **duty-cycled**; **backlight off** in
    normal operation. No light-sleep, no RX duty-cycling (§6.3).
12. **Debug tooling** (§5.4).

### 5.2 Scheduling requirement

- **R-5.2a. The I/O service SHALL NOT be starved by any blocking call.** Inputs are
  polled and relay pulses are timed in firmware, both over the same I²C bus. BLE
  polling, VE.Direct HEX round-trips and LoRa transmission SHALL NOT run inline with the
  I/O loop — put I/O on its own task or a strict cooperative slot.

> The 100 ms poll relaxation (R-3.1.4d) makes this far less delicate than a 10–20 ms
> poll would, but the requirement stands: **a relay pulse whose trailing edge is late is
> a command of the wrong length.**

### 5.3 Configuration management

- **R-5.3a. Every timing interval, window, threshold and debounce value GateLink uses
  SHALL be changeable at runtime, from HA, without reflashing** (G-13). GateLink has no
  OTA, it is ~87 m away, and reflashing means a laptop and a walk.
- **R-5.3b.** Configuration SHALL be carried by an **authenticated** frame pair. A
  parameter that changes how the gate is driven is a command.
- **R-5.3c.** Three layers: **defaults compiled into firmware**, **overrides on
  microSD** loaded at boot, **live values in RAM**.
- **R-5.3d.** A change SHALL be applied to RAM immediately, then written to SD. **With
  no card fitted, or an unwritable one, the change SHALL still be applied and still
  ACKed — reported explicitly as applied-but-not-persisted.**

> A deliberate degradation, in two directions at once: a missing card must never make
> the node unconfigurable, **and HA must never be told a value was saved when it was
> not.** The persistence status is published as a diagnostic sensor, so a node quietly
> running unsaved configuration is visible rather than surprising.

- **R-5.3e.** GateLink SHALL publish its **full effective configuration** on boot, on
  change, and on request, with each value marked `default` or `override`. A
  restore-defaults command SHALL clear all overrides.
- **R-5.3f.** Unknown keys SHALL be rejected individually with a reason, never silently
  ignored; the remainder of the set still applies. Out-of-range values SHALL be
  **clamped to the documented range with the clamp reported**, not applied quietly.
- **R-5.3g.** microSD SHALL also carry on-node logging and retained counters — which is
  the point: repeated writes stay off internal flash (R-4.2c).

#### 5.3.1 What is *not* runtime-configurable

- **The HMAC key** and any other secret. Flash only, provisioned over USB.
- **LoRa PHY parameters.** Changing these from HA means changing the link you are
  changing them over; one mismatch and the node is unreachable until someone walks to
  it with a laptop.
- **Node ID and schema versions**, which are contractual.

### 5.4 Debug and bench tooling requirements

- **R-5.4a. Relay dry-run mode.** A switch that makes GateLink **log and publish every
  relay pulse it would have issued without energizing anything.** This is the single
  most valuable debug feature: command logic can be exercised end to end — HA → LoRa →
  bridge → node → decision — **without moving a large motorized gate.**
- **R-5.4b. Input injection.** Synthetic assertions on IN1–IN6 in configurable order and
  spacing. **SHALL cover the long cases** — 30 s EXIT→SAFETY gaps and partial
  traversals — since those are exactly what a driveway test is slowest to reproduce.
- **R-5.4c. Packet loopback**, both RF (echo received frames) and internal (feed
  transmitted frames back into the receive parser with no radio).
- **R-5.4d. Dummy status push** — synthetic VE.Direct and gate-state data exercising the
  full pipeline without real hardware. Synthetic frames SHALL be **marked as synthetic
  all the way into HA history.**
- **R-5.4e. On-node logging to microSD**, leveled and rotating, so a fault occurring
  while the LoRa link is down is still recoverable afterwards.
- **R-5.4f.** Leveled serial logging.
- **R-5.4g.** Entering any debug mode SHALL light the display (R-4.7c).

---

## 6. Power requirements

### 6.1 Source

- **R-6.1a.** 12 V **100 Ah LiFePO4**, 50 W panel, MPPT 75/15. **100% of nameplate
  capacity is usable.**

| | Previous (75 Ah lead-acid) | Current (LiFePO4) |
|---|---|---|
| Usable | 37.5 Ah (50% derate) | **100 Ah** |
| Daily draw | ~7.3 Ah | **~7.0 Ah** |
| Autonomy at zero harvest | ~5.1 days | **~14.3 days** |
| Winter harvest ratio | 1.45× | **~1.5×** |

- **R-6.1b.** The charge controller SHALL be reconfigured for LiFePO4 **before the pack
  is first charged** — battery type user-defined, equalization **disabled**, temperature
  compensation **0 mV/°C**, absorption and float per the pack specification. Final
  values recorded in `/docs/mppt-config.md`.

> **Sequencing note.** This must happen before LRAN exists, so initial reconfiguration
> is done with the vendor app over a USB cable. **LRAN's HEX path is for ongoing
> adjustment and verification** — and for reading configuration back to confirm it has
> not drifted — not for initial setup. On boot GateLink reads the charge parameters so
> that a wrong profile is visible in HA rather than latent.

### 6.2 Standby is the largest lever

- **R-6.2a.** Controller standby SHALL be retained (R-3.1.3b), and the OUT terminal
  programming SHALL be chosen so that the **resting state — closed and idle — remains
  free to sleep.**

| Programming | Standby is inhibited… | Verdict |
|---|---|---|
| `OUT = Closed` | whenever the gate is closed — i.e. nearly always | **Never use.** Effectively disables standby |
| `OUT = Open` | only while the gate stands open | **Chosen for OUT1.** Bounded, zero in the resting state |
| `OUT = Moving` | while moving, or counting down | **Chosen for OUT2.** Standby-neutral — the board is awake in both conditions anyway |

> **Any energized OUT relay prevents standby** (**D23**). The coil is a load the board
> will not carry while asleep, so programming an OUT terminal is also a decision about
> when the controller is allowed to sleep — and the wrong choice is quietly expensive.
> Disabling standby outright would cost roughly 19% of winter harvest and contradict the
> manufacturer's own solar guidance.

- **R-6.2b.** The **residual cost is the hold-open case**: while the gate is held, OUT1
  is energized and the controller runs at active current for the duration. This is
  unavoidable — any programming that reports "open" must hold a coil while open — and it
  is bounded, self-limiting, and visible in telemetry.

### 6.3 Firmware power optimization is not a design driver

- **R-6.3a.** GateLink SHALL prefer the **simple, always-on, low-latency design** in
  every case. No light-sleep, no RX duty-cycling, no night profile.

> **Findings that support this.** The two 2 W LED lights are ~57% of site consumption —
> more than every other load combined — so real headroom comes from LED runtime, not
> firmware. A bigger battery does not fix a harvest deficit; it extends the
> ride-through, and what 100 Ah bought is **cloud tolerance and time to notice.** The
> RX duty-cycling design would have saved ~0.36 Ah/day — **0.36% of usable capacity** —
> against real costs in complexity, latency and overnight data continuity. And the
> radio is not the constraint anyway: continuous LoRa receive adds ~5 mA on top of an
> ESP32-S3 that dominates at tens of mA, and the MCU stays awake regardless to parse the
> 1 Hz VE.Direct stream. *(The duty-cycling design is preserved as a reserved feature in
> Protocol Spec §17.1, for a possibly battery-powered WellLink.)*

### 6.4 Low-temperature charge inhibition

The pack's BMS blocks charging below approximately 0 °C. This is correct behaviour and
is accepted — the reserve covers it. **But it must be observable**, because its symptom
— a battery not recharging on a sunny day — is otherwise indistinguishable from a
failing panel or charge controller.

- **R-6.4a.** GateLink SHALL publish a **charging-inhibited** indication, derived from
  the BMS flag and pack temperature when available.
- **R-6.4b.** GateLink SHALL ALSO be able to infer it **from VE.Direct alone**, for the
  case where BLE is unavailable:

  > PV power present **AND** charger state is bulk/absorption **AND** battery current
  > ≈ 0 **AND** battery voltage not rising, sustained for `charge_inhibit_confirm_s`
  > (default 300) ⇒ **charging inhibited**

- **R-6.4c.** GateLink SHALL maintain a **cumulative Ah drawn since charging was last
  active**, so a multi-day cold event shows consumed reserve rather than just a boolean.
- **R-6.4d.** Upgrading the panel does not help while charging is inhibited, and the
  documentation SHALL say so where a panel upgrade is discussed.

---

## 7. Home Assistant entity requirements

Entity *types and sources* are requirements; discovery payloads and topic strings are
implementation. Topic grammar and retention rules are in Protocol Spec §16.

### 7.1 Control surface (**D8**)

**A `cover` with `device_class: gate` is the primary control surface** — native
open/close UI, voice-assistant support, dashboard cards, standard `cover.*` services.
Driven from real IN1/IN2 state, non-optimistic.

| HA operation | GateLink action |
|---|---|
| `cover.open_cover` | G-1 — momentary open |
| `cover.close_cover` | G-3 — release, then immediate close if configured |
| `switch.turn_on` ("Hold gate open") | G-2 — open and lock |
| `switch.turn_off` ("Hold gate open") | Unlock; auto-close then closes |

**A `switch` rather than a second cover.** Hold-open is a persistent *mode*, not a
momentary action, and a switch models a mode correctly — it shows current state, it is
togglable from a dashboard, and it can be used as an automation condition. A button
would lose the state.

**The switch reflects the gate, not just LRAN.** Its state comes from the observed hold
derivation, so a hold set from the handheld remote or the keyswitch shows as on. Turning
it off releases the hold regardless of who set it, which is the behaviour a user expects
from a switch that claims to show one.

`cover.stop_cover` is **not implemented in v1**; the entity may omit stop or expose it
as unavailable.

### 7.2 Required entities

**Gate control and state**

| Entity | Type | Source |
|---|---|---|
| Gate | `cover` (gate) | IN1/IN2 |
| Hold gate open | `switch` | derived hold state |
| Hold source | `sensor` (text, diagnostic) | derived |
| Close immediately | `switch` (config) | — |
| Gate state | `sensor` (text, diagnostic) | IN1/IN2 |
| Auto-close pending | `binary_sensor` | IN2 while IN1 asserted |
| Held open by keypad | `binary_sensor` | IN5 |
| **FIRE asserted** | **`event`, non-retained** | IN5 — **route separately** |
| Hard shutdown | `binary_sensor` (problem) | IN6 |
| Movement cause | `sensor` (text) | derived |

**Vehicle detection**

| Entity | Type | Source |
|---|---|---|
| Safety detector | `binary_sensor` (occupancy) | IN3 |
| Exit wand | `binary_sensor` (occupancy) | IN4 |
| Last traversal direction | `sensor` (text) | derived |
| Last traversal time | `sensor` (timestamp) | derived from age |
| Vehicle while held open | `binary_sensor` (problem) | **dashboard visibility only** |
| **Vehicle while held open** | **`event`, non-retained** | **the automation trigger for email/SMS** |

**Battery and solar**

| Entity | Type | Source |
|---|---|---|
| Battery SOC | `sensor` (%) | BMS |
| Battery SOC source | `sensor` (text, diagnostic) | node — honest about provenance |
| Pack voltage / current | `sensor` | BMS |
| Battery voltage, charge current | `sensor` | MPPT |
| Pack temperatures | `sensor` ×4 | BMS |
| Cell voltages | `sensor` ×N (diagnostic) | BMS — publish on change |
| BMS alarm / protection flags | `binary_sensor` ×N | BMS |
| Cycle count / capacity | `sensor` (diagnostic) | BMS |
| **Charging inhibited** | `binary_sensor` (problem) | derived (§6.4) |
| BMS BLE polling | `switch` | node |
| Panel voltage / power, yield today | `sensor` | MPPT |
| Charge state, charger error | `sensor` (text/code) | MPPT |
| MPPT config readback | `sensor` ×N (diagnostic) | MPPT via HEX |
| MPPT config write enable | `switch` | **bridge-enforced** |

**Node health, environment, configuration, diagnostics**

| Entity | Type | Source |
|---|---|---|
| Node supply voltage / current | `sensor` (diagnostic) | INA226 |
| Enclosure temperature | `sensor` | LM75 |
| MPPT temperature | `sensor` (diagnostic) | MPPT |
| Node uptime, boot count | `sensor` (diagnostic) | node |
| BMS link RSSI | `sensor` (diagnostic) | node — evidence for **D28**, and the continuous dataset **M23** reads a trend from. Required, not optional: a cavity null that develops after install shows here as a trend rather than as a silent failure |
| LoRa RSSI / SNR, missed polls, protocol version | `sensor` (diagnostic) | bridge |
| Commonly-tuned parameters | `number` (config) ×N | §5.3 |
| Configuration persisted | `binary_sensor` (diagnostic) | false ⇒ running unsaved config |
| Restore defaults | `button` | §5.3 |
| Relay dry-run | `switch` (config, diagnostic) | §5.4 |
| Poll interval | `number` (config) | bridge |

A `number` entity is required **per commonly-tuned parameter**, plus a raw key/value
path for the long tail. **Do not attempt to model every parameter as an entity.**

### 7.3 Position reporting — deferred

**v1 will not report position.** Time-based estimation on a gate that can be stopped,
reversed or obstructed produces confident wrong answers, and **a cover that lies about
being 40% open is worse than one reporting discrete states.**

---

## 8. Safety requirements

- **S-1. The gate controller remains the safety authority.** Obstruction, photocells and
  detector logic stay with the operator. LRAN issues commands through documented
  accessory inputs and *observes* detector state; **it must never be relied on to
  prevent unsafe motion.** §3.2 is a monitoring and notification feature, not a safety
  feature.
- **S-2. No safety interlock is bypassed.** FIRE was explicitly rejected as a command
  path because it **clears hard shutdown** — a latched entrapment state that must
  require human intervention. A remote path that cleared it would put an invisible
  bypass around a UL325 interlock. GateLink *senses* FIRE and *reports* hard shutdown; it
  never asserts either.
- **S-3. No lock-out hazard.** SHADOW was likewise rejected as a hold mechanism: it also
  *maintains a closed gate closed*, so a stuck assertion with the gate shut would prevent
  anyone — keypad, wand, remote, GateLink — from opening it.
- **S-4. No maintained assertions.** Every output is a momentary pulse on a
  normally-open contact. An unpowered, crashed or removed GateLink asserts nothing and
  the gate behaves exactly as it does today.
- **S-5. The privileged control gets the key.** The panel keyswitch drives OPEN+LOCK and
  the unsecured pushbutton drives UNLOCK. The reverse would let anyone who reaches the
  controller box hold the gate open indefinitely — a security defect at the panel.
- **S-6. Two independent manual UNLOCK paths are required and provided** (**D24**): the
  handheld remote already programmed with UNLOCK, and the panel pushbutton. **Plain STEP
  does not override a lock**, so without these a crashed GateLink would leave a held gate
  with no manual release. Both are recorded in `/docs/1050-config.md`.
- **S-7. GateLink SHALL NOT unlock the gate on boot.** An earlier design pulsed UNLOCK
  at startup to clear a lock a previous instance might have left. **That is withdrawn.**
  GateLink reads the real hold state at boot, adopts it, and marks the source unknown if
  it cannot attribute it. **Closing a gate that a person deliberately held open — after
  a power blip, with nobody watching — is the worse failure**, and releasing a hold stays
  an explicit act.
- **S-8. The held-open alert notifies; it does not close the gate.** No automatic close
  behaviour is initiated by LRAN beyond explicit HA commands.
- **S-9. Configuration changes are authenticated** (R-5.3b).
- **S-10. Fuse the battery tap.** A 100 Ah LiFePO4 delivers far higher short-circuit
  current than the lead-acid pack it replaces.
- **S-11. Charge-controller configuration is a remotely writable, battery-affecting
  path.** The three independent gates on writes are safety requirements, not
  conveniences.
- **S-12. Do not rely on graceful shutdown** (R-4.4c).
- **S-13. Measure IN5 and IN6 before connecting them** (**M3**). Both are voltage-sense,
  not dry contact, and the alarm output supplies fused 12 V. The inputs are rated
  5–36 V so there is no damage risk, but the measurement is required to get the sense
  polarity right — and IN5 is an emergency signal.
- **S-14. The host is operated outside its temperature rating** (R-4.5a). This is a
  deliberate, instrumented exceedance. **If seasonal logging shows sustained excursions,
  mitigate — vents, shade, fan — or revisit the platform. Do not rationalise.**
- **S-15. The controller's protocol bus is not connected in v1.** Its connector carries
  24 V on an adjacent pin.
- **S-16. Grounding discipline.** Bring GateLink ground and the charge-controller signal
  ground to battery negative at a **single common point**, and keep motor-terminal wiring
  physically separated from signal grounds.

---

## 9. Test and verification requirements

*What must be proven. The procedure, the ordering and the acceptance criteria are in the
implementation plan.*

### 9.1 Verification requirements

| # | Must be proven | Why it is not optional |
|---|---|---|
| **V-1** | The radio works on the carrier board at the chosen pin map, TCXO voltage and RF-switch mode, with the 3.3 V rail holding under transmit load | Until this passes, GateLink has no radio. Failure is a documented trigger for reconsidering the co-processor fallback (**D30**) |
| **V-2** | The §3.1.4 state table holds against **real gate cycles**, driven by the keypad **and** by the handheld remote's OPEN+LOCK — in particular that MOVING stays asserted through the auto-close countdown, and that `hold_confirm_ms` rejects the transient 1/1 at the start of a close | The entire hold-observation design rests on this. It is measured, not assumed (**M2**) |
| **V-3** | Direction classification against the long cases — 30 s EXIT→SAFETY gaps, partial traversals, a vehicle stopped on the loops | The failure mode is a corrupted *next* traversal, which is invisible in a short test |
| **V-4** | The held-open alert fires on the first edge, for every hold source including a manual one, with no arming delay | This is G-8, the primary operational requirement |
| **V-5** | Held-open and FIRE events **fire exactly once and do not replay on HA restart or discovery refresh** | These drive email and SMS. A replay is a 2 AM notification about last Tuesday |
| **V-6** | VE.Direct **text parsing and HEX round-trip**, including write rejection when unauthenticated or disarmed | The round-trip is the proof that the charge controller accepts our drive level, which the vendor does not document. **Prove it; do not assume it** |
| **V-7** | The BMS client decodes the live pack in agreement with the reference implementation, and BLE RSSI is adequate at the **final mounting position and orientation**, sampled at **three positions and two orientations** | **D28**, via **M23**. Both ends share one steel cavity (§4.3.1), so the risk is a standing-wave null rather than attenuation, and a single reading cannot distinguish them. The same session measures LoRa-to-BLE isolation with the transmitter keyed and unkeyed |
| **V-8** | Every command path from HA, with **relay dry-run enabled first** | Command logic must be validated before anything can move the gate |
| **V-9** | Both manual UNLOCK paths, **before the first real hold-open** | **S-6**. A held gate with no manual release is the failure this guards against |
| **V-10** | Configuration round-trip: set every parameter, confirm the ACK, power-cycle, confirm the override survived — **then repeat with the microSD removed** and confirm the change still applies and is honestly reported as unpersisted | R-5.3d has two halves and the second one is the one that gets skipped |
| **V-11** | Measured daily consumption against budget, using the onboard INA226 | R-4.4b makes this free; not doing it would be perverse |
| **V-12** | Seasonal enclosure-temperature log across all three sensors | **D29** closes on logged maxima, not on a datasheet |

### 9.2 Staged validation requirement

- **R-9.2a.** Validation SHALL proceed in stages that are **individually safe**, with a
  **read-only phase before any output is connected**: inputs wired and relays physically
  disconnected, validating state derivation, hold detection and direction against real
  gate cycles driven by existing controls. This phase **cannot move the gate.**
- **R-9.2b.** Relays SHALL then be wired with **dry-run enabled**, and dry-run disabled
  only after every command path has been exercised and both manual UNLOCK paths
  confirmed.
- **R-9.2c.** Any rewiring that would leave an **unsecured control able to assert a
  privileged function**, even transiently, SHALL be completed **before** the terminals
  are reprogrammed — not after.

### 9.3 Bench tooling requirement

- **R-9.3a.** Every verification in §9.1 except V-2, V-7 and V-11 SHALL be reachable
  **without a functioning gate installation**, using the §5.4 tooling — input injection,
  dry-run, loopback, dummy status, and simulated peripherals.

> This is not a convenience. Development that requires an ~87 m walk and a moving gate
> for every iteration will not get the iteration count it needs.

---

## 10. Changelog

- **v0.6** — **Citation refresh; no requirement changed.** Protocol specification
  **v0.9 → v0.10**, which fixes the PHY parameters **D1** had been deferring: 917.4 MHz,
  SF9, BW 125 kHz, CR 4/5, −4 dBm conducted with a 3.0 dBi antenna, under §15.249
  Envelope A. **This node is a consumer of all of it and decides none of it.** Two parts
  reach GateLink directly. **`backoff_max_ms` now defaults to 1500** rather than 500,
  because a maximum `PING` at SF9 runs 1107 ms — §12.3. And **the fade tail at the gate is
  what chose SF9**: the worst single SF7 probe in B1b reached 2.2 dB of margin at
  −119.0 dBm, measured within 6 in of where this node's antenna will sit. **R-4.3.1a's
  3.0 dBi antenna and the −4 dBm conducted ceiling are unchanged**, and Envelope B stays a
  fallback whose triggering would reopen **D28** in the same motion. Nothing on the wire
  moved: `ver` stays at `2` and no vector regenerates.

- **v0.5** — **The enclosure stack is confirmed and recorded as requirements.** New
  **§4.3.1** documents the arrangement — a plastic GateLink enclosure inside the steel
  gate-controller enclosure, which also holds the pack and BMS 6–8 in away, with the LoRa
  antenna outside the steel via two bulkheads and a jumper — and states what would falsify
  the analyses resting on it, with **M23** as the tracked check. **R-4.3.1a–e** fix the
  antenna at a nominally 3.0 dBi 19 cm stick, require the steel enclosure to stay
  conductive and closed, note explicitly that the *plastic* inner enclosure carries no RF
  analysis and may be changed freely, require shielded internal assemblies because the LoRa
  feedline is the one radiator inside the cavity with the BLE receiver, and split the
  feedline-loss assumption between compliance and link budget. **R-4.3h** requires strict
  LoRa/BLE mutual exclusion — the Bridge PRD §4.4 records why the bridge deliberately does
  not — and **R-4.3i** requires TX power as two runtime parameters with a firmware EIRP
  clamp and a runtime envelope. **V-7** is rewritten for M23's multi-position sampling: both
  ends share one reverberant cavity, so the risk is a standing-wave null rather than
  attenuation and a single reading cannot tell them apart. The BMS-RSSI diagnostic entity
  is made explicitly required. Binding protocol advanced to **v0.9**.

- **v0.4** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes **W9** (the full-size and fragmented `PING` bench runs both passed over RF on 2026-09-05) and changes **no frame layout, header field, authentication scope or schema length**; no vector regenerates. **This node is a consumer of both halves.** §11.5 named `CONFIG_ACK` crossing the single-frame boundary on GateLink as the reason W9 mattered now rather than hypothetically; that path has now run over the air. The requirement is unchanged. Note also that §11.2's late-fragment rule remains untested at range — W9 saw no late fragments, but at 1 m of bench, which is not evidence about the 500 ft path this node sits at the end of.
- **v0.3** — Citation refresh only. Protocol specification **v0.6 → v0.7**, which captures **D34** (Protocol Spec W12: §9.4 steps 4–5 become `CommandGate` in `/lib/lran-protocol/`, dispatch stays in the application) and changes **no frame layout, header field, authentication scope or schema length**. **This node is a consumer**: GateLink accepts `COMMAND`, so `CommandGate` binds it at **M3**. The requirement it implements — a retried command must not pulse the relay twice — is unchanged and was already stated.
- **v0.2** — Housekeeping revision; **no requirement changed**. Binding protocol
  citation moves **v0.2 → v0.6**. Reconciled first: this document names schema IDs
  (`0x10`, `0x11`, `0x12`, `0xF0`) and nothing else about the wire, and none of those
  IDs changed across v0.3–v0.6, so **nothing was found to conflict**. Cross-document
  links repaired for the `docs/` reorganization.
- **v0.1** — Initial release. Extracted from `lran-prd-v0_8` §1.4, the gate-specific
  parts of §2, §4.2, §4.5–4.7, §5, §7.2–7.4, §8.1/§8.3/§8.4, §11 and §14. **Restated as
  requirements throughout**: v0.8 interleaved requirements with wiring detail, part
  numbers, pin budgets and bench procedure, which is what made it hard to review as a
  specification. Everything implementation-level moved to
  [`LRAN-GateLink_Node-Implementation-Plan`](./LRAN-GateLink_Node-Implementation-Plan.md);
  bench findings and current measurements moved there as integration observations; the
  load inventory and monitoring plan moved there, with §6 keeping the source, the
  findings and the low-temperature requirements. All frame, enumeration and topic detail
  removed in favour of references to
  [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) v0.2. Decision
  statuses removed in favour of references to
  [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md). BusT4 material moved to
  [`LRAN-Research-Archive`](../archive/LRAN-Research-Archive.md). **Added:** numbered requirement
  identifiers (R-*, G-*, S-*, V-*) so implementation and test artifacts can cite them;
  §9 restructured from a procedure into twelve verification requirements plus the staged
  and bench-reachable constraints. **No design change.**
