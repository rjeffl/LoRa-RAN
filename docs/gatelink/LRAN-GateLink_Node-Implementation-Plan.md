# LRAN GateLink Node Implementation Plan

**Document:** `LRAN-GateLink_Node-Implementation-Plan`
**Version:** 0.18
**Node:** `GateLink`, node ID `0x01`
**Firmware target:** `firmware/gatelink/`
**Status:** Reconciled with the built fleet. Four library tasks (§8.1) come before the
firmware starts, and four measurements come before the carrier is populated.
**Requirements source:** [`LRAN-GateLink_Node-PRD`](./LRAN-GateLink_Node-PRD.md) v0.13
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.17**
**Carrier design:** [`gatelink-expansion-board`](./gatelink-expansion-board.md) rev 0.3
**Decision status:** [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md)
**Open document defects:** [`doc-findings`](./doc-findings.md)
**Last updated:** 2026-10-01

> **This document is the basis for hardware build and firmware development, and is what
> is handed to Claude Code for this node.** Requirement identifiers (`R-*`, `G-*`,
> `S-*`, `V-*`) refer to the GateLink PRD.
>
> **The carrier's electrical design lives in `gatelink-expansion-board`, not here.** That
> document owns the carrier's parts, nets, pin map, regulators and bring-up order. This
> plan cites it and does not restate it. Where the two disagreed before v0.18, the
> expansion board was right: it is the later design, and it was revised against the real
> enclosure. Three PRD requirements still state the earlier design, and
> [`doc-findings`](./doc-findings.md) lists them.

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

An M5Stack StamPLC running custom firmware, with a perfboard carrier board mated to its
expansion bus. The carrier holds a Seeed Wio-SX1262 radio module, a 12 V → 5 V buck
followed by a 3.3 V LDO, and a VE.Direct level-shifter front end
([`gatelink-expansion-board`](./gatelink-expansion-board.md) §1). The StamPLC mounts on
DIN rail inside the existing gate controller enclosure, powered directly from the 12 V
LiFePO4 pack, and connects to:

- the gate controller, via four relay outputs and six opto-isolated inputs on screw
  terminals;
- the MPPT 75/15, via a level-shifted VE.Direct UART;
- the battery BMS, over BLE;
- the Bridge Node, over 915 MHz LoRa.

### 1.2 Build sequence at a glance

```
                                                Shared libraries (§8.1)
                                                L1 lran-node · L2 bms-ble
                                                L3 vedirect text · L4 params
                                                        |
  Carrier board        1050 rewiring +          Firmware, on the bench
  (expansion board)    reprogramming            with simulated peripherals
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
in parallel. The library tasks come first on the firmware side, because each one moves
code that the simnode or the bridge already runs. Everything from bench integration
onward is sequential.

The firmware column needs no carrier until M0. The XIAO ESP32S3 + Wio-SX1262 Kit on the
bench carries the same SX1262, TCXO voltage and RF-switch arrangement as the carrier's
module, on different pins (§4.1). A StamPLC with no carrier runs everything else.

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
   budget (§3.3) depends on that, and after the carrier is built the StamPLC has **no
   uncommitted GPIO**.
4. **One implementation of each protocol rule.** The node side of the protocol already
   runs in the simnode, the frame codec in `lib/lran-protocol/`, and media access in
   `lib/lran-link/`. GateLink consumes them (§5.4). A second implementation would drift
   from the first, and the drift would surface at the gate, where a fix is a USB reflash.

---

## 2. Bill of materials

### 2.1 To purchase

| Qty | Item | Notes |
|----:|------|-------|
| 1 | **M5Stack StamPLC (K141)** | Host platform. 4 relays, 8 opto-isolated inputs, 6–36 V in, DIN, screw terminals. ~$43 |
| 1 | **Carrier board, complete** | **[`gatelink-expansion-board`](./gatelink-expansion-board.md) §2 is the carrier's bill of materials**: the Seeed Wio-SX1262 for XIAO header board (p-6379), a SparkFun BabyBuck AP63357, an AMS1117-3.3 module, a 4-channel BSS138 converter, connectors J1–J4 and the discretes. **D27.** Not repeated here, so that one list changes when the design does |
| 1 | IPEX → SMA bulkhead pigtail + 915 MHz antenna | LoRa antenna **outside** the steel enclosure (**R-4.3e**). The expansion board lists the pigtail; PRD §4.3.1 gives the full RF path across both bulkheads |
| *0–1* | *ADuM1201 breakout **or** 74LVC1G17 buffer* | **Only if M4 shows a weak symmetric low-side driver** on the MPPT TX line (§4.2.2) |
| 1 | VE.Direct cable / JST-PH 2.0 4-pin pigtail | **Both data lines used.** Buying a genuine Victron cable and cutting it is the easiest sourcing path |
| 1 | **microSD card** — small, industrial-grade if available | Configuration overrides, retained counters and on-node logging. **Not required for the node to run** (**R-4.2d**) |
| — | Mounting, strain relief, **inline fuse on the battery tap** | Inside the existing enclosure. **S-10** |

### 2.2 The radio module — chosen

**The module is the Seeed Wio-SX1262 for XIAO, header board (p-6379)**
([`gatelink-expansion-board`](./gatelink-expansion-board.md) §2, §6.1). Its FCC grant is
recorded in `LRAN-M21-FCC-Grant-Findings` §2. It meets three of the four points the
earlier selection checklist asked for: it runs from 3.3 V, it states its TCXO voltage
(1.8 V, from DIO3), and it is a 915 MHz part with an IPEX connection.

**It does not meet the fourth.** Seeed does not tie DIO2 to the RF switch inside the
module, so the switch needs a GPIO as well as DIO2-as-RF-switch (expansion board §7.3,
confirmed 2026-09-05). The carrier spends G40 on it. **PRD R-4.3b still says the module
SHALL NOT need that line**, and is listed in `doc-findings`.

**Do not confuse it with the Kit.** The Wio-SX1262 **with** XIAO ESP32S3 (p-5982) joins its
module over a B2B connector on GPIO 38–42 and uses none of the header board's pads. It is
the board the range test and the simnode's `simnode-xiao-wio` env run on. It proves the
module's radio configuration, and it **does not** validate the carrier's wiring.

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
| 1 | **XIAO ESP32S3 + Wio-SX1262 Kit** — in hand | Radio-side firmware development before the carrier exists (§1.2). It is already the bench's target-radio simnode (`docs/bridge/HANDOFF.md`, *Hardware state*); GateLink's development borrows it |
| *1* | *A second StamPLC* | *Optional.* A bench host for everything that does not need the carrier: relays, inputs, display, microSD, BLE. It keeps bench work off the unit destined for the gate once that unit is installed. The StamPLC in hand ran `wattcycle-reader`'s M7a |
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

**The carrier's nets are [`gatelink-expansion-board`](./gatelink-expansion-board.md) §6,
and its firmware constants are §9.** The table below is the firmware's view of rev 0.3.
After this build the StamPLC has no uncommitted GPIO.

| Function | GPIO | StamPLC pin | Note |
|---|---|---|---|
| SX1262 MISO, SCK, MOSI | G9, G7, G8 | Bus 11, 12, 13 | **Shared with the LCD and the microSD** (§5.2) |
| SX1262 NSS | G41 | Bus 16 | R3 pulls it up, so the radio is deselected from power-on |
| SX1262 BUSY | G11 | Bus 14 | The vendor's pin table calls Bus 14 `CS`. It carries BUSY |
| RF switch | G40 | Bus 15 | The **RX enable** in `setRfSwitchPins(rf_sw, RADIOLIB_NC)`, alongside DIO2-as-RF-switch (§2.2) |
| SX1262 DIO1 | G1 | PORT.A white | On a Grove cable. Set `INPUT_PULLDOWN` before `radio.begin()` (§4.1) |
| SX1262 NRESET | G2 | PORT.A yellow | On a Grove cable. R4 holds the radio in reset if the line floats |
| VE.Direct TX (to the MPPT) | G5 | PORT.C yellow | Through BSS138 channel 3 |
| VE.Direct RX (from the MPPT) | G4 | PORT.C white | Through BSS138 channel 4 and R2 |
| I²C, onboard | G15, G13 | Bus 7, 8 | The relay and input expanders. **Land nothing on Bus 7–10** |

**The firmware takes the radio pins as a `lran::link::RadioPins` value**
(`lib/lran-link/include/lran/link/radio_config.h`), declared once in the board profile and
injected into the driver (root rule 10, spec §12.2). The expansion board's `#define`s are
where the values come from, not how the firmware spells them. Its `LORA_TCXO_V 1.8f`
becomes `tcxo_mv = 1800`, because every firmware here keeps the TCXO voltage in
millivolts.

**Reserved — do not use:**

| Signal | Reason |
|---|---|
| **G3** | Common RST for the LCD, the PI4IOE expander **and** any bus expansion module. A radio driver pulsing reset here also resets the display and an expander, **and therefore risks disturbing relay state** (**R-4.3g**). The radio's reset is on G2, which satisfies R-4.3g |
| **G0** | RS485_TX and the ESP32-S3 BOOT strap. Unused; if the RS485 port is ever used, keep the bus idle-high |
| G10, G12, G6 | microSD CS, LCD CS, LCD RS. The radio shares their bus, not their pins |
| G14 | Bus 10, the expanders' shared INT. On the internal I²C side of the header |
| G42 / G43 | PWR-CAN. Unused in v1 |

**Inputs are still polled at `input_poll_ms`** (**R-3.1.4d**). Earlier revisions spent G14
on the radio's reset and polled the inputs to free it. Rev 0.3 put the reset on G2, so the
radio no longer needs G14. Polling stays, because R-3.1.4d requires it and §9.2 gives it
three orders of magnitude of timing margin.

### 3.4 Power rails

**[`gatelink-expansion-board`](./gatelink-expansion-board.md) §4 is the power design.**
The carrier draws from Bus pin 1, which is the 12 V bank, through a polyfuse and a TVS
diode into a buck converter. The buck feeds an LDO. **Bus pin 6 (`EXT_5V`) is not used**,
which keeps the radio off the rail that drives the relays and the opto inputs.

| Rail | Source | Serves |
|---|---|---|
| 12 V | Battery tap, inline fuse → StamPLC VIN; Bus pin 1 → carrier F1 | Host; carrier buck |
| 5 V | SparkFun BabyBuck AP63357 | AMS1117 input; BSS138 HV rail |
| 3.3 V | AMS1117-3.3 module | Wio-SX1262; BSS138 LV rail |

**D26 is half overtaken.** Its finding stands: the StamPLC exposes no 3.3 V rail. Its
consequence does not. D26 excluded the AMS1117 on dropout against `EXT_5V`, which sits near
4.76 V under load. The expansion board feeds the AMS1117 from its own buck instead, so that
comparison no longer describes the build. PRD **R-4.3d**'s ≤300 mV dropout requirement
comes from the same premise. Both are listed in `doc-findings`, and the Decision Register
needs an amendment to D26 before this plan can cite it as closed.

**Load on the 3.3 V rail.** The expansion board budgets the radio at ~125 mA TX peak. That
figure sits above anything either envelope lets this node transmit. Envelope A caps
conducted power at **−4 dBm** with the fitted 3.0 dBi antenna. Envelope B's ceiling is the
Wio module's own tested 19.6 dBm (`LRAN-M21-FCC-Grant-Findings` §6). **M0 accepts at
the operating point only.** If Envelope B is ever triggered, the rail is sized for it but
has not been tested there.

**Undervoltage.** The BabyBuck's floor is 6 V. The expansion board estimates the motors'
10.5 A peak sags the bank by under 1 V, against a nominal 12.8 V (§4, *Measured supply
behaviour*).

**What the INA226 measures is unverified.** Expansion board §7.7 reads the StamPLC's INA226
as the **bank's** voltage and current. This plan's §9.8 and V-11 read it as the **node's**
supply. Which current passes through its shunt decides which reading is right, and whether
the carrier's draw through Bus pin 1 passes through it. **M12 settles it**, at M1, before
V-11 depends on the answer.

### 3.5 Carrier board layout

**The expansion board owns the layout**: §3 for the mechanical mate, §7.6 for the socketed
module, §8 for installation practice, §10 for the checks before soldering and §11 for the
bring-up order. Two of its choices reach the firmware: the shared SPI bus (§5.2) and DIO1 on
a Grove cable (§4.1).

**Mounting does not yet meet R-4.3f.** The PRD requires the carrier to be DIN-mounted, and
D27's remaining sub-item (**M15**) is to pick a DIN carrier and cut the board to it. The
expansion board instead mates the carrier directly to the StamPLC on a right-angle header,
cantilevered, with a standoff at the far end (§3). Either R-4.3f changes or the carrier
gains a DIN mount; `doc-findings` lists it.

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
| BSS138 channels 1–2 | Free, but no GPIO reaches them | — |
| Host GPIO | **None free** after the carrier | — |
| Controller protocol bus port | Untouched | Research-archive Phase 2 |

**A SmartShunt now needs a carrier revision.** Earlier revisions held PORT.C and two
converter channels for its VE.Direct port. Rev 0.3 spends PORT.C on the MPPT, so a second
VE.Direct port has no UART pins to land on.

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

Record both in `docs/gatelink/1050-config.md`. The keyswitch is deliberately *not* part of the
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
| Driver | RadioLib, SX1262, **pinned at the fleet's version** (`jgromes/RadioLib@7.7.1`, **D32**, root rule 9) |
| Pin map / TCXO / RF switch | **Injected by configuration**, never compiled in (spec §12.2). A `lran::link::RadioPins` value: §3.3's pins, `tcxo_mv` 1800, `dio2_as_rf_switch` true, `rf_sw` G40 |
| PHY | `lran::link::kPhy` is the default (spec §12.1, Envelope A). The committed group replaces it at boot and at each retune (spec §12.4, **D56**) |
| EIRP ceiling | `lran::link::within_eirp_ceiling()` runs at compile time on `kPhy`. **R-4.3i also requires it at runtime**, on every configured group, before the radio transmits |
| Media access | `lran::link::MediaAccess` (spec §12.3), the bridge's and the simnode's implementation. CAD before TX; `random(0, backoff_max_ms)` on busy, `cad_retries` attempts, then transmit regardless |
| Address filtering | **None — unavailable in LoRa mode** (spec §12.1, corrected v0.12). `dst` is checked in software at §14 stage 5 |
| Node key | **This node's derived key only** (spec §9.1). It never holds `LRAN_MASTER_KEY`. §6.8 covers how the key reaches the build |

**DIO1 rides a Grove cable** (expansion board §7.1.1). An open conductor does not fail at
`radio.begin()`. It fails later, as transmits that never report completion. The driver
therefore treats a transmit timeout as a counted fault and reads `getIrqStatus()` over SPI
to tell a dead DIO1 line from a dead link. It sets `INPUT_PULLDOWN` on G1 before
`radio.begin()`.

**The driver starts from the simnode's** (`firmware/simnode/src/radio.cpp`). That file
already drives a Wio-SX1262 through the same `RadioPins` shape on the `simnode-xiao-wio`
env. It keeps the RadioLib objects in static storage (root rule 3) and names the RadioLib
traps the bridge's `lora_link.cpp` found. It is a polled loop. GateLink's is a FreeRTOS task
(§5.2), so the bridge's task notification on DIO1 is the pattern for the task side.

### 4.2 VE.Direct — electrical

> **Correction worth keeping visible:** earlier drafts stated VE.Direct is 3.3 V TTL
> requiring no level shifting. **This is incorrect. All Victron MPPTs are 5 V devices.**

**5 V TTL UART, 19200 baud**, on a **JST-PH 2.0 4-pin** connector. Pinout is
vendor-documented, with signal names given from the **device's** perspective:

| Pin | Signal (device POV) | Connection |
|---|---|---|
| 1 | GND | GateLink GND |
| 2 | RX (into MPPT) | BSS138 ch 3 → G5, GateLink TX — **required** for HEX |
| 3 | TX (out of MPPT) | R2 100 Ω → BSS138 ch 4 → G4, GateLink RX — **required** |
| 4 | V+ | **Do not connect** |

Channels and nets are expansion board §6. Firmware names them from the ESP32's side:
`VED_UART_TX` is G5, `VED_UART_RX` is G4.

> **Two statements here disagree, and neither has been withdrawn.** The correction above
> says all Victron MPPTs are 5 V devices. Expansion board §7.4 measured this unit's pin 3
> (TX) idling at **3.25 V** and pin 2 (RX) pulled up to **5.25 V**, and concludes pin 3
> "needs no translation". An idle level does not answer D25's question, which is about the
> **low** level against a 10 kΩ load. **M4 is still the check**, and §4.2.2 stands until it
> runs. `doc-findings` lists the disagreement.

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
- **A refused write answers as spec §7.6's table says** (**D73**):
  `HEX_RSP(REJECTED_UNAUTHENTICATED)` for a bad or missing MAC, and a `COMMAND_ACK` for
  the context, deduplication and `seq` steps. **Every `HEX_RSP` repeats its request's
  `seq`** (**D74**). The simnode's `ROLE_GATELINK` builds both, in `refuse_authenticated()`
  and `on_hex_req()` in `firmware/simnode/src/gatelink.cpp`.

**What is built, and what is not.** `lib/vedirect/` holds the HEX frame codec
(`include/vedirect/hex.h`), Arduino-free and host-tested. The bridge's HEX proxy and the
simnode's simulated MPPT both run it. **The text parser is not built**, and the
line-oriented multiplexer above needs it. Task **L3** (§8.1) adds it to `lib/vedirect/`,
ported from `osh-labs/VE.Direct_mppt_arduino` (MIT) under the repo's conventions: no
`String`, no heap, Arduino-free, fixed buffers. The library's register map and
encode/decode helpers come with the same port where `hex.h` lacks them.

**That library is the reference of record for VE.Direct, not Victron's PDFs.** It has
already decoded both protocols, and it is proven in the field. Take frame layout, register
IDs, scaling and units from its `src/` and `VeDirect_Arduino_Spec.md`. Open Victron's
"VE.Direct Protocol" or "BlueSolar HEX protocol" documents only where the library is
silent, and record that gap in the engineering log. Reading the PDFs from scratch repeats
work already paid for, and yields an interpretation no field unit has run.

#### 4.2.5 If a SmartShunt is added

It presents a second VE.Direct port. The ESP32-S3 has a free UART for it, and the BSS138
has two free channels (1 and 2), but rev 0.3 leaves no GPIO to connect them (§3.6). It is a
carrier revision.

### 4.3 BLE BMS

The pack is a **TDT** BMS advertising as `XDZN_001_xxxx`. **Frame formats, the register
decode and a reference capture are to live in `docs/gatelink/bms-protocol.md`** and are
not duplicated here. **That file does not exist yet.** The write-up is in
`wattcycle-reader/docs/wattcycle-reader-poc_3.md`, and task **L2** (§8.1) lifts it out with
its known-unverified items intact.

**The access sequence — the part nothing else documents:**

1. Write the **`HiLink` handshake to characteristic `FFFA`** — *not* `FFF2`.
2. **Read `FFFA` back** and confirm the `0x01` acknowledgement.
3. **Subscribe to `FFF1`** for notifications.
4. Send requests to **`FFF2`**, using request head **`0x1E`** (`0x7E` is never answered by
   this unit). **All writes are with-response.**

Without step 1, writes to `FFF2` are ATT-acknowledged and then ignored, and the pack drops
the link at ~4 s — **which is exactly what made every earlier probe look like a wrong
protocol** (§9.6).

**Implementation notes:**

- **Stack:** NimBLE-Arduino — materially smaller flash and RAM footprint than Bluedroid.
  ~200–300 KB of flash, comfortable in 8 MB.
- **MTU.** Reference decodes were obtained at a negotiated MTU of **512**, with responses
  arriving unfragmented. **NimBLE defaults lower**; the client must either request a
  larger MTU or implement reassembly.
- **The protocol layer exists, and has run on this host.** `wattcycle-reader/lib/bms_ble/`
  holds `TdtProtocol` (CRC, frame build, reassembly, decode into `BmsData`), the abstract
  `BmsTransport` and its one NimBLE implementation. Its 21 host tests run against captured
  frames, and its M7 poll loop ran on a StamPLC with no change under `lib/bms_ble/`
  (`wattcycle-reader/README.md`). Task **L2** moves it to `lib/bms-ble/`.
- **The client around it is new work.** The PoC holds one connection open and polls on an
  interval. **R-3.4a/R-3.4b** require connect, read, disconnect and BLE controller de-init
  on every `bms_poll_s`. The PoC's `src/main.cpp` is its wiring, not GateLink's. GateLink's
  `bms_task` (§5.2) is written against the PRD and reuses only `lib/bms-ble/`.
- **`0x8D` (alarms) is not decoded**, deliberately. Mapping its bitmaps needs a capture
  taken during a real protection event. **M7** (the `pack_ma` sign) is the protocol's
  other open item.
- **Cell-voltage jitter.** Readings move 1–2 mV between polls from ADC noise. **The
  bridge** rounds or publishes on change; the node transmits what it read.

**Antenna (D28).** The Stamp-S3A's 2.4 GHz antenna is **internal to the DIN case with no
external option**, unlike the LoRa side. **Measure RSSI at the final mounting position
before committing — now M23, which supersedes M5.** §9.6 records why this was a live
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
| Build | **PlatformIO**, `espressif32@6.13.0` (the fleet's pin), board `esp32-s3-devkitc-1` | — |
| Board support | **`m5stack/M5StamPLC`**, pulling in M5Unified and M5GFX, **pinned exactly**. Wrapped by a firmware-local board layer (§5.3), not a shared library | MIT |
| LCD | M5GFX via M5StamPLC | MIT |
| LoRa | **RadioLib 7.7.1**, pinned (**D32**). CAD for media access | MIT |
| VE.Direct | **`lib/vedirect/`**: the HEX codec is built; the text parser is task **L3** | MIT |
| BLE | **NimBLE-Arduino**, pinned exactly at the version `wattcycle-reader` proved | Apache-2.0 |
| BMS protocol | **`lib/bms-ble/`**, moved from `wattcycle-reader/lib/bms_ble/` by task **L2** | MIT |
| Node protocol | **`lib/lran-node/`**, extracted from the simnode by task **L1** (§5.4) | MIT |
| Codec, MAC, `CommandGate` | **`lib/lran-protocol/`**, with `platform/esp32/` for the mbedTLS HMAC | MIT; mbedTLS Apache-2.0 |
| Media access, PHY, pins | **`lib/lran-link/`** | MIT |
| Parameters | **`lib/lran-config/`**; GateLink's block is task **L4** | MIT |
| Gate controller | **No protocol library.** Debounced reads from the input expander, timed pulses to the relay expander | — |

**Start the environment from `wattcycle-reader`'s `m5stack_stamplc`**, which has run on this
board, and bring it to the fleet's rules. Four changes are needed:

1. **Pin every version exactly.** The PoC floats `espressif32@^6.9.0`, `NimBLE-Arduino@^1.4.2`
   and `M5StamPLC@^1.2.0`. Root rule 9 pins RadioLib, and the bridge's `platformio.ini`
   gives the reason for pinning everything else. The PoC also found the published
   M5StamPLC package drifting from its GitHub `main`. Read the **installed** headers.
2. **Keep `-DARDUINO_USB_CDC_ON_BOOT=1`.** The StamPLC has no USB-UART bridge. Without the
   flag, `Serial` binds to unconnected UART0 pins and every print is lost while boot ROM
   lines still appear. Under that flag RadioLib 7.7.1 emits an unconditional `#warning`.
   **Add `-Wno-error=cpp` in this one environment**, with the reason written at the flag,
   as the simnode's `simnode-xiao-wio` env does (root *Style*).
3. **Add `-std=gnu++17 -Wall -Wextra -Werror`**, the `lib_extra_dirs = ../../lib` reach, and
   the `platform/esp32/` source filter, as the simnode does.
4. **Add a `native` environment** whose `build_src_filter` lists the Arduino-free
   translation units by name, as the bridge's and the simnode's do. That list is the seam:
   a file crossing it is a visible edit in review.

**Commit a partition table**, as the bridge does. GateLink has no OTA, so it needs one app
partition and no `otadata`. A platform bump that changed the board definition's default
would otherwise change the layout with no commit to blame.

**Stamp the build into the boot banner.** The bridge's `scripts/version.py` writes the
release and the git commit into the image. At the gate, that banner is how anyone tells
which image is running, and a `-dirty` field means it matches no commit.

### 5.2 Task structure

The scheduling requirement (**R-5.2a**) is the shape of this design: **the I/O service
must never be starved.**

| Task | Period / trigger | Priority | Owns |
|---|---|---|---|
| **`io_task`** | `input_poll_ms` (100) | **Highest** | Input expander polling, debounce, relay pulse timing. **The only task that touches the AW9523B** |
| `vedirect_task` | UART RX event | High | Line-oriented parser; text cache; HEX transaction state machine |
| `lora_task` | DIO1 notification / TX queue | High | The radio driver, CAD, backoff, frame serialize/deserialize, MAC, and `lran-node`'s receive path |
| `app_task` | 100 ms tick | Normal | State derivation, hold tracking, direction classifier, trigger logic, status assembly |
| `bms_task` | `bms_poll_s` (300) | Low | NimBLE connect / read / disconnect, then **de-init the controller** |
| `ui_task` | 100 ms tick | Low | LCD pages, buttons, backlight timeout, buzzer |
| `log_task` | queue | Lowest | Leveled serial + rotating microSD log |

NimBLE also runs its own host task while the controller is up. Its notify callback runs
there, so `lib/bms-ble/`'s reassembler is guarded the way `wattcycle-reader`'s
`BmsNotifyHandler` guards it.

**Rules:**

- `io_task` owns the I²C bus for relay and input access. Any other task needing the
  expander goes through a call that queues to `io_task`. **A relay pulse whose trailing
  edge is late is a command of the wrong length.**
- **`io_task` never blocks on anything but its own period.** The bridge enforces the same
  property for its `lora_task` with `tools/checks/lora_task_never_blocks.py`. GateLink
  needs the equivalent check for `io_task`.
- **One lock guards the SPI bus.** The radio, the LCD (CS G12) and the microSD (CS G10)
  share G7/G8/G9 (expansion board §7.2). `lora_task`, `ui_task` and `log_task` all reach
  it. Every access takes the lock, and DIO1 only notifies `lora_task`; it never does SPI
  work in the interrupt. **The three drivers may not share a bus host cleanly.** RadioLib
  takes an Arduino `SPIClass`, the SD library another, and M5GFX may drive the bus through
  ESP-IDF directly. Proving they coexist is M1's first item, before any task design
  depends on it.
- **One interlock serializes LoRa transmit and BLE activity** (**R-4.3h**). `bms_task`
  holds it from connect to controller de-init, and `lora_task` takes it before each
  transmit. **The interlock delays answers.** A `COMMAND_ACK` due during a BLE window waits
  for it, and the bridge's ACK timeout is 3 s. The BLE window therefore needs a bound, or a
  pending transmit must cut it short. Decide which at M3, with the window measured at M5.
- `bms_task` runs at low priority and its failures are non-blocking (**R-3.4d**).
- No task blocks on the LoRa transmit path; frames are queued.
- Watchdog fed from `app_task`, not from `io_task` — a stalled application must not be
  masked by a healthy I/O loop.
- **`CommandGate::check()` runs in the receive path, before dispatch; `record()` runs
  after execution, and the `COMMAND_ACK` goes out after `record()`.** In the simnode both
  calls run on one loop. Here the task split puts an execution window between them:
  `lora_task` keeps receiving while `io_task` pulses, and a bridge retry landing in it gets
  `InFlight` and no answer (Protocol Spec §9.4, D34 amended 2026-09-11). The gate holds no
  lock, so the two calls are serialized: post the result back to the task that owns the
  gate, or guard it. `lran-node` (L1) must expose `check()` and `record()` as separate
  steps for this reason. **Open:** whether the ACK waits for the pulse to complete or for
  the gate to confirm movement (`command_confirm_timeout_s`, 5 s). It sets how often the
  window is hit against the bridge's 3 s ACK timeout. **Decide it before M3.**
- **`ROLL_CONTEXT` bypasses `CommandGate::check()`** (Protocol Spec §9.4, §10.6,
  **D58**, PRD R-3.5e). While any entry is in flight, GateLink answers `ACTUATOR_BUSY`
  and changes nothing. Otherwise it takes a new random `ctx_id`, calls
  `reset_context()`, resets its status `seq`, and ACKs under the new `ctx_id`. The
  in-flight check runs on the task that owns the gate, for the reason `record()` does. The
  simnode built this as BF-34 (`firmware/simnode/CLAUDE.md`), and L1 moves it.

### 5.3 Module map

```
firmware/gatelink/
  platformio.ini          gatelink (StamPLC + carrier), native              §5.1
  partitions.csv          committed; one app partition, no OTA
  scripts/version.py      release + git commit into the boot banner (from the bridge)
  CLAUDE.md               subproject context for Claude Code
  src/
    main.cpp              boot sequence, task creation; the ONLY file that includes secrets.h
    board_profile.h       RadioPins, VE.Direct pins, chip selects - data only    §3.3
    board_stamplc.cpp     relays, inputs, backlight, buttons, buzzer,
                          INA226, LM75, RTC, SD - over M5StamPLC            [Arduino]
    radio.cpp             RadioLib driver, from the simnode's; the ONLY
                          file that includes RadioLib                       [Arduino]
    spi_bus.cpp           the one SPI lock                                  [Arduino]
    sd_persist.cpp        lran::config::Persist on microSD (D49)            [Arduino]
    bms_client.cpp        connect / read / disconnect / de-init             [Arduino]
    task_runtime.cpp      FreeRTOS tasks, queues, watchdog                  [Arduino]
    --- Arduino-free, listed in the native env's build_src_filter ---
    tasks.cpp             task table, priorities, queue depths
    gate_io.cpp           debounce, pulse sequencing; time passed in        [io_task]
    gate_state.cpp        IN1/IN2 -> state; hold derivation + source        [app_task]
    detect.cpp            direction classifier state machine                [app_task]
    cause.cpp             movement-cause inference                          [app_task]
    triggers.cpp          when to push status / event                       [app_task]
    status.cpp            fills lran-protocol's schema 0x10 / 0x11 / 0xF0   [app_task]
    commands.cpp          GateLink's lran-node application: command ->
                          relay sequence, dry-run                           [app_task]
    interlock.cpp         R-4.3h LoRa/BLE interlock policy
    vedirect_mux.cpp      line multiplexer, one HEX transaction at a time   [vedirect_task]
    ui_pages.cpp          page text, as the simnode's oled_page             [ui_task]
    debug.cpp             dry-run, injection, loopback, dummy push
  test/                   one Unity suite per Arduino-free unit
  lib deps ->
    /lib/lran-protocol/   framing, addressing, HMAC, CRC, fragmentation, CommandGate, schemas
    /lib/lran-link/       media access, PHY config, radio pin shape
    /lib/lran-config/     parameter table, Store, PHY blob
    /lib/lran-node/       node-side protocol engine                         L1
    /lib/vedirect/        HEX (built) + text (L3)
    /lib/bms-ble/         TDT protocol and transport seam                   L2
```

**There is no `/lib/lran-platform/` yet, and System PRD §3.5 requires one.** §3.5 says the
library SHALL abstract the StamPLC so that GateLink and AquaLink, a water-system controller
project outside this repository, compile against one HAL. No firmware in this repository
uses such a library: the bridge and the simnode were built on Heltec and XIAO boards without
one, and AquaLink's code is not here to share it. **The operator chose a firmware-local
board layer on 2026-10-01**, with §5.3's `board_stamplc.cpp` written so it can move to
`/lib/` unchanged when a second StamPLC firmware is built here. That choice contradicts
§3.5's SHALL, and `doc-findings` lists it for the System PRD.

§3.5's other SHALL, one SX1262 driver injected with its pins, holds for the injection and
not for the driver. `RadioPins` is injected everywhere, but the bridge, the simnode and the
range test each wrap RadioLib in their own driver, and the simnode's `radio.cpp` says why.
GateLink adds a fourth (§4.1).

`docs/gatelink/engineering-log.md` carries the dated running record — what was tried,
measured, decided and why. Neither this plan nor the PRD is the right place for "tried X
on the bench, it did not work because Y," and that is exactly the information most
expensive to lose. It does not exist yet; the first GateLink session creates it, along
with `docs/gatelink/HANDOFF.md` (root *Workflow*).

### 5.4 What GateLink reuses

**Most of the protocol this node speaks is already built and running.** This table is the
starting point for each concern, so a session reads the existing code before writing new
code.

| Concern | Built in | State |
|---|---|---|
| Frame codec, MAC, CRC, reassembly, counters, `CommandGate` | `lib/lran-protocol/` | Built (Library P1–P8); 72 W4 vectors pass on host and target |
| Schemas `0x10`, `0x11`, `0x12`, `0xF0` | `lib/lran-protocol/include/lran/schema/` | Built: `gatelink_status_v1`, `gatelink_event_v1`, `node_config_v1`, `node_health_v1` |
| HMAC on target | `lib/lran-protocol/platform/esp32/` | Built. GateLink uses HMAC only; it never derives a key (spec §9.1) |
| Media access, `PhyConfig`, `RadioPins`, RX arrival | `lib/lran-link/` | Built, shared by the bridge and the simnode |
| Parameter table, `Store`, PHY blob | `lib/lran-config/` | Built. **GateLink's `0x1000` block is not declared** (L4) |
| Node receive path, context, roll, reboot and `BOOT`, config readback, PHY trial, HEX gating | `firmware/simnode/src/node.cpp`, `gatelink.cpp`, `phy_trial.cpp` | Built and on air in `ROLE_GATELINK`. **Simnode-only until L1** |
| SX1262 driver, Wio-SX1262 configuration | `firmware/simnode/src/radio.cpp`; bridge `lora_link.cpp` | Reference to copy and adapt (§4.1) |
| VE.Direct HEX codec | `lib/vedirect/` | Built. Text parser is L3 |
| TDT BMS protocol | `wattcycle-reader/lib/bms_ble/` | Built and run on a StamPLC. Moves in L2 |
| StamPLC build environment and TFT page | `wattcycle-reader/platformio.ini`, `src/TftDisplay.cpp` | Reference (§5.1, §6.5) |
| GateLink's HA entities | the bridge's `discovery.cpp`, `tools/ha/`, `ha/discovery/` | Built on the bridge side. GateLink's configuration `number` entities follow L4 |
| A peer to test against | simnode `ROLE_GATELINK`, the bridge's GateLink simulator, `tools/simctl/` | Built. The bridge is the far end of every GateLink bench test |

**Task L1's boundary.** `lran-node` takes everything in the simnode's `Node` that the
specification decides: the receive ladder after the codec, the context and its roll,
`CommandGate`'s two calls, the reboot and `BOOT` announcement, the `CONFIG` path through
`lran-config`'s `Store`, §12.4.2's PHY trial and the HEX write gate. The simnode keeps its
identity table, faults, console and invented telemetry. GateLink supplies an application
interface: execute a command and report when it finishes, produce a status snapshot, and
carry a HEX request to the MPPT. **The simnode must keep passing every test it passes
today**, and it is on air with `ROLE_GATELINK` again before L1 closes. That is the evidence
the extraction changed nothing.

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
discovery payloads and `docs/gatelink/gatelink-config.md` are all **derived from it by code**
(**D44**). GateLink's parameters take `param_id`s from `0x1000`–`0x1FFF`, and the ones
every node holds from `0x0100`–`0x01FF` (Protocol Spec §7.4, **D46**). Three hand-maintained
copies drift, silently: HA offers a range the firmware clamps, or documentation describes
a default that changed two revisions ago.

**GateLink's block is not declared yet.** `lib/lran-config/include/lran/config/table.h`
holds the bridge's rows and the rows every node holds, and leaves `0x1000`–`0x1FFF` to this
milestone. Task **L4** (§8.1) declares it from the PRD's parameter list and this plan's
§4.4, with the bridge's discovery output and the documentation check following. **A name
in that table is permanent** once HA publishes it (spec §16.7). Settle `doc-findings`
finding 2 first: the PRD's `tx_conducted_dbm` and the library plan's `tx_power_dbm` are one
parameter.

**The `Persist` implementation is microSD** (**D49**, **R-4.2c**). `lran-config`'s `Store`
takes a `Persist*` that may be null, and answers `APPLIED_NOT_PERSISTED` when it is
unusable (spec §8.11). The bridge's `Persist` is NVS and is the pattern to follow;
GateLink's writes files.

**A full readback may span several `CONFIG_ACK` messages** (Protocol Spec §7.4.1, **D57**),
and the six PHY rows change only through §12.4's commit-and-revert (**D56**), `READ_ONLY`
until it is built. **Spec §12.4.2 (D59) is GateLink's half**: it answers on the old
settings, retunes, and commits only on an authenticated frame received on the new ones. It
refuses the group while the microSD is unusable, and it sends `EVENT` `PHY_REVERTED` after
a revert.

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
  state + MPPT + battery SOC + link quality. The colour panel fits materially more per
  page than a small monochrome OLED would.
- **The panel is landscape, 240×135**, as M5GFX sets it up (`rotation = 1`), not the
  135×240 its datasheet name suggests. `wattcycle-reader`'s `TftDisplay` lays rows out as
  fractions of `Display.height()` for this reason, and it is the starting point.
- **Page text is Arduino-free and host-tested**, drawing is not. The simnode splits its OLED
  the same way (`oled_page.cpp` and `ui.cpp`).
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
| **Device simulators** | A VE.Direct frame generator covering **both text and HEX**; a dummy BMS BLE peripheral. **Neither exists.** The simnode's `sim_mppt` answers HEX only, inside the simnode, and does not drive a UART |
| **The bridge as the far end** | Every bench test of the LoRa side runs against the real bridge. Its frame log, counters and `config/ack` topics are the instruments. The simnode's `ROLE_GATELINK` gives a known-good node to compare GateLink's behaviour against, frame for frame |
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
   SmartShunt route is unattractive. Under rev 0.3 a SmartShunt also needs a carrier
   revision (§3.6).
3. The pin budget breaks — a future requirement needs pins §3.3 has already spent.

### 6.8 The node key

**GateLink is the first firmware that holds a derived key and not the master.** The bridge
holds `LRAN_MASTER_KEY` and derives on demand. The simnode holds it too, for bench
convenience, and Bridge Impl Plan §10.3 says a real node never does. GateLink
holds only `node_key` for `0x01`, so a GateLink in the wrong hands yields that key alone.

**Nothing provisions that key yet.** `secrets.h.example` documents `LRAN_MASTER_KEY` and has
no field for a node's derived key. Before M3, three things are needed:

1. A field in `secrets.h.example` for GateLink's derived key, documented as the template's
   other fields are, so CI builds against the committed template unchanged.
2. A host tool that derives the key from the master per spec §9.1. It reuses the HKDF that
   `tools/vectors/` already cross-checks, and prints the value for the operator to paste.
   The tool never writes the key into a tracked file, and it never prints the master.
3. A boot check, as the simnode's: a build carrying the template's value says so on the
   banner and the display, so a node that cannot authenticate is never mistaken for a
   provisioned one.

Root *Secrets* governs all three: the key is never committed, echoed into a log or pasted
into a document.

---

## 7. Test and verification plan

### 7.1 Coverage matrix

| Requirement | Verified by | Stage |
|---|---|---|
| V-1 radio on carrier | Bench, expansion board §11 steps 1–4, then a RadioLib link to the bridge | M0 |
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

### 7.5 Host tests and CI

**Every Arduino-free unit has a Unity suite**, and the `native` environment builds the
shared libraries from this project as well as its own units. The bridge's `native` env
states why: a library that compiles only for the ESP32-S3 has acquired a platform
dependency, and the host build is where that shows.

**CI does not see a new firmware until ci.yml names it.** Its `changes` filter already
covers `lib/` and `firmware/`, but each suite and each target is a named step:

- a `pio test -d firmware/gatelink -e native` step in the `native` job;
- a `firmware/gatelink` row in the `firmware` job's matrix;
- one `pio test -d lib/<lib> -e native` step for each new library: `lran-node` and
  `bms-ble`. Each needs its own `platformio.ini`, as `lib/vedirect/` has.

`python3 tools/checks/run_ci_local.py` reads ci.yml, so it picks these up with no change of
its own. The new `io_task` check (§5.2) joins the `checks` job.

### 7.4 Controller bring-up procedure

Ordered, and safe to perform incrementally. **No LRAN hardware is required for steps
1–6**, which is why they run in parallel with carrier and firmware work.

1. **Rewire the keyswitch and pushbutton** (§3.1.1). Remove the series link; keyswitch to
   AUX1, pushbutton to AUX2. **Do this before reprogramming the AUX terminals**, so there
   is no window in which an unsecured pushbutton can assert OPEN+LOCK.
2. **Reprogram the controller.** AUX1 → OPEN and LOCK; AUX2 → UNLOCK; OUT1 → OPEN;
   OUT2 → MOVING. Record all settings, including the 60 s auto-close and standby timeouts,
   in `docs/gatelink/1050-config.md`.
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

### 8.1 Library and provisioning tasks — before M1

These move code the fleet already runs, or add what no document has assigned. Each is
one branch and one session, and each leaves every existing suite passing.

| # | Task | Acceptance criteria |
|---|---|---|
| **L1** | **Extract `lib/lran-node/`** from the simnode (§5.4) | The library builds and tests in `native`, Arduino-free. The simnode consumes it and passes every suite it passes today. `ROLE_GATELINK` is back on air against the bridge, and the BF-34 roll, the §10.7 reboot and the §12.4.2 PHY trial each pass their bench check again. `check()` and `record()` are separate calls (§5.2) |
| **L2** | **Move `bms_ble` to `lib/bms-ble/`**, and write `docs/gatelink/bms-protocol.md` | Repo conventions applied: `snake_case` files, license headers, `-Werror`, NimBLE pinned exactly. The 21 host tests pass in a `native` env of the library's own, and CI runs them. `wattcycle-reader` builds against the moved library, or is frozen with a note that says so. The protocol write-up keeps every known-unverified item, including the `pack_ma` sign (**M7**, **W6**) and `0x8D` |
| **L3** | **VE.Direct text parser** in `lib/vedirect/`, ported from osh-labs | Parses every field of a captured MPPT 75/15 text block, checksum included. Rejects a bad checksum and counts it. Interleaved HEX lines pass through to the HEX codec. No heap, Arduino-free |
| **L4** | **GateLink's parameter block** in `lib/lran-config/` | `0x1000`–`0x1FFF` rows declared from the PRD and §4.4, `doc-findings` finding 2 settled first. The bridge's discovery output and `docs/gatelink/gatelink-config.md` derived from the table and checked |
| **L5** | **Node key provisioning** (§6.8) | The `secrets.h.example` field, the host tool and the boot check. CI builds against the template |
| **L6** | **`firmware/gatelink/` skeleton** | §5.1's `platformio.ini`, partition table and version stamp; §5.3's layout; the `native` env and its CI rows (§7.5). Boots on a bare StamPLC, prints its banner and starts its tasks with stub bodies |

L1 and L2 are the long ones. L3, L4 and L5 are independent of each other and of L1.

### 8.2 Node milestones

> **These identifiers collide with the Decision Register's measurements.** Milestone M4 is
> VE.Direct and measurement M4 is the MPPT's TX low level; milestone M0 depends on
> measurement M4. In this table **"measurement M*n*"** always says so. `doc-findings` lists
> the collision, because renaming the milestones touches documents that cite them.

| # | Milestone | Depends on | Acceptance criteria |
|---|---|---|---|
| **M0** | **Carrier board bring-up** | Carrier BOM in hand; **measurement M4** settled; expansion board §10's checks ticked | Expansion board §11 steps 1–4 pass in order: power with no module seated, radio, DIO1 continuity, shared bus. The 3.3 V rail holds ≥3.2 V through SX1262 TX **at the D33 ceiling, −4 dBm conducted** — the power this node operates at. **If Envelope B is ever triggered, re-run this at the power it allows** (§3.4); the rail is sized for it but untested there. RadioLib initialises the radio from §3.3's `RadioPins`: TCXO 1.8 V, DIO2-as-RF-switch **and** `setRfSwitchPins(G40, NC)`. The IRQ is seen to fire on the first transmit, not inferred. Ping and loopback with the bridge succeed on the bench. **Failure here is D30 trigger 1** |
| **M1** | **Board layer** | L6; a StamPLC | **The LCD, the microSD and an SPI peripheral standing in for the radio work concurrently under the one lock** (§5.2), or the plan changes before anything builds on it. Relays pulse to a measured width within ±10 ms at the configured value; inputs read and debounce correctly against a bench switch; LCD, buttons, buzzer, INA226, LM75, RTC and SD all accessible through the board layer. **Every relay output stays off on a scope through a power cycle, a watchdog reset and a brownout** (PRD R-3.5j), with `M5StamPLC`'s own initialisation included. **Measurement M12** says which current the INA226 sees (§3.4) |
| **M2** | **Controller rewire, reprogram and manual validation** | Nothing — runs in parallel | §7.4 steps 1–6 complete. `docs/gatelink/1050-config.md` written. **Measurements M1, M2, M3 and M8 captured.** The §3.2 state table confirmed by DVM through real cycles, including the handheld remote's OPEN+LOCK |
| **M3** | **Protocol, framing and configuration on the bench** | M0, M1, L1, L4, L5 | The ACK-timing question and the BLE-window bound (§5.2) are decided and recorded. Frames serialize and deserialize against the committed test vectors. MAC, sequence, context resync, the context roll after a bridge restart (Protocol Spec §10.6) and command dedup all verified. **A reset of each cause the bench can produce is verified against spec §10.7**: the ACK before a `REBOOT`, a `BOOT` event with its reset cause, no repeated `ctx_id`, active alarms sent again, and the radio reset at boot (PRD R-3.5f–R-3.5k). **`simnode` runs alongside**, validating addressing, per-node keying, availability watchdog, fragmentation and CAD/backoff. Direction classification passes injection including **30 s gaps and partial traversals**. Held-open alert fires on the first edge for all four hold sources. **Configuration round-trip passes with a card and again with the card removed**, reporting honestly in both cases |
| **M4** | **VE.Direct** | M0, L3, **measurement M4** | Translator selected per D25. All documented text fields parse from a real MPPT 75/15. **HEX round-trip proven** — request out, response in, correlated. Write rejected when unauthenticated, and rejected by the bridge when disarmed. Staleness flag asserts when the stream stops. The bridge's register readback (BF-30) agrees with the real MPPT, which B6 waits on. §9.8 baseline log started (**measurement M14**) |
| **M5** | **Battery and BMS** | M1, L2 | `bms_task` runs connect, read, disconnect and controller de-init on `bms_poll_s`, and its window is measured for §5.2's interlock. The client decodes the live pack in agreement with the reference implementation. **BLE RSSI measured from the intended mounting position (measurement M23, D28)** and judged adequate — or a fallback selected. MPPT reconfigured for LiFePO4 and verified by readback. Low-temperature inhibition detection validated by both paths. **Pack current captured under charge and under load (measurement M7)**, settling the sign convention |
| **M6** | **Inputs live, read-only** | M2, M3 | Relays physically disconnected. State derivation, hold detection, detection and direction all confirmed against real gate cycles driven by the keypad and the remote. `hold_confirm_ms` demonstrably rejects the transient 1/1 at the start of a close. **The gate cannot be moved by GateLink in this phase** |
| **M7** | **Relays live** | M6 | Dry-run first: every command path exercised from HA, logged intent matching expectation. **Both manual UNLOCK paths confirmed working.** Then dry-run disabled and each command tested with a clear line of sight |
| **M8** | **HA integration** | M3–M7 | Discovery publishes one device per node with correct availability. Command round-trip works end to end. All §7.2 entities present and populated. **Held-open and FIRE events verified to fire exactly once and not replay on HA restart or discovery refresh.** Configuration `number` entities read and write |
| **M9** | **Field soak** | M8 | Installed. Measured daily consumption from the INA226 compared against budget. Overnight ΔSOC and days-since-full tracked. **Seasonal enclosure-temperature log begun across all three sensors (D29).** Error paths exercised: link loss, BLE failure, VE.Direct stall, microSD removal |

**Critical path:** L1 → M3 → M6 → M7 → M8 → M9, with M0 and M1 joining at M3. L1 is the
longest library task and nothing on the node's protocol side starts without it. M2 runs in
parallel from the start. M4 and M5 are parallel to M6 once M0 and M1 land.

**The bridge waits on this table.** Its B6 needs GateLink M6, and its B7 follows
(`docs/bridge/HANDOFF.md`). Both are the bridge's acceptance, not GateLink's, and the bridge
tasks them.

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

- **v0.18** — **Reconciled with the fleet as built**, which earlier revisions never were:
  their citations moved with the specification while their architecture stayed where v0.1
  left it. **The carrier is now `gatelink-expansion-board` rev 0.3**, which this plan had
  never cited. §2–§3 defer to it: a Wio-SX1262 that needs an RF-switch GPIO, a buck and
  an AMS1117 fed from Bus pin 1 instead of an LDO on `EXT_5V`, VE.Direct on PORT.C, and no
  free GPIO. §4.2 keeps the 5 V statement and the expansion board's 3.25 V measurement side
  by side, unresolved, with M4 as the check. **§5 is rewritten against the built code**:
  the fleet's pinned versions, a StamPLC environment taken from `wattcycle-reader`, a shared
  SPI lock, the R-4.3h interlock and the delay it puts on an ACK, a firmware-local board
  layer in place of `/lib/lran-platform/`, and §5.4's map of what each concern reuses.
  **New work no document had assigned**: L1 extracts `lib/lran-node/` from the simnode, L2
  moves `bms_ble` to `lib/bms-ble/` and writes `bms-protocol.md`, L3 builds the VE.Direct
  text parser, L4 declares GateLink's parameter block, L5 provisions the node key (§6.8) and
  L6 creates the firmware skeleton (§8.1). M0, M1, M3, M4 and M5 take the new dependencies.
  Eight defects found on the way are listed in `doc-findings` rather than
  fixed here. Paths for `bms-protocol.md`, `1050-config.md` and `gatelink-config.md` move
  under `docs/gatelink/`, and two references to §9.5 that meant §9.6 are corrected.

- **v0.17** — **Protocol specification v0.16 → v0.17, and PRD v0.12 → v0.13.** §4.2.4's
  HEX rules take **D73** and **D74**, PRD R-3.3e's two additions. No milestone changes.

- **v0.16** — **Protocol specification v0.15 → v0.16, and PRD v0.11 → v0.12.** M1's
  acceptance gains the relays held off through a reset (PRD R-3.5j). M3's gains the reset
  behaviour of spec §10.7 (PRD R-3.5f–R-3.5k). No build step changes yet; the milestones
  are where the requirements are checked.

- **v0.15** — **§4.2.4 names `osh-labs/VE.Direct_mppt_arduino` the reference of record for
  VE.Direct**, over Victron's PDFs, which serve only where the library is silent. The
  library already decodes both protocols and is proven in the field.
- **v0.14** — **Protocol specification v0.14 → v0.15.** Nothing in the body changes. What
  reaches the build when it starts: each `CONFIG_ACK` result sets `OVERRIDE` for an
  override (spec §7.4, **D68**), `RESTORE_DEFAULTS` keeps the committed PHY group (§8.10,
  **D60**), and a configuration change no `CONFIG_ACK` reported is sent as `CONFIG_CHANGE`
  (§8.7, **D69**). The PRD citation moves from v0.10 to v0.11.

- **v0.13** — **Header citation reconciled**: the PRD moves from v0.9 to v0.10. Nothing in
  the body changes. The PRD's v0.10 adds D59's three points to §5.3.1, and §6.4 took all
  three in this plan's v0.12. The PRD also records that **W17** stays open until after
  GateLink deploys, which asks nothing of the build.

- **v0.12** — **Protocol specification v0.13 → v0.14.** §6.4 names spec §12.4.2 (**D59**)
  as GateLink's half of a PHY change, including the refusal without a usable microSD and
  `EVENT` `PHY_REVERTED`.

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
