# EL3H4 Isolated Digital Input — Circuit Analysis

## Circuit summary

| Ref | Value | Role |
|---|---|---|
| PC1 | EL3H4 | AC-input (anti-parallel LED) phototransistor optocoupler |
| R14 | 6.2 kΩ 1% | Shunt / bleed resistor across the input terminals |
| R22 | 6.2 kΩ 1% | LED current-limiting resistor |
| R30 | 5.1 kΩ 1% | Emitter load (pull-down) |
| R38 | 1 kΩ 1% | Series resistor to MCU pin |
| C11 | 1 nF / 50 V | Filter capacitor at MCU pin |

Netlist:

- `EXCON_COM` → PC1-1, R14-1
- `EXCOM_IN1` → R14-2, R22-1
- R22-2 → PC1-2
- PC1-4 (collector) → SYS_3.3V
- PC1-3 (emitter) → R38-1, R30-1
- `SYS_IN1` → R38-2, C11-1
- GND → R30-2, C11-2

Output stage is an **emitter follower**: collector to 3.3 V, emitter loaded by R30, so SYS_IN1 goes high when the LED is on.

### Key EL3H4 parameters (Everlight datasheet)

| Parameter | Value |
|---|---|
| Forward voltage VF | 1.2 V typ, 1.4 V max @ IF = ±20 mA |
| CTR, standard grade | 20 % min / 300 % max @ IF = ±1 mA, VCE = 5 V |
| CTR, A-rank | 50 % min / 150 % max |
| VCE(sat) | 0.2 V max |
| Dark current | 100 nA max |
| IF max | ±50 mA |
| Turn-on / turn-off | ~18 µs (rated) |

---

## 1. DVM resistance between EXCOM_IN1 and EXCON_COM

**≈ 6.2 kΩ (6.14–6.26 kΩ at 1 %).**

R14 is directly across the terminals. The parallel R22 + LED branch appears open because a DVM's ohms-range test voltage (typically a few tenths of a volt) is below the LED turn-on voltage (~1 V). A meter with an unusually high open-circuit test voltage may read slightly lower. The reading is the same with probes reversed (anti-parallel LEDs). Disconnect the field source before measuring.

## 2. Input current with 13.4 V applied

| Path | Calculation | Current |
|---|---|---|
| R14 | 13.4 V / 6.2 kΩ | 2.16 mA |
| R22 + LED | (13.4 − ~1.1 V) / 6.2 kΩ | 1.98 mA |
| **Total** | | **≈ 4.1–4.2 mA** |

LED VF at ~2 mA is taken as ~1.1 V (below the 1.2 V typ rated at 20 mA). Current is the same in either polarity.

More than half the input current flows through R14 rather than the LED. R14 shunts leakage from 2-wire sensors or open contacts so an "off" input stays off, and provides minimum load / wetting current. The anti-parallel LEDs and shared `COM` terminal allow either sinking (NPN) or sourcing (PNP) field wiring.

## 3. Is ~2 mA LED current enough to produce a high on SYS_IN1?

SYS_IN1 voltage ≈ Ic × 5.1 kΩ (no DC current flows into the MCU pin), capped at 3.3 V − VCE(sat) ≈ **3.1 V**.

Reaching saturation requires Ic ≈ 3.1 V / 5.1 kΩ ≈ **0.61 mA** → CTR ≈ **31 %** at IF ≈ 2 mA.

| Part / CTR | Ic | SYS_IN1 | Result |
|---|---|---|---|
| Standard, typical (~100 %) | wants ~2 mA → saturates | ≈ 3.1 V | Clean high |
| Standard, minimum (20 %) | ≈ 0.4 mA | ≈ 2.0 V | **Below** 3.3 V CMOS VIH (0.7 × VDD ≈ 2.3 V; ESP32 ≈ 2.5 V) |
| A-rank, minimum (50 %) | ≈ 1 mA → saturates | ≈ 3.1 V | Clean high |

Additional derating: CTR is specified at VCE = 5 V, but here VCE is only ~1.3 V at 0.4 mA, so worst-case current is lower still. LED aging and high temperature reduce CTR further.

**Conclusion:** Works with typical parts at 13.4 V, but a standard-grade EL3H4 is **marginal at worst case**. Check the BOM — an `(A)` suffix (A-rank) gives comfortable margin.

## 4. Will the same circuit work at a 5 V input?

**Not reliably.**

LED current: (5 − ~1.05 V) / 6.2 kΩ ≈ **0.64 mA**

| CTR | Ic | SYS_IN1 | Result |
|---|---|---|---|
| Typical (~100 %) | ≈ 0.64 mA (just saturates) | ≈ 3.1 V | Probably works |
| Minimum (20 %) | ≈ 0.13 mA | ≈ 0.65 V | Reads **low** |

Behavior becomes unit-to-unit luck.

### Options

- **5 V-only inputs:** reduce R22 to **1.5–2.0 kΩ** (IF ≈ 2–2.6 mA).
- **One design for 5–24 V:**
  - A smaller R22 dissipates more at 24 V — e.g., 1.8 kΩ passes ~12.7 mA and dissipates ~0.29 W, requiring a ½ W part.
  - Or keep R22, specify the **A-rank** part, and raise R30 to ~10 kΩ (halves the current needed to saturate, at the cost of slower edges).
- **Most robust:** use a constant-current input stage (common in PLC-style inputs).

## 5. Function of the output network

### R30 — 5.1 kΩ emitter load / pull-down

- Converts phototransistor current into a voltage.
- Holds SYS_IN1 low when the LED is off (100 nA max dark current → ~0.5 mV).
- Its value sets how much transistor current is needed to reach a valid high (see §3–4).

### R38 + C11 — RC low-pass filter

- τ = 1 kΩ × 1 nF = **1 µs**, corner ≈ **160 kHz**.
- Filters fast noise, EMI pickup, and glitches before they reach the MCU.
- R38 limits current into the MCU pin's ESD clamp diodes during transients.
- C11 provides a local high-frequency path to ground at the pin.

**Not a contact debounce:** the optocoupler itself is far slower than 1 µs (rated ~18 µs, slower still with a 5.1 kΩ load). Debounce for mechanical contacts must be handled in firmware.

---

## Sources

- [Everlight EL3H4-G datasheet (promelec)](https://www.promelec.ru/fs/sources/2a/7d/e8/b7/f75b2c3b0166012cd7e8314f.pdf)
- [EL3H4-G listing (rcscomponents)](https://rcscomponents.kiev.ua/product/el3h4-g.html)
- [EL3H4(A)(TA)-G specs (zenode)](https://zenode.ai/parts/56spmc1m5m)
- [EL3H4(TB)-VG specs (zenode)](https://zenode.ai/parts/5hwzhewqvt)
