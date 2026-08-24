# GateLink Expansion Board

**Status:** design frozen for prototype build
**Target:** GateLink node — M5Stack StamPLC + SX1262 LoRa radio + Victron VE.Direct interface
**Revision:** 0.1 (2026-08-24)

---

## 1. Purpose

A hand-built perfboard carrier that mates to the StamPLC's expansion bus and provides:

- An SX1262 LoRa radio (Seeed Wio-SX1262 for XIAO) on the StamPLC's SPI bus
- A bidirectional VE.Direct interface to the Victron MPPT
- Local regulation from the 12 V battery bank, independent of the StamPLC's own 5 V rail

The board consumes the StamPLC's Bus header plus both Grove ports (PORT.A and PORT.C). After this build there are no uncommitted GPIO left on the StamPLC.

---

## 2. Bill of materials

### Modules

| Item | Notes |
|---|---|
| Seeed Wio-SX1262 for XIAO | 2.54 mm headers fitted; XIAO footprint pad labels |
| AP63356-based 12 V → 5 V buck module | Synchronous, wide input |
| AMS1117-3.3 LDO module | 3-pin right-angle header (VI / GND / VO) |
| Hiletgo 4-channel BSS138 level converter | Two channels used (3 and 4); 1 and 2 spare |

### Connectors

| Ref | Item | Function |
|---|---|---|
| J1 | 2×8 2.54 mm **right-angle** header | Direct board-to-board to StamPLC Bus |
| J2 | JST 2.0 4-pin | StamPLC PORT.A |
| J3 | JST 2.0 4-pin | StamPLC PORT.C |
| J4 | JST 2.0 4-pin | VE.Direct to MPPT |
| — | IPEX → SMA bulkhead pigtail | Antenna |

### Discretes

| Ref | Value | Placement |
|---|---|---|
| F1 | 500 mA polyfuse | 12 V entry, first component |
| D1 | P6KE18A (unidirectional) | 12 V rail → GND, after F1. Band/cathode to +12 V |
| C10 | 47 µF / 50 V electrolytic | Buck VIN → GND |
| C11 | 100 nF X7R | Buck VIN → GND |
| C3 | 22 µF / 10 V X5R | AMS1117 VO → GND (LDO loop stability) |
| C4 | 100 µF low-ESR electrolytic | **At the Wio 3V3 pad** |
| C5 | 100 nF X7R | At the Wio 3V3 pad, beside C4 |
| C6 | 100 nF X7R | Converter LV rail → GND |
| C7 | 100 nF X7R | Converter HV rail → GND |
| R2 | 100 Ω | Series with VE.Direct pin 3, HV side of channel 4 |
| R3 | 10 kΩ | Wio D4 (NSS) → 3V3 — see §7.1 |

**Do not add series resistors on SPI or the Wio control lines.** Seeed already fitted 22 Ω on MOSI / MISO / SCK and pull-ups on the board.

Check the buck and AMS1117 modules for existing input/output capacitors before fitting C10, C11 and C3. Any electrolytic on the 12 V input must be rated above 14.6 V absorption — 25 V or better.

---

## 3. Mechanical interconnect

The board mates to the StamPLC **directly via a right-angle 2×8 header**, not a ribbon cable. Two consequences:

**The footprint must be mirrored.** Direct board-to-board mating flips the column order relative to reading the StamPLC's pin table. Verify pin 1 orientation with a continuity check between the seated boards *before* soldering anything to J1. This is the single easiest way to build the board backwards.

**The board is mechanically cantilevered off J1.** Add a standoff or bracket at the far end. A perfboard hanging on header pins alone will fatigue the joints under vibration, and a gate enclosure sees vibration every cycle.

Direct mating is otherwise an improvement over ribbon: shorter SPI runs, lower series inductance on the 12 V feed, no IDC crimp to get wrong.

---

## 4. Power architecture

```
Bus pin 1 (12 V bank)
  → F1 polyfuse 500 mA
  → D1 P6KE18A to GND
  → C10 47 µF ∥ C11 100 nF
  → AP63356 buck VIN

AP63356 VOUT (5 V) ──┬─→ level converter HV rail
                     └─→ AMS1117 VI

AMS1117 VO (3.3 V) ──┬─→ C3 22 µF
                     ├─→ Wio 3V3 pad (C4 100 µF + C5 100 nF local)
                     └─→ level converter LV rail
```

**Bus pin 6 (StamPLC 5 V) is not used.** Everything derives from the bank through local regulation. This deliberately keeps the radio off the rail that drives the relays and opto inputs.

### Rationale

Two-stage buck-then-LDO rather than a single buck. The AP63356 handles the 12 V → 5 V conversion efficiently; the AMS1117 then provides supply rejection at the radio, cleaning up both the buck's switching residue and the motor-induced ripple on the bank. On a node where the gate motors share the battery, "packets drop only while the gate is moving" is the failure mode this avoids.

### Budget

| Quantity | Value |
|---|---|
| Radio TX peak (3.3 V) | ~125 mA |
| AMS1117 dissipation at TX peak | ~0.21 W (comfortable in SOT-223) |
| Buck input current at 12 V, TX peak | ~60–70 mA |
| F1 rating | 500 mA hold — fault protection, not load limiting |

### Measured supply behaviour

Gate motors (2× Apollo Titan 912, dual-leaf, 1050 controller with soft start):

- ~10.5 A peak during ramp to full speed
- ~5.5 A steady at full speed

Estimated rail sag at 10.5 A is under 1 V, leaving ~6 V of margin against the buck's 4.5 V minimum and the StamPLC's 6 V minimum. No brownout risk. Note that a clamp meter under-reads PWM peaks and will not show the reversal transient at all — D1 exists for the latter.

---

## 5. StamPLC bus reference

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | VIN | 2 | GND |
| 3 | GND | 4 | GND |
| 5 | GND | 6 | 5V_OUT |
| 7 | G15 (SCL) | 8 | G13 (SDA) |
| 9 | G3 (PHY_RST) | 10 | G14 (INT) |
| 11 | G9 (MISO) | 12 | G7 (SCK) |
| 13 | G8 (MOSI) | 14 | G11 (CS) |
| 15 | G40 (custom) | 16 | G41 (custom) |

Pins 7–10 belong to the StamPLC's internal I²C bus and the AW9523B / PI4IOE expanders that drive the relays and opto inputs. **Land nothing on them.**

Grove ports: PORT.A = G1 (white), G2 (yellow). PORT.C = G4 (white), G5 (yellow). Red = 5 V on both, unused.

---

## 6. Net list

### Power and ground

| Net | From | To |
|---|---|---|
| GND | Bus 2, 3, 4, 5 | Board ground rail |
| GND | Rail | Buck GND, AMS GND, converter GND (both sides), Wio GND, J2 black, J3 black, J4 pin 1 |
| VBAT_12V | Bus 1 | F1 → D1 / C10 / C11 → buck VIN |
| +5V | Buck VOUT | AMS VI, converter HV rail |
| +3V3 | AMS VO | Wio 3V3 pad, converter LV rail |

### Radio

| Net | StamPLC | Wio pad |
|---|---|---|
| SPI_MISO | Bus 11 (G9) | D9 |
| SPI_SCK | Bus 12 (G7) | D8 |
| SPI_MOSI | Bus 13 (G8) | D10 |
| LORA_RST | Bus 14 (G11) | D2 |
| LORA_BUSY | Bus 15 (G40) | D3 |
| LORA_DIO1 | Bus 16 (G41) | D1 |
| LORA_RF_SW | PORT.A white (G1) | D5 |
| LORA_NSS | PORT.A yellow (G2) | D4 |

### VE.Direct

Named from the **ESP32's** perspective. Victron labels its connector from the MPPT's perspective, so their TX is our RX. Keep this convention everywhere in firmware and comments.

| Net | From | To |
|---|---|---|
| VED_UART_TX | PORT.C yellow (G5) | Converter LV3 |
| — | Converter HV3 | J4 pin 2 (Victron RX) |
| VED_UART_RX | PORT.C white (G4) | Converter LV4 |
| — | Converter HV4 | R2 100 Ω → J4 pin 3 (Victron TX) |
| VED_GND | J4 pin 1 | Board ground rail |

### Not connected

Bus 6, 7, 8, 9, 10 · PORT.A red · PORT.C red · VE.Direct pin 4 (+5 V) · Wio D0 (user button) · Wio 5V/VIN pad · converter channels 1 and 2

---

## 7. Design notes

### 7.1 NSS is on a Grove pigtail

NSS must stay stably asserted for the whole duration of every SPI transaction; a glitch mid-transfer silently aborts the command. RST is pulsed once at init. The routing-driven assignment puts the more fragile signal on the more exposed conductor.

Risk is low — the PORT.A cable is short and NSS is a static level, not a clocked edge — but fit **R3, a 10 kΩ pull-up from D4 to 3V3**, so "deselected" is the failure state if the conductor opens or the pin floats during boot. Check whether Seeed's on-board pull-ups already land on NSS before duplicating.

If a later revision frees up the routing, moving NSS back to Bus 14 and RST to PORT.A is the safer arrangement.

### 7.2 Shared SPI bus

The radio shares SPI with the StamPLC's LCD (CS on G12) and microSD (CS on G10). Separate chip selects make this legal, but firmware must:

- Wrap every access in `SPI.beginTransaction()` / `endTransaction()`
- Defer DIO1 interrupt handling to the main loop, so an SD write cannot be preempted mid-transaction

Treat shared-bus behaviour under load as its own bring-up milestone, not an assumption.

### 7.3 Radio configuration

- **TCXO:** the Wio-SX1262 powers its TCXO from DIO3 at **1.8 V**. Omitting this in `radio.begin()` is the most common "module doesn't respond" failure with this part. The Heltec V3 bridge node uses a different value — this cannot be a shared constant in the protocol library.
- **RF switch:** Seeed does not tie DIO2 to the RF switch internally. Use `setRfSwitchPins(LORA_RF_SW, RADIOLIB_NC)` alongside `DIO2_AS_RF_SWITCH`.
- **Never transmit without the antenna connected.** +22 dBm into an open connector damages the PA.

### 7.4 VE.Direct port is asymmetric

Measured at the MPPT connector:

| Pin | Function | Measured |
|---|---|---|
| 1 | GND | — |
| 2 | RX (into MPPT) | 5.25 V — idles via internal pull-up to the MPPT's 5 V rail |
| 3 | TX (from MPPT) | 3.25 V — 3.3 V logic |
| 4 | V+ | 5.25 V — not used |

Pin 3 is already at 3.3 V and needs no translation; it is routed through a converter channel anyway for uniform treatment of both directions. Pin 2 genuinely requires the converter: driving a 5 V-pulled-up input from a 3.3 V push-pull output causes back-current into the ESP32's ESD clamp. The BSS138's open-drain behaviour handles this correctly — LV low pulls HV low, LV high releases and lets the Victron's own pull-up take the line.

**Both directions now depend on the 5 V rail.** The 1 Hz text frames are the entire MPPT telemetry path and the text protocol is broadcast-only; TX exists solely for HEX register access. If HEX is ever deferred, the converter, the 5 V stage and two nets can be deleted.

At 19200 baud (52 µs bit times) the BSS138's RC edges are irrelevant.

### 7.5 Free telemetry

With Bus pin 1 tied to the bank, the StamPLC's on-board INA226 measures bank voltage and current directly — an independent cross-check against VE.Direct that still works if the MPPT drops off the serial link.

---

## 8. Installation practice

**Star-ground at the battery negative bus bar.** Run the StamPLC's ground directly there; give the 1050 its own conductor. At 10.5 A, 10 mΩ of shared return is 105 mV of common-mode on the VE.Direct pair, during exactly the window gate telemetry matters.

**Antenna routing.** Keep the coax out of any conduit carrying the actuator harness (the 912L-2 second-leaf harness is long). Cross at right angles if crossing is unavoidable. A clamp-on ferrite at the 1050 end of the motor leads is cheap insurance.

**Feed sizing.** Size the 1050's supply for under 3% drop at 10.5 A — roughly 10 AWG at 25 ft one-way, 8 AWG at 40 ft.

---

## 9. Firmware constants

```c
// GateLink expansion board — GPIO assignments (ESP32-S3 / StampS3)
#define LORA_MISO      9    // Bus 11
#define LORA_SCK       7    // Bus 12
#define LORA_MOSI      8    // Bus 13
#define LORA_RST      11    // Bus 14
#define LORA_BUSY     40    // Bus 15
#define LORA_DIO1     41    // Bus 16
#define LORA_RF_SW     1    // PORT.A white
#define LORA_NSS       2    // PORT.A yellow

#define LORA_TCXO_V  1.8f   // Wio-SX1262 TCXO on DIO3 — NOT shared with Heltec V3

// VE.Direct — named from the ESP32's perspective.
// Victron's connector labels are from the MPPT's perspective: their TX is our RX.
#define VED_UART_TX    5    // PORT.C yellow -> conv LV3 -> HV3 -> VE.Direct pin 2
#define VED_UART_RX    4    // PORT.C white  -> conv LV4 -> HV4 -> VE.Direct pin 3
#define VED_BAUD   19200
```

---

## 10. Verify before soldering

- [ ] J1 orientation — continuity-check pin 1 with the boards seated. The right-angle mate mirrors the footprint.
- [ ] Wio header pad mapping — ring out each D-pad to its module pin. The D-number mapping is derived from the Meshtastic variant config plus XIAO ESP32S3 numbering, not from a Seeed pin table.
- [ ] Buck module input capacitor voltage rating — must clear 14.6 V absorption.
- [ ] P6KE18A manufacturer — ST's part specs 32.5 V max clamping vs Vishay/Taiwan Semi's 25.2 V. The AP63356's input limit is 32 V.
- [ ] P6KE18A is the unidirectional "A", not the bidirectional "CA".
- [ ] Whether Seeed's on-board pull-ups already cover NSS (determines whether R3 is fitted).

---

## 11. Bring-up order

1. **Power only.** No Wio seated. Confirm 5 V and 3.3 V at the module pads, then confirm 3.3 V holds with a dummy load.
2. **Radio.** Seat the Wio, antenna connected first. `radio.begin()` with `tcxoVoltage = 1.8` and `setRfSwitchPins()`. Confirm the SX1262 responds before attempting TX.
3. **Shared bus.** Exercise LCD, microSD and radio concurrently. This is where transaction discipline problems surface.
4. **VE.Direct.** Watch for 1 Hz text frames. If nothing arrives, suspect the RX/TX naming convention first.
5. **Noise characterisation.** Log INA226 voltage at 10 Hz plus beacon RSSI/SNR through ten full gate cycles. This single dataset answers the real rail dip, whether the LDO holds, and whether the receiver desenses during ramps — before the enclosure is sealed.

---

## 12. Open questions

- Apollo Titan 912L published current specification not located; measured values in §4 used instead. Worth confirming from the manual for stall current.
- Whether HEX protocol support is in scope for the first firmware release (determines whether §7.4's 5 V branch is load-bearing).
