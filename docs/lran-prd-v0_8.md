# Product Requirements Document — LoRa Remote Automation Network (LRAN)

**Working name:** LoRa Remote Automation Network — **LRAN**
**Node projects:** `GateLink` (gate), `WellLink` (well, future), `LoRaBridge` (house)
**Version:** 0.8 (draft, for iteration)
**Status:** Architecture settled. 1050 interface rebuilt on documented I/O (v0.6); GateLink host is the M5Stack StamPLC (v0.7). **v0.8 folds in a second round of bench observation on the OUT relays, standby behaviour and current draw; resolves the BMS protocol; and makes every GateLink timing parameter runtime-configurable.** PHY parameters and several field measurements remain open (§13)
**Last updated:** 2026-08-15
**Supersedes:** v0.7
---

## 1. Overview

### 1.1 Purpose

Provide a **property-wide point-to-multipoint LoRa network** linking a Home
Assistant (HA) instance to remote, low-power monitoring and control nodes that
are outside practical WiFi range.

The first node is **GateLink**: a remote, solar-powered driveway gate operator.
HA opens and closes the gate, holds it open on request, reads gate and solar
state, reads and writes charge-controller configuration, and detects and
classifies vehicle traffic through the gate.

A second node, **WellLink**, is planned (§1.7). The network is designed for it
from the outset rather than retrofitted later.

### 1.2 The v0.6 pivot, in one paragraph

v0.1–v0.5 assumed GateLink would talk to the Nice/Apollo 1050 over its **BusT4**
protocol port. Bench probing (§1.5) established three things that undermine that
plan: BusT4 is dead during the 1050's low-power standby, the loop and exit
detectors are **not** readable over the bus when it matters, and a wake mechanism
would be needed regardless. At the same time, probing established that everything
LRAN actually wants is available through the 1050's **documented accessory I/O** —
programmable inputs for commands and two unused programmable relay outputs for
state. **v0.6 therefore builds GateLink on that documented I/O and moves BusT4 to
an optional Phase 2** (Appendix B). The result is simpler, lower-risk, removes a
GPL obligation, and deletes the highest-risk hardware work from the critical path.

### 1.2.1 The v0.7 platform change, in one paragraph

v0.6 rebuilt the 1050 interface on relays and discrete inputs but left GateLink on
the **Heltec LoRa V3**, which has neither. The v0.6 BOM therefore carried an
external 4-channel relay module, resistive dividers with clamp diodes for the two
voltage-sense inputs, and a 12 V→USB-C adapter — a Heltec plus three bolt-on
subassemblies to reach an I/O profile that an industrial controller provides
natively. **v0.7 moves the GateLink host to the M5Stack StamPLC** (Stamp-S3A /
ESP32-S3FN8): 4 relay outputs, 8 opto-isolated 5–36 V inputs, 6–36 V input, DIN
mount, screw terminals throughout. The relay module, the dividers and clamps, and
the USB-C adapter all leave the BOM. The cost is that StamPLC has no radio, so
**LoRa becomes an external SX1262 module on a small carrier board** (§4.7.4) —
which also has to carry a 3.3 V regulator, because StamPLC exposes no 3.3 V rail
(§4.7.3). **LoRaBridge stays on the Heltec V3**; it has no I/O requirement.
Nothing above §4 changes: the protocol, the command model, the detection logic,
the HA entity model and the power budget all carry forward intact.

### 1.2.2 What v0.8 changes

v0.8 is a **measurement and closure revision**, not an architecture change. A second
round of bench work on the installed 1050 established what its programmable OUT
relays actually do, and one observation is worth more than it first appears:
**`OUT = Moving` stays energized while the gate is open and counting down to
auto-close.** Combined with `OUT = Open`, that makes the 1050's *lock* state
directly observable rather than inferred — OPEN asserted with MOVING clear means
nothing is counting down, which means the gate is held (§4.2.4). Every hold-open
mechanism on the property — LRAN's OPEN+LOCK, the programmed handheld remote, the
keyswitch, the keypad FIRE code — is therefore visible to GateLink with no new
wiring, and §5.3.4 stops being a flag the firmware hopes is still true.

The same round established that **any energized OUT relay prevents standby**
(**D23**), which turns the choice of OUT programming into a power decision as well as
a telemetry one (§4.2.7), and produced a first set of clamp-meter current figures
that **do not agree with the power budget and are not yet trustworthy** (§1.5.4,
§8.2.2). v0.8 also: resolves the BMS protocol, now implemented and validated end to
end (§5.7.4); makes **every GateLink timing interval runtime-configurable from HA**,
with defaults in flash and overrides on microSD (§5.8); extends the relay pulse to
500 ms and relaxes input polling to 100 ms; splits the keyswitch and pushbutton so
the secured control holds the privileged function (§4.2.2.1); corrects the
safety-loop topology to **series**; settles the license on **MIT** (**D11**); and
closes **D15, D23, D24, D26, D27** and **D30**.

### 1.3 System summary

One house-side bridge and N remote nodes:

- **LoRaBridge** (house): a **general-purpose LoRa↔MQTT gateway**. Receives and
  sends LoRa to any registered node, connects to the LAN over WiFi, and bridges
  to the existing Mosquitto broker on the HA host. HA entities are created via
  **MQTT Discovery**, one HA device per node. Mains powered; no power constraint.
  Supports OTA update (§6.2).
- **GateLink** (remote, ~500 ft): an **M5Stack StamPLC** (§4.7) with an external
  **SX1262** radio. Interfaces to a Nice/Apollo **1050** control board via
  **dry-contact relay outputs and isolated inputs** (§4.2), and to a Victron
  **MPPT 75/15** via **VE.Direct** (text + HEX). Reads the battery BMS over
  **BLE**. Runs custom firmware. Powered **directly** from the 12 V **100 Ah
  LiFePO4** battery — no 5 V adapter.
- **WellLink** (remote, ~500 ft, different bearing): well level monitoring.
  Scoped in §1.7, specified in a later revision.

```
                       +---------------------+
   +--------------+    |                     |
   |   GateLink   |<-->|                     |
   | StamPLC      |    |                     |
   | + SX1262     |    |     LoRaBridge      |        +-------------------+
   | 4x relay out |    |     Heltec V3       |        |  Home Assistant   |
   | 6x iso. in   |    |                     |        |                   |
   |    <-> 1050  |    |                     |        | (Mosquitto broker,|
   | VE.Direct    |    |  LoRa 915 MHz  <--> |        |  MQTT Discovery)  |
   |   <-> MPPT   |    |  WiFi -> LAN        |------->|                   |
   | BLE <-> BMS  |    |  MQTT -> Mosquitto  |  MQTT  +-------------------+
   | 12V LiFePO4  |    |  OTA capable        |
   +--------------+    |                     |
                       |                     |
   +--------------+    |                     |
   |   WellLink   |<-->|                     |
   |  (planned)   |    |                     |
   +--------------+    +---------------------+
```

### 1.4 Physical installation context (gate)

The **1050 controller, MPPT 75/15, LiFePO4 battery, and GateLink node all share a
single controller enclosure.** The gate motors are mounted externally on the gate
itself and are driven through dedicated motor ports on the 1050 board — **no
motor resides inside the enclosure.**

This matters for three design decisions and is referenced from §4.4, §4.5, and
§11:
- All signal runs (accessory I/O, VE.Direct) are short — inches to a couple of feet.
- Motor current enters the enclosure only via the 1050's own supply and motor
  terminals.
- Cable capacitance on the level-shifted lines is minimal.

It also places the BLE BMS within inches of the node antenna (§5.7) — with the
caveat that the StamPLC's 2.4 GHz antenna is internal to its case and has no
external option, unlike the LoRa side (§4.7.5, **D28**).

**Thermal.** The StamPLC is specified 0–40 °C, and this enclosure is outdoors: it
sees sub-freezing winter mornings and summer solar gain. **v0.8 narrows the concern
to the high end** — cold exposure is brief and touches nothing the system depends
on, while summer heat in a closed box is cumulative. The enclosure already has
screened vents, and shade or a thermostatically controlled fan are available if
logging justifies them. Three temperature sensors already sit inside it — the
StamPLC's LM75, the MPPT, and the pack BMS. See §4.7.6 and **D29**.

### 1.5 Bench findings

All established by direct measurement on the installed board. This section is the
evidence base for §4.2 and for Appendix B. **Findings added in v0.8 are marked.**

**BusT4 port (6P4C "Oview" jack), measured:**

| Position | Signal | Measured |
|---|---|---|
| 1 | — | NC |
| 2 | VCC | **24 V** |
| 3 | data | 2.5 V |
| 4 | data | 2.5 V |
| 5 | GND | 0 V |
| 6 | — | NC |

Resistance: pin 3 ↔ pin 4 = **145 Ω** powered (standby), **174 Ω** unpowered;
pin 3 ↔ GND and pin 4 ↔ GND both **open** when unpowered.

**Interpretation — the physical layer is differential.** Two independent
single-ended UART lines would show a resistance *to a rail* (the pull-up) and be
effectively open *between each other*. The measurement is the exact inverse: open
to ground on both, finite between them. That is a terminated differential pair.
The 145 → 174 Ω shift across power states also indicates active silicon in
parallel with the terminator — the 1050's own transceiver, whose input impedance
drops when biased. **D13 resolves to differential (SN65HVD230), for Appendix B
purposes only.**

**Standby behavior, measured:**

- Entering standby sheds pin 2 (24 V) and both data pins. **BusT4 is entirely
  unavailable during standby.**
- The Diablo DSP-7LP loop detector was powered from **gated** V+ — unpowered, not
  merely idle, during standby. **v0.8: corrected in the field. The Diablo now runs
  from terminal 10 (ungated); §4.6 is done.**
- The **exit wand is powered from ungated V+** (terminals 10/11) and remains live.

**Input wake behavior, measured.** A consistent model emerged:

| Class | Inputs tested | Wakes? |
|---|---|---|
| **Command** — causes an action or state change | EXIT, Guard Station Open (via keypad), FIRE (via keypad), AUX=STEP, AUX=UNLOCK | **Yes** |
| **Conditioning** — qualifies motion already in progress | SAFETY, SHADOW, ENTRAPMENT (shorted directly to GND) | No |
| **Unassigned** | AUX programmed "No function" | No |

Note AUX=UNLOCK causes no motion of its own yet still woke the board, so the
predictor is *is this a command*, not *does this move the gate*.

#### 1.5.1 OUT relay semantics — **new in v0.8**

Measured by programming the relays and cycling the gate:

| Programmed as | The relay energizes |
|---|---|
| **Open** | when the gate is **fully open**, and stays energized while it remains open |
| **Closed** | when the gate is **fully closed**, and stays energized while it remains closed |
| **Moving** | while the gate is **opening or closing — *and* while it is open and waiting on the auto-close timer** |

**Any energized OUT relay prevents standby.** The coil is a load the board will not
carry while asleep, so programming an OUT terminal is also a decision about when the
1050 is allowed to sleep. This **resolves D23**: the relays do not drop their state
in standby, because the board does not enter standby while they are held. No sense
inversion and no NC wiring is needed — §4.2.4 uses NO contacts as originally drawn.

Two consequences are developed later: the `Moving` semantics make the 1050's lock
state **observable** (§4.2.4), and `Open` is the only state-reporting choice whose
standby cost is bounded (§4.2.7).

#### 1.5.2 Auxiliary input behaviour — **new in v0.8**

Confirmed on the bench: an AUX input programmed **Open+Lock** opens the gate and
holds it open as expected, and an AUX input programmed **Unlock** releases that
state and lets the gate close. The §5.3 command model is now verified against the
board rather than inferred from the manual.

#### 1.5.3 Additional moving indicator — **new in v0.8**

The board provides a **12 V lamp output that is active while the gate is moving.**
LRAN does not use it in v1, but it is recorded in §4.2.5 as a second source of a
motion signal should OUT2 ever be wanted for another function. Whether it also
tracks the auto-close countdown the way `OUT = Moving` does is **`TBM`** (§13.1);
if it does not, it is not a drop-in replacement.

#### 1.5.4 Current draw — **first measurements, and they do not add up**

Method: current clamp on the V+ feed into the 1050 board, **40 A range (0.01 A
resolution)**, PV disabled at the MPPT, V+ ≈ 13.95 V. The measured branch carries
the 1050, the keypad, the exit wand and the Diablo — not the LED lights, and not
GateLink.

| Gate controller state | Measured |
|---|---:|
| Open+Lock — active | 0.33 A |
| Open+Lock — standby | 0.32 A |
| Closed — active | 0.23 A |
| Closed — standby | **0.29 A** |

**These figures are recorded, not adopted.** Three things are wrong with them:

1. Standby measures **higher** than active in the closed case, which is not
   physically sensible.
2. The standby figures are roughly **15–20× the ~17 mA** the board is expected to
   draw asleep.
3. Taken at face value this branch alone is **5.5–7.9 Ah/day**, which with the LED
   lights would put the site near 12 Ah/day against ~10.6 Ah/day of winter harvest —
   a standing deficit that would have flattened the previous Group 24 FLA every
   winter. It demonstrably did not.

The likeliest explanation is the instrument rather than the board: at 0.2–0.3 A on a
40 A range the reading sits below 1% of full scale, where clamp offset and a few
counts of resolution dominate and ±0.1 A of error is unremarkable. **§8.2.2 keeps the
budget on the previous figures and makes a proper measurement the top item in
§13.1.** Preferred methods: an inline DC ammeter or shunt in the V+ lead on a mA
range; or the same clamp on its lowest range, zeroed, with ten turns of the conductor
through the jaw and the reading divided by ten. A SmartShunt (§5.7.4) would settle it
permanently.

#### 1.5.5 Existing controls, as installed

| Control | Drives | v0.8 note |
|---|---|---|
| Remote keypad | Guard Station Open (34); a secondary relay drives FIRE (32), separate codes | The FIRE code is reserved for testing and real fire emergencies — §5.4.6 |
| Keyswitch + pushbutton | AUX1 (16), programmed STEP. **Wired in series** — the keyswitch must be unlocked before the pushbutton acts | To be split and rewired — §4.2.2.1 |
| **Handheld remote** | **Programmed OPEN+LOCK on one button, UNLOCK on the other** | **New in v0.8.** A manual hold-open path already exists, and it satisfies **D24** |
| Radio Open/Close (39, 40) | **Free** | Held in reserve |
| OUT1, OUT2 relay outputs | **Free** | To be programmed OPEN and MOVING — §4.2.4 |
| Auto-close | **Enabled, 60 s** | Was `TBM` in v0.7 |
| Standby timeout | **60 s** | Was `TBM` in v0.7 |

**Existing hold-open mechanism.** The gate is held open using **OPEN and LOCK**,
released by **UNLOCK**, after which auto-close closes the gate. Available today from
the handheld remote, and from the AUX terminals LRAN will drive.

### 1.6 Naming conventions

| Thing | Value |
|---|---|
| Repo | `lran` |
| Firmware targets | `lran-bridge`, `lran-gatelink`, `lran-welllink` (future) |
| MQTT topic root | `lran/` |
| Node topic form | `lran/<node>/...` — e.g. `lran/gatelink/...` |
| HA device names | "LoRa Bridge", "GateLink", "WellLink" |
| C++ namespace | `lran` |
| Node IDs | `0x00` bridge, `0x01` gatelink, `0x02` welllink, `0xFF` broadcast |

### 1.7 WellLink scope (forward-looking)

Not specified in this revision, but the network must accommodate it:

| Attribute | Status |
|---|---|
| Distance / bearing | ~500 ft, different direction from GateLink |
| Power | **TBD** — mains is possible; battery/solar must remain viable |
| Function | Well level monitoring. Possible expansion later. |
| Reporting | Fixed-interval poll/push, **plus** an event push on rapid level change |
| Payload | Level + **battery status** — reserve packet space for both |
| Commands | None currently anticipated |

Two design consequences carried into this revision:

- The **RX duty-cycle and extended-preamble wake design is retained in Appendix
  A** rather than deleted, because WellLink may be battery powered even though
  GateLink no longer needs it.
- The status schema is **per-node and versioned** (§6.4.3), so WellLink can define
  its own payload without touching GateLink's.

---

## 2. Goals and non-goals

### 2.1 Goals

**Network**
- A single house-side bridge serving **multiple** independent remote nodes over
  one LoRa channel, with per-node addressing, keying, and availability.
- Adding a node requires reflashing only the bridge and the new node — never the
  existing nodes (§6.4.5 version tolerance).

**GateLink — gate control**
- **Momentary open** (§5.3.1, case 1): open the gate, auto-close returns it.
- **Open and hold** (§5.3.2, case 2): open the gate and hold it open until
  instructed to close. The common case — deliveries, contractors, guests.
- **Close**: release the hold, or close immediately.
- All commands issued through the 1050's documented accessory inputs, replicating
  what a human control panel does.

**GateLink — monitoring**
- Gate state (open / closed / moving) from the 1050's programmable relay outputs.
- **Hold state observed rather than assumed** — `OUT = Moving` covers the auto-close
  countdown, so an open gate with nothing counting down is a held gate, whoever held
  it (§4.2.4).
- **Vehicle detection and direction-of-travel classification** from the existing
  safety-loop and exit-wand contacts, tapped directly (§5.4).
- **Alert on vehicle detection while the gate is being held open** (§5.4.5) — the
  primary operational requirement behind §5.4.
- **Immediate, separately routable alert on any FIRE assertion** (§5.4.6).
- Hard-shutdown / entrapment alarm state.
- Full status from the MPPT 75/15 (all VE.Direct fields), plus **read/write access
  to all MPPT configuration registers** via the VE.Direct HEX protocol, with
  GateLink acting as transport only (§6.6).
- Battery health and state of charge (§5.7).

**GateLink — configurability**
- **Every timing interval, window and threshold GateLink uses is settable at runtime
  from HA**, without reflashing and without a walk to the gate (§5.8).

**Integration**
- Lightweight, native-feeling HA integration (cover + sensor / binary_sensor /
  button / switch / number entities via MQTT Discovery).
- Built-in bench debug tooling (packet loopback, dummy status pushes, device
  simulators, detector event injection).
- Best-practice repo, build, and dev workflow (GitHub + VS Code + Claude Code).

### 2.2 Non-goals (v1)

- **BusT4.** Moved to optional Phase 2 — Appendix B. Not a v1 requirement, not on
  the critical path, and not required for any goal in §2.1.
- Strong cryptographic security / full replay & spoofing prevention. Command and
  MPPT-write authentication only (§6.5).
- **OTA for remote nodes.** The bridge supports OTA (§6.2); GateLink and WellLink
  are USB-only.
- Persisting a sequence counter across reboots.
- Controlling **multiple gates** or multiple charge controllers. Multiple *nodes*
  are explicitly in scope; multiple gate operators are not.
- **Gate position reporting** (percentage open). Discrete states only — §7.3.
- Partial open, step-by-step, and block/release as HA commands. Available via
  spare 1050 inputs if wanted later (§4.2.5); not in v1.
- Replacing or modifying the 1050's own safety logic. §11.
- Mesh or multi-hop routing. The topology is a star with the bridge at the centre.

---

## 3. System architecture

### 3.1 Nodes

| | LoRaBridge | GateLink | WellLink (planned) |
|---|---|---|---|
| Board | Heltec WiFi LoRa 32 V3 | **M5Stack StamPLC** (ESP32-S3FN8) + external SX1262 (§4.7) | TBD |
| Node ID | `0x00` | `0x01` | `0x02` |
| Firmware | LoRa↔MQTT gateway + decoders | Custom (I/O + interfaces) | TBD |
| Wired interfaces | none functional | 1050 accessory I/O (relays + isolated inputs), VE.Direct (MPPT) | level sensor |
| Wireless | LoRa + WiFi (BLE off) | LoRa + **BLE (duty-cycled)**; WiFi **off** | LoRa |
| Power | USB-C (mains) | 12 V **LiFePO4** direct to VIN (6–36 V input) | TBD |
| Update | **OTA + USB** | USB only | USB only |
| Configuration | build config | **runtime from HA** — defaults in flash, overrides on microSD (§5.8) | TBD |
| Display | May stay on | 1.14" LCD; backlight off in normal op; on-demand + auto in debug (§5.6) | TBD |
| Location | House, near LAN | Inside existing controller enclosure (§1.4) | At the well |

### 3.2 Shared code

All nodes link a common **LoRa protocol/packet library** (framing, addressing,
HMAC, CRC, sequence handling, fragmentation) so the wire format is defined and
tested once. This shared library is the **fleet-wide contract** and is what the
bench tools and simulators exercise.

Because remote nodes have no OTA, this library carries a **compatibility
obligation** (§6.4.5): the bridge must accept protocol version *N* and *N−1*, so
a protocol revision can be rolled out node-by-node rather than in a single
flag-day flash of every device on the property.

### 3.3 Decode placement — simplified from v0.5 (D14)

v0.5 specified a hybrid split of BusT4 decoding between node and bridge. With
BusT4 out of v1, that complexity disappears: **GateLink reads discrete GPIO
states and drives discrete relays.** There is nothing to decode.

What remains:

| Layer | Runs on | Why |
|---|---|---|
| Debounce, edge detection, direction classification, hold-state tracking | **GateLink** | Sub-second local decisions; a LoRa round trip per edge is not viable |
| VE.Direct text parsing | **GateLink** | Continuous 1 Hz stream; only the cached snapshot is transmitted |
| VE.Direct HEX register interpretation | **LoRaBridge** | Bridge is mains powered, OTA-capable, in the house. GateLink is transport only (§6.6) |
| Entity mapping, discovery config | **LoRaBridge** | Changes often; must not require a walk to the gate |

**D14 resolved: GateLink is a discrete I/O device plus a VE.Direct transport.**

### 3.4 Data flows

- **Command (HA → node):** HA publishes to an MQTT command topic → bridge builds
  an authenticated LoRa command frame addressed to the node → node verifies HMAC
  and sequence, pulses the appropriate relay → node returns a command-ACK frame →
  bridge publishes result. The relay pulse also wakes the 1050 (§4.2.3).
- **Status (node → HA):** each node caches its latest state and transmits on (a)
  poll request, (b) a locally significant state change, or (c) an event. For
  GateLink: gate state change on OUT1/OUT2, non-zero VE.Direct charger error code,
  **vehicle detection event** (§5.4), hard-shutdown alarm, or BMS alarm.
- **MPPT config (HA → MPPT):** HA publishes a HEX request → bridge wraps it →
  GateLink transports it verbatim to the MPPT → response transported back →
  bridge publishes it. **Writes require HMAC and an armed write-enable switch**
  (§6.6).
- **Poll:** per-node poll scheduler on the bridge, each node's interval
  runtime-configurable via its own HA `number` entity (§6.3).

### 3.5 Platform convergence with AquaLink — new in v0.7

AquaLink, the water-system controller project, independently selected the StamPLC.
With v0.7 the two share a host platform. This is a **platform and framework**
convergence, not a feature one — the two nodes share almost no application
behaviour.

**Shared:** the platform HAL (relay and input abstraction over the AW9523B, LCD,
buttons, buzzer, INA226, LM75, RTC, SD logging); the `lran-protocol` library and
the SX1262 driver; configuration, persistence and logging; the debug tooling in
§9.2; the PlatformIO environment and CI.

**Not shared:** AquaLink is mains-powered indoors and publishes MQTT over WiFi
directly. GateLink publishes over LoRa and is bridged.

**Requirement.** `/lib/lran-platform/` (§10) SHALL abstract the host so that both
node applications compile against the same HAL, and the **radio pin map SHALL be
injected by configuration rather than hardcoded**, so one SX1262 driver serves both
the Heltec LoRaBridge (fixed internal pins, 1.8 V TCXO, DIO2 RF switch) and the
StamPLC carrier (§4.7.4).

**Useful side effect:** because the transport is abstracted, GateLink can be
bench-exercised over WiFi/MQTT with the radio entirely out of the loop — a
strictly easier bring-up path than v0.6 had.

---

## 4. Hardware and BOM

### 4.1 Bill of materials

#### 4.1.1 System

| Qty | Item | Notes |
|----:|------|-------|
| 1+ | Heltec WiFi LoRa 32 V3 (US 915 MHz variant) | ESP32-S3, SX1262, 0.96" OLED. **LoRaBridge and bench/simnode targets.** No longer used for GateLink — see §4.1.2 |
| 2+ | 915 MHz antennas | one per node |

#### 4.1.2 GateLink

| Qty | Item | Notes |
|----:|------|-------|
| 1 | **M5Stack StamPLC (K141)** | Host platform, §4.7. 4 relays, 8 opto-isolated inputs, 6–36 V in, DIN, screw terminals. ~$43 |
| 1 | **SX1262 module, 915 MHz, SPI** | Must use **DIO2 RF-switch control**. **Exclude any module needing separate TXEN/RXEN lines — including the Waveshare Core1262**, which v0.7 named as the reference part in error; the pin budget has no room (§4.7.2). A **WIN-SX1262-class** module is the current candidate — check it against the §4.7.2 checklist before ordering |
| 1 | SMA bulkhead pigtail + 915 MHz antenna | LoRa antenna **outside** the enclosure |
| 1 | **3.3 V LDO, ≥300 mA, dropout ≤300 mV** — AP2112K-3.3 breakout or equiv. | **Required — D26 resolved.** StamPLC exposes no 3.3 V rail and EXT_5V sits at ~4.76 V under load (§4.7.3). **An AMS1117-3.3 will not do** — ~1.1 V dropout |
| 1 | **Perfboard + 2×8 2.54 mm header, in a DIN-rail carrier** | Carries the SX1262 module, the LDO and the VE.Direct front end (§4.7.4). **D27 resolved — perfboard with prefab modules**; regulator and discretes mounted directly. Pick the DIN carrier first and cut the board to it |
| 1 | **BSS138 4-channel bidirectional level shifter module** | Adafruit #757 / SparkFun BOB-12009 or equivalent. **VE.Direct only** (2 channels used, 2 spare). Retained — but see **D25**: the TX direction is contingent on measurement (§4.4) |
| *0–1* | *ADuM1201 breakout **or** 74LVC1G17 buffer* | **Only if D25 shows a weak low-side driver** on the MPPT TX line (§4.4) |
| 1 | VE.Direct cable / JST-PH 2.0 4-pin pigtail | to the MPPT VE.Direct port. **Both data lines used** (§4.4) |
| 1 | **microSD card** — small, industrial-grade if available | Configuration overrides and on-node logging (§5.8.3, §4.5.1). **Not required for the node to run**: absent, it degrades to RAM-only configuration |
| 1 | **12 V 100 Ah LiFePO4 battery** — WattCycle 100 Ah mini w/ Bluetooth | §8.1 |
| — | Mounting, strain relief, **inline fuse on the battery tap** | inside existing enclosure |
| *0–1* | *SN65HVD230 CAN transceiver module* | **Appendix B only** — already on hand; not needed for v1. Appendix B may not need it at all (§B.5) |
| *0–1* | *Victron SmartShunt 500 A* | **Contingency only.** No longer needed for SOC — **D15 is resolved** (§5.7.4) — but still the cleanest source of true load current, and the fallback if **D28** fails |

**Removed from the v0.6 BOM by the platform change:** the 4-channel relay module
(StamPLC has four relays), the divider resistors and clamp diodes for IN5/IN6
(StamPLC's inputs accept 5–36 V natively, §4.2.4), the 12 V→USB-C adapter and its
cable (StamPLC takes 6–36 V directly, §4.5), and the Heltec V3 for this node.

**Added in v0.7:** the StamPLC, the SX1262 module and antenna, the LDO, and a
carrier board. **Added in v0.8:** the microSD card. Net part count is roughly flat;
net *assembly* count is lower, and the number of hand-built discrete circuits is
zero.

No separate enclosure is required — GateLink mounts inside the existing
controller enclosure (§1.4).

**Existing installed hardware, not purchased:** Nice/Apollo 1050 control board,
Victron MPPT 75/15, 50 W panel, **Diablo DSP-7LP** loop detector with two safety
loops **in series** (§5.4.1), self-contained exit wand sensor, remote keypad,
panel keyswitch + pushbutton, **a handheld remote programmed OPEN+LOCK / UNLOCK**,
2 × 2 W LED lights.

> **BOM trajectory.** v0.5 required the SN65HVD230 *or* two more BSS138 channels
> for BusT4, plus a bench logic analyzer as a project-critical item. v0.6 replaced
> that with a relay module. v0.7 absorbed the relay module, the dividers and the
> power adapter into the host and spent the saving on a radio carrier. **v0.8 adds
> one microSD card and retires the SmartShunt as an SOC dependency.**

#### 4.1.3 Bench / debug (§9)

| Qty | Item | Notes |
|----:|------|-------|
| 1 | USB-TTL serial adapter (5 V / 3.3 V selectable) | VE.Direct bring-up and observation |
| *1* | *8-channel USB logic analyzer (sigrok/PulseView)* | Optional. Useful for VE.Direct HEX timing; **required only if Appendix B is pursued** |
| — | Jumper leads, breadboard, DVM | |

### 4.2 1050 accessory interface — **the core change in v0.6**

GateLink drives and reads the 1050 exclusively through terminals the manufacturer
documented for third-party use. Nothing here is reverse-engineered, nothing
touches a 24 V rail, and nothing depends on the bus being awake.

#### 4.2.1 Design principle

**LRAN presents itself to the 1050 as a control panel.** Every command is a
momentary dry-contact closure on an input the 1050 already expects a human device
to drive; every status read is a dry contact the 1050 already provides for
signalling accessories. This is the interface Nice designed for exactly this
purpose.

Two consequences worth stating explicitly:

- **All GateLink outputs are momentary pulses.** No maintained assertions
  anywhere. There is no relay state that, if welded or stuck, holds the gate in an
  abnormal condition. Hold-open is a **latched state inside the 1050**, set and
  cleared by pulses, not a wire held down.
- **All existing human controls are paralleled, never replaced.** Adding a dry
  contact in parallel with an existing one costs nothing and preserves every
  manual path — keypad, keyswitch, remote, front panel.

#### 4.2.2 Output map — relays

| Relay | Terminal | Programmed as | Function |
|---|---|---|---|
| **K1** | AUX1 (16) | **OPEN and LOCK** | Case 2 — open and hold (§5.3.2) |
| **K2** | AUX2 (18) | **UNLOCK** | Release hold; auto-close then closes |
| **K3** | Guard Station Open (34) | *(fixed function)* | Case 1 — momentary open (§5.3.1) |
| **K4** | Guard Station Close (36) | *(fixed function)* | Immediate close (§5.3.3) |

**Pulse width: `relay_pulse_ms`, default 500 — raised from 300 in v0.8.** The 1050's
internal input debounce time is undocumented, and nothing in the design is sensitive
to a longer closure: these are momentary commands on inputs a human operates with a
pushbutton. 500 ms buys actuation margin for free, and like every other interval in
the firmware it is adjustable at runtime (§5.8). All contacts are wired
**normally-open**, so a de-energized or unpowered GateLink asserts nothing.

**These are the host's own relays.** K1–K4 are StamPLC's four onboard relays (SPDT,
COM/NO/NC, DC 5 A @ 28 V), driven through the **AW9523B I²C expander**, not by
direct GPIO. Three consequences:

- Pulse timing is generated in firmware over I²C rather than by a GPIO write. At a
  500 ms pulse width I²C latency is irrelevant, but the platform HAL owns the timing
  and must not be interrupted mid-pulse by a blocking BLE or VE.Direct call.
- The relays are dry contacts on screw terminals, so §4.2.1's "present as a control
  panel" principle is unchanged, and the opto-isolation the v0.6 relay module
  provided is no longer needed — there is no shared logic rail to isolate.
- **The de-energized-asserts-nothing property is preserved**, including through the
  LiFePO4 BMS-disconnect case in §4.5.

**AUX reprogramming.** AUX1 currently reads STEP. Reprogramming AUX1 to OPEN+LOCK
and AUX2 to UNLOCK consumes no additional terminals. **The Radio Open/Close inputs
(39, 40) remain free** and are held in reserve — tying them together yields
step-by-step operation if a physical SBS control is ever wanted.

#### 4.2.2.1 Keyswitch and pushbutton — rewired in v0.8

The panel keyswitch and pushbutton are presently wired **in series** on AUX1: the
keyswitch must be unlocked before the pushbutton does anything. That arrangement
cannot survive the AUX reprogramming, because it would leave an **unsecured
pushbutton able to assert OPEN+LOCK** — a security defect at the controller box,
where the consequence is that anyone who reaches the panel can hold the gate open
indefinitely.

**Resolution — the series link is removed and the two controls are split, the
secured control taking the privileged function:**

| Control | Wired to | Becomes |
|---|---|---|
| **Keyswitch** | AUX1 (16) = OPEN and LOCK | Manual "open and hold" — requires the key |
| **Pushbutton** | AUX2 (18) = UNLOCK | Manual release. Unsecured, and safely so: its only effect is to let a held gate close |

The asymmetry is deliberate. Opening and holding is the privileged operation;
releasing a hold is not, and making the release path the easy one is what §4.2.6
wants anyway. The pushbutton is therefore also the **panel-side manual UNLOCK path
of record** (**D24**), alongside the programmed handheld remote.

#### 4.2.3 Wake — solved by construction

The 1050 wakes on command-class inputs (§1.5). Every relay in §4.2.2 drives a
command-class input, so **the pulse that carries the command is also the pulse
that wakes the board.** No dedicated wake mechanism, no wake relay, no
fail-safe-STOP wiring, and no dependency on standby configuration.

This retires the entire wake-strategy problem that dominated v0.5's late review.
It also means **standby can stay enabled** — measured at a **60 s** timeout
(§1.5.5) — keeping the ~2 Ah/day that disabling it would have cost (§8.2.1). Note
that the OUT programming, not the timeout, is now the thing most likely to keep the
board awake (§4.2.7).

Latency note: the board needs a short interval to wake and act. Allow
`post_wake_settle_ms` (default **500**) after a pulse before evaluating OUT1/OUT2
state, and do not conclude a command failed until at least
`command_confirm_timeout_s` (default **5**) has elapsed.

#### 4.2.4 Input map — isolated inputs

| Input | Source | Configuration | Reads |
|---|---|---|---|
| **IN1** | OUT1 relay | Programmed **OPEN** | Gate is at the fully-open position |
| **IN2** | OUT2 relay | Programmed **MOVING** | Gate is in motion **or** open with the auto-close timer running |
| **IN3** | Diablo DSP-7LP contact, paralleled | — | Safety loops (either loop) |
| **IN4** | Exit wand contact, paralleled | — | Exit wand |
| **IN5** | FIRE terminal (32), sensed | — | Someone used the keypad FIRE code |
| **IN6** | Alarm output, sensed | — | Hard shutdown / entrapment latch |

**Gate state derivation — revised in v0.8.** v0.7 assumed `MOVING` meant motion
only. It does not: per §1.5.1 it is also asserted while the gate stands open with
auto-close counting down. That makes the pair strictly more informative:

| IN1 (OPEN) | IN2 (MOVING) | State |
|---|---|---|
| 0 | 0 | **Closed and idle** — the only state in which the 1050 can sleep |
| 0 | 1 | **Moving** — opening from closed, or closing once past the open limit |
| 1 | 1 | **Open, auto-close countdown running** — the gate will close on its own |
| 1 | 0 | **Open and held** — nothing is counting down, so a lock is in force |

**The bottom row is the important one.** An open gate with no countdown is a gate
somebody locked open, and it does not matter who: LRAN's own OPEN+LOCK, the
programmed handheld remote, the keyswitch, or the keypad FIRE code all land in the
same observable state. §5.3.4 therefore tracks hold state from **observation**
rather than from GateLink's memory of what it commanded, and §5.4.5's held-open
alert now covers manual holds it previously could not see.

Two caveats, both settled during §9.3 bring-up:

- The 1/1 state is briefly re-entered at the start of a close cycle, before the gate
  leaves the open limit. Require **`hold_confirm_ms` (default 2000)** of a stable
  1/0 reading before declaring a hold.
- Whether `MOVING` dips momentarily at the open limit before the countdown begins is
  **`TBM`** (§13.1). If it does, the same confirmation timer absorbs it.

Direction of travel during motion is inferred from the previous stable state, which
GateLink tracks. This is sufficient for §7.2's `cover` entity; position percentage
remains out of scope (§7.3).

**Electrical notes.**

All six inputs land on StamPLC's **opto-isolated 5–36 V DC channels** (EL3H4, read
via the AW9523B). Eight are available; six are used.

- **IN1–IN4 are dry contacts.** Wire in the high-level configuration: `EXCOM_COM`
  to the 12 V supply negative, each `INPUT` fed from 12 V+ through the contact.
  No internal pull-ups, no GPIO-referenced grounds.
- **IN5 and IN6 are voltage sense**, and connect directly — StamPLC's inputs are
  specified for 5–36 V. Metering both before wiring is still required (§13.1), to
  establish sense polarity and idle state, not to size a divider.
- The 1050's OUT1/OUT2 provide common, NO and NC contacts. **Use NO.** v0.7 held
  open the possibility of inverting the sense if the relays dropped out in standby;
  §1.5.1 shows they do not, because they prevent standby. **D23 resolved.**

**Acquisition is by polling, not interrupt.** The AW9523B's INT line is shared with
the other onboard I²C peripherals, and §4.7.2 spends that pin on the radio.
GateLink SHALL poll the input expander at **`input_poll_ms`, default 100**
(configurable 20–500, §5.8), with debounce expressed as **`input_debounce_samples`,
default 2** — two consecutive agreeing reads, so ~200 ms at the default rate.

> **v0.8 relaxation.** v0.7 specified a 10–20 ms poll. That is far beyond anything
> here: §5.4's EXIT↔SAFETY discrimination operates on windows of seconds to tens of
> seconds, the closest pair of events of interest is separated by at least 20 ft of
> driveway, and the 1050's own inputs are debounced in hardware. 100 ms leaves three
> orders of magnitude of margin on the detection logic and gives the I/O task room to
> be preempted by BLE or VE.Direct work without missing an edge. The interval may
> also be **stretched at runtime** if the timing budget ever gets tight — a
> configuration change, not a reflash (§5.8).

#### 4.2.5 Spare capacity

Deliberately unspent, recorded so future revisions know what is available:

| Terminal | Status | Possible use |
|---|---|---|
| Radio Open (39), Radio Close (40) | Free | SBS if tied together; or two more command inputs |
| Guard Station Stop (35) | Free (jumpered to GND) | Stop command, if wanted (caution below) |
| **12 V moving-lamp output** | **Free — §1.5.3** | **Backup source of a motion signal** if OUT2 is ever needed for another function. A voltage-sense signal, which StamPLC's inputs take directly. **Not** a drop-in for OUT2 unless it is confirmed to track the auto-close countdown as well (§13.1) |
| SHADOW / LOOP1 (24), ENTRAPMENT / LOOP2 (26) | Likely free — **`TBM`**, §13.1 | Additional detection inputs |
| EDGE (28) | In use or free — **`TBM`** | — |
| StamPLC inputs 7 and 8 | Free | Two spare opto-isolated channels |
| BSS138 channels 1–2 | Free | Second VE.Direct pair if a SmartShunt is added (§5.7.4) |
| BusT4 port | Untouched | Appendix B |

**Stop is not implemented in v1.** It is available, but Guard Station Stop is
normally-closed and asserting it disables all other inputs for the duration. If
added later, wire the relay's **NC** contacts in series with the existing
STOP–GND jumper so that a de-energized relay leaves the circuit intact and every
failure mode lands safe.

#### 4.2.6 Failure modes and manual recovery

| Failure | Result | Recovery |
|---|---|---|
| GateLink loses power or crashes | All relays de-energize. No input asserted. Gate behaves exactly as it does today. | None needed |
| GateLink crashes **while the gate is held OPEN+LOCKED** | Gate stays open and locked. Plain STEP does **not** override a lock — only STEP H does. | **Manual UNLOCK — resolved, below** |
| Welded relay contact | A single sustained closure on a command input. Repeated open commands on an already-open gate; repeated unlock on an unlocked gate. Undesirable but not hazardous. | De-power or unplug the node |
| 1050 in standby, command issued | Pulse wakes the board and executes (§4.2.3) | None needed |
| microSD absent or unreadable | Firmware defaults apply; runtime config changes are accepted, applied and reported as unpersisted (§5.8.3) | Fit a card |

**Manual UNLOCK — D24 resolved.** Because LRAN will enter the OPEN+LOCKED state far
more often than a human does today, there must be a way to clear it without
GateLink. **Two independent paths exist:**

1. **The programmed handheld remote**, which already carries UNLOCK on one button
   (§1.5.5). Available from a vehicle, which is where the problem is usually
   noticed.
2. **The control-panel pushbutton**, rewired to AUX2 = UNLOCK (§4.2.2.1).

Record both in `/docs/1050-config.md`. The keyswitch is deliberately *not* part of
the unlock path — it moves to OPEN+LOCK, which is the operation that should require
a key.

#### 4.2.7 OUT relays and standby — **resolved in v0.8**

v0.7 flagged as unknown whether OUT1/OUT2 hold their state when the board sleeps.
The measured answer (§1.5.1) is that the question does not arise: **an energized OUT
relay prevents the board from entering standby at all.** An asserted output is
therefore always valid, and **D23 closes with no sense inversion** — normally-open
contacts, straightforward logic.

What this does introduce is a **power dependency on how the OUT terminals are
programmed**, which is worth stating plainly because the wrong choice is quietly
expensive:

| Programming | Standby is inhibited... | Verdict |
|---|---|---|
| `OUT = Closed` | whenever the gate is closed — i.e. nearly always | **Never use.** Effectively disables standby |
| `OUT = Open` | only while the gate stands open | **Chosen for OUT1.** Bounded, and zero in the resting state |
| `OUT = Moving` | while moving, or counting down to auto-close | **Chosen for OUT2.** Standby-neutral: the board is awake in both conditions anyway |

The selected pair — **OUT1 = OPEN, OUT2 = MOVING** — is thus the least
standby-costly combination that still yields a complete state picture, and the board
sleeps in exactly the state it spends most of its life in: closed and idle.

**The residual cost is the hold-open case.** While the gate is held open, OUT1 is
energized, the 1050 cannot sleep, and the site runs at active current for the
duration of the hold. This is unavoidable — any programming that reports "open" must
hold a coil while open — and it is bounded, self-limiting and visible in telemetry.
Quantifying it depends on the §8.2.2 re-measurement, not on the figures currently in
hand.

### 4.3 What happened to BusT4

Not deleted — **relocated to Appendix B** as an optional Phase 2 enhancement,
with all bench findings preserved. It is not required for any v1 goal.

Removed from the critical path as a result: the safety-critical bring-up
procedure, the Branch A/B physical-layer analysis, the pin-identification gate,
raw frame streaming, the laptop sniffing phase, and decisions D3, D13, D18. See
§15 for the full list and §12.2 for the licensing consequence.

### 4.4 VE.Direct electrical — unchanged from v0.5

> **Correction retained from v0.3:** earlier drafts stated VE.Direct is 3.3 V TTL
> requiring no level shifting. **This is incorrect.** All Victron MPPTs are 5 V
> devices.

VE.Direct is a **5 V TTL UART, 19200 baud**, on a **JST-PH 2.0 4-pin** connector.
Pinout is vendor-documented, with signal names given from the **device's**
perspective:

| Pin | Signal (device POV) | LRAN connection |
|---|---|---|
| 1 | GND | GateLink GND |
| 2 | RX (into MPPT) | BSS138 ch 1 → GateLink TX — **required** for HEX (§6.6) |
| 3 | TX (out of MPPT) | BSS138 ch 2 → GateLink RX — **required** |
| 4 | V+ | **Do not connect** |

Both data lines are mandatory: HEX is a request/response protocol and cannot
function without the MPPT RX line.

> **v0.7 addition — the 5 V TX driver is weak, and this may invalidate the BSS138
> on the TX direction (D25).** Victron's own solar-charger documentation specifies
> the TX port as a logic 5 V signal driving **at most a 22 kΩ load, at which point
> the output has already fallen to 3.3 V**. That implies an effective source
> impedance around **10–11 kΩ**. A BSS138 translator presents a 10 kΩ pull-up to
> 5 V on the HV node; against a comparably weak driver the low level lands near
> **2.4 V**, the FET never sees a valid gate-source condition, and the LV side stays
> permanently high. The failure mode is silent — no framing errors, simply no data.
>
> This is contingent on whether the driver is symmetric. Many outputs are strong-low
> and weak-high, in which case the BSS138 is fine and this note is moot. It is
> cheap to settle:
>
> **Measurement (D25, §13.1).** Put 10 kΩ from the MPPT TX pin to GND with the port
> streaming and observe the **low** excursions, preferably on a scope.
>
> | Result | Meaning | Action |
> |---|---|---|
> | Low ≈ 0–0.5 V | Strong low side | **BSS138 as planned.** No BOM change |
> | Low ≈ 2–2.5 V | Weak symmetric driver | BSS138 cannot translate this direction. Use an **ADuM1201** (CMOS input, unloads the driver, isolation as a bonus) or a **74LVC1G17** buffer at 3.3 V |
>
> **The BSS138 stays for the RX direction regardless** — a strong 3.3 V output into
> a high-impedance MPPT input is exactly what that topology handles well. A split
> solution is acceptable and is the expected outcome if the measurement goes badly.
>
> Note also that whether a 5 V-family MPPT reliably reads 3.3 V as a logic high on
> its RX input is **not documented by Victron** and has been asked on their own
> community forum without answer. §14 phase 4 must prove the HEX round-trip, not
> assume it.

Implementation notes:

- **Galvanic isolation is not required.** Given §1.4 — no motor inside the
  enclosure, all devices sharing one box, short heavy battery leads — the
  worst-case ground offset is on the order of **16 mV** (2 ft of 12 AWG at 5 A) to
  ~40 mV for lighter wire. Against a 5 V logic threshold this is noise. **D12
  resolved: BSS138, no isolator.**
- **Wire colors are actively misleading.** VE.Direct cables are crossover cables;
  red may be GND and black may be V+, and the two data conductors differ in
  meaning between the cable's ends. **Meter every conductor.** Buying a genuine
  Victron cable and cutting it is the easiest sourcing path.
- Community sources disagree about whether pin 4 is V+ or GND on some units. Moot
  since pin 4 is unconnected, but a further argument for metering first.
- Rise-time analysis: BSS138 is open-drain with 10 kΩ pull-ups, RC-limited. At
  ~20 pF (module plus short trace, which is this install) rise time is ~440 ns
  against a 52 µs bit period — **0.85%**, three orders of magnitude of headroom.
  If a far-end edge looks lazy during bring-up, parallel additional pull-ups to
  bring the effective resistance to 2.2–4.7 kΩ.
- **Rail sourcing — changed in v0.7.** HV rail from StamPLC's **EXT_5V** (nominal
  5 V, **~4.76 V measured under load**, §4.7.3); LV rail from the **carrier board's
  3.3 V LDO**, because StamPLC exposes no 3.3 V. Both Victron device power pins stay
  unconnected. Do not attempt to power the front end from the VE.Direct V+ pin — it
  is limited to roughly 10 mA average.
- **If a SmartShunt is added** (§5.7.4) it presents a second VE.Direct port
  requiring BSS138 channels 3–4 and a third hardware UART. The ESP32-S3 has three;
  with BusT4 gone, two are free.

### 4.5 GateLink power source

**12 V LiFePO4 (100 Ah)** → inline fuse → **StamPLC VIN terminal block**. The
intermediate 12 V→5 V USB-C adapter is **deleted**: StamPLC accepts DC 6–36 V
directly, comfortably spanning the LiFePO4 range from BMS-cutoff to ~14.6 V
absorption.

| Requirement | Value | Rationale |
|---|---|---|
| Input range | 6–36 V (spec) | LiFePO4 operating range sits mid-band |
| Termination | screw terminal (VIN/GND) | Barrel jack also available; terminal block preferred |
| Fusing | inline on the battery tap | unchanged from v0.6, §11 |
| Onboard measurement | **INA226 on VIN** | GateLink reports its own supply voltage and current with no added hardware — see §8.5 |

**Three consequences worth noting.** One connector, one converter and one cable
leave the design. The adapter's ~0.12 Ah/day overhead leaves the power budget
(§8.2). And the INA226 makes the node's own consumption a **measured, published
telemetry value** rather than a `TBM` row.

#### 4.5.1 Loss of supply, and where nonvolatile state lives

Short of a wiring fault there are exactly two ways this node loses power: the pack
**BMS opens**, or the **inline fuse blows**. Both are low-probability, and the first
is not a surprise — cell voltages, SOC and protection flags are polled over BLE
(§5.7) and published to HA, so a pack heading for a low-voltage disconnect announces
itself days ahead of the event. The fuse case is catastrophic-only.

Unlike an FLA, which sags, a LiFePO4 pack goes to **zero volts at the terminals**
when the BMS opens. GateLink will not brown out gracefully; it will drop dead and
reboot when the BMS re-closes. **Nothing may depend on a clean shutdown or on RAM
surviving a power event.** This is benign for the 1050 interface: all relays
de-energize and assert nothing (§4.2.6), and per §4.2.4 the gate's true state —
including whether it is being held open — is readable from IN1/IN2 the moment the
node returns.

**Nonvolatile state lives on the microSD card, not in onboard flash.** Configuration
overrides (§5.8.3), retained counters and on-node logs are written to SD, so a node
that reboots repeatedly under a marginal supply is not wearing the ESP32's internal
flash. The card is optional by design: absent, the node runs on firmware defaults
and accepts runtime configuration into RAM only.

### 4.6 Detector power — **requirement met in v0.8**

The Diablo DSP-7LP was powered from gated V+, and was therefore unpowered — not
merely idle — during 1050 standby. That made §5.4.5 unimplementable: the gate
standing open is precisely when the board sleeps and the detector would be dark, and
it would not have mattered how GateLink acquired the contact.

**Done. The Diablo's V+ has been moved to terminal 10 (ungated).** Power remains
present there during standby, the manual recommends terminals 10/11 for accessories
in standby applications, and the exit wand already runs from the same source
(terminals 10/11) and needed no change.

Cost: ~1 mA continuous, **0.02 Ah/day**.

**Secondary benefit, independent of LRAN.** Inductive loop detectors need time to
tune after power-up. Previously the Diablo powered up cold at the moment a gate
cycle began. Continuous power removes that window.

*Remaining note:* terminal 11 tracks the highest incoming voltage (~13.5 V from the
battery); if the detector is ever moved there, check that against the DSP-7LP's
input range. On terminal 10 as wired, this is not an open item.

### 4.7 GateLink host platform — StamPLC (new in v0.7)

#### 4.7.1 Why this platform

v0.6 established the I/O requirement precisely: **four momentary relay outputs and
six inputs, two of which are 12 V voltage-sense.** The StamPLC meets that natively.

| v0.6 requirement | Heltec V3 | StamPLC |
|---|---|---|
| 4 × dry-contact relay out | external relay module | **native**, SPDT, screw terminals |
| 4 × dry-contact in | GPIO + pull-ups | **native**, opto-isolated |
| 2 × 12 V voltage-sense in | divider + clamp per channel | **native** (5–36 V inputs) |
| 12 V supply | 12 V→USB-C adapter | **native** (6–36 V VIN) |
| DIN mount, screw terminals | none | **native** |
| LoRa radio | **native (SX1262)** | **external module required** |

The trade is one subsystem in exchange for three. It also aligns GateLink with
AquaLink (§3.5) and eliminates every hand-built discrete circuit from the design —
a long-standing project preference.

| Item | Value |
|---|---|
| Product | M5Stack StamPLC, SKU K141 |
| Module | Stamp-S3A — **ESP32-S3FN8, 8 MB flash, no PSRAM** (same SoC family as the Heltec V3) |
| Relays | 4 × SPDT, COM/NO/NC, AC 5 A @ 250 V / DC 5 A @ 28 V, via AW9523B |
| Inputs | 8 × opto-isolated (EL3H4), DC 5–36 V, via AW9523B |
| Supply | DC 6–36 V; DC5521 barrel + VIN/GND/COM terminal block |
| Expansion | StamPLC-Bus (2×8, 2.54 mm), PORT.A and PORT.C (HY2.0-4P) |
| Fieldbus | PWR-485 (SIT3088), PWR-CAN (SIT1044) — neither used in v1; see §B.5 |
| HMI | 1.14" ST7789v2 LCD (135×240), 3 user buttons, buzzer, RGB LED (via PI4IOE5V6408) |
| Onboard sensing | **INA226** (VIN V/I), LM75 temperature, RX8130CE RTC |
| Storage | microSD |
| Mounting | DIN rail |
| Operating temp | **0 – 40 °C** — see §4.7.6 |
| Consumption | 12 V: **15.22 mA standby, 47.84 mA working** (vendor spec) |

Because relays, inputs, buttons and RGB all sit behind I²C expanders, they consume
**no general-purpose GPIO** — which is what makes §4.7.2 feasible.

#### 4.7.2 Pin budget

Documented StamPLC-Bus pinout:

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | VIN | 2 | GND |
| 3 | GND | 4 | GND |
| 5 | GND | 6 | **EXT_5V** |
| 7 | G15 (SCL) | 8 | G13 (SDA) |
| 9 | G3 (PHY_RST) | 10 | G14 (INT) |
| 11 | G9 (SPI MISO) | 12 | G7 (SPI SCK) |
| 13 | G8 (SPI MOSI) | 14 | G11 (SPI CS) |
| 15 | **G40 (custom)** | 16 | **G41 (custom)** |

HY2.0-4P: **PORT.A** = GND / 5 V / G2 / G1; **PORT.C** = GND / 5 V / G5 / G4.

**Allocation:**

| Function | Signals |
|---|---|
| SX1262 SPI | G7 SCK, G8 MOSI, G9 MISO, G11 NSS |
| SX1262 BUSY | G41 |
| SX1262 DIO1 | G40 |
| SX1262 NRESET | **G14**, freed by the polling decision in §4.2.4 |
| VE.Direct UART | PORT.A — G1, G2 |
| I²C (onboard peripherals) | G15 / G13, already in use |
| **Spare** | PORT.C (G4, G5) — reserved for a second VE.Direct pair if a SmartShunt is added (§5.7.4) |

**Reserved — do not use:**

| Signal | Reason |
|---|---|
| **G3** | Common RST for the LCD, the PI4IOE expander **and** any StamPLC-Bus expansion module. A radio driver that pulses reset here also resets the display and an expander — and therefore risks disturbing relay state. **The SX1262 reset must not be wired to G3.** M5's own PoE module documentation flags this same hazard and recommends passing `-1` for the reset pin |
| **G0** | RS485_TX and the ESP32-S3 BOOT strap. Unused by GateLink; if PWR-485 is ever used, keep the bus idle-high |
| G42 / G43 | PWR-CAN. Unused in v1; see §B.5 |

*Fallback if G14 is later needed:* pull SX1262 NRESET to 3.3 V and pass
`RADIOLIB_NC`, accepting software-reset-only. Not preferred for an outdoor node.

**Module selection checklist — new in v0.8.** The allocation above has **no spare
pins for RF-path control**, which rules out a whole class of otherwise attractive
modules. Before ordering, confirm the candidate:

1. **Uses DIO2 for RF switching and requires no TXEN/RXEN lines.** This **excludes
   the Waveshare Core1262**, which v0.7 named as the reference part in error, along
   with most PA/LNA "long range" variants.
2. Runs from **3.3 V** (the carrier LDO, §4.7.3) and states its **TCXO voltage** —
   RadioLib must be given the correct value or the radio will not calibrate.
3. Breaks out SCK, MOSI, MISO, NSS, BUSY, DIO1, NRESET, 3V3 and GND. Nothing else is
   needed, and anything else is a pin the budget does not have.
4. Is a **915 MHz** part with an SMA or IPEX antenna connection.

A **WIN-SX1262-class module** is the current candidate on that basis. Confirm items
1–4 against the vendor's own schematic, not a marketplace listing.

#### 4.7.3 Power rails — **no 3.3 V is exposed**

Confirmed across current M5 documentation, and **D26 closes on that basis: no 3.3 V
rail is available.**

- StamPLC-Bus power pins are **VIN, GND and EXT_5V only**.
- The StamPLC PoE accessory documentation independently republishes the same bus
  map, naming pin 6 `EXT_5V`.
- Both HY2.0-4P ports are GND / 5 V / GPIO / GPIO.
- M5's own SPI expansion module (PoE, carrying a W5500 — a 3.3 V part) is specified
  for PoE / DC 12 V / DC 5 V input and **regulates its own 3.3 V locally.**

Rail capacity, vendor-specified under load: **expansion port 4.76 V @ 700 mA**,
HY2.0-4P **4.81 V @ 700 mA**. Note the rail sits near 4.8 V, not 5.0 V.

**Requirement.** The carrier board SHALL provide a local 3.3 V regulator serving
the SX1262 and the VE.Direct LV rail. Given a 4.76 V source, an **AMS1117-3.3 is
excluded** (~1.1 V dropout). Specify ≤300 mV dropout — AP2112K-3.3 or equivalent.
Load: ~120 mA peak SX1262 TX at +22 dBm plus the level-shifter rail; 700 mA of
headroom is ample.

> A netlist-level check against `K141_sch_StamPLC_V10_IO.pdf` is still worth doing
> when the carrier is laid out, but it can no longer change the design — only
> confirm it.

#### 4.7.4 Carrier board

**D27 is resolved: a perfboard carrier populated with prefabricated modules.** No
fabricated PCB, no breadboarded discretes. The board seats on the StamPLC-Bus header
and carries:

1. **3.3 V LDO breakout** from EXT_5V (§4.7.3)
2. **SX1262 module** meeting the §4.7.2 checklist, plus an SMA bulkhead to the
   enclosure exterior
3. **BSS138 level-shifter module** for the VE.Direct front end, plus D25's outcome
4. Screw or JST landing for the VE.Direct cable

The 3.3 V regulator and any discretes mount directly to the perfboard; everything
else arrives as a module on headers. This preserves the "no hand-built circuits"
property that motivated the platform change in the first place.

**Mounting.** The StamPLC is DIN mounted and the carrier should be too. Source a
**DIN-rail PCB carrier or DIN module enclosure** and cut the perfboard to fit it,
rather than free-mounting the assembly — the SMA bulkhead and the VE.Direct cable
both pull on the board, and an outdoor enclosure that sees a seasonal thermal cycle
is no place for something floating on its header. **`TBM`, §13.1** — pick the
carrier first.

**Layout.** Keep the SPI run short and away from the relay terminals; the relays are
dry contacts carrying only the 1050's low-voltage accessory signalling, so coupling
risk is low, but layout should not invite it. Keep the antenna feed short and give
the SX1262 module a solid ground return to the header.

#### 4.7.5 BLE — driver portability confirmed

Stamp-S3A is an **ESP32-S3FN8**; the Heltec LoRa V3 is an **ESP32-S3FN8**. Same
core, same flash, no PSRAM on either. The BMS client (§5.7) is therefore a
**recompile, not a port** — NimBLE-Arduino behaves identically on both.

Two carry-over items, neither StamPLC-specific except the second:

- **MTU.** Reference decodes were obtained at a negotiated MTU of 512, with
  responses arriving unfragmented. NimBLE defaults lower; the client must either
  request a larger MTU or implement reassembly.
- **Antenna (D28).** The Stamp-S3A's 2.4 GHz antenna is internal to the DIN case
  with **no external option**, unlike the LoRa side. The pack's own BLE transmitter
  is already known to be weak. §1.4 puts the node within inches of the battery, so
  this should be comfortable — but **measure RSSI from the intended mounting
  position** before committing. The §5.7.4 SmartShunt fallback is unchanged and
  remains the contingency.

#### 4.7.6 Operating temperature — **the principal risk of this platform**

StamPLC is specified **0–40 °C**. The gate enclosure is outdoors in western NC:
sub-freezing winter mornings and summer solar gain that can push a closed box well
past 40 °C.

**v0.8 narrows this to a high-temperature concern.** The ESP32-S3 die is rated
−40/+85. At the cold end the exposed parts are electrolytics, the LCD (which ghosts
badly when cold and is a debug aid, not a functional dependency) and the RTC
crystal's accuracy — none of them things the system depends on, and the exposure is
a few hours of a winter morning. At the hot end the concern is real and cumulative:
a sealed box in summer sun is the case that shortens component life.

**Mitigation ladder, cheapest first:**

1. **The enclosure already has screened vents.** Verify they are unobstructed and
   that convection has a path — inlet low, outlet high.
2. **Shade, or a reflective surface**, on the sun-facing side.
3. **A thermostatically controlled fan**, if the logs justify one. It costs a few
   Ah/day *while running*, which is a summer expense set against a summer harvest
   roughly double the winter figure — affordable, but not to be fitted speculatively.

**D29 — instrument first.** Three temperature sensors already sit inside this
enclosure: the StamPLC's **LM75**, the **MPPT** (via VE.Direct), and the **pack BMS**
(four sensors, via BLE). Publish all of them and log a full season before deciding.
The pack's low-temperature charge inhibition (§8.4) makes enclosure temperature a
value the system wants regardless, so **D29 costs nothing extra to instrument** —
and a decision to fit a fan should rest on logged maxima, not on a datasheet number.

This risk does not apply to AquaLink, which is indoors.

#### 4.7.7 Considered and not adopted — a LoRa/BLE co-processor (D30)

The obvious alternative to a bare SX1262 on a carrier is an **intelligent radio
module** — a Heltec LoRa V3, say — hung off a StamPLC UART, running the radio and
the BLE client itself and exchanging framed messages with the host. It packages the
SX1262, its antenna connection and a 3.3 V regulator into one off-the-shelf board,
which is exactly the kind of trade this project has preferred in hardware terms.

**It is not adopted, for firmware reasons rather than hardware ones:**

- It converts a wiring problem into a **two-MCU problem**: an inter-processor
  protocol to define, version and debug, carried underneath the LoRa protocol it is
  already carrying.
- **Two flash procedures at the gate**, on a node with no OTA. Every firmware visit
  doubles.
- **Split debugging.** A dropped packet becomes ambiguous between two processors and
  the link between them.
- The compute it offloads is not scarce. RadioLib plus NimBLE on an ESP32-S3, moving
  a few packets a minute and running one BLE poll every five, is nowhere near the
  headroom limit — and §4.2.4's relaxation to 100 ms polling removed the only
  real-time pressure the host was under.

**What it would genuinely buy** is an **externally positionable 2.4 GHz antenna**,
which is precisely the open problem in **D28**: the Stamp-S3A's BLE antenna is
internal to the DIN case with no external option, and the pack's transmitter is weak.

**D30 — decision: keep the direct SX1262 module as the plan of record, and retain
the co-processor as a documented fallback.** Revisit it if any of these occur:

1. Phase 0 carrier bring-up (§14) fails or proves fragile.
2. **D28** measures inadequate BLE margin from the final mounting position *and* the
   SmartShunt route (§5.7.4) is unattractive.
3. The pin budget breaks — a future requirement needs pins §4.7.2 has already spent.

---

## 5. Firmware — GateLink

### 5.1 Framework & libraries

- Build: **PlatformIO**, ESP32-S3 target.
- LoRa: **RadioLib** driving the SX1262. Channel Activity Detection (CAD) used
  for media access (§6.7).
- VE.Direct: use/port **osh-labs/VE.Direct_mppt_arduino** (**MIT** — confirmed,
  §12.1) for **both** the text-protocol parser and the HEX protocol definitions,
  register map, and encode/decode helpers.
- BLE: **NimBLE-Arduino** (§5.7) — materially smaller flash and RAM footprint
  than Bluedroid.
- **Platform HAL: `M5StamPLC` + `M5Unified`** (both M5Stack, MIT), wrapped behind
  `/lib/lran-platform/` (§3.5, §10) so GateLink and AquaLink compile against one
  abstraction and the Heltec targets keep building.
- 1050 interface: **no protocol library**. Debounced reads from the input expander,
  timed pulses to the relay expander. This is still the point of §4.2 — v0.7 only
  changes *where* those bits are read and written, not what they mean.

> **Scheduling constraint.** Inputs are polled at `input_poll_ms` (default **100**,
> §4.2.4) and relay pulses are timed in firmware, both over the same I²C bus. Neither
> may be starved by a blocking call. BLE polling (§5.7), VE.Direct HEX round-trips
> (§6.6) and LoRa TX must not run inline with the I/O loop — put the I/O service on
> its own task or a strict cooperative slot. **v0.8's relaxation from 10–20 ms to
> 100 ms makes this far less delicate than v0.7 made it sound**, but the requirement
> stands: a relay pulse whose trailing edge is late is a command of the wrong length.

> **v0.6:** the BusT4 port is gone from v1. That removes the project's only GPL
> dependency, its only reverse-engineered protocol, and its only firmware
> component requiring a custom UART break implementation. See §12.2.

### 5.2 Responsibilities

1. **1050 command driver** — pulse K1–K4 per §4.2.2 on authenticated command;
   enforce pulse width and inter-pulse spacing; never assert two conflicting
   relays.
2. **1050 state reader** — debounce IN1–IN6; derive gate **and hold** state per
   §4.2.4; maintain `hold_source` (§5.3.4).
3. **Detection and direction classification** — §5.4.
4. **VE.Direct text reader** — continuously parse the ~1 Hz text frames; cache the
   latest complete snapshot; watch the charger error field.
5. **VE.Direct HEX transport** — multiplex HEX request/response with the text
   stream on the same UART (§6.6); enforce the write-authentication rule
   (§6.6.3). **No local interpretation of register semantics.**
6. **BLE BMS client** — duty-cycled connect/read/disconnect against the battery
   BMS (§5.7).
7. **LoRa endpoint** — receive commands, transmit status/ACK/event frames; CAD +
   backoff before transmit (§6.7); fragmentation for oversized payloads (§6.4.4).
8. **Trigger/cache logic** — transmit on poll, gate or hold state change, VE.Direct
   critical error, vehicle detection event (§5.4), **FIRE assertion (§5.4.6)**,
   hard-shutdown alarm, or BMS alarm.
9. **Authentication** — verify HMAC + sequence before pulsing any relay, before
   applying a configuration change, and before passing HEX writes to the MPPT
   (§6.5, §6.6.3).
10. **Configuration management** — apply, acknowledge and persist runtime
    configuration; fall back to firmware defaults; publish the effective
    configuration and its provenance (§5.8).
11. **Power management** — WiFi **off**; BLE **duty-cycled** (§5.7.2); **LCD
    backlight off** in normal operation. No light-sleep, no RX duty-cycling (§8.6).
12. **Debug** — display control (§5.6), packet loopback, dummy status push,
    detection event injection, relay dry-run mode (§9.2), leveled serial logging.

### 5.3 Gate command model

Two operational cases, per requirement. Both are expressed as HA `cover` and
`switch` operations (§7.2). Every pulse is `relay_pulse_ms` wide — **default 500 in
v0.8**, raised from 300 (§4.2.2).

#### 5.3.1 Case 1 — momentary open

**Pulse K3 (Guard Station Open).** The gate opens to full open with auto-close
enabled; the 1050 closes it after the auto-close timeout, measured at **60 s**
(§1.5.5). Analogous to pressing open on a remote.

- Confirmed wake input (it is what the keypad drives).
- No hold state is entered. IN1/IN2 settle at 1/1 — open, countdown running.
- If the gate is already open and unlocked, the pulse restarts the auto-close
  countdown — harmless, and a useful way to extend the window without holding.

#### 5.3.2 Case 2 — open and hold

**Pulse K1 (AUX1 = OPEN and LOCK).** The gate opens and the 1050 latches a LOCK
state that inhibits auto-close and further commands. The gate stays open
indefinitely, and IN1/IN2 settle at **1/0** — open with nothing counting down
(§4.2.4). Bench-confirmed in v0.8 (§1.5.2).

Three properties make this the right mechanism, and it is the one the installation
already uses:

- **It is a latched state inside the 1050, set by a momentary pulse.** No wire is
  held down, so no stuck contact can hold the gate abnormally.
- **No safety interlock is bypassed.** FIRE was considered and rejected: it clears
  hard shutdown, which is a latched entrapment state deliberately requiring human
  intervention. A remote path that clears it would put an invisible bypass around
  a UL325 interlock.
- **No lock-out hazard.** SHADOW was considered and rejected: it also *maintains a
  closed gate closed*, so a stuck assertion with the gate shut would prevent
  anyone — keypad, wand, remote, GateLink — from opening it.

One power consequence, new in v0.8: while the gate is open, OUT1 is energized and
**the 1050 cannot enter standby** (§4.2.7). A long hold is a long active period.

#### 5.3.3 Case 3 — close

Two paths, both available:

| Path | Action | Result |
|---|---|---|
| **Release** (default) | Pulse K2 (AUX2 = UNLOCK) | Lock clears, auto-close resumes and closes the gate within the 60 s timeout. Matches existing behavior. |
| **Immediate** | Pulse K2, wait `unlock_settle_ms` (default 500), then pulse K4 (Guard Station Close) | Closes without waiting out the auto-close timeout |

K2 must precede K4 whenever the gate is held — a locked 1050 ignores a close
command. GateLink sequences this automatically; HA sees a single "close" operation.

#### 5.3.4 Hold-state tracking — **observed, not remembered (v0.8)**

v0.7 maintained `hold_open` as a flag GateLink set when it pulsed K1, because the
1050's lock state was believed unreadable through the accessory I/O. §4.2.4 shows it
is readable: **IN1 asserted with IN2 clear, stable for `hold_confirm_ms`, means the
gate is held.**

Hold state is therefore derived from the inputs, with a separate `hold_source` field
recording who GateLink believes did it:

| Observation | `held_open` | `hold_source` |
|---|---|---|
| IN1=1, IN2=0 stable, following a GateLink K1 pulse | true | `lran` |
| IN1=1, IN2=0 stable, following an IN5 (FIRE) assertion | true | `keypad_fire` |
| IN1=1, IN2=0 stable, with neither of the above | true | `manual` — remote, keyswitch or panel |
| IN1=1, IN2=1 | false | — (countdown running) |
| IN1=0 | false | — |

This closes the v0.7 gap directly: **a hold set by the programmed handheld remote or
the keyswitch is now visible**, so §5.4.5's alert covers it, HA shows it, and the
"Hold gate open" switch reflects the gate rather than only LRAN's own actions.

**Boot behaviour — changed in v0.8.** v0.7 pulsed K2 once at boot to clear a lock a
previous instance might have left. **That is withdrawn.** GateLink can now read the
hold state directly, and a blind unlock at boot would release a hold a human
deliberately set — closing a gate on a contractor's van after a power blip is a
worse failure than the one the pulse guarded against. On boot GateLink reads IN1/IN2,
adopts the observed state, sets `hold_source` to `unknown` if it cannot attribute
it, and publishes both. Releasing a hold stays an explicit act, from HA or by either
manual path (§4.2.6).

### 5.4 Vehicle detection — **REQUIRED**

#### 5.4.1 Installed detection hardware

| Signal | Source | Position | Semantics |
|---|---|---|---|
| **SAFETY** (IN3) | **Diablo DSP-7LP** | Two loops, **one outside and one inside the gate**, wired **in series** into one detector channel | **Single** dry contact. Asserts when a vehicle is over **either** loop. The two loops are *not* separately observable. |
| **EXIT** (IN4) | Self-contained **wand-style** sensor | **At least 20 ft** further inside the gate than the inside safety loop | Separate dry contact. Asserts on vehicle presence at the wand. |

> **Correction in v0.8:** the loops are wired in **series**, not in parallel as
> stated in v0.5–v0.7. The detector sees the combined inductance of both loops on one
> channel, so the observable behaviour — one contact, asserting for either loop, with
> no way to tell which — is unchanged. Only the wiring description was wrong.

Both contacts are tapped in parallel with their existing connections to the 1050
(§4.2.4). The Diablo's power change (§4.6) is **done**; the wand needed none.

The DSP-7LP draws **1 mA static** with no vehicle detected.

#### 5.4.2 Direction classification

The two signals are widely separated along the drive — SAFETY at the gate, EXIT at
least 20 ft inside it. Rising-edge ordering discriminates:

| First rising edge | Then | Classification |
|---|---|---|
| `EXIT` | `SAFETY` | `EXIT` — vehicle departing (moving outward) |
| `SAFETY` | `EXIT` | `ENTRY` — vehicle arriving (moving inward) |
| either | *(no second edge within window)* | `UNDETERMINED` |
| both within one debounce period | — | `UNDETERMINED` |

Four properties of the installed topology shape the logic:

1. **The window must be large.** A departing vehicle trips EXIT, then waits for
   the gate to open before reaching SAFETY. Gaps of 10–30 s are normal.
2. **SAFETY cannot distinguish inside from outside.** Both loops feed one channel,
   so a traversal produces one continuous SAFETY assertion spanning both loops, not
   two separable pulses. The classifier must not attempt to read structure inside a
   single SAFETY assertion.
3. **EXIT is also an actuator.** The wand's assertion causes the 1050 to open the
   gate. A gate-open transition on IN1/IN2 shortly after an EXIT rising edge is
   self-evidently exit-triggered.
4. **Wand hold behavior must be characterized.** Some self-contained wand sensors
   de-assert after a hold time even with a vehicle stationary above them, which
   would produce repeated edges from an idling vehicle. **`TBM`**, §13.1.

**Timing is not a constraint here.** With at least 20 ft between the two sensors and
normal gaps measured in seconds, the 100 ms poll interval and ~200 ms debounce of
§4.2.4 sit three orders of magnitude inside what classification needs. What does
matter is that the poll loop is never *stalled* for seconds by other work (§5.1).

#### 5.4.3 Requirements

- **5.4.3.1** GateLink SHALL represent the instantaneous state of SAFETY and EXIT
  as discrete booleans in the status payload.
- **5.4.3.2** GateLink SHALL derive direction of travel from the rising-edge
  ordering in §5.4.2.
- **5.4.3.3** A detection event SHALL trigger an **immediate unsolicited status
  push**, independent of the poll schedule.
- **5.4.3.4** GateLink SHALL raise the **held-open alert** defined in §5.4.5.
- **5.4.3.5** Direction classification SHALL be implemented as a state machine with
  an explicit idle-reset condition, so that partial traversals (vehicle pulls up to
  the wand and reverses; vehicle stops on the loops and waits) resolve to
  `UNDETERMINED` rather than corrupting the next real traversal.

#### 5.4.4 Configuration

| Parameter | Default | Meaning |
|---|---|---|
| `detect_sequence_window_ms` | **60000** | Max gap between first and second rising edge for the pair to count as one traversal |
| `detect_sequence_idle_ms` | 10000 | Both inputs clear this long ends the sequence and resets to idle |
| `input_debounce_samples` | **2** | Debounce per input, in poll samples — ~200 ms at `input_poll_ms` = 100. Edges closer than this across the two inputs are treated as simultaneous |
| `held_open_alert_repeat_s` | 0 | 0 = one alert per detection; non-zero re-alerts at this interval while the condition persists |

All four are runtime-configurable (§5.8). `held_open_alert_delay_s` from v0.5
remains **removed** — see §5.4.5.

#### 5.4.5 Held-open alert — **the primary operational requirement**

The need: *the gate is sometimes deliberately left standing open (deliveries,
contractors, guests), and in that state any vehicle movement through it should
notify by email and SMS from HA.*

**v0.8 defines this on observed hold state.** v0.6 improved on v0.5 by keying the
alert to an explicit hold rather than an inferred "statically open" — but it could
only see holds *LRAN itself* had commanded, plus the keypad FIRE code. §5.3.4 now
derives hold state from IN1/IN2 directly, so the third case the review identified —
**a hold set from the programmed handheld remote** — is covered by the same
mechanism, as is one set from the keyswitch.

**The gate is `HELD_OPEN` whenever §5.3.4 reports `held_open` = true**, whatever the
`hold_source`:

| `hold_source` | Set by |
|---|---|
| `lran` | GateLink pulsed K1 (OPEN+LOCK) |
| `manual` | Handheld remote, keyswitch, or panel |
| `keypad_fire` | Someone entered the keypad FIRE code (IN5) |
| `unknown` | A hold observed across a GateLink reboot |

**Requirement.** While `HELD_OPEN`, any rising edge on SAFETY or EXIT SHALL:

1. Set the `vehicle_while_held_open` alert in an immediate `EVENT` push, carrying
   `hold_source`.
2. Include the direction classification if available at push time, and send a
   follow-up push when classification completes.

**Why there is no arming delay.** With hold state observed rather than inferred,
there is no risk of firing on a normal traversal: a routine pass leaves the
auto-close countdown running, which asserts IN2, which is not a hold. The v0.5
30-second arming delay existed only to suppress false positives from an inferred
condition. Alerts fire on the first edge, with no dead window.

**HA delivery.** Because this drives email and SMS, the event must be delivered as
a **non-retained MQTT event message**, not solely as a retained `binary_sensor`
state (§7.4). Retained binary sensors replay on HA restart and on discovery
refresh, which would produce spurious 2 AM notifications. The binary sensor is for
dashboard visibility; **the event topic is the automation trigger.**

#### 5.4.6 FIRE is an emergency, not a mode — **new in v0.8**

The keypad's FIRE code is used **only for testing and in a real fire emergency.** It
is not a routine hold-open mechanism, and treating an IN5 assertion as merely
another way to hold the gate open understates it.

**Requirement.** Any rising edge on IN5 SHALL generate an **immediate, independent
`EVENT` push** — `lran/gatelink/event/fire`, non-retained — regardless of gate
state, in addition to setting `hold_source = keypad_fire` when the gate is open.
Keeping it separate from the held-open event lets HA route it differently: this is
the one signal in the system that should be allowed to wake someone up.

GateLink **senses** FIRE and never asserts it (§11).

### 5.5 Movement cause

Derived locally rather than read from the controller:

| Observation | Inferred cause |
|---|---|
| EXIT rising edge, then gate opens within `cause_window_ms` (default 10000) | Exit wand |
| GateLink pulsed K1 or K3, then gate opens | LRAN command |
| IN5 asserts, then gate opens | Keypad FIRE code |
| Gate opens and settles to **IN1=1 / IN2=0** with none of the above | **Manual hold** — handheld remote programmed OPEN+LOCK, or the keyswitch (§4.2.2.1) |
| Gate opens and settles to **IN1=1 / IN2=1** with none of the above | **External momentary open** — keypad, remote, or front panel |

Published as a text sensor. The last two rows are new in v0.8 and come free with the
`MOVING` semantics: a manual *hold* and a manual *momentary open* are now
distinguishable, which they were not when both simply read "open".

### 5.6 Display control (D6)

Display **off by default in all normal operating modes**, with two independent
activation paths.

**Manual.** A **StamPLC user button (A, B or C)** toggles the display. On
activation it shows a status page and starts an inactivity timer
(`display_timeout_s`, default 60); on expiry the backlight is switched off via the
PI4IOE expander (`LCD_BL`, P7). A press while on cancels the timer and blanks
immediately.

> **v0.7 — this got easier.** The Heltec design had one button on GPIO0, which is
> also the boot strap, requiring press-duration filtering and a post-reset sampling
> delay. StamPLC provides **three dedicated user buttons** behind the PI4IOE
> expander, none of them strapping pins. The GPIO0 caution is **withdrawn**. Three
> buttons also make the multi-page proposal below straightforward rather than a
> gesture-overloading exercise.

**Automatic.** The display SHALL power on and remain on, **ignoring the inactivity
timer**, whenever the node is in any debug mode (packet loopback, simulated status
push, detection event injection, relay dry-run, or any future registered debug
mode). It returns to manual/timeout behavior when the last debug mode exits.

**Sub-items for development:**
- **Multi-page cycling.** One page will not fit gate state + hold state + detector
  state + MPPT + battery SOC + link quality. With three buttons: **A = next page,
  B = previous, C = off**. The 135×240 colour LCD also fits materially more per page
  than the 0.96" OLED it replaces.
- **Backlight, not rail power.** v0.6 required full SSD1306 re-initialisation after
  every Vext power cycle — a known way to build a display that works exactly once.
  StamPLC's LCD stays initialised and only its **backlight** is switched, so that
  entire failure mode is **withdrawn**. Idle cost is the panel's static draw, which
  is inside the §8.2 node allocation.
- **Buzzer — new capability, optional.** StamPLC has one. Candidate use: audible
  confirmation during §9.3 bring-up, when the operator is at the gate and not
  looking at the screen. Default **off** in normal operation; a gate that beeps at
  its own accord is not wanted.

### 5.7 Battery BMS over BLE

#### 5.7.1 Why this matters

LiFePO4 has a famously flat discharge curve — roughly **13.2–13.3 V across 20–80%
SOC**. Voltage-derived SOC is close to meaningless through the middle of the range
and only usable near the endpoints. The MPPT compounds this: it measures **charge**
current only, so coulomb counting on discharge is unavailable from it.

**The BLE BMS is therefore the only good SOC source in the base BOM.**

#### 5.7.2 Requirements

- **5.7.2.1** GateLink SHALL, on a configurable interval (`bms_poll_s`, default
  **300**), connect to the battery BMS over BLE, read state of charge, pack
  voltage, pack current, cell voltages, temperature(s) and alarm/protection flags,
  then disconnect.
- **5.7.2.2** GateLink SHALL de-initialize the BLE controller between polls rather
  than leaving the stack resident.
- **5.7.2.3** BMS alarm or protection flags SHALL trigger an immediate unsolicited
  status push.
- **5.7.2.4** A failed BLE connection SHALL NOT block or delay any other node
  function. BMS data is published with a staleness timestamp; consumers treat
  absence as unknown, not as zero.
- **5.7.2.5** The connect/read/disconnect cadence SHALL leave the BMS reachable
  from a phone between polls — §5.7.5.

#### 5.7.3 Power cost

A connect / read / disconnect cycle is roughly **3–6 s at 60–100 mA ≈ 0.1 mAh**.
At `bms_poll_s` = 300 that is ~**0.03 Ah/day** against a ~7 Ah/day budget — under
half a percent.

**No radio coexistence problem.** The SX1262 is separate silicon on SPI with its
own antenna at 915 MHz; the ESP32-S3's BLE radio is 2.4 GHz. WiFi remains off
regardless (§8.7). **This is unchanged by v0.7** — the radio is now on a carrier
board rather than the same PCB, which if anything improves isolation. The open
question is link margin, not interference: see §4.7.5 and **D28**.

**Flash is the real budget item.** NimBLE adds ~200–300 KB. The 8 MB flash
accommodates this comfortably — and with the BusT4 stack gone, there is more
headroom than v0.5 assumed.

#### 5.7.4 Protocol — **D15 RESOLVED**

The BMS protocol is **known, implemented and validated**. The concerns and fallback
plan carried through v0.5–v0.7 were written before any of this was established and
are summarised here only so the reasoning is not lost.

**What it turned out to be.** The pack is a **TDT** BMS (advertising as
`XDZN_001_xxxx`), not the JBD/Xiaoxiang or JK family originally assumed — a
20-combination sweep against both of those returned nothing, which is what forced
the re-identification. It was pinned down by cross-checking against the `aiobmsble`
Python library, whose TDT plugin produced a full decode of the pack; the protocol
was then reimplemented independently and verified frame-for-frame against that
reference.

**The access sequence — the part nothing else documents:**

1. Write the **`HiLink` handshake to characteristic `FFFA`** — *not* `FFF2`.
2. **Read `FFFA` back** and confirm the `0x01` acknowledgement.
3. **Subscribe to `FFF1`** for notifications.
4. Send requests to **`FFF2`**, using request head **`0x1E`** (`0x7E` is never
   answered by this unit). All writes are **with response**.

Without step 1, writes to `FFF2` are ATT-acknowledged and then ignored, and the pack
drops the link at ~4 s — which is exactly what made every earlier probe look like a
wrong protocol. Frame formats, the register decode and a reference capture live in
`/docs/bms-protocol.md`.

**Validation.** An independent client ran **32 consecutive polls over ~81 s on one
connection, with zero CRC failures and no dropped frames**, decoding SOC, pack
voltage, per-cell voltages, four temperature sensors, capacity, cycle count and
MOSFET state in agreement with the reference library. The C++ client for GateLink is
a port of that implementation, testable against the same captures offline.

**Carry-over implementation notes:**

- **MTU.** The reference decodes were obtained at a negotiated MTU of 512, with
  responses arriving unfragmented. NimBLE defaults lower; the client must either
  request a larger MTU or implement reassembly.
- **Cell-voltage jitter.** Readings move 1–2 mV between polls from ADC noise.
  Round, or publish on change, rather than pushing every reading to HA.
- **Current sign convention is the one open item.** Bit `0x4000` is believed to be
  the discharge flag but has only ever been observed at 0.0 A. **`TBM`** (§13.1) —
  capture once under charge and once under load.
- **Link margin.** The pack's own BLE transmitter is weak — about −80 dBm from
  inches away, confirmed independently with a phone, so this is the battery and not
  the test hardware. GateLink will sit ~12 in from the pack, which should be
  comfortable, but **D28** still asks for an RSSI check from the final mounting
  position, because the StamPLC's 2.4 GHz antenna is internal to its case (§4.7.5).

**Fallback, if D28 goes badly:** a **Victron SmartShunt**. True coulomb-counted SOC
and load current, speaking VE.Direct, reusing the existing parser and transport at
the cost of a second level-shifted UART pair and ~1 mA. It is **no longer needed to
identify SOC** — only as an escape route if BLE cannot reach from the mounting
position. The **D30 co-processor** (§4.7.7) is the other escape route.

**Interim fallback**, if neither is available: publish battery voltage from the MPPT
with an explicit `soc_source: voltage_coarse` marker and track daily Vmin and daily
yield (H19/H20/H21) trends rather than instantaneous SOC.

#### 5.7.5 Single-connection constraint

The BMS accepts **one connection at a time**, so a node holding a persistent
connection locks the vendor phone app out entirely. At `bms_poll_s` = 300 the BMS is
free ~98% of wall-clock time.

Provide an HA `switch` — **"BMS BLE polling"** — to suspend polling while working
with the phone app. Default on.

### 5.8 Configuration management — **new in v0.8**

#### 5.8.1 Requirement

**Every timing interval, window, threshold and debounce value used by GateLink SHALL
be changeable at runtime, from HA, without reflashing the node.** GateLink has no
OTA (§6.2), it is 500 ft away, and reflashing means a laptop and a walk. Anything
that might plausibly want tuning after installation — `input_poll_ms`,
`relay_pulse_ms`, `post_wake_settle_ms`, `hold_confirm_ms`, the §5.4.4 detection
windows, `bms_poll_s`, `hex_timeout_ms`, alert repeat intervals, `display_timeout_s`
— must be reachable over the link.

#### 5.8.2 Mechanism

- An authenticated frame pair, **`CONFIG` / `CONFIG_ACK`** (§6.4.2), carrying
  key/value sets. **`CONFIG` requires a valid HMAC** (§6.5.2): a parameter that
  changes how the gate is driven is a command.
- MQTT interface on the bridge: `lran/gatelink/config/set` (HA → node, not
  retained), `lran/gatelink/config/state` (node → HA, **retained**, the full
  effective configuration), and `lran/gatelink/config/ack` carrying the outcome of
  the last change.
- HA exposure: a **`number` entity per commonly-tuned parameter** (§7.4), plus a raw
  key/value path for the long tail. Do not attempt to model every parameter as an
  entity.
- **Readback and defaults.** The node publishes its full effective configuration on
  boot, on change, and on request, with each value marked `default` or `override`. A
  `restore_defaults` command clears all overrides.
- Unknown keys are rejected individually with a reason, never silently ignored; the
  remainder of the set still applies.
- Out-of-range values are clamped to the documented range, and the clamp is reported
  in the ACK rather than applied quietly.

#### 5.8.3 Persistence — defaults in flash, overrides on microSD

| Layer | Where | Behaviour |
|---|---|---|
| **Defaults** | Compiled into firmware | Always present. A node with no card and no commands runs on these |
| **Overrides** | **microSD**, single file | Loaded at boot, applied over the defaults |
| **Live values** | RAM | What the node is actually using |

- A `CONFIG` change is applied to RAM immediately, then written to SD.
- **With no card fitted, or an unwritable one, the change is still applied and still
  ACKed — with status `applied_not_persisted`.** The node runs on the new values
  until it reboots, then returns to defaults. This is a deliberate degradation: a
  missing card must never make the node unconfigurable, and HA must never be told a
  value was saved when it was not.
- The persistence status is published as a diagnostic `binary_sensor`, so a node
  quietly running unsaved configuration is visible rather than surprising.
- SD also carries any on-node log (§9.2) and retained counters, which is the point:
  repeated writes stay off the ESP32's internal flash (§4.5.1).

#### 5.8.4 What is *not* runtime-configurable

- **The HMAC key** and any other secret. Flash only, provisioned over USB (§6.5.1).
- **LoRa PHY parameters** (SF/BW/CR/frequency/sync word). Changing these from HA
  means changing the link you are changing them over; one mismatch and the node is
  unreachable until someone walks to it with a laptop. If this is ever wanted it
  needs a commit-and-revert scheme — apply, require a confirmation frame within N
  seconds, otherwise revert. Out of scope for v1.
- **Node ID and schema versions**, which are contractual (§6.4.3).

---

## 6. Firmware — LoRaBridge & LoRa protocol

### 6.1 Framework & libraries (D5)

PlatformIO, ESP32-S3. RadioLib (SX1262). WiFi. Links the same shared LoRa protocol
library as every node, plus the VE.Direct HEX register model (§3.3).

**Define a thin `MqttTransport` interface; implement first against PubSubClient.**
Bridge traffic is modest — a poll per node every 1–5 minutes plus occasional
commands and events — so throughput and QoS 2 are irrelevant. What matters is
reliable reconnect, LWT, and publishing discovery-config JSON.

| Option | License | Assessment |
|---|---|---|
| **PubSubClient** | MIT | Tiny, synchronous, extremely stable, ubiquitous. **Gotcha:** default max packet 256 bytes — Discovery configs exceed this and fail confusingly. Fix with `MQTT_MAX_PACKET_SIZE` ≥1024. |
| **espMqttClient** | MIT | Actively maintained, sync and async variants, large payloads, QoS 0/1/2. Designated fallback. |
| **AsyncMqttClient** | MIT | Effectively unmaintained. Avoid for new work. |
| **esp-mqtt** (IDF native) | Apache-2.0 | Most robust reconnect/TLS, but pulls the design toward IDF. Reserve for later migration. |

> **v0.6 note:** v0.5 flagged raw BusT4 frame streaming as the feature most likely
> to stress a blocking publish. That feature is gone, so bridge MQTT load is now
> uniformly light and PubSubClient is a comfortable fit.

### 6.2 OTA update (D16)

**The bridge supports OTA; remote nodes do not.**

| | Bridge | GateLink / WellLink |
|---|---|---|
| Update path | **OTA over WiFi** + USB | USB only |
| Rationale | On the LAN, mains powered, physically accessible, and the node whose firmware changes most often | 500 ft away; a bad flash is a walk with a laptop, and there is no second radio path to recover through |

Requirements:
- ArduinoOTA or ESP-IDF `esp_https_ota`, on an authenticated endpoint with a
  password/key held in untracked config (§10).
- **Dual-partition (A/B) with rollback.** A bricked bridge takes the whole
  property's telemetry offline.
- OTA disabled during an active LoRa transaction; deferred until idle.
- Version string published to `lran/bridge/version` and exposed as a diagnostic
  sensor.

### 6.3 Responsibilities

1. LoRa RX → verify → decode per source node → publish to MQTT state topics.
2. MQTT command topic subscribe → build authenticated LoRa command addressed to
   the target node → TX → await ACK → publish result; retry with backoff.
3. **VE.Direct HEX proxy** (§6.6) — including write-authorization enforcement.
4. Publish **MQTT Discovery** config on boot and reconnect, **one HA device per
   registered node** plus the bridge itself.
5. **Availability** — bridge's own via MQTT LWT; **per-node via watchdog**
   (§6.4.6).
6. **Per-node poll scheduler**, each runtime-configurable via that node's HA
   `number` entity.
7. Publish per-node link diagnostics (RSSI/SNR, last-seen, missed polls).
8. Republish detection events as HA events (§7.4).
9. Debug: loopback mode, dummy-status publish, per-node simulators.
10. Serve OTA (§6.2).

### 6.4 LoRa protocol

#### 6.4.1 Topology

Star. The bridge is the centre and the only node that initiates polls. Nodes never
address each other. No mesh, no relaying.

#### 6.4.2 Frame format

```
| ver | type | src | dst | seq | frag | schema | payload... | MAC (8B, auth only) | CRC16 |
   1     1     1     1     2      1       1        0..N            0 or 8            2
```

| Field | Size | Notes |
|---|---|---|
| `ver` | 1 | Protocol version. See §6.4.5. |
| `type` | 1 | `COMMAND`, `COMMAND_ACK`, `POLL`, `STATUS`, `EVENT`, `ERROR`, `PING`/`LOOPBACK`, `HEX_REQ`, `HEX_RSP`, **`CONFIG`**, **`CONFIG_ACK`** |
| `src` / `dst` | 1 each | Node IDs per §1.6. `0x00` bridge, `0xFF` broadcast. 254 usable addresses. |
| `seq` | 2 | Monotonic per source, per boot. |
| `frag` | 1 | High nibble = fragment index, low nibble = total. `0x11` = single unfragmented frame. |
| `schema` | 1 | Payload schema ID + version for `STATUS`/`EVENT`. See §6.4.3. |
| `MAC` | 0 or 8 | Truncated HMAC-SHA256. Present on authenticated types only (§6.5). |
| `CRC16` | 2 | Over the whole frame. |

Header is 9 bytes. Against the SX126x 255-byte PHY payload limit that leaves
**~244 bytes** unauthenticated or **~236 bytes** authenticated, before CRC.

> **v0.6:** with raw frame streaming gone, GateLink's status payload is comfortably
> **40–80 bytes** and never approaches the limit. Fragmentation is retained anyway
> (§6.4.4) — it costs one header byte and WellLink or a future node may need it.
> The `RAW` frame type is removed.

#### 6.4.3 Per-node payload schemas

Each node type defines its own payload schema, identified explicitly:

| `schema` value | Meaning |
|---|---|
| `0x10` | GateLink status v1 |
| `0x11` | GateLink event v1 (detection, held-open, fire) |
| `0x12` | **GateLink configuration v1 (§5.8)** |
| `0x20` | WellLink status v1 (reserved) |
| `0xF0` | Generic node health (uptime, RSSI, boot count) — all nodes |

**Why explicit rather than inferred from `src`.** The bridge *could* look up node
type from its registry. Explicit schema IDs additionally survive **firmware version
skew** — GateLink running an older build announces schema `0x10` while the bridge
already understands `0x12`, and the bridge decodes it correctly instead of
misparsing. Given that remote nodes have no OTA and will drift out of sync, this is
worth one byte.

Schema definitions live in `/lib/lran-protocol/schemas/` and are the versioned
contract between node and bridge.

#### 6.4.4 Fragmentation

- Sender splits into ≤15 fragments, each a complete frame with its own CRC.
- Receiver reassembles by `(src, seq, schema)`; incomplete sets expire after
  `frag_reassembly_timeout_ms` (default 5000) and are discarded with an error log.
- **Fragments are individually acknowledged only for `COMMAND`.** Status is
  fire-and-forget; a dropped fragment discards the set.
- Fragmentation is **not** used for commands in v1. Commands are small; keeping
  them single-frame keeps authentication and replay logic simple.

#### 6.4.5 Version tolerance — the no-OTA consequence

Remote nodes are USB-only. A protocol change means a physical visit to every node.
To make rollout incremental rather than a flag day:

- **The bridge SHALL accept protocol version `N` and `N−1`** and decode both.
- Nodes accept only their own version from the bridge; the bridge downgrades
  per-node based on the version last heard from that node.
- A node running an unsupported version is marked `unavailable` with a distinct
  reason, not silently ignored.
- Breaking changes to `/lib/lran-protocol/` require a version bump and an entry in
  `/docs/protocol-changelog.md`.

#### 6.4.6 Per-node availability

MQTT LWT covers only the bridge's connection to the broker. It says nothing about
whether GateLink is alive.

- The bridge maintains `last_seen` per node.
- A node is marked `offline` after `missed_poll_threshold` (default **3**)
  consecutive unanswered polls, and `online` on any valid frame received.
- State published to `lran/<node>/availability`, **retained**, and referenced by
  every entity belonging to that node in its discovery config.
- The bridge's own LWT marks all nodes unavailable implicitly.

### 6.5 Authentication and keying

#### 6.5.1 Per-node keys

A single fleet-wide key would mean **compromising the well sensor grants gate
command authority** — an unacceptable coupling between a low-value node and the
only node that moves a large motorized object.

**Derive per-node keys from one master:**

```
node_key = HKDF-SHA256(master_key, salt = "lran-v1", info = "node-" || node_id)
```

- Only `master_key` is provisioned to the bridge.
- Each node is flashed with only its own derived key.
- Compromising a node yields that node's key alone.
- Adding a node requires no change to existing nodes.

`master_key` lives in untracked build config (§10) and is never committed.

#### 6.5.2 What is authenticated

| Frame type | MAC required | Rationale |
|---|---|---|
| `COMMAND` | **Yes** | Moves the gate. |
| **`CONFIG`** | **Yes** | Changes pulse widths, detection windows and alert behaviour — a command by any other name (§5.8.2). |
| `HEX_REQ` — Set (`0x8`) / Restart (`0x6`) | **Yes** | Writes MPPT config — a battery-damage path under LiFePO4 (§6.6.3). |
| `HEX_REQ` — Get (`0x7`) | No | Read-only, consistent with status. |
| `STATUS`, `EVENT`, `POLL` | No | Spoofed status is a nuisance, not a hazard. |
| `COMMAND_ACK`, `CONFIG_ACK` | No | Correlated to an authenticated request by `seq`. |

#### 6.5.3 Sequence / replay

Monotonic counter, no cross-reboot persistence. Each node picks a random `boot_id`
at boot; the bridge keeps a **per-node table** of `(boot_id, last_seq)` and accepts
increasing `seq` within a `boot_id`, resyncing on a new one.

#### 6.5.4 Reliability

Commands are ACKed (receipt + execution result); the bridge retries with backoff.

> **v0.6 caution on retries.** GateLink's commands are now **physical relay
> pulses**, so a retried command is a second pulse, not an idempotent re-send. A
> duplicate K3 restarts the auto-close countdown (harmless); a duplicate K1 or K2
> re-asserts a state already held (harmless). But GateLink SHALL deduplicate on
> `(boot_id, seq)` and return the cached ACK rather than pulsing twice — the bridge
> retrying an ACK it never received must not move the gate again.

### 6.6 VE.Direct HEX proxy

#### 6.6.1 Requirement

HA must be able to read and write **all** MPPT 75/15 configuration and status
registers. GateLink acts as **transport only** — it does not interpret register
semantics, hold a register cache, or replay writes.

#### 6.6.2 On-node multiplexing

The MPPT emits ~1 Hz text frames and HEX responses **on the same UART**, and they
interleave. The parser is a line-oriented state machine:

- A line beginning with `:` is a **HEX** frame — hex nibbles, checksummed,
  newline-terminated.
- Anything else belongs to the **text** protocol (`LABEL\tVALUE\r\n`, blocks
  terminated by a `Checksum` field).

Rules:
- **One outstanding HEX transaction at a time.** Correlate the response by its
  echoed register ID; discard unmatched responses with a log entry.
- `hex_timeout_ms` default **1000**; on timeout return a `HEX_RSP` carrying an
  explicit timeout status rather than silence.
- **Do not disable the text protocol.** The 1 Hz stream is the primary telemetry
  source.
- HEX requests are transported **verbatim**. GateLink inspects only the command
  nibble, for §6.6.3.

#### 6.6.3 Write protection — three independent gates

Writing MPPT charge parameters under LiFePO4 is a **battery-damage path**.
Re-enabling temperature compensation or equalization on a lithium pack is exactly
the kind of one-character mistake that is invisible until the battery is harmed.

1. **HMAC on the frame.** Set (`0x8`) and Restart (`0x6`) require a valid MAC
   (§6.5.2). GateLink rejects unauthenticated writes at the transport layer and
   returns an `ERROR`.
2. **Armed write-enable switch.** An HA `switch` — **"MPPT config write enable"** —
   must be on. **Default off**, with **auto-expiry after `mppt_write_arm_timeout_s`
   (default 300)**. Enforced on the **bridge**, which refuses to build a write
   frame while disarmed. Deliberately a two-step operation.
3. **Audit trail.** Every write attempt — request payload, authorization outcome,
   MPPT response — published to `lran/gatelink/vedirect/hex/audit`, **retained**.

#### 6.6.4 MQTT interface

| Topic | Direction | Retained | Purpose |
|---|---|---|---|
| `lran/gatelink/vedirect/hex/request` | HA → bridge | No | Raw HEX request string |
| `lran/gatelink/vedirect/hex/response` | bridge → HA | No | Raw HEX response + status |
| `lran/gatelink/vedirect/hex/audit` | bridge → HA | Yes | §6.6.3 item 3 |
| `lran/gatelink/vedirect/write_enable/{state,set}` | both | Yes | §6.6.3 item 2 |

Raw HEX in and out is the v1 interface — the general case, needing no
register-by-register modelling. **Once the small set of registers actually adjusted
in practice is known, promote those to named `number`/`select` entities.** Do not
attempt to model 100+ registers as entities.

#### 6.6.5 Sequencing — the chicken-and-egg

The MPPT must be reconfigured for LiFePO4 **before the new battery is first
charged**, and LRAN will not exist at that point.

- **Initial reconfiguration:** VictronConnect over a VE.Direct-to-USB cable. Do this
  when the battery arrives. §8.1.2 lists the required settings.
- **LRAN's HEX path is for ongoing adjustment and verification**, and for reading
  back configuration to confirm it has not drifted — not for initial setup.

On boot, GateLink reads the charge parameters and the bridge publishes them as
diagnostic sensors, so a wrong profile is visible in HA rather than latent.

### 6.7 Media access

Point-to-point had no contention. A shared channel with N nodes does.

| Mechanism | Detail |
|---|---|
| **Bridge serializes polls** | Never more than one outstanding poll across the fleet. Removes the largest predictable collision source for free. |
| **CAD before TX** | Nodes use RadioLib's Channel Activity Detection before transmitting. Cheap in time and power. |
| **Randomized backoff** | On CAD-busy, back off `random(0, backoff_max_ms)` (default 500), retry up to `cad_retries` (default 5), then transmit regardless — an event push must not be starved indefinitely. |
| **Single channel** | All nodes share one frequency/SF/BW/sync word. Per-node channels would require the bridge to listen on multiple configurations, which one SX1262 cannot do. |

> **v0.6:** removing raw frame streaming removes the only feature that could
> occupy the channel for extended periods. Expected occupancy is now dominated by
> short status frames on a 1–5 minute cadence, which is negligible.

**FCC.** 915 MHz ISM, digital modulation under Part 15.247. Not LoRaWAN, so no TTN
duty-cycle policy applies, but transmit power and bandwidth limits do.

### 6.8 LoRa link parameters

- Band: **US 915 MHz**, point-to-multipoint star (not LoRaWAN), private sync word.
- SF / BW / CR / TX power: **TBD** after the range test (D1). Starting point for
  ~150 m near-LOS: SF7–9, BW 125 kHz, CR 4/5, moderate TX power.
- **Bridge antenna placement is a two-bearing problem.** GateLink and WellLink are
  at similar distances in **different directions**. Favour an omnidirectional
  antenna in a central, elevated position over anything with a pattern optimized
  toward the gate. Range-test **both bearings** before committing to a location.
- Node-address filtering in the SX126x packet handler is enabled so nodes discard
  frames not addressed to them in hardware. Low value while GateLink runs
  continuous RX; retained because it matters for any duty-cycled node (Appendix A).

---

## 7. Home Assistant integration (MQTT Discovery)

### 7.1 Device model

**One HA device per node, plus one for the bridge.** Each node's entities carry
that node's `device` block and reference that node's availability topic (§6.4.6).

| HA device | Node | Availability source |
|---|---|---|
| LoRa Bridge | `0x00` | MQTT LWT |
| GateLink | `0x01` | `lran/gatelink/availability` (bridge watchdog) |
| WellLink | `0x02` | `lran/welllink/availability` (bridge watchdog) |

Discovery `unique_id`s are prefixed per node (`lran_gatelink_*`) so they are stable
and non-colliding as the fleet grows.

### 7.2 Entity model (D8): `cover` **plus** a hold switch

**`cover` (device_class: `gate`) is the primary control surface** — native
open/close/stop UI, voice-assistant support, dashboard cards, standard `cover.*`
services. Driven from real IN1/IN2 state with `optimistic: false`.

Mapping to §5.3:

| HA operation | GateLink action |
|---|---|
| `cover.open_cover` | Case 1 — pulse K3 (momentary open, auto-close returns it) |
| `cover.close_cover` | Case 3 — pulse K2, then K4 if `close_immediate` is set |
| `switch.turn_on` ("Hold gate open") | Case 2 — pulse K1 (OPEN and LOCK) |
| `switch.turn_off` ("Hold gate open") | Pulse K2 (UNLOCK); auto-close then closes |

**A `switch` rather than a second cover.** "Hold open" is a persistent mode, not a
momentary action, and a switch models a mode correctly — it shows current state,
it is togglable from a dashboard, and it can be used as an automation condition.
Modelling it as a button would lose the state.

**v0.8: the switch reflects the gate, not just LRAN.** Its state comes from the
observed hold derivation in §5.3.4, so a hold set from the handheld remote or the
keyswitch shows as on. Turning it off pulses K2 regardless of who set the hold,
which is the behaviour a user expects from a switch that claims to show one.

`cover.stop_cover` is **not implemented in v1** (§4.2.5). The entity may either
omit stop or expose it as unavailable.

### 7.3 Position reporting — deferred to v2

**v1 will not report position.** Time-based estimation on a gate that can be
stopped, reversed, or obstructed produces confident wrong answers, and a cover that
lies about being 40% open is worse than one reporting discrete states.

### 7.4 Entity table — GateLink

Topic hierarchy `lran/gatelink/...`, retained discovery configs, per-node
availability topic.

**Gate control and state**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Gate | `cover` (device_class: gate) | IN1/IN2 | open/close; no position (§7.3) |
| Hold gate open | `switch` | derived hold state (§5.3.4) | **Reflects any hold, not only LRAN's.** Turning it off pulses K2 |
| Hold source | `sensor` (text, diagnostic) | §5.3.4 | `lran` / `manual` / `keypad_fire` / `unknown` |
| Close immediately | `switch` (config) | — | When on, close uses K2+K4 rather than K2 alone |
| Gate state | `sensor` (text, diagnostic) | IN1/IN2 | closed / moving / open (counting down) / open (held) |
| Auto-close pending | `binary_sensor` | IN2 while IN1 asserted | The countdown is running |
| Held open by keypad | `binary_sensor` | IN5 (FIRE sense) | A human used the FIRE code |
| **FIRE asserted** | **`event`** | IN5 | **`lran/gatelink/event/fire`, non-retained.** Emergency — route separately (§5.4.6) |
| Hard shutdown | `binary_sensor` (problem) | IN6 (alarm sense) | Entrapment latch — requires physical reset |
| Movement cause | `sensor` (text) | derived, §5.5 | exit wand / LRAN / keypad / manual hold / external |

**Vehicle detection (§5.4)**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Safety detector | `binary_sensor` (occupancy) | IN3 | Combined DSP-7LP contact, both loops |
| Exit wand | `binary_sensor` (occupancy) | IN4 | Separate wand contact |
| Last traversal direction | `sensor` (text) | derived | `ENTRY` / `EXIT` / `UNDETERMINED` |
| Last traversal time | `sensor` (timestamp) | derived | |
| Vehicle while held open | `binary_sensor` (problem) | derived | **Dashboard visibility only** |
| **Vehicle while held open** | **`event`** | derived | **`lran/gatelink/event/held_open`, non-retained. This is the automation trigger for email/SMS.** Carries `hold_source` |

> **Why both.** A retained `binary_sensor` replays its state when HA restarts or
> re-reads discovery, which would fire the notification automation at arbitrary
> times. The non-retained event topic fires exactly once, when it happens. Use the
> `binary_sensor` for the dashboard and the **event for the automation** (§5.4.5).

**Battery and solar**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Battery SOC | `sensor` (%) | BMS via BLE | §5.7.4 — protocol resolved |
| Battery SOC source | `sensor` (text, diagnostic) | GateLink | `bms_ble` / `smartshunt` / `voltage_coarse` — honest about provenance |
| Pack voltage / current | `sensor` (V / A) | BMS | Current sign convention pending (§5.7.4) |
| Battery voltage | `sensor` (V) | MPPT | |
| Battery current (charge) | `sensor` (A) | MPPT | Charge only unless a SmartShunt is fitted |
| Pack temperatures | `sensor` ×4 | BMS | §8.4, §4.7.6 |
| Cell voltages | `sensor` ×N (diagnostic) | BMS | Publish on change, not every poll (§5.7.4) |
| BMS alarm / protection flags | `binary_sensor` ×N | BMS | Trigger an immediate push (§5.7.2.3) |
| Cycle count / capacity | `sensor` (diagnostic) | BMS | |
| **Charging inhibited (low temp)** | `binary_sensor` (problem) | derived | **§8.4** |
| BMS BLE polling | `switch` | GateLink | §5.7.5 |
| Panel voltage / power | `sensor` (V / W) | MPPT | |
| Yield today | `sensor` (kWh) | MPPT | |
| Charge state | `sensor` (text) | MPPT | bulk/absorption/float/off |
| Charger error | `sensor` (text/code) | MPPT | non-zero → alert + push |
| MPPT config readback | `sensor` ×N (diagnostic) | MPPT via HEX | §6.6.5 |
| MPPT config write enable | `switch` | bridge | §6.6.3, default off, auto-expiry |

**Node health and environment**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Node supply voltage / current | `sensor` (V / A, diagnostic) | **INA226** | §4.5 — the node measures its own draw |
| Enclosure temperature | `sensor` (°C) | **LM75** | §4.7.6, **D29** seasonal log |
| MPPT temperature | `sensor` (°C, diagnostic) | MPPT | Third thermal reference point |

**Configuration (§5.8)**

| Entity | Type | Notes |
|---|---|---|
| Input poll interval | `number` (config) | `input_poll_ms`, 20–500 |
| Relay pulse width | `number` (config) | `relay_pulse_ms`, 100–2000 |
| Hold confirm time | `number` (config) | `hold_confirm_ms` |
| Detection sequence window | `number` (config) | `detect_sequence_window_ms` |
| Detection idle reset | `number` (config) | `detect_sequence_idle_ms` |
| BMS poll interval | `number` (config) | `bms_poll_s` |
| Held-open alert repeat | `number` (config) | `held_open_alert_repeat_s` |
| Configuration persisted | `binary_sensor` (diagnostic) | False ⇒ running unsaved config; no usable microSD (§5.8.3) |
| Restore defaults | `button` | Clears all overrides |

**Diagnostics**

| Entity | Type | Source | Notes |
|---|---|---|---|
| LoRa RSSI / SNR | `sensor` (diagnostic) | bridge | per node |
| BMS link RSSI | `sensor` (diagnostic) | GateLink | Ongoing evidence for **D28** |
| Missed polls | `sensor` (diagnostic) | bridge | feeds §6.4.6 |
| Node uptime | `sensor` (diagnostic) | GateLink | |
| Protocol version | `sensor` (diagnostic) | bridge | §6.4.5 — shows node/bridge skew |
| Poll interval | `number` (config) | bridge | per node, runtime-configurable |
| Relay dry-run | `switch` (config, diagnostic) | GateLink | §9.2 — log intent without pulsing |

### 7.5 Entity table — LoRaBridge

| Entity | Type | Notes |
|---|---|---|
| Bridge availability | via LWT | |
| WiFi RSSI | `sensor` (diagnostic) | |
| Bridge uptime | `sensor` (diagnostic) | |
| Firmware version | `sensor` (diagnostic) | §6.2 |
| Nodes online | `sensor` (count, diagnostic) | |

### 7.6 WellLink — reserved

Entity model to be defined with the node (§1.7). Expected: level `sensor`, battery
`sensor`s, availability, RSSI, poll interval `number`. The device and topic
namespace are reserved now so nothing else claims them.

---

## 8. Power budget (gate side)

### 8.1 Source — LiFePO4

#### 8.1.1 Capacity

**12 V 100 Ah LiFePO4** (WattCycle 100 Ah mini w/ Bluetooth), replacing the Group
24 FLA. **100% of nameplate capacity is usable.** 50 W panel via MPPT 75/15.

| | v0.4 (FLA) | v0.6 (LiFePO4) |
|---|---|---|
| Nameplate | 75 Ah | 100 Ah |
| Usable | 37.5 Ah (50% derate) | **100 Ah** |
| Daily draw (§8.2) | ~7.3 Ah | **~7.0 Ah** |
| Autonomy at zero harvest | ~5.1 days | **~14.3 days** |
| Winter harvest ratio | 1.45× | **1.51×** |

#### 8.1.2 Required MPPT reconfiguration — **before first charge**

Charging LiFePO4 on a lead profile is harmful. Do this with VictronConnect over USB
when the battery arrives, then verify via the HEX readback sensors (§6.6.5).

| Setting | Required value | Why |
|---|---|---|
| Battery type | **User-defined** | Not any lead preset |
| Absorption voltage | **14.2–14.6 V** | Per WattCycle spec; confirm against the pack datasheet |
| Absorption time | Short / fixed | LiFePO4 does not need long absorption |
| Float voltage | **~13.5 V** | Avoids holding the pack at high SOC indefinitely |
| **Equalization** | **Disabled** | Destructive to LiFePO4 |
| **Temperature compensation** | **0 mV/°C** | Default −16.2 mV/°C is a lead-acid behavior and is actively wrong here. **The single most consequential setting in this table.** |
| Low-voltage cutoff / load output | Per pack spec | The pack BMS is the real protection |

Record final values in `/docs/mppt-config.md`.

### 8.2 Load inventory

**TBM** = To Be Measured; placeholders are conservative. **Read §8.2.2 before
treating this table as settled.**

| Load | Current @ 12 V | Duty | Ah/day |
|---|---:|---|---:|
| 2 × 2 W LED lights (night only) | 0.33 A | 12 h | 4.00 |
| 1050 controller, **standby retained** (§4.2.3) | 0.017 A **TBM** | ~23 h | 0.39 |
| 1050 controller, active windows | 0.110 A **TBM** | ~1 h | 0.11 |
| Diablo DSP-7LP, continuously powered (§4.6) | 0.001 A | 24 h | 0.02 |
| GateLink node — continuous RX (StamPLC) | 0.048 A (vendor) **TBM** | 24 h | 1.15 |
| GateLink — BLE BMS polling | — | 288 cycles/day | 0.03 |
| GateLink — relay coils | ~0.070 A | ~10 pulses × 0.5 s/day | <0.01 |
| MPPT 75/15 self-consumption | 0.010 A | 24 h | 0.24 |
| Gate motors while operating | ~5 A **TBM** | 2 cycles × ~20 s | 0.06 |
| *SmartShunt* — **contingency only** | *0.001 A* | *24 h* | *0.02* |
| **Total** | | | **≈ 6.9 Ah/day** |

**Harvest:** 50 W × ~3.0 winter peak-sun-hours (western NC, non-optimal tilt) ×
0.85 system efficiency ≈ 127 Wh ≈ **10.6 Ah/day**. Summer roughly doubles this.

Not in the table because it is occasional rather than continuous: **a hold-open
prevents 1050 standby** (§4.2.7), so every hour of hold costs roughly the difference
between the active and standby rows above.

#### 8.2.1 Standby is retained, and the OUT programming protects it

Because every LRAN command wakes the 1050 on its own (§4.2.3), **standby stays
enabled**, at the measured **60 s** timeout (§1.5.5).

| Configuration | 1050 contribution | Total | Harvest ratio |
|---|---|---:|---:|
| **Standby retained** | ~0.50 Ah/day | 6.9 | **1.53×** |
| Standby disabled | ~2.40 Ah/day | 8.9 | 1.19× |

Disabling standby would cost roughly **19% of winter harvest** and contradict the
manufacturer's own solar guidance. v0.8 adds a second way to lose it that has
nothing to do with the timeout: **programming an OUT relay to a state the gate rests
in.** `OUT = Closed` would inhibit standby permanently (§4.2.7). The chosen
OUT1 = OPEN / OUT2 = MOVING pair leaves the resting state — closed and idle — free
to sleep.

#### 8.2.2 The first measurements do not agree with this table

§1.5.4 records the first clamp readings of the 1050 branch: **0.23–0.33 A across all
four gate states, with standby reading higher than active.** Both the magnitude and
the ordering are inconsistent with the table above — and with a site that has run
for years on a 75 Ah FLA derated to 50%.

**The budget is not revised on those numbers.** They are more consistent with
instrument error below 1% of full scale than with the board's behaviour, and
rewriting a working system's budget on a measurement that fails its own internal
sanity check would be the wrong move. Nor can they be dismissed: if the standby
figure is real, the site runs a winter deficit, and the fix would be **LED runtime**,
not firmware (§8.3).

**Re-measurement is the top item in §13.1** — inline meter or shunt on a mA range,
or the same clamp on its lowest range with ten turns through the jaw, in all four
gate states, with the LED lights separately accounted. Until then, treat §1.5.4 as
an upper bound and this table as the planning basis.

### 8.3 Findings

1. **The LEDs are the budget.** At ~4 Ah/day they are ~57% of consumption, more
   than every other load combined. Real headroom comes from LED runtime, not
   firmware.
2. **A bigger battery does not fix a harvest deficit — it extends the
   ride-through.** Harvest vs. load is 10.6 in / 7.0 out. That ratio is a property
   of the panel and the loads. What 100 Ah buys is **cloud tolerance and time to
   notice**.
3. **Autonomy is ~14.3 days** from full at zero harvest, up from ~5.
4. **Firmware power optimization is not a design driver.** At 100 Ah usable, prefer
   the simple, always-on, low-latency design in every case. §8.6 acts on this.
5. **The system already works.** The existing install has run for years. LRAN adds
   ~1.4 Ah/day, roughly 20% of the new total, against an autonomy figure that
   nearly tripled.

### 8.4 Low-temperature charge inhibition

The pack's BMS blocks charging below approximately 0 °C. This is correct behaviour
and is accepted; the reserve covers it. But it must be **observable**, because its
symptom — a battery not recharging on a sunny day — is otherwise indistinguishable
from a failing panel or MPPT.

**Autonomy check.** ~14.3 days of reserve comfortably exceeds any realistic western
NC sub-freezing spell, where daytime highs typically rise above freezing even in a
cold snap and restore a charging window each day. This is a monitoring problem, not
a capacity problem.

**Detection without the BMS** — inferable from VE.Direct alone:

> PV power present **AND** charger state is bulk/absorption **AND** battery current
> ≈ 0 **AND** battery voltage not rising, sustained for `charge_inhibit_confirm_s`
> (default 300) ⇒ **charging inhibited**

**Detection with the BMS:** read the charge-inhibit / low-temperature protection
flag and pack temperature directly. Preferred when available.

**Requirements:**
- Publish a `binary_sensor` "Charging inhibited" (device_class `problem`).
- Publish pack temperature when the BMS is reachable.
- Maintain a **cumulative Ah drawn since charging was last active** counter, so a
  multi-day cold event shows consumed reserve rather than just a boolean.
- Note **upgrading the panel does not help while charging is inhibited**.

### 8.5 Validation and ongoing monitoring

**Before install:** log the MPPT's own yield history (`H19`/`H20`/`H21`) and battery
voltage minima for one week via VE.Direct. That yields the *actual* present margin
for free.

**After install — the plan of record.** Monitor nightly consumption and overnight
recovery over time:

| Metric | Source | What it tells you |
|---|---|---|
| Daily yield (H20) | MPPT | Harvest trend, panel/shading degradation |
| Overnight ΔSOC | BMS or SmartShunt | True nightly consumption |
| Daily Vmin | MPPT | Proxy for depth of discharge if SOC is unavailable |
| Days since full | derived | Early warning of a sustained deficit |
| Charging-inhibited hours | §8.4 | Distinguishes cold events from real faults |

**Decision rule for a panel upgrade:** upgrade only if *days-since-full* trends
upward across a season **and** the shortfall is not attributable to
charging-inhibited hours.

### 8.6 Not implemented — night profile, RX duty-cycling, extended preamble

Removed in v0.5 and confirmed removed here. The saving was ~0.36 Ah/day — **0.36%
of 100 Ah usable** — against real costs in complexity, latency, and overnight
VE.Direct data continuity. **Retires D2, D9, D10** for GateLink. The design is
preserved in **Appendix A** for a possibly battery-powered WellLink.

### 8.7 Fixed levers

- **WiFi disabled** on GateLink (largest single ESP32 saving).
- **BLE duty-cycled**, not resident (§5.7.2).
- **LCD backlight off** in normal operation (§5.6). Note this switches the backlight
  only — the panel stays initialised, unlike the v0.6 OLED rail-power scheme.
- Status caching minimizes LoRa TX (the peak consumer).
- **1050 standby retained** (§8.2.1) — the largest single lever in the budget, and
  now explicitly protected by the OUT programming choice (§4.2.7).
- Relays are momentary; coil current stays negligible even at the 500 ms pulse.

### 8.8 Where the current goes on the node

The **radio is not the constraint.** Per the SX1262 datasheet, LoRa 125 kHz receive
is **4.2 mA** (normal) or **5.3 mA** (Rx-boosted, +3 dB); TX is ~90 mA @ +14 dBm
and ~118 mA @ +22 dBm; sleep with config retained is sub-µA. Continuous RX adds only
~5 mA on top of an **ESP32-S3 that dominates** at tens of mA while awake. The floor
is set by keeping the MCU awake, and the MCU stays awake to parse the ~1 Hz
VE.Direct stream.

**D10 is retired as a power question** — 1.1 mA is 0.026 Ah/day. If the range test
shows any benefit from Rx-boosted gain, **take it**.

The StamPLC platform does not change this analysis. What it adds to the node's floor
is the LCD backlight when on (off by default, §5.6), the I²C expanders, and the
INA226/LM75/RTC — all small, all inside the vendor's 47.84 mA working figure. What
it removes is the USB-C adapter's conversion loss. The input polling loop is I²C
traffic on an already-awake MCU, and **v0.8's move from 10–20 ms to 100 ms makes it
an order of magnitude cheaper again**: no measurable power implication either way.

---

## 9. Debug and bench tooling

### 9.1 What changed in v0.6

The BusT4 sniffing programme is gone from the critical path along with BusT4
itself. No pin-identification gate, no laptop capture phase, no read-only tap
procedure, no raw frame streaming. Those are preserved in Appendix B for anyone
pursuing Phase 2.

What replaces it is much simpler: **the 1050 interface can be exercised with a
DVM, a continuity tester, and the board's own front panel.** Every signal is a dry
contact or a documented terminal.

### 9.2 On-node debug tooling

- **Relay dry-run mode — new.** A `switch` that makes GateLink log and publish
  every relay pulse it *would* have issued without energizing anything. This is the
  single most valuable new debug feature: command logic can be exercised end to end
  — HA → LoRa → bridge → node → decision — **without moving a large motorized
  gate.** Default off; enabling it also lights the display (§5.6).
- **Input injection.** Synthetic assertions on IN1–IN6 in configurable order and
  spacing, to test §5.4 direction classification, §4.2.4 state derivation, and the
  §5.4.5 held-open alert without driving a car back and forth. **Must cover the long
  cases** — 30 s EXIT→SAFETY gaps and partial traversals.
- **Packet loopback:** (a) RF loopback — a node echoes received frames to validate
  link and framing without the peer; (b) internal loopback — feed TX frames back
  into the RX parser with no radio.
- **Dummy status push:** synthetic VE.Direct and gate-state data exercising the full
  pipeline (node → LoRa → bridge → MQTT → HA) without real hardware.
- **MQTT as bench harness:** `mosquitto_sub -t 'lran/#'` to watch every decoded
  payload live; `mosquitto_pub` to inject commands or fake status, decoupled from HA
  and the RF link.
- **Device simulators:** a VE.Direct frame generator covering **both text and HEX**;
  a dummy BMS BLE peripheral.
- **Multi-node simulator:** a `lran-simnode` firmware target that registers as node
  `0x02`, answers polls, and emits events on demand — validates addressing, per-node
  keying, availability watchdog, fragmentation, and CAD/backoff (§6.7) before
  WellLink exists.
- **On-demand display:** §5.6.
- **Configuration round-trip:** set every §5.8 parameter from HA, confirm the ACK,
  power-cycle, and confirm the override survived — then repeat **with the microSD
  removed** and confirm the change still applies and is honestly reported as
  `applied_not_persisted` (§5.8.3).
- **On-node logging to microSD**, leveled and rotating, so a fault occurring while
  the LoRa link is down is still recoverable afterwards.
- **Leveled serial logging** on all nodes.

### 9.3 1050 interface bring-up procedure

Ordered, and safe to perform incrementally.

1. **Rewire the keyswitch and pushbutton** (§4.2.2.1). Remove the series link;
   keyswitch to AUX1, pushbutton to AUX2. Do this *before* reprogramming the AUX
   terminals, so there is no window in which an unsecured pushbutton can assert
   OPEN+LOCK.
2. **Reprogram the 1050.** AUX1 → OPEN and LOCK; AUX2 → UNLOCK; OUT1 → OPEN;
   OUT2 → MOVING. Record all settings, including the 60 s auto-close and standby
   timeouts, in `/docs/1050-config.md`.
3. **Verify by hand.** With no GateLink connected, operate each control and confirm
   the expected behaviour. Meter OUT1/OUT2 through a full open/close cycle and
   confirm the §4.2.4 state table — in particular that **MOVING stays asserted
   through the auto-close countdown**, and whether it dips at the open limit before
   the countdown starts. Confirm the handheld remote's OPEN+LOCK produces the same
   1/0 reading LRAN's own command will.
4. **Characterise the 12 V lamp output** against the countdown while the meter is
   out, so §4.2.5's backup option is either usable or ruled out.
5. **Measure IN5 and IN6 levels** to establish idle state and sense polarity.
6. **Re-measure 1050 branch current** properly, in all four gate states (§8.2.2).
   This is the one item here that can change the power budget.
7. **Wire inputs only.** Connect IN1–IN6 with GateLink's relay outputs left
   physically disconnected. Validate state derivation, hold detection, detection and
   direction against real gate cycles driven by the keypad and the remote. This is a
   **read-only** phase and cannot move the gate.
8. **Wire relays with dry-run enabled** (§9.2). Exercise every command path from HA
   and confirm the logged intent matches expectations.
9. **Disable dry-run.** Test each command with a clear line of sight to the gate,
   and confirm both manual UNLOCK paths before the first real hold-open.

*The Diablo's move to unswitched power (§4.6) is already done and needs only a
confirmation that it still detects.*

---

## 10. Dev environment, repo, and build

```
/firmware/bridge/        # LoRaBridge PlatformIO project
/firmware/gatelink/      # GateLink PlatformIO project
/firmware/welllink/      # WellLink (future)
/firmware/simnode/       # simulated node for multi-node bench testing (§9.2)
/lib/lran-platform/      # host HAL: relays, inputs, display, buttons, INA226,
                         #   LM75, RTC, SD. Shared with AquaLink (§3.5)
/lib/lran-protocol/      # shared framing/addressing/HMAC/CRC/fragmentation
    /schemas/            # versioned per-node payload schemas (§6.4.3)
/lib/lran-config/        # runtime config: parameter table, SD persistence,
                         #   CONFIG frame handling (§5.8)
/lib/vedirect/           # VE.Direct text + HEX (osh-labs port) [MIT]
/lib/bms-ble/            # TDT BLE BMS client (§5.7.4)
/tools/                  # bench scripts, simulators, MQTT helpers; includes
                         #   bms_probe.py and instrument.py from the BLE PoC
/ha/                     # example discovery payloads + automations
/hardware/carrier/       # GateLink carrier: perfboard layout, module list, BOM (§4.7.4)
/docs/                   # this PRD, design notes, 1050-config.md,
                         #   bms-protocol.md, mppt-config.md, gatelink-config.md,
                         #   protocol-changelog.md, appendix-b-bust4/
LICENSE                  # MIT (§12.2) — holder name pending D31
THIRD_PARTY_NOTICES.md   # see §12
```

- **PlatformIO** multi-environment build (one env per firmware target), VS Code +
  Claude Code.
- **CI:** GitHub Actions building **all** firmware targets on push. With several
  targets sharing `/lib/lran-protocol/`, CI catches a protocol change breaking a
  node nobody rebuilt locally.
- **Secrets:** LoRa `master_key` (§6.5.1), WiFi creds, MQTT creds, OTA password via
  untracked config / build flags (never committed).
- **One source of truth for configuration.** `/lib/lran-config/` declares every
  parameter once — name, type, unit, range, default — and the firmware defaults, the
  HA `number` discovery payloads and `/docs/gatelink-config.md` are all generated
  from that table. Three hand-maintained copies would drift (§5.8).
- **Updates:** bridge OTA + USB (§6.2); remote nodes USB only.
- **Protocol changes** require a version bump and a `/docs/protocol-changelog.md`
  entry (§6.4.5).

---

## 11. Safety

- The **1050 remains the safety authority** — obstruction, photocells, and detector
  logic stay with the operator. LRAN issues commands through documented accessory
  inputs and *observes* detector state; it must never be relied on to prevent unsafe
  motion. §5.4 is a monitoring and notification feature, not a safety feature.
- **No safety interlock is bypassed.** FIRE was explicitly rejected as a command
  path because it clears hard shutdown — a latched entrapment state that must
  require human intervention (§5.3.2). GateLink *senses* FIRE (IN5) and *reports*
  hard shutdown (IN6); it never asserts either. Any FIRE assertion raises an
  immediate, independent alert (§5.4.6).
- **No maintained assertions.** Every GateLink output is a momentary pulse on a
  normally-open contact (§4.2.1). An unpowered, crashed, or removed GateLink asserts
  nothing and the gate behaves exactly as it does today.
- **The privileged control gets the key.** The panel keyswitch drives OPEN+LOCK and
  the unsecured pushbutton drives UNLOCK (§4.2.2.1). The reverse — which is what the
  present series wiring becomes once AUX1 is reprogrammed — would let anyone who
  reaches the controller box hold the gate open indefinitely.
- **Two manual UNLOCK paths are required and provided** (§4.2.6): the programmed
  handheld remote and the panel pushbutton. Plain STEP does not override a lock.
- **GateLink does not unlock the gate on boot.** v0.7 pulsed UNLOCK at startup to
  clear an unknown lock state; v0.8 reads the state instead (§5.3.4). Closing a gate
  that a person deliberately held open — after a power blip, with nobody watching —
  is the worse failure.
- **The §5.4.5 held-open alert notifies; it does not close the gate.** No automatic
  close behavior is initiated by LRAN beyond explicit HA commands.
- **Configuration changes are authenticated** (§5.8.2). A timing parameter is a
  command in every sense that matters: `relay_pulse_ms` and the detection windows
  change how the gate is driven and when alerts fire.
- **Fuse the battery tap.** A 100 Ah LiFePO4 delivers far higher short-circuit
  current than the FLA it replaces. Size and place the fuse for the new pack.
- **MPPT configuration is a remotely writable, battery-affecting path.** The three
  gates in §6.6.3 are safety requirements, not conveniences. Reconfigure for LiFePO4
  before first charge (§8.1.2).
- **Do not rely on graceful shutdown.** The pack BMS opens under fault and takes
  terminal voltage to zero (§4.5.1). Nothing critical may depend on an orderly
  power-down. Benign for the 1050 interface — relays simply drop out, and true gate
  state is re-read from IN1/IN2 on the way back up.
- **Measure IN5 and IN6 before connecting them** (§4.2.4). Both are voltage-sense,
  not dry contact, and the alarm output supplies fused 12 V. StamPLC's inputs are
  rated 5–36 V so there is no damage risk, but the measurement is still required to
  get the sense polarity right.
- **The host is operated outside its temperature rating** (§4.7.6, **D29**). This is
  a deliberate, instrumented exceedance, not an oversight. If seasonal logging shows
  sustained excursions, mitigate — vents, shade, fan — or revisit the platform.
  Do not rationalise.
- **The BusT4 port is not connected in v1.** Its VCC pin carries 24 V. If Appendix
  B is ever pursued, that section's cautions apply in full.
- Grounding discipline: bring GateLink ground and the MPPT signal ground to battery
  negative at a **single common point**, and keep the 1050's motor-terminal wiring
  physically separated from signal grounds.

---

## 12. Third-party code and licenses

### 12.1 Inventory

| Component | Source | License |
|---|---|---|
| VE.Direct parser + HEX | `osh-labs/VE.Direct_mppt_arduino` | **MIT — confirmed** |
| LoRa radio driver | RadioLib | MIT |
| BLE stack | NimBLE-Arduino | Apache-2.0 |
| **BMS client** | **Own implementation of the TDT protocol** (§5.7.4), written against `aiobmsble` as a behavioural reference | **No third-party code vendored.** If any is later taken from `aiobmsble` / `BMS_BLE-HA`, verify its license first |
| Arduino-ESP32 core | Espressif | LGPL-2.1-or-later |
| ESP-IDF components | Espressif | Apache-2.0 |
| mbedTLS (HMAC, HKDF) | via ESP-IDF | Apache-2.0 |
| MQTT client | PubSubClient (§6.1) | MIT |
| JSON | ArduinoJson | MIT |
| Display | U8g2 (Heltec OLED targets only) | BSD-2-Clause |
| **Platform HAL** | `m5stack/M5StamPLC` + `M5Unified` | **MIT** — verify at the pinned commit |
| **LCD driver** | LovyanGFX (via M5Unified) | **FreeBSD/BSD-2-Clause** |
| SD / filesystem | via ESP-IDF / Arduino core | Apache-2.0 / LGPL-2.1-or-later |
| NVS / Preferences | via ESP-IDF | Apache-2.0 |
| *Nice BusT4 protocol logic* | *`pruwait` / `xdanik` / `makstech` lineage* | ***GPL-3.0 — not used in v1***. Appendix B only. |

### 12.2 Project license — **D11 RESOLVED: MIT**

v0.5 resolved D11 to GPL-3.0 because porting the Nice BusT4 lineage would have made
the firmware a derivative work of GPL-3.0 code. With BusT4 out of v1 that obligation
disappeared and v0.7 reopened the question. **v0.8 closes it: the project is
MIT-licensed.**

The remaining stack is MIT, Apache-2.0, BSD-2-Clause and LGPL-2.1-or-later (the last
dynamically satisfied by the Arduino core in the usual embedded way). None imposes
copyleft, so MIT is available, and it is the simplest thing that meets the project's
aims: maximum reuse, attribution only.

**Action:** add a `LICENSE` file at the repo root containing the MIT text with
`Copyright (c) 2026 <holder>`. **The holder's name is still to be chosen** —
personal name or a project/entity name — and is now the only thing standing between
this repo and a public push (**D31**).

**If Appendix B is ever pursued**, a BusT4 port linking the GPL-3.0 community
lineage would make *that binary* GPL-3.0 regardless of this repo's stated license.
Keep such a port in its own clearly-marked subtree so the copyleft scope is explicit
at that point rather than assumed now.

### 12.3 Repo obligations

- `LICENSE` at root — **MIT** (§12.2); the holder name is pending **D31**.
- `THIRD_PARTY_NOTICES.md` listing §12.1 with copyright lines. MIT and BSD
  components require attribution retention.
- Nice's own reference documents (1050 manual, TTPCI manual, DMBM integration
  protocol PDF) are Nice-copyrighted: **link them, do not vendor them.**
- Victron VE.Direct protocol documents: link, do not vendor.

---

## 13. Open decisions register

| # | Decision | Status / notes | Resolve by |
|---|---|---|---|
| D1 | LoRa PHY params (SF/BW/CR/TX power) | open — pick after range test at ~500 ft **on both bearings** (§6.8) | Phase 1 |
| D2 | RX duty-cycle period + preamble length | **retired for GateLink** (§8.6). Appendix A; reopens only if WellLink is battery powered | — |
| D3 | BusT4 detector/movement-cause coverage vs. GPIO | **RESOLVED — discrete inputs** (§4.2.4, §5.4.2) | done |
| D4 | Poll scheduler location | **resolved** — bridge firmware, per node, runtime-configurable via HA `number` | done |
| D5 | MQTT client library | **resolved** — `MqttTransport` abstraction, PubSubClient first, `MQTT_MAX_PACKET_SIZE` >= 1024 (§6.1) | done |
| D6 | Gate-node display trigger | **resolved** — button toggle + auto-on in any debug mode (§5.6) | done |
| D7 | BusT4 VCC handling | **superseded** — the BusT4 port is not connected in v1 (§4.3) | done |
| D8 | Entity modeling | **resolved** — `cover` primary + "Hold gate open" `switch`; position deferred (§7.2–7.3) | done |
| D9 | PV-aware profile thresholds | **retired** (§8.6) | — |
| D10 | Rx-boosted gain on/off | **retired as a power question** (§8.8). Take the +3 dB if the range test shows benefit | Phase 1 |
| **D11** | **Project license** | **RESOLVED — MIT** (§12.2). Copyright holder name outstanding as **D31** | done |
| D12 | VE.Direct isolation vs. level shifting | **resolved for isolation — none needed** (§4.4). Level-shifter choice tracked as D25 | done |
| D13 | BusT4 physical layer | **RESOLVED — differential** (§1.5). **Appendix B only** | done |
| D14 | Decode placement | **RESOLVED — moot.** GateLink reads discrete inputs and drives discrete relays (§3.3) | done |
| **D15** | **Battery SOC source** | **RESOLVED — BLE BMS.** Pack is a **TDT** unit; access sequence documented and an independent client validated over 32 consecutive polls (§5.7.4). SmartShunt demoted to a physical-layer contingency behind D28 | done |
| D16 | OTA policy | **resolved** — bridge yes, remote nodes no (§6.2) | done |
| D17 | Naming | **resolved** — LRAN umbrella; `lran/` MQTT root; GateLink / WellLink / LoRaBridge (§1.6) | done |
| D18 | Auto-close observability | **RESOLVED — and better than expected.** `OUT = Moving` covers the countdown, so auto-close state *is* observable and hold state falls out of it (§4.2.4) | done |
| D19 | WellLink power source | **open** — mains vs. battery/solar. Determines whether Appendix A duty-cycling is needed and whether battery telemetry is required in the WellLink schema (§1.7) | Before WellLink design |
| D20 | 1050 standby policy | **RESOLVED — standby retained**, timeout measured at **60 s** (§1.5.5). Command relays wake the board on their own (§4.2.3) | done |
| D21 | Wake mechanism | **RESOLVED — none needed** (§4.2.3) | done |
| D22 | Hold-open mechanism | **RESOLVED — OPEN+LOCK / UNLOCK** (§5.3.2), confirmed on the bench (§1.5.2) | done |
| **D23** | **OUT1/OUT2 sense polarity** | **RESOLVED — no inversion.** Relays hold state because an energized relay prevents standby (§1.5.1). Use NO contacts, OUT1 = OPEN, OUT2 = MOVING (§4.2.7) | done |
| **D24** | **Manual UNLOCK path** | **RESOLVED — two paths**: the programmed handheld remote, and the panel pushbutton rewired to AUX2 (§4.2.2.1, §4.2.6) | done |
| **D25** | **VE.Direct TX translator** | **open** — BSS138 retained by default but may fail against a weak symmetric 5 V driver. Settled by one measurement (§4.4); fallback ADuM1201 or 74LVC1G17. BSS138 stays on the RX direction either way | Before carrier build |
| **D26** | **StamPLC 3.3 V rail** | **RESOLVED — no 3.3 V rail is exposed** (§4.7.3). The carrier LDO stays in the BOM | done |
| **D27** | **Carrier board fabrication** | **RESOLVED — perfboard with prefab modules** (§4.7.4); regulator and discretes mounted directly. Remaining: pick a DIN-rail carrier/enclosure and cut the board to it (§13.1) | done |
| **D28** | **BLE link margin from StamPLC position** | **open** — the Stamp-S3A 2.4 GHz antenna is internal with no external option (§4.7.5), and the pack's transmitter is weak. Measure RSSI from the intended mounting position. Fallbacks: SmartShunt, or the D30 co-processor | Phase 5 |
| **D29** | **Enclosure thermal envelope** | **open, narrowed to the high end** (§4.7.6). Cold exposure affects no functional dependency; summer solar gain does. Instrument LM75 + MPPT + BMS, verify the existing screened vents, shade, and fit a thermostatic fan only if logged maxima justify it | Phase 9 / ongoing |
| **D30** | **LoRa/BLE co-processor** | **RESOLVED — not adopted** (§4.7.7). Direct SX1262 on the carrier remains the plan of record; the Heltec-class co-processor is retained as a documented fallback with three explicit triggers | done |
| **D31** | **Copyright holder name** | **open** — MIT text and the 2026 year are settled; the name on the copyright line is not (§12.2). Blocks the first public push, nothing else | Before first public push |

### 13.1 Measurement backlog

| Item | Section | Blocks |
|---|---|---|
| **1050 branch current in all four gate states, by inline meter/shunt or low-range clamp** | §1.5.4, §8.2.2 | **The top item.** Power-budget credibility |
| **MOVING behaviour at the open limit and through the auto-close countdown** | §4.2.4 | Hold detection, `hold_confirm_ms` |
| **Whether the 12 V lamp output also tracks the countdown** | §1.5.3, §4.2.5 | Viability of the OUT2 backup |
| **IN5 (FIRE) and IN6 (alarm) idle/asserted voltages** | §4.2.4 | Sense polarity and idle state |
| **BMS pack-current sign convention, captured under charge and under load** | §5.7.4 | Last open item in the BMS protocol |
| **Copyright holder name for the LICENSE file** | §12.2 | **D31**, first public push |
| **BLE RSSI to the BMS from the StamPLC mounting position** | §4.7.5 | **D28** |
| **MPPT VE.Direct TX low-excursion under 10 kohm** | §4.4 | **D25**, carrier BOM |
| **DIN-rail carrier selection, then cut the perfboard to it** | §4.7.4 | Carrier build |
| Which loop input the Diablo occupies | §4.2.5 | Spare-capacity record |
| Whether EDGE (28) is in use | §4.2.5 | Spare-capacity record |
| Exit wand hold/de-assert behavior | §5.4.2 | Debounce, re-trigger lockout |
| Real EXIT to SAFETY gap times (drive the vehicle) | §5.4.4 | `detect_sequence_window_ms` default |
| StamPLC IO schematic — netlist-level 3.3 V check | §4.7.3 | Confirms **D26**; cannot change it |
| Node current via onboard INA226 | §4.5, §8.2 | Budget confidence — self-measuring |
| Enclosure temperature, seasonal (LM75 + MPPT + BMS) | §4.7.6 | **D29** |
| Range/RSSI on both bearings | §6.8 | D1, bridge antenna siting |
| One week MPPT yield + Vmin baseline | §8.5 | Install go/no-go |

---

## 14. Test plan / bring-up phases

0. **Carrier board bring-up.** Build the carrier (LDO + SX1262 module + VE.Direct
   front end) on perfboard per **D27**, verify the 3.3 V rail under SX1262 TX load,
   and prove RadioLib talks to the radio on the §4.7.2 pin map — including the TCXO
   voltage and DIO2 RF-switch configuration. Confirm the chosen module needs no
   TXEN/RXEN. *Do this before phase 1* — until it passes, GateLink has no radio, and
   failure here is trigger 1 for **D30**.
1. **RF link only** — StamPLC+carrier and a Heltec bridge; ping + loopback; RSSI/SNR
   at ~500 ft **on both the gate bearing and the well bearing**. Resolve D1, D10.
   Site the bridge antenna. **Two Heltecs remain the faster way to characterise the
   link alone** if the carrier is not ready — the PHY result is host-independent.
2. **1050 rewiring, configuration and manual validation** — §9.3 steps 1–6. Split
   the keyswitch and pushbutton, reprogram AUX and OUT terminals, verify by hand
   with a DVM, **characterise MOVING through the auto-close countdown**, measure
   IN5/IN6 levels, and **re-measure the 1050 branch current properly** (§8.2.2).
   **No LRAN hardware required.** Runs in parallel with phase 1.
3. **Protocol/framing** — input injection to validate §5.4 direction logic, §4.2.4
   hold derivation and §5.4.5 held-open alerting, including 30 s gaps and partial
   traversals. **Run `simnode` alongside GateLink** to validate addressing, per-node
   keys, availability watchdog, fragmentation, and CAD/backoff (§6.7). Exercise the
   §5.8 configuration round-trip, with and without a microSD fitted.
4. **VE.Direct** — **resolve D25 by measurement first** (§4.4), then real MPPT 75/15
   on the bench through the selected translator; verify full text field parsing
   **and HEX request/response round-trip** — the round-trip is the proof that the
   MPPT accepts our RX drive level, which is undocumented. Include write rejection
   when disarmed or unauthenticated (§6.6.3). Start the §8.5 one-week baseline log.
5. **Battery and BMS** — port and bring up the TDT client (§5.7.4) against the
   captured reference frames, then the live pack; MPPT reconfiguration for LiFePO4
   (§8.1.2) and readback verification; low-temp inhibition detection (§8.4); capture
   pack current **under charge and under load** to settle the sign convention.
   **Measure BLE RSSI from the intended StamPLC mounting position (D28)** before
   declaring the BMS path viable.
6. **1050 inputs live** — §9.3 step 7. Wire IN1–IN6 to the opto-isolated input
   terminals, relays still disconnected. Read-only; cannot move the gate. Confirm
   the §4.2.4 state table against real cycles driven by the keypad **and by the
   handheld remote's OPEN+LOCK**, and that `hold_confirm_ms` rejects the transient
   1/1 at the start of a close.
7. **1050 relays live** — §9.3 steps 8–9. Dry-run first, then real pulses with a
   clear line of sight. Confirm both manual UNLOCK paths (**D24**) work before the
   first real hold-open.
8. **HA integration** — MQTT Discovery entities per device; command round-trip;
   per-node availability; detection/direction entities; configuration `number`
   entities; **verify the held-open and fire events fire exactly once and do not
   replay on HA restart** (§7.4).
9. **Field** — install, range/power soak, error-path validation, confirm measured
   daily Ah against §8.2 **using the onboard INA226**, begin §8.5 ongoing monitoring,
   and **start the §4.7.6 seasonal enclosure-temperature log (D29)** across all
   three sensors.

---

## Appendix A — Low-power RX duty-cycling (retained design, not used by GateLink)

Removed from GateLink in v0.5, preserved because **WellLink may be battery
powered** (D19) without a 100 Ah pack behind it.

**PV-aware adaptive profile.** A node reads its charge controller and uses PV
output to select a power profile, with **hysteresis** to avoid dawn/dusk flapping:

| Profile | Trigger | MCU | Peripherals | LoRa RX | Poll |
|---|---|---|---|---|---|
| Daytime | PV charging | awake | full rate | near-continuous | normal |
| Night/low-PV | PV fallen off | light-sleep | occasional | SX1262 hardware RX duty-cycle | reduced |

**Keeping duty-cycled RX responsive.** The **bridge sends commands with an extended
preamble** long enough to span the node radio's sleep window, so the sleeping
receiver detects the preamble on its next wake. Worst-case latency ≈ one duty-cycle
period, independent of sleep depth. Reference target: ≤ 2 s with a ~2 s RX
duty-cycle period.

**Multi-node caveat.** An extended preamble is address-agnostic — **every**
duty-cycled node on the channel wakes and receives the header before discarding a
frame not addressed to it. Enable **SX126x hardware node-address filtering** (§6.8)
so the discard happens in silicon.

**Do not adopt this design for a node that does not need it.** Measured against
adequate storage it buys single-digit percentages of the budget at real cost in
complexity, latency, and overnight data continuity.

---

## Appendix B — BusT4 (optional Phase 2)

Not required for any v1 goal. Preserved because the bench work is done, the
findings are solid, and the hardware is on hand.

### B.1 What it would add

Everything in §2.1 is already met without it. BusT4 would additionally provide:

- **1050 configuration read/write** — auto-close time, force, speed, standby
  timeout, and the rest, from HA instead of the front panel.
- **Specific error and diagnostic codes**, where v1 sees only the hard-shutdown
  latch (IN6).
- **Cycle counters and service interval.**
- **Encoder position**, on controllers that expose it — the only route to the
  position reporting deferred in §7.3.
- **Movement cause as reported by the board**, rather than inferred (§5.5).

### B.2 What it costs

- A GPL-3.0 dependency, with the licensing consequence in §12.2.
- A differential transceiver wired to a connector whose adjacent pin carries 24 V.
- A custom UART implementation: 19200 8N1 with a **519–590 µs break** preceding each
  burst, which a stock `HardwareSerial` configuration will not produce.
- A characterization phase — laptop, logic analyzer, read-only tap — before any
  frame is transmitted.
- **It is unavailable during standby.** Any BusT4 feature must either tolerate that
  or be preceded by a wake pulse on a command input (§4.2.3).

### B.3 Bench findings, preserved

Measured pinout, resistances, and the differential determination are in §1.5.
Summary: **6P4C jack, position 2 = 24 V, positions 3 and 4 = differential data pair
(~145–174 Ω between them, open to ground), position 5 = GND. Use the SN65HVD230
with its onboard 120 Ω terminator lifted**, since the 1050 end already presents a
defined differential impedance and doubling the load buys nothing at 19200 baud
over inches of cable.

Do **not** substitute the SN65HVD231 — it disables the receiver in standby. Confirm
the '230 has no TXD dominant timeout before relying on sustained dominant for the
break; the feature belongs to the later SN65HVD25x parts, but verify against the
datasheet timing table.

### B.4 Community sources

`pruwait/Nice_BusT4` (original), `xdanik/Nice_BusT4` (English), `makstech/esphome-BusT4`
(ESP-IDF, most complete), `karol27/Nice_BusT4_WT32-ETH01`, `bpietroiu/esphome-nice-bidiwifi`
(clean-room rewrite), `gashtaan/nice-bidiwifi-firmware` (BiDi-WiFi schematics).
All GPL-3.0 except the last two — verify each before use.

Known command frames and the `INF_IO` request used for limit-switch confirmation
are documented in those repos. Note that `makstech` exposes `set_standby(bool)` and
`set_auto_close(bool)` as SET commands, which is the natural Phase 2 entry point:
1050 configuration from HA.

### B.5 If pursued — v0.7 note on the physical layer

**The StamPLC may make Appendix B cheaper.** BusT4 is UART framing on a differential
pair, and the ESP32-S3 GPIO matrix permits routing a UART to **G42/G43**, which are
wired to StamPLC's onboard **SIT1044 CAN transceiver** on the PWR-CAN port. If that
works, the SN65HVD230 is not needed and PORT.C stays free.

Two things must be verified before relying on it, neither of which is worth doing
unless Phase 2 is actually pursued:

1. **Termination.** Whether M5 fitted a 120 Ω terminator on the PWR-CAN pair. The
   §1.5 measurement of 145 Ω across the 1050's data pins indicates that side is
   already terminated; a second terminator on a point-to-point link is undesirable.
   D13's note about lifting the SN65HVD230's terminator applies equally here.
2. **XT30 pinout.** The PWR-CAN connector's power pins are tied **directly to VIN**.
   Per §11 and D7, the 1050's Oview VCC pin carries 24 V and is not connected —
   under no circumstances may these meet.

### B.5.1 If pursued

Read §4.2 of PRD v0.5 for the full bring-up procedure, branch analysis, and safety
gating. That document remains the authoritative reference for the BusT4 physical
layer and should be preserved in `/docs/appendix-b-bust4/`.

---

## 15. Changelog

- **v0.8** — **A measurement and closure revision.** A second round of bench work on
  the installed 1050 established what its programmable OUT relays actually do, and
  one observation carries the revision: **`OUT = Moving` stays energized while the
  gate is open and counting down to auto-close**, so `OPEN` asserted with `MOVING`
  clear means nothing is counting down, which means the gate is **held** (§4.2.4).
  Hold state is therefore **observed rather than remembered** (§5.3.4) — a hold set
  from the programmed handheld remote or the keyswitch is now visible, which closes
  the review's "infer a manual open+lock" item with a measurement, extends §5.4.5's
  alert to manual holds, and lets **§5.5 distinguish a manual hold from a manual
  momentary open**. Consequently **the v0.7 boot-time UNLOCK pulse is withdrawn**:
  GateLink reads the gate's real state at boot instead of blind-releasing a hold a
  person may have set (§5.3.4, §11). Also measured: **any energized OUT relay
  prevents standby**, resolving **D23** with no sense inversion and making OUT
  programming a power decision — `OUT = Closed` would disable standby permanently,
  while the chosen OPEN/MOVING pair leaves the resting state free to sleep (§4.2.7).
  First **current-clamp figures on the 1050 branch are recorded but not adopted**
  (§1.5.4): 0.23–0.33 A with standby reading above active, which fails an internal
  sanity check and is more consistent with instrument error below 1% of full scale
  than with the board — **a proper re-measurement is now the top item in §13.1**
  (§8.2.2). **New §5.8: every timing interval, window and threshold is
  runtime-configurable from HA** over an authenticated `CONFIG`/`CONFIG_ACK` frame
  pair, with **defaults in flash and overrides on microSD**; with no card the change
  applies to RAM and is honestly ACKed as `applied_not_persisted`. microSD also
  takes on-node logging and any retained state, keeping repeated writes off the
  ESP32's flash (§4.5.1). **`relay_pulse_ms` raised 300 → 500**, since the 1050's
  input debounce is undocumented and nothing is sensitive to a longer closure.
  **Input polling relaxed 10–20 ms → 100 ms** with debounce expressed in samples,
  freeing the I/O task from the v0.7 scheduling tightrope (§4.2.4, §5.1). The
  **keyswitch and pushbutton are split** out of their series wiring — keyswitch to
  AUX1 (OPEN+LOCK), pushbutton to AUX2 (UNLOCK) — so the privileged operation
  requires the key and the unsecured button can only let a held gate close
  (§4.2.2.1); the pushbutton and the programmed remote together **resolve D24**.
  **§5.7 is rewritten as resolved: D15 closes** — the pack is a **TDT** BMS, the
  access sequence (HiLink handshake to `FFFA`, ack read-back, subscribe `FFF1`,
  requests to `FFF2` with head `0x1E`) is documented, and an independent client
  validated 32 polls with zero CRC failures; the SmartShunt drops from SOC
  dependency to a physical-layer contingency. **FIRE is treated as an emergency**,
  raising its own immediate event rather than being merely another hold source
  (§5.4.6). Corrections: safety loops are in **series**, not parallel; the exit wand
  is **≥20 ft** from the inside loop; the **Diablo is already moved to unswitched
  power** (§4.6 done); auto-close and standby timeouts are both **60 s**; OLED
  references corrected to LCD. **D11 resolved to MIT** (holder name outstanding as
  **D31**); **D26** resolved (no 3.3 V rail); **D27** resolved (perfboard with
  prefab modules, DIN-mounted); **D29** narrowed to a high-temperature concern with
  vents, shade and an optional thermostatic fan as the mitigation ladder. Added
  **D30** — a Heltec-class **LoRa/BLE co-processor was considered and not adopted**
  (two MCUs, two flash procedures, split debugging, for compute that is not scarce),
  retained as a documented fallback triggered by carrier-bring-up trouble, a failed
  **D28** BLE margin, or a broken pin budget (§4.7.7). BOM: **the Waveshare Core1262
  is excluded** — it needs TXEN/RXEN lines the pin budget cannot supply, and v0.7
  named it in error; a module-selection checklist replaces the part number (§4.7.2),
  with a WIN-SX1262-class module the current candidate. Added a microSD card.
- **v0.7** — **Moved the GateLink host from the Heltec LoRa V3 to the M5Stack
  StamPLC** (§4.7). v0.6 defined the I/O requirement — four momentary relays and six
  inputs, two of them 12 V voltage-sense — and met it with an external relay module,
  per-channel dividers and clamps, and a 12 V→USB-C adapter bolted to a Heltec.
  StamPLC provides all of it natively behind screw terminals on a DIN rail, so
  **the relay module, the dividers and clamps, and the power adapter all leave the
  BOM** (§4.1.2), along with every hand-built discrete circuit in the design. The
  cost is the radio: **LoRa becomes an external SX1262 on a carrier board** (§4.7.4),
  which must also carry a **3.3 V LDO because StamPLC exposes no 3.3 V rail**
  (§4.7.3, **D26**) and whose EXT_5V sits at ~4.76 V — excluding an AMS1117.
  §4.7.2 sets out the pin budget: SPI plus G40/G41 for BUSY and DIO1, with **G14
  freed for the radio reset by moving input acquisition to a 10–20 ms poll**
  (§4.2.4), and **G3 explicitly reserved** because it resets the LCD and the
  expanders together. **The BLE BMS work ports unchanged** — Stamp-S3A and Heltec V3
  are both ESP32-S3FN8, so §5.7 is a recompile (§4.7.5) — but the 2.4 GHz antenna is
  internal with no external option, giving **D28**. **Power is a wash**: ~7.0 →
  ~6.9 Ah/day, since the vendor's 47.84 mA working figure lands where the Heltec
  allocation was and the adapter's conversion overhead disappears; the **onboard
  INA226 now measures the node's own draw**, closing several `TBM` rows by
  construction (§4.5, §8.2). The display gets simpler: **three dedicated buttons
  instead of one strapping pin, and backlight switching instead of Vext power
  cycling**, retiring the SSD1306 re-init failure mode (§5.6). Added **§3.5**, the
  platform convergence with **AquaLink**, requiring a `/lib/lran-platform/` HAL and
  an **injected radio pin map** so one SX1262 driver serves both the StamPLC carrier
  and the Heltec bridge. **LoRaBridge stays on the Heltec V3.** Reopened the
  VE.Direct translator question as **D25**: Victron specifies the MPPT TX port as a
  5 V logic signal driving at most 22 kΩ, implying a ~10 kΩ source impedance that a
  BSS138's 10 kΩ pull-up may not overcome — one measurement decides between keeping
  the BSS138 and falling back to an ADuM1201 or 74LVC1G17, with the BSS138 retained
  for the RX direction regardless (§4.4). Added **D27** (carrier fabrication) and
  **D29** (**operating temperature — the principal risk of this revision**, since
  StamPLC is rated 0–40 °C and the enclosure is outdoors; instrumented via the
  onboard LM75). Added a **phase 0 carrier bring-up** to the test plan. Noted in
  **Appendix B** that StamPLC's onboard PWR-CAN transceiver may serve as the BusT4
  differential driver, potentially retiring the SN65HVD230 from Phase 2 as well.
  **Nothing above §4 changed**: protocol, command model, detection logic, HA entity
  model, keying and safety posture all carry forward from v0.6 intact.
- **v0.6** — **Rebuilt the 1050 interface on documented accessory I/O and moved
  BusT4 to an optional Phase 2 (Appendix B).** Bench probing established that BusT4
  is dead during 1050 standby, that the loop detector is unpowered during standby,
  and that a wake mechanism would be needed regardless — while also establishing
  that every v1 goal is reachable through terminals Nice documented for third-party
  use. GateLink now drives **four momentary relays** (AUX1 = OPEN and LOCK,
  AUX2 = UNLOCK, Guard Station Open, Guard Station Close) and reads **six GPIO
  inputs** (OUT1 = OPEN, OUT2 = MOVING, Diablo contact, exit wand contact, FIRE
  sense, alarm sense) — §4.2. **The wake problem dissolves** (**D21**): every
  command relay drives a command-class input, which wakes the board, so **standby is
  retained** (**D20**) and the ~2 Ah/day it saves is kept (§8.2.1). **Hold-open
  resolves to OPEN+LOCK / UNLOCK** (**D22**), replicating the mechanism already in
  use on this installation; FIRE was rejected because it clears hard shutdown and
  SHADOW because it would block a closed gate from opening (§5.3.2). **§5.4.5 is
  redefined on explicit hold state** rather than inferred "statically open",
  retiring **D18** and removing the 30 s arming delay — alerts now fire on the first
  edge with no dead window. **D3 resolves to GPIO**; **D14 becomes moot** since
  there is nothing left to decode; **D13 resolves to differential** and is preserved
  for Appendix B only; **D7 is superseded**. **D11 is REOPENED** — with `/lib/bust4/`
  gone the project has no copyleft dependency and the license is a free choice
  (§12.2). Added a **new requirement to move the Diablo DSP-7LP to unswitched
  power** (§4.6), without which §5.4.5 is unimplementable. Added **relay dry-run
  mode** and **input injection** as debug features (§9.2) and a staged 1050 bring-up
  procedure with a read-only phase (§9.3). Added **D23** (OUT relay sense polarity)
  and **D24** (manual UNLOCK path — now a safety requirement, since plain STEP does
  not override a lock). Added command **deduplication on `(boot_id, seq)`** because
  relay pulses are not idempotent (§6.5.4). BOM: relay module in, SN65HVD230 and the
  logic analyzer out of the critical path; BSS138 demoted to VE.Direct only.
  Removed: raw BusT4 frame streaming, the `RAW` frame type, the laptop sniffing
  phase, and the BusT4 pin-identification gate.
- **v0.5** — Renamed to LoRa Remote Automation Network (LRAN); MQTT root `gatelink/`
  → `lran/`; one HA device per node (**D17**). Expanded the bridge to a
  general-purpose multi-node LoRa↔MQTT gateway — per-node addressing, per-node HMAC
  keys via HKDF, per-node sequence tables, availability watchdog, explicit payload
  schema IDs, fragmentation, protocol version tolerance N/N−1, and CAD + randomized
  backoff media access. Scoped **WellLink** and added a `simnode` bench target.
  Corrected the vehicle detection topology to a **Diablo DSP-7LP** with a single
  combined safety contact plus a separate exit wand. Added the **VE.Direct HEX
  protocol** with HMAC + armed write-enable + audit trail. Replaced the FLA with a
  **100 Ah LiFePO4**; added LiFePO4 MPPT reconfiguration and low-temperature
  charge-inhibition detection. Dropped the night/low-PV profile (**D2, D9, D10**).
  Enabled **BLE** for the WattCycle BMS with a SmartShunt fallback (**D15**). Moved
  BusT4 sniffing off the node; added opt-in raw frame streaming with hybrid decode
  (**D14**). Enabled **OTA for the bridge only** (**D16**). Resolved **D11** to
  GPL-3.0.
- **v0.4** — Added physical installation context (single enclosure, no motor
  inside); resolved **D12** on that basis. Consolidated signal conditioning around a
  single BSS138 module with rise-time analysis. Restructured the BusT4 electrical
  section into Branch A (single-ended) and Branch B (differential); added **D13**.
- **v0.3** — Renamed to LoRa GateLink. **Corrected VE.Direct to 5 V.** Added BusT4
  electrical characteristics and a pin identification procedure. Promoted loop
  detection to a hard requirement with direction classification. Expanded the power
  budget. Added the license inventory and **D11**. Resolved **D5–D8**.
- **v0.2** — Added BusT4 sniff/monitor as a retained diagnostic (**D3**); PV-aware
  day/night power profile with extended-preamble wake; poll scheduler fixed to the
  bridge (**D4**); added **D9**, **D10**.
- **v0.1** — Initial draft. Two custom Heltec V3 nodes, BusT4 + VE.Direct on the
  gate node, LoRa 915 MHz link, bridge as LoRa↔MQTT gateway via MQTT Discovery.
