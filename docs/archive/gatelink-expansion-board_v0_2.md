# GateLink Expansion Board

**Status:** design frozen for prototype build
**Target:** GateLink node — M5Stack StamPLC + SX1262 LoRa radio + Victron VE.Direct interface
**Revision:** 0.2 (2026-08-26)

> **Changes from 0.1:** NSS and RST swapped (§6, §7.1, §9); R4 RST pull-down added; C3 and C11
> deleted following module cap survey (§2); buck identified as SparkFun BabyBuck AP63357 with
> 6 V minimum input (§2, §4); R2 rationale documented (§7.4); Wio mounting resolved — socketed,
> upright (§7.6); §8 rewritten for the actual enclosure layout; §3 vibration language struck.

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
| Seeed Wio-SX1262 for XIAO | 2.54 mm headers fitted; XIAO footprint pad labels. **Socketed, not soldered** — see §7.6 |
| SparkFun BabyBuck 12 V → 5 V | **AP63357**, not AP63356. Synchronous, 3.8–32 V IC range, **6 V module minimum** |
| AMS1117-3.3 LDO module | 3-pin right-angle header (VI / GND / VO) |
| Hiletgo 4-channel BSS138 level converter | Two channels used (3 and 4); 1 and 2 spare |

### Connectors

| Ref | Item | Function |
|---|---|---|
| J1 | 2×8 2.54 mm **right-angle** header | Direct board-to-board to StamPLC Bus |
| J2 | JST 2.0 4-pin | StamPLC PORT.A |
| J3 | JST 2.0 4-pin | StamPLC PORT.C |
| J4 | JST 2.0 4-pin | VE.Direct to MPPT |
| — | 2.54 mm female socket strip | Wio module mount (§7.6) |
| — | IPEX → SMA bulkhead pigtail | Antenna |

### Discretes

| Ref | Value | Placement |
|---|---|---|
| F1 | 500 mA polyfuse | 12 V entry, first component |
| D1 | P6KE18A (unidirectional) | 12 V rail → GND, after F1. Band/cathode to +12 V |
| D2 | 1N5819 Schottky — **optional**, see §7.5 | AMS1117 VO → VI, cathode at VI |
| C10 | 47 µF / 50 V electrolytic | Buck VIN → GND (damping, see §7.5) |
| C4 | 100 µF low-ESR electrolytic | **At the Wio 3V3 socket pin** |
| C5 | 100 nF X7R | At the Wio 3V3 socket pin, **closest to the pin**, C4 beside it |
| C6 | 100 nF X7R | Converter LV rail → GND |
| C7 | 100 nF X7R | Converter HV rail → GND |
| R2 | 100 Ω | Series with VE.Direct pin 3, HV side of channel 4 — see §7.4 |
| R3 | 10 kΩ | Wio D4 (NSS) → 3V3 — see §7.1 |
| R4 | 10 kΩ | Wio D2 (RST) → GND — see §7.1 |

**Deleted in rev 0.2:**

- **C11 (100 nF at buck VIN)** — the BabyBuck carries 2× 4.7 µF ceramics millimetres from the IC pins. A hand-wired 100 nF at the end of a perfboard trace adds inductance, not decoupling.
- **C3 (22 µF at AMS1117 VO)** — the LDO module carries 10 µF + 0.1 µF doublets on *both* VI and VO, and C4's 100 µF sits 0.5 in downstream, which is the same node at the AMS1117's loop bandwidth. See §7.5.

**Do not add series resistors on SPI or the Wio control lines.** Seeed already fitted 22 Ω on MOSI / MISO / SCK and pull-ups on the board — but **not on NSS** (confirmed; hence R3).

Any electrolytic on the 12 V input must be rated above 14.6 V absorption — 25 V minimum, 50 V specified. The BabyBuck's own input caps clear this by design, since the module is rated to 32 V input.

---

## 3. Mechanical interconnect

The board mates to the StamPLC **directly via a right-angle 2×8 header**, not a ribbon cable.

**The footprint must be mirrored.** Direct board-to-board mating flips the column order relative to reading the StamPLC's pin table. Verify pin 1 orientation with a continuity check between the seated boards *before* soldering anything to J1. This is the single easiest way to build the board backwards.

**The board is cantilevered off J1.** Add a standoff or bracket at the far end. This is a handling and service concern, not a fatigue one — the enclosure is pier-mounted and the motors are on independent columns (§8), so the board sees no operational vibration. But a cantilevered perfboard is something you will be reaching past with a probe.

Direct mating is otherwise an improvement over ribbon: shorter SPI runs, lower series inductance on the 12 V feed, no IDC crimp to get wrong.

---

## 4. Power architecture

```
Bus pin 1 (12 V bank)
  → F1 polyfuse 500 mA
  → D1 P6KE18A to GND
  → C10 47 µF (damping bulk)
  → BabyBuck VIN  [module: 2× 4.7 µF on VIN]

BabyBuck VOUT (5 V) ──┬─→ level converter HV rail
  [module: 2× 22 µF]  └─→ AMS1117 VI  [module: 10 µF + 0.1 µF]

AMS1117 VO (3.3 V) ──┬─→ [module: 10 µF + 0.1 µF]
  [D2 optional]      ├─→ 0.5 in trace → Wio 3V3 socket pin (C5 100 nF + C4 100 µF local)
                     └─→ branch → level converter LV rail (C6 100 nF)
```

**Bus pin 6 (StamPLC 5 V) is not used.** Everything derives from the bank through local regulation. This deliberately keeps the radio off the rail that drives the relays and opto inputs.

### Rationale

Two-stage buck-then-LDO rather than a single buck. The AP63357 handles the 12 V → 5 V conversion efficiently; the AMS1117 then provides supply rejection at the radio, cleaning up both the buck's 450 kHz switching residue and the motor-induced ripple on the bank. On a node where the gate motors share the battery, "packets drop only while the gate is moving" is the failure mode this avoids.

### Budget

| Quantity | Value |
|---|---|
| Radio TX peak (3.3 V) | ~125 mA |
| AMS1117 dissipation at TX peak | ~0.21 W (comfortable in SOT-223) |
| Buck input current at 12 V, TX peak | ~60–70 mA |
| BabyBuck quiescent at 12 V | ~209 µA with output enabled |
| F1 rating | 500 mA hold — fault protection, not load limiting |

The AP63357's 4 ms soft-start means inrush charging C10 will not trip F1.

### Measured supply behaviour

Gate motors (2× Apollo Titan 912, dual-leaf, 1050 controller with soft start):

- ~10.5 A peak during ramp to full speed
- ~5.5 A steady at full speed

Estimated rail sag at 10.5 A is under 1 V. **The BabyBuck's floor is 6 V, not the bare IC's 4.5 V** — SparkFun configures the EN divider for a 5.6 V rising / 5.18 V falling UVLO and specifies 6 V minimum in practice. Against a nominal 12.8 V bank that still leaves ~5.8 V of margin, and the LiFePO4 BMS cuts out well above 6 V regardless. No brownout risk.

Note that a clamp meter under-reads PWM peaks and will not show the reversal transient at all — D1 exists for the latter.

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
| GND | Rail | Buck GND, AMS GND, converter GND (both sides), Wio socket GND, J2 black, J3 black, J4 pin 1 |
| VBAT_12V | Bus 1 | F1 → D1 / C10 → buck VIN |
| +5V | Buck VOUT | AMS VI, converter HV rail |
| +3V3 | AMS VO | Wio 3V3 socket pin, converter LV rail |

### Radio

**NSS and RST are swapped relative to rev 0.1.**

| Net | StamPLC | Wio pad |
|---|---|---|
| SPI_MISO | Bus 11 (G9) | D9 |
| SPI_SCK | Bus 12 (G7) | D8 |
| SPI_MOSI | Bus 13 (G8) | D10 |
| **LORA_NSS** | **Bus 14 (G11)** | **D4** |
| LORA_BUSY | Bus 15 (G40) | D3 |
| LORA_DIO1 | Bus 16 (G41) | D1 |
| LORA_RF_SW | PORT.A white (G1) | D5 |
| **LORA_RST** | **PORT.A yellow (G2)** | **D2** |

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

### 7.1 NSS on the bus, RST on the pigtail

NSS must stay stably asserted for the whole duration of every SPI transaction; a glitch mid-transfer silently aborts the command, and the resulting failure is invisible in a log. It now sits on Bus 14 — a direct board-to-board pin with no cable, no crimp and no connector in the path. The mechanism for that failure is eliminated rather than mitigated.

RST takes the exposed Grove conductor instead. This is the right trade: RST is pulsed once at init and then static, and if the PORT.A conductor opens, the SX1262 either never leaves reset or never gets reset. Both fail loudly at `radio.begin()` during bring-up.

**R3 — 10 kΩ from D4 (NSS) to 3V3.** Confirmed: Seeed fits no on-board pull-up on NSS. Without R3, G11 is a floating input from power-on until firmware configures it. That matters here specifically because the radio shares SPI with the LCD and microSD (§7.2) — a floating NSS during boot means the SX1262 can respond to traffic intended for the SD card. R3 makes "deselected" the boot state.

**R4 — 10 kΩ from D2 (RST) to GND.** SX1262 RST is active-low, so a pull-down holds the radio in reset if G2 floats at boot or the pigtail opens. Fail-loud rather than fail-weird. The ESP32 fights it with 330 µA when driving RST high, which is nothing. G2 is not an ESP32-S3 strapping pin, so there is no boot-mode interaction either way.

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

**R2 (100 Ω, series with pin 3)** does three things, in descending order of importance:

1. **Contention limiting.** HV4 sits at 5 V through the converter's 10 kΩ pull-up. If LV4 is ever driven as an output — firmware bug, a TX/RX naming mistake of exactly the kind this section warns about, a swapped J4 crimp — the BSS138 pulls HV4 hard low while the MPPT pushes it high. R2 caps that contention at ~33 mA, survivable for both ends indefinitely.
2. **Transient series impedance.** Pin 3 connects directly to the MPPT's MCU pin. R2 sits in front of the BSS138's gate and body diode, keeping induced spikes from finding a path into either device.
3. **Edge damping** on the interconnect.

R2 is *not* meaningful short-circuit protection — against a 12 V fault it limits to 120 mA. If a real fault limit is wanted, the value can rise: at 1 kΩ, a Victron low reads 0.45 V and a high reads 3.45 V at HV4, both cleanly on the right side of the BSS138 threshold, capping a 12 V fault at 12 mA. The constraint is the 10 kΩ pull-up it divides against — **anything approaching 10 kΩ breaks the low level.** 100 Ω is the conservative pick and is what's specified.

**Both directions depend on the 5 V rail.** The 1 Hz text frames are the entire MPPT telemetry path and the text protocol is broadcast-only; TX exists solely for HEX register access. If HEX is ever deferred, the converter, the 5 V stage and two nets can be deleted.

At 19200 baud (52 µs bit times) the BSS138's RC edges are irrelevant.

### 7.5 3.3 V rail decoupling

The LDO module carries 10 µF + 0.1 µF doublets on **both** VI and VO. That plus the BabyBuck's 2× 22 µF output caps means neither the LDO's input cap nor its output cap needs supplementing on the perfboard.

**C3 is deleted.** The AMS1117 datasheet's "22 µF tantalum" output spec is really asking for a minimum ESR, not a minimum capacitance — the part wants a resistive zero in its output impedance. C4's 100 µF low-ESR electrolytic, 0.5 in downstream, supplies that better than additional X5R would; at the AMS1117's loop bandwidth (tens of kHz), 0.5 in of trace is ~10–15 nH and contributes microhms. The regulator effectively sees ~110 µF with real ESR. Adding ceramic here moves the loop in the wrong direction.

**C4 and C5 stay, at the Wio end.** The transient load is the radio's 125 mA TX burst, so local decoupling belongs at the load, not the regulator. Fit C5 (100 nF) closest to the socket's 3V3/GND pins with C4 (100 µF) beside it, so the burst current loop closes locally and never traverses the socket contacts.

**C10 stays, with a changed job.** It is no longer the buck's input decoupling — the module's ceramics handle that. It is *damping*. An all-ceramic input stage fed through wiring inductance forms an underdamped LC tank whose symptom is input voltage ringing on load steps. An electrolytic with tens of mΩ of ESR in parallel is the standard fix. DC-bias derating also means those 2× 4.7 µF parts deliver closer to 5–6 µF at 12 V, so the bulk is useful on its own merits.

**The level-converter LV branch needs nothing extra.** Its load is two 10 kΩ pull-ups; C6 covers it.

**D2 (optional).** The 3.3 V rail now holds ~110 µF against ~54 µF on the 5 V rail feeding it. On a fast input collapse the output cap can reverse-bias the AMS1117's pass element. A 1N5819 from VO to VI (cathode at VI) gives the output cap a backward discharge path. At a 2:1 ratio the risk is low and many designs ignore it; it is a two-cent part on a node that would be tedious to diagnose in the field.

### 7.6 Wio module mounting — socketed, upright

**Decision: female header on the perfboard, module upright, plugged in.** Not soldered, not inverted.

The decisive argument is bring-up step 1 (§11): power-only validation with no Wio seated. That step only exists if the module is removable. Soldering it down deletes the first bring-up milestone and makes any power-stage mistake immediately fatal to the radio.

The electrical objection to socketing is weak. SPI runs at single-digit MHz, where a socket's few mΩ and handful of nH are irrelevant. The RF never touches the header — it leaves via IPEX coax. The one place contact impedance could matter is the 3V3/GND path during TX bursts, and C4/C5 placement at the socket pins (§7.5) closes that loop locally.

Practice:

- Sockets may be standard stamped strip. Machined round-pin is marginally better for contact quality but is not load-bearing here, since the install sees no operational vibration (§8).
- Retain mechanically with a nylon standoff and screw at the far end of the module, pairing with the standoff §3 requires at the far end of J1.
- **Strain-relieve the IPEX pigtail.** IPEX connectors are rated for few mating cycles and are fragile during assembly and service. Anchor the coax to the perfboard within an inch of the connector.

**Inverted mounting was rejected.** It puts the IPEX connector and the module's top-side components facing the perfboard — a clearance problem — and it mirrors the pad labels. §3 and §10 already identify one mirroring hazard at J1 as the easiest way to build the board backwards; a second independent mirroring operation on the same assembly compounds that risk for no benefit.

### 7.7 Free telemetry

With Bus pin 1 tied to the bank, the StamPLC's on-board INA226 measures bank voltage and current directly — an independent cross-check against VE.Direct that still works if the MPPT drops off the serial link.

---

## 8. Installation practice

### Enclosure layout

The gate controller enclosure houses the 12 V battery, the 1050, the MPPT and the GateLink node, and is mounted to a large concrete pier. Gate leaves and motors are attached to their own 6×6 steel columns, mechanically independent of the enclosure. The board therefore sees no operational vibration.

### Feed sizing — not a concern

The 1050 has a direct ~12 in run of 12 AWG from the battery and is fused at 20 A. At 1.588 mΩ/ft, 12 in out and back is roughly 3.2 mΩ — **33 mV at 10.5 A, about 0.24%.** No action required. (Rev 0.1's 10 AWG / 8 AWG guidance sized a long run that does not exist in this install and has been removed.)

The 20 A fuse against a 10.5 A measured peak is reasonable headroom, and makes §12's open question sharper: if Titan 912L stall current exceeds 20 A, the fuse acts on a jam, which is the intent.

### Motor leads are now the dominant noise source

The motors connect to separate connectors on the 1050 and sit 8 ft and 20 ft away. Both runs terminate inside the same enclosure as the radio.

- **Antenna placement is the priority.** Mount the SMA bulkhead on the opposite side of the enclosure from the 1050's motor terminals, and keep the IPEX pigtail and coax physically separated from both motor harnesses inside the box. This is harder than it sounds when everything shares one enclosure — plan it before drilling.
- **Ferrites.** A clamp-on ferrite at the 1050 end of each motor lead is more valuable here, not less, since the harnesses radiate into the same volume as the receiver.
- **The reversal transient is larger than a short-lead install would produce.** With 16 ft and 40 ft round-trip inductive runs, flyback energy at direction reversal is higher and dumps back into the enclosure. D1 is well justified. A TVS across the 1050's own supply terminals is a $0.30 part in a spot where it protects everything in the box.

### Grounding

Land the StamPLC ground and the 1050 ground **separately** on the battery negative post or bus bar; do not daisy-chain. With a ~1 ft return, a shared path would add roughly 17 mV of common-mode on the VE.Direct pair at 10.5 A — below the threshold where it would corrupt the link, so this is now cheap insurance rather than a necessity. The battery negative is right there; do it correctly anyway.

### Motor lead voltage drop (informational)

At roughly 5.25 A per leaf: a 20 ft run in 16 AWG drops about 0.84 V, in 18 AWG about 1.34 V. The 8 ft run is under half that. This costs the far leaf a little torque and speed relative to the near one and does not threaten any electronics rail. Worth addressing only if leaf synchronisation becomes visibly uneven.

---

## 9. Firmware constants

```c
// GateLink expansion board — GPIO assignments (ESP32-S3 / StampS3)
// rev 0.2: NSS and RST swapped relative to rev 0.1
#define LORA_MISO      9    // Bus 11
#define LORA_SCK       7    // Bus 12
#define LORA_MOSI      8    // Bus 13
#define LORA_NSS      11    // Bus 14 (CS)   — R3 10k pull-up to 3V3
#define LORA_BUSY     40    // Bus 15
#define LORA_DIO1     41    // Bus 16
#define LORA_RF_SW     1    // PORT.A white
#define LORA_RST       2    // PORT.A yellow — R4 10k pull-down to GND

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
- [ ] Wio socket pad mapping — ring out each D-pad to its module pin. The D-number mapping is derived from the Meshtastic variant config plus XIAO ESP32S3 numbering, not from a Seeed pin table.
- [ ] NSS lands on Bus 14 and RST on PORT.A yellow — **this is reversed from rev 0.1.** Check the firmware header matches the board before first power-up.
- [ ] R3 fitted (NSS → 3V3) and R4 fitted (RST → GND).
- [ ] P6KE18A manufacturer — ST's part specs 32.5 V max clamping vs Vishay/Taiwan Semi's 25.2 V. The AP63357's input limit is 32 V.
- [ ] P6KE18A is the unidirectional "A", not the bidirectional "CA".
- [ ] C10 rated 25 V minimum (50 V specified).
- [ ] Decide whether D2 is fitted.

*Resolved in rev 0.2 and removed from this list:* Seeed's pull-up coverage on NSS (confirmed absent — R3 required); BabyBuck input cap voltage rating (module is rated to 32 V input by design).

---

## 11. Bring-up order

1. **Power only.** No Wio seated — the socket makes this possible. Confirm 5 V and 3.3 V at the socket pins, then confirm 3.3 V holds with a dummy load.
2. **Radio.** Seat the Wio, antenna connected first. `radio.begin()` with `tcxoVoltage = 1.8` and `setRfSwitchPins()`. Confirm the SX1262 responds before attempting TX. A no-response here points first at TCXO voltage, second at the NSS/RST swap.
3. **Shared bus.** Exercise LCD, microSD and radio concurrently. This is where transaction discipline problems surface.
4. **VE.Direct.** Watch for 1 Hz text frames. If nothing arrives, suspect the RX/TX naming convention first.
5. **Noise characterisation.** Log INA226 voltage at 10 Hz plus beacon RSSI/SNR through ten full gate cycles. This single dataset answers the real rail dip, whether the LDO holds, and whether the receiver desenses during ramps — before the enclosure is sealed.

---

## 12. Open questions

- Apollo Titan 912L published current specification not located; measured values in §4 used instead. Worth confirming from the manual for stall current, particularly against the 1050's 20 A fuse.
- Whether HEX protocol support is in scope for the first firmware release (determines whether §7.4's 5 V branch is load-bearing).
- Whether D2 is fitted (§7.5).
