# LRAN GateLink Node Implementation Plan

**Document:** `LRAN-GateLink_Node-Implementation-Plan`
**Version:** 0.11
**Node:** `GateLink`, node ID `0x01`
**Firmware target:** `lran-gatelink`
**Status:** Ready for build. Four measurements outstanding before the carrier is populated.
**Requirements source:** [`LRAN-GateLink_Node-PRD`](./LRAN-GateLink_Node-PRD.md) v0.9
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.13**
**Decision status:** [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md)
**Last updated:** 2026-09-23

> **This document is the basis for hardware build and firmware development, and is what
> is handed to Claude Code for this node.** Requirement identifiers (`R-*`, `G-*`,
> `S-*`, `V-*`) refer to the GateLink PRD.

---

## Table of contents

1. [Overview](#1-overview)
2. [Bill of materials](#2-bill-of-materials)
3. [Hardware interconnect](#3-hardware-interconnect)
4. [Interfaces and protocols in detail](#4-interfaces-and-protocols-in-detail)
5. [Firmware architecture](#5-firmware-architecture)
6. [Implementation specifics](#6-implementation-specifics)
7. [Test and verification plan](#7-test-and-verification-plan)
8. [Milestones and acceptance criteria](#8-milestones-and-acceptance-criteria)
9. [Integration observations](#9-integration-observations)
10. [Changelog](#10-changelog)

---

## 1. Overview

### 1.1 What is being built

An M5Stack StamPLC running custom firmware, with a perfboard carrier board carrying an
SX1262 radio module, a 3.3 V regulator and a VE.Direct level-shifter front end. It
mounts on DIN rail inside the existing gate controller enclosure, powered directly from
the 12 V LiFePO4 pack, and connects to:

- the gate controller, via four relay outputs and six opto-isolated inputs on screw
  terminals;
- the MPPT 75/15, via a level-shifted VE.Direct UART;
- the battery BMS, over BLE;
- the Bridge Node, over 915 MHz LoRa.

### 1.2 Build sequence at a glance

```
  Carrier board        1050 rewiring +          Firmware, on the bench
  (M2 → M9)            reprogramming            with simulated peripherals
       |                     |                          |
       +---------+-----------+                          |
                 |                                      |
           Bench integration  <------------------------+
                 |
      Read-only input phase (cannot move the gate)
                 |
      Relay dry-run phase (logs intent, energizes nothing)
                 |
      Live phase (manual unlock paths confirmed first)
                 |
      Field soak (seasonal thermal + power log)
```

Carrier work, controller rewiring and firmware development are **independent** and run
in parallel. Everything from bench integration onward is sequential.

### 1.3 Non-obvious properties to preserve

Three things about this design are easy to break during implementation and expensive to
discover late:

1. **No hand-built discrete circuits.** Every subassembly is a prefabricated module on
   headers; only the regulator and a handful of passives mount directly. This was a
   primary driver for the platform choice and should not erode module by module.
2. **De-energized asserts nothing.** All relay contacts are normally-open. This holds
   through the pack-BMS-disconnect case, in which the node loses power entirely with no
   warning (**R-4.4c**).
3. **Relays and inputs consume no GPIO.** They sit behind I²C expanders. The radio pin
   budget (§3.3) depends on that, and it has no spare pins.

---

## 2. Bill of materials

### 2.1 To purchase

| Qty | Item | Notes |
|----:|------|-------|
| 1 | **M5Stack StamPLC (K141)** | Host platform. 4 relays, 8 opto-isolated inputs, 6–36 V in, DIN, screw terminals. ~$43 |
| 1 | **SX1262 module, 915 MHz, SPI** | Must satisfy the §2.2 checklist. **A WIN-SX1262-class module is the current candidate** |
| 1 | SMA bulkhead pigtail + 915 MHz antenna | LoRa antenna **outside** the enclosure |
| 1 | **3.3 V LDO, ≥300 mA, dropout ≤300 mV** | AP2112K-3.3 breakout or equivalent. **Required — D26.** **An AMS1117-3.3 will not do** (~1.1 V dropout against a 4.76 V source) |
| 1 | **Perfboard + 2×8 2.54 mm header, in a DIN-rail carrier** | Carries the radio module, the LDO and the VE.Direct front end. **D27.** Pick the DIN carrier first (**M15**) and cut the board to it |
| 1 | **BSS138 4-channel bidirectional level shifter module** | Adafruit #757 / SparkFun BOB-12009 or equivalent. **VE.Direct only** — 2 channels used, 2 spare. Retained, but the TX direction is contingent on **D25** |
| *0–1* | *ADuM1201 breakout **or** 74LVC1G17 buffer* | **Only if M4 shows a weak symmetric low-side driver** on the MPPT TX line (§4.2.2) |
| 1 | VE.Direct cable / JST-PH 2.0 4-pin pigtail | **Both data lines used.** Buying a genuine Victron cable and cutting it is the easiest sourcing path |
| 1 | **microSD card** — small, industrial-grade if available | Configuration overrides, retained counters and on-node logging. **Not required for the node to run** (**R-4.2d**) |
| — | Mounting, strain relief, **inline fuse on the battery tap** | Inside the existing enclosure. **S-10** |

### 2.2 SX1262 module selection checklist

The pin allocation in §3.3 has **no spare pins for RF-path control**, which rules out a
whole class of otherwise attractive modules. Before ordering, confirm against the
vendor's own schematic — **not a marketplace listing**:

1. **Uses DIO2 for RF switching and requires no TXEN/RXEN lines.** This **excludes the
   Waveshare Core1262** and most PA/LNA "long range" variants.
2. Runs from **3.3 V** and **states its TCXO voltage.** The radio library must be given
   the correct value or the radio will not calibrate.
3. Breaks out SCK, MOSI, MISO, NSS, BUSY, DIO1, NRESET, 3V3 and GND. Nothing else is
   needed, and anything else is a pin the budget does not have.
4. Is a **915 MHz** part with an SMA or IPEX antenna connection.

### 2.3 Existing installed hardware — not purchased

Nice/Apollo 1050 control board · Victron MPPT 75/15 · 50 W panel · **Diablo DSP-7LP**
loop detector with two safety loops **in series** · self-contained exit wand sensor ·
remote keypad · panel keyswitch + pushbutton · **a handheld remote programmed OPEN+LOCK
/ UNLOCK** · 2 × 2 W LED lights · 12 V 100 Ah LiFePO4 pack (WattCycle 100 Ah mini w/
Bluetooth).

**No separate enclosure is required** — GateLink mounts inside the existing controller
enclosure.

### 2.4 Bench and debug equipment

| Qty | Item | Notes |
|----:|------|-------|
| 1 | USB-TTL serial adapter (5 V / 3.3 V selectable) | VE.Direct bring-up and observation |
| 1 | DVM, and a **current clamp or inline mA meter** | **M1 needs the inline meter**; see §9.3 |
| *1* | *8-channel USB logic analyzer (sigrok/PulseView)* | Optional. Useful for VE.Direct HEX timing |
| — | Jumper leads, breadboard | |

### 2.5 Contingency parts — not in the base build

| Item | Trigger |
|---|---|
| *Victron SmartShunt 500 A* | **D28** fails: BLE margin from the final mounting position is inadequate. Also the cleanest source of true load current. **No longer needed for SOC** |
| *Heltec LoRa V3 as a co-processor* | **D30** trigger fires (§6.7) |
| *SN65HVD230 CAN transceiver* | Research-archive Phase 2 only. Already on hand |

> **BOM trajectory.** Earlier revisions carried a differential transceiver and a
> project-critical logic analyzer for the protocol bus; then an external 4-channel relay
> module, per-channel dividers with clamp diodes, and a 12 V→USB-C adapter bolted to a
> bare radio board. The platform change absorbed all three of those subassemblies into
> the host and spent the saving on a radio carrier. **Net part count is roughly flat,
> net assembly count is lower, and the number of hand-built discrete circuits is zero.**

---

## 3. Hardware interconnect

### 3.1 Gate controller — outputs

The host's four onboard relays (SPDT, COM/NO/NC, DC 5 A @ 28 V), driven through the
**AW9523B I²C expander**.

| Relay | Terminal | Program the controller as | Serves |
|---|---|---|---|
| **K1** | AUX1 (16) | **OPEN and LOCK** | Open and hold (G-2) |
| **K2** | AUX2 (18) | **UNLOCK** | Release hold (G-3) |
| **K3** | Guard Station Open (34) | *(fixed function)* | Momentary open (G-1) |
| **K4** | Guard Station Close (36) | *(fixed function)* | Immediate close (G-3) |

All contacts wired **normally-open**, so a de-energized or unpowered GateLink asserts
nothing.

**Three consequences of the relays being behind an I²C expander:**

- Pulse timing is generated in firmware over I²C rather than by a GPIO write. At a
  500 ms pulse width I²C latency is irrelevant, **but the platform HAL owns the timing
  and must not be interrupted mid-pulse** by a blocking BLE or VE.Direct call (§5.2).
- The relays are dry contacts on screw terminals, so the "present as a control panel"
  principle is unchanged, and the opto-isolation an external relay module would have
  provided is unnecessary — there is no shared logic rail to isolate.
- The de-energized-asserts-nothing property is preserved, including through the pack
  BMS-disconnect case.

**AUX reprogramming.** AUX1 currently reads STEP. Reprogramming AUX1 to OPEN+LOCK and
AUX2 to UNLOCK consumes no additional terminals.

#### 3.1.1 Keyswitch and pushbutton — rewiring required

The panel keyswitch and pushbutton are presently wired **in series** on AUX1: the
keyswitch must be unlocked before the pushbutton does anything. **That arrangement
cannot survive the AUX reprogramming**, because it would leave an unsecured pushbutton
able to assert OPEN+LOCK — a security defect at the controller box, where the
consequence is that anyone who reaches the panel can hold the gate open indefinitely.

**Resolution — remove the series link and split the two controls, the secured control
taking the privileged function:**

| Control | Wired to | Becomes |
|---|---|---|
| **Keyswitch** | AUX1 (16) = OPEN and LOCK | Manual "open and hold" — requires the key |
| **Pushbutton** | AUX2 (18) = UNLOCK | Manual release. Unsecured, and safely so: its only effect is to let a held gate close |

The asymmetry is deliberate. Opening and holding is the privileged operation; releasing
a hold is not, and making the release path the easy one is what **S-6** wants anyway.
The pushbutton is therefore also the **panel-side manual UNLOCK path of record**,
alongside the programmed handheld remote.

**Sequencing is a safety requirement (R-9.2c): rewire before reprogramming**, so there
is no window in which an unsecured pushbutton can assert OPEN+LOCK.

### 3.2 Gate controller — inputs

All six land on the host's **opto-isolated 5–36 V DC channels** (EL3H4, read via the
AW9523B). Eight are available; six are used.

| Input | Source | Type |
|---|---|---|
| **IN1** | OUT1 relay, program **OPEN** | Dry contact |
| **IN2** | OUT2 relay, program **MOVING** | Dry contact |
| **IN3** | Diablo DSP-7LP contact, paralleled | Dry contact |
| **IN4** | Exit wand contact, paralleled | Dry contact |
| **IN5** | FIRE terminal (32), sensed | **Voltage sense** |
| **IN6** | Alarm output, sensed | **Voltage sense** |

- **IN1–IN4 are dry contacts.** Wire in the high-level configuration: `EXCOM_COM` to the
  12 V supply negative, each `INPUT` fed from 12 V+ through the contact. No internal
  pull-ups, no GPIO-referenced grounds.
- **IN5 and IN6 connect directly** — the inputs are specified for 5–36 V, so no divider
  or clamp is needed. **Meter both before wiring** (**M3**, **S-13**) to establish sense
  polarity and idle state — not to size a divider.
- The controller's OUT1/OUT2 provide common, NO and NC contacts. **Use NO.** An earlier
  revision held open the possibility of inverting the sense if the relays dropped out in
  standby; they do not, because an energized OUT relay prevents standby (**D23**).

### 3.3 Radio carrier — pin budget

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
| SX1262 NRESET | **G14**, freed by the decision to poll inputs rather than use the expander interrupt |
| VE.Direct UART | PORT.A — G1, G2 |
| I²C (onboard peripherals) | G15 / G13, already in use |
| **Spare** | PORT.C (G4, G5) — reserved for a second VE.Direct pair if a SmartShunt is added |

**Reserved — do not use:**

| Signal | Reason |
|---|---|
| **G3** | Common RST for the LCD, the PI4IOE expander **and** any bus expansion module. A radio driver pulsing reset here also resets the display and an expander — **and therefore risks disturbing relay state** (**R-4.3g**). The vendor's own PoE module documentation flags this same hazard and recommends passing `-1` for the reset pin |
| **G0** | RS485_TX and the ESP32-S3 BOOT strap. Unused; if the RS485 port is ever used, keep the bus idle-high |
| G42 / G43 | PWR-CAN. Unused in v1 |

*Fallback if G14 is later needed:* pull SX1262 NRESET to 3.3 V and pass the
no-connect sentinel, accepting software-reset-only. **Not preferred for an outdoor
node.**

**The interrupt line is deliberately spent on the radio.** The AW9523B's INT line is
shared with the other onboard I²C peripherals; polling the input expander at 100 ms
(**R-3.1.4d**) frees G14 for the radio reset, and the detection logic has three orders
of magnitude of timing margin (§9.2).

### 3.4 Power rails

Confirmed across current vendor documentation, and **D26 closes on that basis: no 3.3 V
rail is exposed.**

- Bus power pins are **VIN, GND and EXT_5V only**.
- The PoE accessory documentation independently republishes the same bus map, naming
  pin 6 `EXT_5V`.
- Both HY2.0-4P ports are GND / 5 V / GPIO / GPIO.
- The vendor's own SPI expansion module carries a W5500 — a 3.3 V part — and
  **regulates its own 3.3 V locally.**

Rail capacity, vendor-specified under load: **expansion port 4.76 V @ 700 mA**,
HY2.0-4P **4.81 V @ 700 mA**. **Note the rail sits near 4.8 V, not 5.0 V** — which is
what excludes an AMS1117.

| Rail | Source | Serves |
|---|---|---|
| 12 V | Battery tap, inline fuse → VIN terminal block | Host |
| ~4.76 V | EXT_5V, bus pin 6 | Carrier LDO input; VE.Direct **HV** rail |
| 3.3 V | **Carrier LDO** | SX1262; VE.Direct **LV** rail |

**Load on the LDO.** This line read *"~120 mA peak SX1262 TX at +22 dBm"*, a power
**neither envelope permits**: Envelope A caps conducted power at **−4 dBm** with the fitted
3.0 dBi antenna, and Envelope B's ceiling is the Wio module's own tested **19.6 dBm**
(`LRAN-M21-FCC-Grant-Findings` §6). **Size the rail against 19.6 dBm**, the highest power
any permitted configuration reaches; the node operates at −4 dBm, in the PA's low-power
path, so the working draw sits well below that.

**700 mA of headroom is ample, by a wider margin than this section originally claimed.**
No rail, part or layout decision moves. **M0 accepts at the operating point only** — if
Envelope B is ever triggered, the rail is sized for it but has not been tested there.

> A netlist-level check against the vendor IO schematic (**M16**) is still worth doing
> when the carrier is laid out, but it can no longer change the design — only confirm
> it.

### 3.5 Carrier board layout

The board seats on the StamPLC-Bus header and carries:

1. **3.3 V LDO breakout** from EXT_5V
2. **SX1262 module** meeting the §2.2 checklist, plus an SMA bulkhead to the enclosure
   exterior
3. **BSS138 level-shifter module** for the VE.Direct front end, plus D25's outcome
4. Screw or JST landing for the VE.Direct cable

The regulator and any discretes mount directly to the perfboard; everything else arrives
as a module on headers.

**Mounting.** Source a **DIN-rail PCB carrier or DIN module enclosure** (**M15**) and cut
the perfboard to fit it, rather than free-mounting the assembly. The SMA bulkhead and
the VE.Direct cable both pull on the board, and an outdoor enclosure that sees a seasonal
thermal cycle is no place for something floating on its header.

**Layout.** Keep the SPI run short and away from the relay terminals — the relays are dry
contacts carrying only low-voltage accessory signalling, so coupling risk is low, but
layout should not invite it. Keep the antenna feed short and give the SX1262 module a
solid ground return to the header.

### 3.6 Spare capacity

Deliberately unspent, recorded so future revisions know what is available.

| Terminal / resource | Status | Possible use |
|---|---|---|
| Radio Open (39), Radio Close (40) | Free | Step-by-step if tied together; or two more command inputs |
| Guard Station Stop (35) | Free (jumpered to GND) | Stop, **with caution below** |
| **12 V moving-lamp output** | **Free** | Backup source of a motion signal if OUT2 is ever needed elsewhere. Voltage-sense, which the inputs take directly. **Not a drop-in for OUT2 unless M8 confirms it also tracks the auto-close countdown** |
| SHADOW / LOOP1 (24), ENTRAPMENT / LOOP2 (26) | Likely free — **M11** | Additional detection inputs |
| EDGE (28) | In use or free — **M11** | — |
| Host inputs 7 and 8 | Free | Two spare opto-isolated channels |
| BSS138 channels 3–4 | Free | Second VE.Direct pair if a SmartShunt is added |
| PORT.C (G4, G5) | Free | Third hardware UART for the same |
| Controller protocol bus port | Untouched | Research-archive Phase 2 |

**Stop is not implemented.** Guard Station Stop is **normally-closed** and asserting it
disables all other inputs for the duration. If added later, wire the relay's **NC**
contacts in series with the existing STOP–GND jumper, so that a de-energized relay leaves
the circuit intact and every failure mode lands safe.

### 3.7 Failure modes and manual recovery

| Failure | Result | Recovery |
|---|---|---|
| GateLink loses power or crashes | All relays de-energize. No input asserted. **Gate behaves exactly as it does today** | None needed |
| GateLink crashes **while the gate is held OPEN+LOCKED** | Gate stays open and locked. **Plain STEP does not override a lock** | **Manual UNLOCK — two paths, below** |
| Welded relay contact | A single sustained closure on a command input: repeated open commands on an already-open gate, or repeated unlock on an unlocked gate. Undesirable but not hazardous | De-power or unplug the node |
| Controller in standby, command issued | Pulse wakes the board and executes | None needed |
| microSD absent or unreadable | Firmware defaults apply; runtime changes accepted, applied and reported as unpersisted | Fit a card |
| Pack BMS opens | Node drops dead with no warning; reboots when the BMS re-closes. Relays de-energize | None needed. BMS telemetry gives days of notice (§4.3) |

**Manual UNLOCK — two independent paths** (**D24**, **S-6**). Because LRAN will enter the
OPEN+LOCKED state far more often than a human does today, there must be a way to clear it
without GateLink:

1. **The programmed handheld remote**, which already carries UNLOCK on one button.
   Available from a vehicle, **which is where the problem is usually noticed.**
2. **The control-panel pushbutton**, rewired to AUX2 = UNLOCK (§3.1.1).

Record both in `/docs/1050-config.md`. The keyswitch is deliberately *not* part of the
unlock path — it moves to OPEN+LOCK, which is the operation that should require a key.

---

## 4. Interfaces and protocols in detail

### 4.1 LoRa

Framing, addressing, authentication, sequencing, fragmentation and media access are
defined in [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) and are not
restated. Implementation obligations for this node:

| Item | Value |
|---|---|
| Node ID | `0x01` |
| Schemas emitted | `0x10` status, `0x11` event, `0x12` config, `0xF0` health |
| Driver | RadioLib, SX1262 |
| Pin map / TCXO / RF switch | **Injected by configuration**, never compiled in (System PRD §3.5) |
| Media access | CAD before TX; `random(0, backoff_max_ms)` on busy, `cad_retries` attempts, then transmit regardless |
| Address filtering | **None — unavailable in LoRa mode** (spec §12.1, corrected v0.12). `dst` is checked in software at §14 stage 5 |

### 4.2 VE.Direct — electrical

> **Correction worth keeping visible:** earlier drafts stated VE.Direct is 3.3 V TTL
> requiring no level shifting. **This is incorrect. All Victron MPPTs are 5 V devices.**

**5 V TTL UART, 19200 baud**, on a **JST-PH 2.0 4-pin** connector. Pinout is
vendor-documented, with signal names given from the **device's** perspective:

| Pin | Signal (device POV) | Connection |
|---|---|---|
| 1 | GND | GateLink GND |
| 2 | RX (into MPPT) | BSS138 ch 1 → GateLink TX — **required** for HEX |
| 3 | TX (out of MPPT) | BSS138 ch 2 → GateLink RX — **required** |
| 4 | V+ | **Do not connect** |

**Both data lines are mandatory:** HEX is a request/response protocol and cannot function
without the MPPT RX line.

#### 4.2.1 Wiring cautions

- **Wire colours are actively misleading.** VE.Direct cables are crossover cables; red
  may be GND and black may be V+, and the two data conductors differ in meaning between
  the cable's ends. **Meter every conductor.**
- Community sources disagree about whether pin 4 is V+ or GND on some units. Moot since
  pin 4 is unconnected, but a further argument for metering first.
- **Do not attempt to power the front end from the VE.Direct V+ pin** — it is limited to
  roughly 10 mA average. Both Victron device power pins stay unconnected.

#### 4.2.2 D25 — the TX direction may need a different translator

Victron's own solar-charger documentation specifies the MPPT TX port as a logic 5 V
signal driving **at most a 22 kΩ load, at which point the output has already fallen to
3.3 V.** That implies an effective source impedance around **10–11 kΩ**. A BSS138
translator presents a 10 kΩ pull-up to 5 V on the HV node; against a comparably weak
driver the low level lands near **2.4 V**, the FET never sees a valid gate-source
condition, and the LV side stays permanently high. **The failure mode is silent** — no
framing errors, simply no data.

This is contingent on whether the driver is symmetric. Many outputs are strong-low and
weak-high, in which case the BSS138 is fine and this note is moot. **It is cheap to
settle.**

**Measurement (M4).** Put 10 kΩ from the MPPT TX pin to GND with the port streaming and
observe the **low** excursions, preferably on a scope.

| Result | Meaning | Action |
|---|---|---|
| Low ≈ 0–0.5 V | Strong low side | **BSS138 as planned.** No BOM change |
| Low ≈ 2–2.5 V | Weak symmetric driver | BSS138 cannot translate this direction. Use an **ADuM1201** (CMOS input, unloads the driver, isolation as a bonus) or a **74LVC1G17** buffer at 3.3 V |

**The BSS138 stays for the RX direction regardless** — a strong 3.3 V output into a
high-impedance MPPT input is exactly what that topology handles well. **A split solution
is acceptable and is the expected outcome if the measurement goes badly.**

> Note also that whether a 5 V-family MPPT reliably reads 3.3 V as a logic high on its RX
> input is **not documented by Victron** and has been asked on their own community forum
> without answer. **V-6 must prove the HEX round-trip, not assume it.**

#### 4.2.3 Isolation and rise time

- **Galvanic isolation is not required** (**D12**). Given the single-enclosure
  installation — no motor inside, all devices sharing one box, short heavy battery leads
  — worst-case ground offset is on the order of **16 mV** (2 ft of 12 AWG at 5 A) to
  ~40 mV for lighter wire. Against a 5 V logic threshold this is noise.
- **Rise time.** BSS138 is open-drain with 10 kΩ pull-ups, RC-limited. At ~20 pF (module
  plus short trace, which is this install) rise time is ~440 ns against a 52 µs bit
  period — **0.85%**, three orders of magnitude of headroom. If a far-end edge looks lazy
  during bring-up, parallel additional pull-ups to bring the effective resistance to
  2.2–4.7 kΩ.

#### 4.2.4 On-node protocol multiplexing

The MPPT emits ~1 Hz text frames and HEX responses **on the same UART**, interleaved.
The parser is a **line-oriented state machine**:

- A line beginning with `:` is a **HEX** frame — hex nibbles, checksummed,
  newline-terminated.
- Anything else belongs to the **text** protocol (`LABEL\tVALUE\r\n`, blocks terminated
  by a `Checksum` field).

Rules:

- **One outstanding HEX transaction at a time.** Correlate the response by its echoed
  register ID; discard unmatched responses with a log entry.
- `hex_timeout_ms` default **1000**; on timeout return an explicit timeout status rather
  than silence.
- **Do not disable the text protocol.** The 1 Hz stream is the primary telemetry source.
- HEX requests are transported **verbatim.** GateLink inspects only the command nibble,
  to enforce the write-authentication rule.

Use / port **`osh-labs/VE.Direct_mppt_arduino`** (MIT) for both the text parser and the
HEX protocol definitions, register map and encode/decode helpers.

#### 4.2.5 If a SmartShunt is added

It presents a second VE.Direct port requiring BSS138 channels 3–4 and a third hardware
UART on PORT.C. The ESP32-S3 has three UARTs; two are free.

### 4.3 BLE BMS

The pack is a **TDT** BMS advertising as `XDZN_001_xxxx`. **Frame formats, the register
decode and a reference capture live in `/docs/bms-protocol.md`** and are not duplicated
here.

**The access sequence — the part nothing else documents:**

1. Write the **`HiLink` handshake to characteristic `FFFA`** — *not* `FFF2`.
2. **Read `FFFA` back** and confirm the `0x01` acknowledgement.
3. **Subscribe to `FFF1`** for notifications.
4. Send requests to **`FFF2`**, using request head **`0x1E`** (`0x7E` is never answered by
   this unit). **All writes are with-response.**

Without step 1, writes to `FFF2` are ATT-acknowledged and then ignored, and the pack drops
the link at ~4 s — **which is exactly what made every earlier probe look like a wrong
protocol** (§9.5).

**Implementation notes:**

- **Stack:** NimBLE-Arduino — materially smaller flash and RAM footprint than Bluedroid.
  ~200–300 KB of flash, comfortable in 8 MB.
- **MTU.** Reference decodes were obtained at a negotiated MTU of **512**, with responses
  arriving unfragmented. **NimBLE defaults lower**; the client must either request a
  larger MTU or implement reassembly.
- **Driver portability is a recompile, not a port.** The Stamp-S3A and the Heltec V3 are
  both ESP32-S3FN8 — same core, same flash, no PSRAM on either — so the client developed
  during the BLE proof of concept builds unchanged.
- **Cell-voltage jitter.** Readings move 1–2 mV between polls from ADC noise. **The
  bridge** rounds or publishes on change; the node transmits what it read.
- **Testable offline.** The C++ client is a port of a validated implementation and can be
  unit-tested against the same captured frames.

**Antenna (D28).** The Stamp-S3A's 2.4 GHz antenna is **internal to the DIN case with no
external option**, unlike the LoRa side. **Measure RSSI at the final mounting position
before committing — now M23, which supersedes M5.** §9.5 records why this was a live
concern; the paragraph below records what changed on 2026-09-06.

> **Updated 2026-09-06 — the geometry is better than this section assumed, and the
> measurement is a different one.** The GateLink node sits in a **plastic enclosure inside
> the steel gate-controller enclosure**, and **the pack and its BMS are inside that same
> steel enclosure**, 6–8 in away (GateLink PRD §4.3.1). The BLE link therefore never
> crosses a metal wall — both ends are in one cavity. A **−50 to −60 dBm** reading was
> taken in that enclosure with a Heltec V3, 20–30 dB better than §9.5's premise; that
> discrepancy is recorded and not silently reconciled (Decision Register §2.2), and §9.5's
> dated −80 dBm figure stands as written.
>
> **What this changes about the measurement.** A closed steel box holding both ends is a
> reverberant cavity: energy is returned rather than radiated away, so the mean level over
> six inches is typically *better* than free space. The cost is structure, not loss —
> **standing-wave nulls that are position- and orientation-dependent**, and can be deep.
> **M23 therefore samples at least three positions and two orientations** with the
> Stamp-S3A's own antenna, rather than taking one reading and calling it the figure. A null
> is defeated by moving the node a few centimetres, which is a mounting decision, not a
> redesign.

**Radio coexistence: the isolation is good, and the one path that is hard to characterise
is inside the box.** The SX1262 is separate silicon on SPI at 915 MHz, the BLE radio is
2.4 GHz, WiFi is off, and 915 MHz harmonics land nowhere near 2400–2483.5 MHz — the
mechanism to worry about was never harmonics but broadband PA noise and receiver blocking
from a transmitter inches away. **The steel wall sits between the LoRa antenna and the BLE
receiver**, adding to the 20–30 dB of free-space spacing, and the Envelope A working point
of −4 dBm conducted is ~24 dB below what the Wio could emit at its grant power.

**The exception, and it is the reason R-4.3h exists.** The LoRa feedline does not leave the
box at the module: it runs from the Wio's IPEX to a bulkhead on the plastic enclosure,
across a jumper, to a second bulkhead on the steel — **three connector pairs and two cable
runs inside the cavity that holds the BLE receiver and the BMS**, alongside the 1050's
motor drive, the MPPT's switcher and the loop detector's oscillator. Leakage there cannot
escape. Mitigation is mechanical: shielded assemblies, sound connectors, short runs away
from the motor harnesses (§ layout guidance already requires this for the reverse reason).
**Strict LoRa/BLE mutual exclusion (R-4.3h) is kept because it covers this path cheaply**,
and because a connector degrading over years of outdoor thermal cycling is exactly the
failure that would not announce itself.

### 4.4 Gate controller I/O — timing

| Parameter | Default | Range | Note |
|---|---|---|---|
| `input_poll_ms` | 100 | 20–500 | Poll the input expander |
| `input_debounce_samples` | 2 | 1–10 | Two consecutive agreeing reads ⇒ ~200 ms at the default |
| `relay_pulse_ms` | 500 | 100–2000 | Raised from 300; the controller's internal debounce is undocumented |
| `post_wake_settle_ms` | 500 | — | After a pulse, before evaluating state |
| `command_confirm_timeout_s` | 5 | — | Before declaring a command failed |
| `unlock_settle_ms` | 500 | — | Between K2 and K4 on an immediate close |
| `hold_confirm_ms` | 2000 | — | Stable 1/0 before declaring a hold |

All runtime-configurable (§6.4).

---

## 5. Firmware architecture

### 5.1 Framework and libraries

| Concern | Choice | License |
|---|---|---|
| Build | **PlatformIO**, ESP32-S3 target | — |
| Platform HAL | **`M5StamPLC` + `M5Unified`**, wrapped behind `/lib/lran-platform/` | MIT |
| LCD | LovyanGFX via M5Unified | BSD-2-Clause |
| LoRa | **RadioLib** driving the SX1262, CAD for media access | MIT |
| VE.Direct | port of **`osh-labs/VE.Direct_mppt_arduino`**, text **and** HEX | MIT |
| BLE | **NimBLE-Arduino** | Apache-2.0 |
| HMAC / HKDF | mbedTLS via ESP-IDF | Apache-2.0 |
| Gate controller | **No protocol library.** Debounced reads from the input expander, timed pulses to the relay expander | — |

`/lib/lran-platform/` abstracts the host so GateLink and AquaLink compile against one
HAL and the Heltec targets keep building.

### 5.2 Task structure

The scheduling requirement (**R-5.2a**) is the shape of this design: **the I/O service
must never be starved.**

| Task | Period / trigger | Priority | Owns |
|---|---|---|---|
| **`io_task`** | `input_poll_ms` (100) | **Highest** | Input expander polling, debounce, relay pulse timing. **The only task that touches the AW9523B** |
| `vedirect_task` | UART RX event | High | Line-oriented parser; text cache; HEX transaction state machine |
| `lora_task` | RX interrupt / TX queue | High | RadioLib, CAD, backoff, frame serialize/deserialize, MAC |
| `app_task` | 100 ms tick | Normal | State derivation, hold tracking, direction classifier, trigger logic, status assembly |
| `bms_task` | `bms_poll_s` (300) | Low | NimBLE connect / read / disconnect, then **de-init the controller** |
| `ui_task` | 100 ms tick | Low | LCD pages, buttons, backlight timeout, buzzer |
| `log_task` | queue | Lowest | Leveled serial + rotating microSD log |

**Rules:**

- `io_task` owns the I²C bus for relay and input access. Any other task needing the
  expander goes through a HAL call that queues to `io_task`. **A relay pulse whose
  trailing edge is late is a command of the wrong length.**
- `bms_task` runs at low priority and its failures are non-blocking (**R-3.4d**).
- No task blocks on the LoRa transmit path; frames are queued.
- Watchdog fed from `app_task`, not from `io_task` — a stalled application must not be
  masked by a healthy I/O loop.
- **`CommandGate::check()` runs in the receive path, before dispatch; `record()` runs
  after execution, and `app_task` sends the `COMMAND_ACK` after `record()`.** This task
  split puts an execution window between the two — `lora_task` keeps receiving while
  `io_task` pulses — and a bridge retry landing in it gets `InFlight` and no answer
  (Protocol Spec §9.4, D34 amended 2026-09-11). The gate holds no lock, so the two calls
  are serialized: post the result back to the task that owns the gate, or guard it.
  **Open:** whether the ACK waits for the pulse to complete or for the gate to confirm
  movement (`command_confirm_timeout_s`, 5 s). It sets how often the window is hit against
  the bridge's 3 s ACK timeout. **Decide it before M3.**
- **`ROLL_CONTEXT` bypasses `CommandGate::check()`** (Protocol Spec §9.4, §10.6,
  **D58**, PRD R-3.5e). While any entry is in flight, GateLink answers `ACTUATOR_BUSY`
  and changes nothing. Otherwise it takes a new random `ctx_id`, calls
  `reset_context()`, resets its status `seq`, and ACKs under the new `ctx_id`. The
  in-flight check runs on the task that owns the gate, for the reason `record()` does.

### 5.3 Module map

```
/firmware/gatelink/
  src/
    main.cpp              task creation, HAL init, boot sequence
    gate_io.cpp           input debounce, relay pulse driver          [io_task]
    gate_state.cpp        IN1/IN2 -> state; hold derivation + source  [app_task]
    detect.cpp            direction classifier state machine          [app_task]
    cause.cpp             movement-cause inference                    [app_task]
    triggers.cpp          when to push status / event                 [app_task]
    status.cpp            schema 0x10 / 0x11 / 0xF0 assembly          [app_task]
    commands.cpp          COMMAND dispatch, dedup cache, ACK          [app_task]
    ui.cpp                LCD pages, buttons, backlight               [ui_task]
    debug.cpp             dry-run, injection, loopback, dummy push
  lib deps ->
    /lib/lran-platform/   relays, inputs, LCD, buttons, INA226, LM75, RTC, SD
    /lib/lran-protocol/   framing, addressing, HMAC, CRC, fragmentation
    /lib/lran-config/     parameter table, SD persistence, CONFIG handling
    /lib/vedirect/        text + HEX
    /lib/bms-ble/         TDT client
  CLAUDE.md               subproject context for Claude Code
```

`/docs/gatelink/engineering-log.md` carries the dated running record — what was tried,
measured, decided and why. Neither this plan nor the PRD is the right place for "tried X
on the bench, it did not work because Y," and that is exactly the information most
expensive to lose.

---

## 6. Implementation specifics

### 6.1 Gate command model

Every pulse is `relay_pulse_ms` wide.

**Case 1 — momentary open.** Pulse **K3** (Guard Station Open). The gate opens to full
open with auto-close enabled; the controller closes it after the auto-close timeout,
measured at **60 s**.

- Confirmed wake input — it is what the keypad drives.
- No hold state is entered. IN1/IN2 settle at **1/1**.
- If the gate is already open and unlocked, the pulse **restarts the auto-close
  countdown** — harmless, and a useful way to extend the window without holding.

**Case 2 — open and hold.** Pulse **K1** (AUX1 = OPEN and LOCK). The gate opens and the
controller latches a LOCK state that inhibits auto-close and further commands. IN1/IN2
settle at **1/0**.

Three properties make this the right mechanism, and it is the one the installation
already uses:

- **It is a latched state inside the controller, set by a momentary pulse.** No wire is
  held down, so no stuck contact can hold the gate abnormally.
- **No safety interlock is bypassed.** FIRE was considered and rejected (**S-2**).
- **No lock-out hazard.** SHADOW was considered and rejected (**S-3**).

One power consequence: while the gate is open, OUT1 is energized and **the controller
cannot enter standby.** A long hold is a long active period.

**Case 3 — close.** Two paths:

| Path | Action | Result |
|---|---|---|
| **Release** (default) | Pulse K2 | Lock clears, auto-close resumes and closes the gate within the 60 s timeout. Matches existing behaviour |
| **Immediate** | Pulse K2, wait `unlock_settle_ms`, then pulse K4 | Closes without waiting out the auto-close timeout |

K2 must precede K4 whenever the gate is held — a locked controller ignores a close
command. GateLink sequences this automatically; **HA sees a single "close" operation.**

### 6.2 Hold-state tracking — observed, not remembered

Hold state is derived from IN1/IN2 per **R-3.1.4a**, with a separate source field
recording who GateLink believes did it:

| Observation | `held_open` | `hold_source` |
|---|---|---|
| IN1=1, IN2=0 stable, following a GateLink K1 pulse | true | `lran` |
| IN1=1, IN2=0 stable, following an IN5 (FIRE) assertion | true | `keypad_fire` |
| IN1=1, IN2=0 stable, with neither of the above | true | `manual` — remote, keyswitch or panel |
| IN1=1, IN2=1 | false | — (countdown running) |
| IN1=0 | false | — |

**Boot behaviour.** On boot GateLink reads IN1/IN2, **adopts the observed state**, sets
the source to `unknown` if it cannot attribute it, and publishes both. It does **not**
pulse UNLOCK at startup (**S-7**).

### 6.3 Movement-cause inference

Derived locally rather than read from the controller:

| Observation | Inferred cause |
|---|---|
| EXIT rising edge, then gate opens within `cause_window_ms` (default 10000) | Exit wand |
| GateLink pulsed K1 or K3, then gate opens | LRAN command |
| IN5 asserts, then gate opens | Keypad FIRE code |
| Gate opens and settles to **IN1=1 / IN2=0** with none of the above | **Manual hold** — handheld remote, or the keyswitch |
| Gate opens and settles to **IN1=1 / IN2=1** with none of the above | **External momentary open** — keypad, remote, or front panel |

The last two rows come free with the MOVING semantics: **a manual *hold* and a manual
*momentary open* are now distinguishable**, which they were not when both simply read
"open."

### 6.4 Configuration

Three layers — firmware defaults, microSD overrides, RAM live values — carried by the
authenticated `CONFIG`/`CONFIG_ACK` pair (Protocol Spec §7.4).

**Parameters are declared once in `/lib/lran-config/`** — name, type, unit, range,
default — in a hand-written C++ table, and the firmware defaults, the HA `number`
discovery payloads and `/docs/gatelink-config.md` are all **derived from it by code**
(**D44**). GateLink's parameters take `param_id`s from `0x1000`–`0x1FFF`, and the ones
every node holds from `0x0100`–`0x01FF` (Protocol Spec §7.4, **D46**). Three hand-maintained
copies drift, silently: HA offers a range the firmware clamps, or documentation describes
a default that changed two revisions ago.

**A full readback may span several `CONFIG_ACK` messages** (Protocol Spec §7.4.1, **D57**),
and the six PHY rows change only through §12.4's commit-and-revert (**D56**), `READ_ONLY`
until it is built.

**microSD contents:**

| File | Purpose |
|---|---|
| `config.json` | Configuration overrides |
| `state.json` | Last-known gate/hold state, boot counter |
| `log/*.txt` | Rotating leveled event log |

**Absent-card behaviour:** defaults apply; runtime changes are applied to RAM, ACKed with
an explicit not-persisted status, and the condition is published as a diagnostic sensor.

### 6.5 Display

- **Backlight only** is switched, via the PI4IOE expander (`LCD_BL`, P7). The panel stays
  initialised.
- **Three dedicated user buttons**, none of them strapping pins: **A = next page,
  B = previous, C = off.**
- Multi-page cycling is needed — one page will not fit gate state + hold state + detector
  state + MPPT + battery SOC + link quality. The 135×240 colour panel fits materially
  more per page than a small monochrome OLED would.
- **Buzzer: default off.** Candidate use is audible confirmation during bring-up, when
  the operator is at the gate and not looking at the screen. **A gate that beeps of its
  own accord is not wanted.**

### 6.6 Debug tooling

| Tool | Implementation note |
|---|---|
| **Relay dry-run** | A switch that short-circuits the pulse call, logging and publishing intent. `COMMAND_ACK` returns the dry-run result code so HA sees the difference. **Enabling it also lights the display** |
| **Input injection** | Synthetic assertions on IN1–IN6 in configurable order and spacing, injected *below* the debounce layer so debounce is exercised too. Must cover 30 s gaps and partial traversals |
| **Packet loopback** | RF echo, and internal loopback feeding serialized frames back into the receive parser with no radio — the path with **no PHY CRC**, hence the application CRC16 |
| **Dummy status push** | Synthetic VE.Direct and gate-state data, marked synthetic all the way into HA history |
| **Device simulators** | A VE.Direct frame generator covering **both text and HEX**; a dummy BMS BLE peripheral |
| **MQTT as bench harness** | `mosquitto_sub -t 'lran/#'` to watch every decoded payload live; `mosquitto_pub` to inject commands or fake status, decoupled from HA and the RF link |
| **microSD logging** | Leveled and rotating, so a fault occurring while the LoRa link is down is still recoverable afterwards |

### 6.7 D30 — the co-processor fallback

The obvious alternative to a bare SX1262 on a carrier is an **intelligent radio module** —
a Heltec LoRa V3 — hung off a UART, running the radio and the BLE client itself and
exchanging framed messages with the host. It packages the SX1262, its antenna connection
and a 3.3 V regulator into one off-the-shelf board, which is exactly the kind of trade
this project has preferred in hardware terms.

**It is not adopted, for firmware reasons rather than hardware ones:**

- It converts a wiring problem into a **two-MCU problem**: an inter-processor protocol to
  define, version and debug, carried underneath the LoRa protocol it is already carrying.
- **Two flash procedures at the gate**, on a node with no OTA. Every firmware visit
  doubles.
- **Split debugging.** A dropped packet becomes ambiguous between two processors and the
  link between them.
- The compute it offloads is not scarce. RadioLib plus NimBLE on an ESP32-S3, moving a few
  packets a minute and running one BLE poll every five, is nowhere near the headroom limit
  — and the 100 ms input poll removed the only real-time pressure the host was under.

**What it would genuinely buy** is an **externally positionable 2.4 GHz antenna**, which
is precisely the open problem in **D28**.

**Revisit if any of these occur:**

1. Carrier bring-up (§8, M0) fails or proves fragile.
2. **D28** measures inadequate BLE margin from the final mounting position *and* the
   SmartShunt route is unattractive.
3. The pin budget breaks — a future requirement needs pins §3.3 has already spent.

---

## 7. Test and verification plan

### 7.1 Coverage matrix

| Requirement | Verified by | Stage |
|---|---|---|
| V-1 radio on carrier | Bench, LDO under TX load + RadioLib link | M0 |
| V-2 state table | Real gate cycles, DVM + logged frames | M2, M6 |
| V-3 direction classification | **Input injection**, then real vehicle | M3, M6 |
| V-4 held-open alert | Injection across all four hold sources | M3, M6 |
| V-5 events fire once | HA restart + discovery refresh with an event in history | M8 |
| V-6 VE.Direct | Bench MPPT, text parse + HEX round-trip + write rejection | M4 |
| V-7 BMS | Live pack vs. reference decode; RSSI from mounting position | M5 |
| V-8 command paths | **Dry-run first**, then live | M7 |
| V-9 manual unlock | Physical test, **before the first real hold-open** | M7 |
| V-10 config round-trip | With card, then **with the card removed** | M3 |
| V-11 consumption vs. budget | Onboard INA226 over a soak period | M9 |
| V-12 thermal | Seasonal log, three sensors | M9 |

### 7.2 Staged safety gating

Three gates, each of which makes the next stage safe:

1. **Read-only.** Inputs wired, **relays physically disconnected.** Validates state
   derivation, hold detection, detection and direction against real gate cycles driven by
   the keypad and the remote. **This phase cannot move the gate.**
2. **Dry-run.** Relays wired, dry-run enabled. Every command path exercised from HA;
   logged intent compared against expectation. **Nothing energizes.**
3. **Live.** Dry-run disabled, with a clear line of sight to the gate, and **both manual
   UNLOCK paths confirmed before the first real hold-open.**

### 7.3 Bench-reachable requirement

Every verification except V-2, V-7 and V-11 must be reachable **without a functioning gate
installation**, using injection, dry-run, loopback, dummy status and simulated peripherals
(**R-9.3a**). Development that requires an ~87 m walk and a moving gate for every
iteration will not get the iteration count it needs.

### 7.4 Controller bring-up procedure

Ordered, and safe to perform incrementally. **No LRAN hardware is required for steps
1–6**, which is why they run in parallel with carrier and firmware work.

1. **Rewire the keyswitch and pushbutton** (§3.1.1). Remove the series link; keyswitch to
   AUX1, pushbutton to AUX2. **Do this before reprogramming the AUX terminals**, so there
   is no window in which an unsecured pushbutton can assert OPEN+LOCK.
2. **Reprogram the controller.** AUX1 → OPEN and LOCK; AUX2 → UNLOCK; OUT1 → OPEN;
   OUT2 → MOVING. Record all settings, including the 60 s auto-close and standby timeouts,
   in `/docs/1050-config.md`.
3. **Verify by hand.** With no GateLink connected, operate each control and confirm the
   expected behaviour. Meter OUT1/OUT2 through a full open/close cycle and confirm the
   state table — in particular that **MOVING stays asserted through the auto-close
   countdown**, and whether it dips at the open limit before the countdown starts
   (**M2**). Confirm the handheld remote's OPEN+LOCK produces the same 1/0 reading LRAN's
   own command will.
4. **Characterise the 12 V lamp output** against the countdown while the meter is out
   (**M8**), so the backup option is either usable or ruled out.
5. **Measure IN5 and IN6 levels** (**M3**) to establish idle state and sense polarity.
6. **Re-measure the controller branch current properly**, in all four gate states
   (**M1**). **This is the one item here that can change the power budget.**
7. **Wire inputs only.** Read-only phase — gate 1 above.
8. **Wire relays with dry-run enabled.** Gate 2.
9. **Disable dry-run.** Gate 3.

---

## 8. Milestones and acceptance criteria

| # | Milestone | Depends on | Acceptance criteria |
|---|---|---|---|
| **M0** | **Carrier board bring-up** | BOM in hand; **M4** settled | LDO holds ≥3.2 V through SX1262 TX **at the D33 ceiling, −4 dBm conducted** — the power this node operates at. *Previously read "+22 dBm", which neither envelope permits.* **If Envelope B is ever triggered, re-run this at the power it allows** (§3.4); the rail is sized for it but untested there. RadioLib initialises the radio on the §3.3 pin map with the correct TCXO voltage and DIO2 RF-switch mode. Module confirmed to need no TXEN/RXEN. Ping/loopback to a Heltec succeeds on the bench. **Failure here is D30 trigger 1** |
| **M1** | **Platform HAL** | Host in hand | Relays pulse to a measured width within ±10 ms at the configured value; inputs read and debounce correctly against a bench switch; LCD, buttons, buzzer, INA226, LM75, RTC and SD all accessible through `/lib/lran-platform/`. **The same HAL compiles for the Heltec bridge target** |
| **M2** | **Controller rewire, reprogram and manual validation** | Nothing — runs in parallel | §7.4 steps 1–6 complete. `/docs/1050-config.md` written. **M1, M2, M3, M8 measurements captured.** The §3.2 state table confirmed by DVM through real cycles, including the handheld remote's OPEN+LOCK |
| **M3** | **Protocol, framing and configuration on the bench** | M0, M1 | Frames serialize and deserialize against the committed test vectors. MAC, sequence, context resync, the context roll after a bridge restart (Protocol Spec §10.6) and command dedup all verified. **`simnode` runs alongside**, validating addressing, per-node keying, availability watchdog, fragmentation and CAD/backoff. Direction classification passes injection including **30 s gaps and partial traversals**. Held-open alert fires on the first edge for all four hold sources. **Configuration round-trip passes with a card and again with the card removed**, reporting honestly in both cases |
| **M4** | **VE.Direct** | M0, **M4 measurement** | Translator selected per D25. All documented text fields parse from a real MPPT 75/15. **HEX round-trip proven** — request out, response in, correlated. Write rejected when unauthenticated, and rejected by the bridge when disarmed. Staleness flag asserts when the stream stops. §9.6 baseline log started |
| **M5** | **Battery and BMS** | M1 | TDT client decodes the live pack in agreement with the reference implementation. **BLE RSSI measured from the intended mounting position (D28)** and judged adequate — or a fallback selected. MPPT reconfigured for LiFePO4 and verified by readback. Low-temperature inhibition detection validated by both paths. **Pack current captured under charge and under load (M7)**, settling the sign convention |
| **M6** | **Inputs live, read-only** | M2, M3 | Relays physically disconnected. State derivation, hold detection, detection and direction all confirmed against real gate cycles driven by the keypad and the remote. `hold_confirm_ms` demonstrably rejects the transient 1/1 at the start of a close. **The gate cannot be moved by GateLink in this phase** |
| **M7** | **Relays live** | M6 | Dry-run first: every command path exercised from HA, logged intent matching expectation. **Both manual UNLOCK paths confirmed working.** Then dry-run disabled and each command tested with a clear line of sight |
| **M8** | **HA integration** | M3–M7 | Discovery publishes one device per node with correct availability. Command round-trip works end to end. All §7.2 entities present and populated. **Held-open and FIRE events verified to fire exactly once and not replay on HA restart or discovery refresh.** Configuration `number` entities read and write |
| **M9** | **Field soak** | M8 | Installed. Measured daily consumption from the INA226 compared against budget. Overnight ΔSOC and days-since-full tracked. **Seasonal enclosure-temperature log begun across all three sensors (D29).** Error paths exercised: link loss, BLE failure, VE.Direct stall, microSD removal |

**Critical path:** M0 → M3 → M6 → M7 → M8 → M9. M2 runs in parallel from the start; M4 and
M5 are parallel to M6 once M0 and M1 land.

---

## 9. Integration observations

*Everything measured or established on the real installation. This is the evidence base
for the design; nothing here is a plan.*

### 9.1 Controller output relay semantics — measured

Measured by programming the relays and cycling the gate.

| Programmed as | The relay energizes |
|---|---|
| **Open** | when the gate is **fully open**, and stays energized while it remains open |
| **Closed** | when the gate is **fully closed**, and stays energized while it remains closed |
| **Moving** | while the gate is **opening or closing — *and* while it is open and waiting on the auto-close timer** |

**Any energized OUT relay prevents standby.** The coil is a load the board will not carry
while asleep. This **resolves D23**: the relays do not drop their state in standby,
because the board does not enter standby while they are held. No sense inversion and no NC
wiring is needed.

> **The `Moving` semantics are the single most consequential measurement in this project.**
> They make the controller's *lock* state directly observable rather than inferred — OPEN
> asserted with MOVING clear means nothing is counting down, which means the gate is held.
> Every hold-open mechanism on the property is therefore visible to GateLink with **no new
> wiring**, and hold tracking stops being a flag the firmware hopes is still true.

### 9.2 Input wake behaviour — measured

A consistent model emerged:

| Class | Inputs tested | Wakes? |
|---|---|---|
| **Command** — causes an action or state change | EXIT, Guard Station Open (via keypad), FIRE (via keypad), AUX=STEP, AUX=UNLOCK | **Yes** |
| **Conditioning** — qualifies motion already in progress | SAFETY, SHADOW, ENTRAPMENT (shorted directly to GND) | No |
| **Unassigned** | AUX programmed "No function" | No |

Note **AUX=UNLOCK causes no motion of its own yet still woke the board**, so the predictor
is *is this a command*, not *does this move the gate*. The loop-input test was done by
shorting the inputs directly to GND, so it is a conclusive test of the input, not of the
detector.

**Auxiliary input behaviour, confirmed on the bench:** an AUX input programmed **Open+Lock**
opens the gate and holds it open as expected, and an AUX input programmed **Unlock**
releases that state and lets the gate close. The command model in §6.1 is verified against
the board, not inferred from the manual.

**Standby behaviour, measured:** entering standby sheds the accessory 24 V rail and the
protocol-bus data pins entirely.

### 9.3 Current draw — first measurements, and they do not add up

**Method:** current clamp on the V+ feed into the controller, **40 A range (0.01 A
resolution)**, PV disabled at the MPPT, V+ ≈ 13.95 V. The measured branch carries the
controller, the keypad, the exit wand and the loop detector — **not** the LED lights, and
not GateLink.

| Gate controller state | Measured |
|---|---:|
| Open+Lock — active | 0.33 A |
| Open+Lock — standby | 0.32 A |
| Closed — active | 0.23 A |
| Closed — standby | **0.29 A** |

**These figures are recorded, not adopted.** Three things are wrong with them:

1. Standby measures **higher** than active in the closed case, which is not physically
   sensible.
2. The standby figures are roughly **15–20× the ~17 mA** the board is expected to draw
   asleep.
3. Taken at face value this branch alone is **5.5–7.9 Ah/day**, which with the LED lights
   would put the site near 12 Ah/day against ~10.6 Ah/day of winter harvest — a standing
   deficit that would have flattened the previous 75 Ah lead-acid pack every winter. **It
   demonstrably did not.**

**The likeliest explanation is the instrument rather than the board:** at 0.2–0.3 A on a
40 A range the reading sits below 1% of full scale, where clamp offset and a few counts of
resolution dominate and ±0.1 A of error is unremarkable.

**The budget is not revised on these numbers**, because rewriting a working system's budget
on a measurement that fails its own internal sanity check would be the wrong move. **Nor
can they be dismissed:** if the standby figure is real, the site runs a winter deficit, and
the fix would be **LED runtime, not firmware.**

**Re-measurement is M1, the top item in the backlog.** Preferred methods: an inline DC
ammeter or shunt in the V+ lead on a mA range; or the same clamp on its lowest range,
zeroed, with **ten turns of the conductor through the jaw and the reading divided by ten**.
A SmartShunt would settle it permanently.

### 9.4 Existing controls, as installed

| Control | Drives | Note |
|---|---|---|
| Remote keypad | Guard Station Open (34); a secondary relay drives FIRE (32), separate codes | **The FIRE code is reserved for testing and real fire emergencies** |
| Keyswitch + pushbutton | AUX1 (16), programmed STEP. **Wired in series** | To be split and rewired — §3.1.1 |
| **Handheld remote** | **Programmed OPEN+LOCK on one button, UNLOCK on the other** | A manual hold-open path already exists, and it satisfies **D24** |
| Radio Open/Close (39, 40) | **Free** | Held in reserve |
| OUT1, OUT2 | **Free** | To be programmed OPEN and MOVING |
| Auto-close | **Enabled, 60 s** | |
| Standby timeout | **60 s** | |

**Existing hold-open mechanism.** The gate is held open using **OPEN and LOCK**, released
by **UNLOCK**, after which auto-close closes the gate. Available today from the handheld
remote, and from the AUX terminals LRAN will drive.

**Additional moving indicator.** The board provides a **12 V lamp output active while the
gate is moving.** LRAN does not use it in v1, but it is recorded as a second source of a
motion signal should OUT2 ever be wanted for another function. **Whether it also tracks
the auto-close countdown is M8** — if it does not, it is not a drop-in replacement.

### 9.5 Detector power — **done**

The loop detector was powered from **gated** V+, and was therefore unpowered — not merely
idle — during controller standby. **That made the held-open alert unimplementable**: the
gate standing open is precisely when the board sleeps and the detector would be dark, and
it would not have mattered how GateLink acquired the contact.

**Done. The detector's V+ has been moved to terminal 10 (ungated).** Power remains present
there during standby, the manual recommends terminals 10/11 for accessories in standby
applications, and the exit wand already runs from the same source and needed no change.

Cost: ~1 mA continuous, **0.02 Ah/day**.

*Remaining note:* terminal 11 tracks the highest incoming voltage (~13.5 V from the
battery); if the detector is ever moved there, check that against the DSP-7LP's input
range. On terminal 10 as wired, this is not an open item.

### 9.6 BMS — how the protocol was actually found

Recorded because it explains why the final answer is trusted, and because the failure
signature is one a future reader will otherwise misdiagnose.

- The pack was initially assumed to be a **JBD/Xiaoxiang** unit. A **20-combination sweep**
  — JBD basic and version frames, Daly with two host addresses, each sent to all five
  writable characteristics while subscribed to three notify characteristics — **produced
  zero notifications** and a consistent ~3.9 s disconnect every time.
- **That consistency was the clue.** The link dropped at the same moment regardless of what
  was sent, which is not the signature of a wrong protocol; it is the signature of a
  connection that was never authorised in the first place.
- Re-identification via the `aiobmsble` library's **TDT** plugin produced a full decode.
  The protocol was then reimplemented independently and instrumented against the reference
  transport, which is what revealed the **`HiLink` handshake to `FFFA`** (§4.3).
- **Validation:** the independent client ran **32 consecutive polls over ~81 s on one
  continuous connection, with zero CRC failures and no dropped frames**, decoding SOC, pack
  voltage, per-cell voltages, four temperatures, capacity, cycle count and MOSFET state in
  agreement with the reference. The written protocol spec is complete enough to build the
  C++ driver from.

**Link margin is a real concern, not a formality.** The pack advertises at about **−80 dBm
from inches away** — confirmed independently with a phone, which needs to rest on top of
the battery to beat −60 dBm. **This is the battery's own transmitter, not the test
hardware**; the reading is consistent with a weak transmitter or an antenna shielded under
the BMS heat sink. GateLink will sit 6–8 in from the pack, which should be comfortable,
**but the host's 2.4 GHz antenna is internal to its DIN case with no external option** —
hence **D28** and **M5**.

> **Superseded in part, 2026-09-06.** M5 is superseded by **M23**, and the paragraph above
> is a dated record whose *outlook* has changed: a −50 to −60 dBm reading taken inside the
> deployed steel enclosure is 20–30 dB better than the −80 dBm recorded here. The −80 dBm
> observation itself is left standing — it may have been taken outside the enclosure, at a
> cavity null, or at a different pack state, and which of those holds is still open. See
> §4.3's updated note and Decision Register §2.2.

The full record of dead ends is in [`LRAN-Research-Archive`](../archive/LRAN-Research-Archive.md).

### 9.7 Power budget — the working table

**`TBM` = to be measured; placeholders are conservative. Read §9.3 before treating this as
settled.**

| Load | Current @ 12 V | Duty | Ah/day |
|---|---:|---|---:|
| 2 × 2 W LED lights (night only) | 0.33 A | 12 h | **4.00** |
| Controller, standby retained | 0.017 A **TBM** | ~23 h | 0.39 |
| Controller, active windows | 0.110 A **TBM** | ~1 h | 0.11 |
| Loop detector, continuously powered | 0.001 A | 24 h | 0.02 |
| GateLink node — continuous RX | 0.048 A (vendor) **TBM** | 24 h | 1.15 |
| GateLink — BLE BMS polling | — | 288 cycles/day | 0.03 |
| GateLink — relay coils | ~0.070 A | ~10 pulses × 0.5 s/day | <0.01 |
| MPPT self-consumption | 0.010 A | 24 h | 0.24 |
| Gate motors while operating | ~5 A **TBM** | 2 cycles × ~20 s | 0.06 |
| *SmartShunt* — contingency only | *0.001 A* | *24 h* | *0.02* |
| **Total** | | | **≈ 6.9 Ah/day** |

**Harvest:** 50 W × ~3.0 winter peak-sun-hours (western NC, non-optimal tilt) × 0.85 system
efficiency ≈ 127 Wh ≈ **10.6 Ah/day**. Summer roughly doubles this.

**Not in the table** because it is occasional rather than continuous: a hold-open prevents
controller standby, so every hour of hold costs roughly the difference between the active
and standby rows.

| Configuration | Controller contribution | Total | Harvest ratio |
|---|---:|---:|---:|
| **Standby retained** | ~0.50 Ah/day | 6.9 | **1.53×** |
| Standby disabled | ~2.40 Ah/day | 8.9 | 1.19× |

**Where the current goes on the node.** The **radio is not the constraint.** LoRa 125 kHz
receive is 4.2 mA (normal) or 5.3 mA (Rx-boosted); TX is ~90 mA @ +14 dBm and ~118 mA @
+22 dBm, **both above anything this node transmits at** — D33 caps it at **−4 dBm
conducted**, in the PA's low-power path, so the real TX draw is lower than either figure
and the budget below is conservative in the node's favour. Sleep with configuration
retained is sub-µA. Continuous RX adds only ~5 mA on top
of an **ESP32-S3 that dominates** at tens of mA while awake. **The floor is set by keeping
the MCU awake, and the MCU stays awake to parse the ~1 Hz VE.Direct stream.** The host
platform adds the LCD backlight when on (off by default), the I²C expanders and the
INA226/LM75/RTC — all small, all inside the vendor's 47.84 mA working figure — and removes
a USB adapter's conversion loss.

### 9.8 Monitoring plan

**Before install:** log the MPPT's own yield history (H19/H20/H21) and battery voltage
minima for one week via VE.Direct (**M14**). That yields the *actual* present margin for
free.

**After install:**

| Metric | Source | What it tells you |
|---|---|---|
| Daily yield (H20) | MPPT | Harvest trend, panel/shading degradation |
| Overnight ΔSOC | BMS | True nightly consumption |
| Daily Vmin | MPPT | Proxy for depth of discharge if SOC is unavailable |
| Days since full | derived | Early warning of a sustained deficit |
| Charging-inhibited hours | derived | Distinguishes cold events from real faults |
| Node supply V/I | INA226 | Node consumption, measured not assumed |
| Three temperatures | LM75 / MPPT / BMS | **D29** |

**Decision rule for a panel upgrade:** upgrade only if *days-since-full* trends upward
across a season **and** the shortfall is not attributable to charging-inhibited hours.

---

## 10. Changelog

- **v0.11** — **Protocol specification v0.12 → v0.13.** §5.2 gains how GateLink handles
  `ROLL_CONTEXT` (spec §10.6, **D58**, PRD R-3.5e), and M3 verifies it. §6.4 notes that a
  readback may span several messages (**D57**) and that the PHY rows change only through
  §12.4's commit-and-revert (**D56**). §6.4 was already reconciled with D44 and D46 in
  v0.10. The header's requirements citation had fallen behind at PRD v0.5, and now reads
  v0.9.

- **v0.10** — **§6.4 follows D44 and D46.** The parameter table stays hand-written, and its
  outputs are derived by code rather than *generated*, which had implied a generator the
  project decided against. GateLink's `param_id` block is named. No requirement, milestone
  or BOM line changes. The binding citation stays at v0.12 until spec v0.13's sweep.

- **v0.9** — **Protocol specification v0.11 → v0.12.** §4.1's radio table loses its
  address-filtering row: the SX126x filters node addresses in **GFSK only**, verified
  against the datasheet as **M24**, so `dst` is checked in software at §14 stage 5 on this
  node as on every other. **`CONFIG` and `CONFIG_ACK` are single-frame** (§11.4), so the
  config path here is a message-splitting problem rather than a fragmentation one — **W10**
  is now a counting question and should be answered against this node's real parameter
  list. A repeated `CONFIG` is answered from the dedup cache (§7.4, **D37**). **M3's
  configuration round-trip is unchanged** in what it must show. Decision Register
  **D37**, **D38**, and **M24**.

- **v0.8** — **§5.2 gains the rule for where `CommandGate`'s two calls run**, and names the
  open question it depends on. Protocol specification **v0.10 → v0.11**, which answers
  the check/record window this plan's own task split creates: `lora_task` receives while
  `io_task` pulses, so a bridge retry can arrive mid-execution. Under the library plan as
  first written that retry would have pulsed the relay a second time; under D34 as amended
  2026-09-11 it is counted and not answered. **This plan is the design that falsified the
  old precondition**, and nothing had tracked it. What the `COMMAND_ACK` waits for —
  pulse or confirmed movement — stays open and is flagged for **M3**. No requirement, BOM
  line or milestone changes.

- **v0.7** — **Citation refresh; no requirement and no BOM line changed.** Protocol
  specification **v0.9 → v0.10**, which closes **D1**: 917.4 MHz, SF9, BW 125 kHz, CR 4/5,
  −4 dBm conducted with the fitted 3.0 dBi antenna, under §15.249 Envelope A. **This node
  consumes the decision and decides none of it**, and two parts of it land in this plan's
  own text. §9.7's radio energy figures are computed at SF9 already — a 534 ms `STATUS` at
  ~5 µAh — so **the power budget is unchanged and was never the constraint**; the MCU
  dominates while awake. And §12.3's **`backoff_max_ms` default is now 1500**, not 500,
  because a maximum `PING` at SF9 runs 1107 ms; the media-access row in §7 describes the
  mechanism rather than the number, so it stands as written. **M0's LDO margin note is
  untouched** — the rail is still sized against Envelope B's 19.6 dBm and tested at −4 dBm,
  and Envelope B is still a fallback whose triggering reopens **D28**.

- **v0.6** — **Three statements of TX power reconciled with D33, and one rename.** §3.4
  sized the carrier LDO against *"~120 mA peak SX1262 TX at +22 dBm"*, milestone **M0**
  accepted on *"LDO holds ≥3.2 V through SX1262 TX at +22 dBm"*, and §9.7's power budget
  quoted the same figure. **Neither envelope permits +22 dBm**: Envelope A caps conducted
  power at **−4 dBm** with the fitted 3.0 dBi antenna, and Envelope B's ceiling is the Wio
  module's own tested **19.6 dBm** (`LRAN-M21-FCC-Grant-Findings` §6). Raised by Bridge
  Implementation Plan v0.13 §2.3.
  **No rail, part, layout or budget decision moves, and that is the point** — every one of
  them gets *more* headroom, not less, because the node transmits in the PA's low-power
  path rather than at its maximum. §3.4 now sizes against 19.6 dBm as the worst permitted
  case and says the operating draw sits well below it. §9.7 keeps the datasheet figures,
  which are useful, and states that both sit above anything this node reaches, so the
  budget is conservative in the node's favour. **M0 accepts at the operating point and
  nothing more** — −4 dBm conducted, the power the node actually uses. A margin run at
  19.6 dBm was considered and dropped as bring-up work that buys little: the rail is sized
  for that power, and testing it there would mean either radiating above the Envelope A
  ceiling or building a dummy-load setup for a case three documented triggers stand in
  front of. **M0 says to re-run it if Envelope B is ever triggered**, which is where the
  question actually becomes live.
  **Naming:** §1's peer list says **Bridge Node** rather than `LoRaBridge`, retired across
  the live set in System PRD v0.11 and amended in **D17**.

- **v0.5** — **§4.3 and §9.5 updated for the confirmed enclosure geometry**, both by dated
  annotation rather than rewriting: the pack and BMS are inside the *same* steel enclosure
  as the node, so the BLE link never crosses a metal wall, and the −50 to −60 dBm reading
  taken in that enclosure is 20–30 dB better than §9.5's premise. §9.5's dated −80 dBm
  observation stands as written; the discrepancy is recorded in Decision Register §2.2, not
  reconciled. **M5 is superseded by M23**, which samples three positions and two
  orientations — a closed steel box holding both ends is a reverberant cavity, so the risk
  is a position-dependent standing-wave null, not attenuation. §4.3's coexistence paragraph
  is expanded: the isolation from the steel wall is good, but the LoRa feedline runs
  *inside* the cavity across three connector pairs alongside the motor drive, the MPPT
  switcher and the loop detector, and that path is why R-4.3h's mutual exclusion is kept
  even on a favourable M23. Requirements source and binding protocol advanced.

- **v0.4** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes **W9** (the full-size and fragmented `PING` bench runs both passed over RF on 2026-09-05) and changes **no frame layout, header field, authentication scope or schema length**; no vector regenerates. **Two things land on this node.** §11's reassembly path has now been exercised over the air at the 15-fragment ceiling, which §11.5 wanted before GateLink depends on it for a `CONFIG_ACK` that crosses the single-frame boundary — and this node has no OTA. Separately, the survey found the strongest near-band neighbour of any site at the gate, −66 dBm at 914.0 MHz, 1 MHz off channel.
- **v0.3** — Citation refresh only. Protocol specification **v0.6 → v0.7**, which captures **D34** (Protocol Spec W12: §9.4 steps 4–5 become `CommandGate` in `/lib/lran-protocol/`, dispatch stays in the application) and changes **no frame layout, header field, authentication scope or schema length**. §4.1's obligations are unchanged; the dedup cache §4.1 assumes now has a named home and a `dedup_cache_depth` parameter in `/lib/lran-config/`.
- **v0.2** — Housekeeping revision; **no change to the build or the firmware
  architecture**. Binding protocol citation moves **v0.2 → v0.6**; §4.1's obligations
  table was checked against v0.3–v0.6 and **nothing was found to conflict**. The
  RadioLib line in §4.1 is now backed by a decision — **D32** — which also makes the
  injected TCXO voltage and DIO2-as-RF-switch settings a day-one requirement of the
  carrier's board config rather than a bring-up discovery, and requires the RadioLib
  version to be **pinned** in this node's `platformio.ini`. Cross-document links
  repaired for the `docs/` reorganization.
- **v0.1** — Initial release. Assembled from `lran-prd-v0_8` §1.4, §1.5, §4.1.2, §4.2,
  §4.4–4.7, §5 (implementation-level content), §8.2/§8.5/§8.7/§8.8, §9.2, §9.3 and §14.
  **Restructured around the build rather than the specification**: BOM, interconnect,
  interface detail, firmware architecture, test plan, milestones and integration
  observations, so that a section can be handed to Claude Code as a coherent unit of work.
  All requirements moved to [`LRAN-GateLink_Node-PRD`](./LRAN-GateLink_Node-PRD.md) and
  referenced by identifier; all frame and enumeration detail replaced by references to
  [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md); decision statuses
  replaced by references to [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md).
  **Added:** §5.2 explicit task structure with priorities and I²C ownership, which v0.8
  implied as a constraint but never laid out; §5.3 module map; §8 nine milestones with
  written acceptance criteria and a stated critical path, replacing v0.8's numbered bring-up
  phases; §7.1 a requirement-to-milestone coverage matrix. **Bench findings and
  measurements relocated here as §9 integration observations**, which is where they belong
  — they are evidence, not specification — including §9.6, a new account of how the BMS
  protocol was actually identified and why the ~3.9 s disconnect signature was the clue.
  **No design change.**
