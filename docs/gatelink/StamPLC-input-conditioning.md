# StamPLC Opto Input — Signal Conditioning for 12 V and 5 V Signals

Companion to `StamPLC-el3h4-input-analysis.md`. Goal: reliably drive the StamPLC's EL3H4 optocoupler inputs to a high state **without modifying the board's input circuit**, for both 12 V nominal and 5 V pulled-high signals.

## The constraint

With R22 fixed, the only external levers are **drive voltage** and **source stiffness**:

    IF = (V − ~1.1 V) / 6.2 kΩ

A worst-case 20 % CTR part, derated for low VCE and LED aging, needs about **3 mA** of LED current for a guaranteed saturated high. That requires a drive voltage of roughly **20 V or more**.

| Drive | LED current | Worst-case result |
|---|---|---|
| 5 V (stiff source) | ≈ 0.64 mA | Fails (≈ 0.65 V on SYS_IN1) |
| 12 V nominal | ≈ 1.76 mA | Marginal (≈ 1.8 V on SYS_IN1) |
| **24 V** | **≈ 3.7 mA** | **Saturated high (≈ 3.1 V)** |

**Approach:** use your signals to *switch a local 24 V rail* into the inputs rather than driving the inputs with the signals themselves.

## The board supports 24 V

- The StamPLC opto inputs are rated **DC 5–36 V** (M5Stack documentation).
- At 24 V each active input draws **≈ 7.6 mA**: ≈ 3.7 mA through the LED (via R22) and ≈ 3.9 mA through R14.

## Why 5 V pulled-high signals are the bigger problem

A pull-up resistor cannot supply what this input draws: R14 alone takes 0.8 mA at 5 V. With a 10 kΩ pull-up, the input collapses to under 2 V and the LED barely conducts. **These signals need buffering regardless of drive voltage.**

## Wire all channels as sinking inputs

EXCON_COM is almost certainly shared across all 8 inputs. M5Stack documents both low-level (sinking) and high-level (sourcing) wiring, but **every channel on a common COM must use the same method.** Sinking works for every signal type here:

```
+24V ──── EXCON_COM (shared)

EXCOM_INx ──┬── dry contact ──────────────── 0V        (direct, no parts)
            │
            └── drain   N-MOSFET (BSS138 / 2N7002)
                 source ───────────────────── 0V
                 gate ── 1k ──┬── 5V or 12V signal
                              └── 100k ── 0V
```

### Per signal type

| Signal | Conditioning | Notes |
|---|---|---|
| **5 V push-pull or pulled-up** | Logic-level N-MOSFET (BSS138 preferred) | High-impedance gate, so the existing pull-up works as intended. Non-inverting: signal high → FET on → SYS_IN1 high. |
| **12 V signal** (e.g., power-present monitor) | Same MOSFET circuit | 12 V is within ±20 V VGS. Add a 15 V gate-to-0V zener if the line can carry transients. |
| **12 V signal — alternative** | 12 V-coil interposing relay; contact switches 24 V | Keeps full isolation from the source; slower and has a wear life. |
| **Dry contact** (NO/NC indicator) | None — wire between EXCOM_INx and 0V | 3.7 mA wetting current is fine for most contacts. |
| **Open-collector, pull-up removable** | Collector directly to EXCOM_INx | Transistor must tolerate 24 V off-state. Remove the 5 V pull-up or 24 V back-feeds through it. |

### MOSFET stage parts (per channel)

- N-MOSFET: BSS138 (50 V) or 2N7002 (60 V), SOT-23
- Gate series resistor: 1 kΩ
- Gate pull-down: 100 kΩ
- Optional: 15 V zener gate-to-0V for 12 V lines with transients

## Grounding and isolation

- **Ground sharing:** the 5 V or 12 V device ground must connect to the 24 V supply's 0V so the MOSFET gate has a reference. Fine within one enclosure.
- **Isolation tradeoff:** powering the StamPLC from the same 24 V supply bypasses the input isolation. It still works, but use a **separate small 24 V supply** for the inputs if you want the isolation barrier to remain meaningful.

## Lower-effort check for the 12 V signals

Before adding hardware, measure the actual margin on your board:

1. Drive an input from an adjustable bench supply.
2. Lower the voltage and note where the input drops out.

| Dropout voltage | Interpretation | Action |
|---|---|---|
| Below ~6 V | > 2× margin at 12 V nominal (likely A-rank or typical-CTR part) | Direct 12 V drive is reasonable |
| ~9–10 V | Little margin | Buffer it — supply sag or LED aging will eventually cause missed highs |

This characterizes only the board under test, but shows whether the 12 V channels are actually at risk.

## Physical build

The MOSFET stage is two resistors and a SOT-23 per channel. A small terminal-block carrier board or a DIN-mount 8-channel interface board keeps it neater than flying breakout-board wires to the terminals.

## Sources

- [M5Stack StamPLC documentation](https://docs.m5stack.com/en/core/StamPLC)
