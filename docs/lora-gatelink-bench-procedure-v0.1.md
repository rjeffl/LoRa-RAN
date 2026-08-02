# LoRa GateLink — Bench Characterization Procedure

**Document:** Bench Procedure v0.1 (draft)
**Companion to:** PRD v0.4 (§4.2, §4.3, D3, D13) and Wire Format v0.1 (§6.5)
**Purpose:** Resolve the open measurement items blocking design finalization,
using read-only observation before any node drives either bus.
**Platform:** MacBook (macOS)
**Last updated:** 2026-07-23

---

## 0. What this procedure resolves

| Item | Source | Closed by |
|---|---|---|
| VE.Direct field encodings (`TBM` values) | Wire Format §6.5 | Session A |
| Full VE.Direct field list vs. "all fields" | PRD §7.3, W1 | Session A |
| 1050 Oview pinout | PRD §4.2.2 | Session B |
| Data-line count (1 shared vs. 2) | PRD §4.2.2 step 2 | Session B |
| **Physical layer: single-ended vs. differential → D13** | PRD §4.2.2 step 3 | Session B |
| BusT4 break timing (519–590 µs claim) | PRD §4.2.1 | Session C |
| Loop state over BusT4 (`INF_IO`) → D3 | PRD §5.4.3 | Session C |
| Movement-cause encoding | PRD §5.5, W2 | Session C |

Sessions are ordered by rising risk and rising toolchain complexity. **Do them
in order.** Session A validates the toolchain on a harmless bus before Session B
goes near 24–28 V.

---

## 1. Safety preconditions — read before touching the enclosure

The 1050 Oview jack carries **24–28 V on its VCC pin** (PRD §4.2.1). Every step
in this document is **read-only observation** — meter, logic analyzer probes, and
scope only. **Nothing with a transmit line touches the Oview port in this
procedure.** Command injection happens later, on the bench, only after the pinout
and physical layer are confirmed and the correct signal conditioning is built.

Hard rules:
1. **Never connect a USB-serial adapter's TX pin to the Oview port.** A UART
   adapter driving an unknown 5 V (or differential) bus, near a 24–28 V pin, is
   the one combination that can destroy the 1050. Logic analyzer and scope probes
   are passive and high-impedance; they observe without driving.
2. **Identify VCC before connecting any instrument ground.** Meter first (§B.2).
3. **One hand rule / no floating grounds.** Reference every instrument to the
   same battery negative the 1050 uses, at a single point.
4. **Fuse remains in place** on the FLA tap throughout.
5. If anything is ambiguous — a pin that reads a voltage you didn't expect, a
   waveform that doesn't match either branch in §B.4 — **stop and reassess** rather
   than connecting the next thing.

---

## 2. Tools

### 2.1 Required
| Tool | Use | Notes |
|---|---|---|
| **Victron VE.Direct-to-USB cable** *(preferred)* | Session A | FTDI-based; JST-PH one end, USB other. Appears as `/dev/tty.usbserial-*`. |
| *or* 5 V-capable FTDI/CP2102 adapter + JST-PH pigtail | Session A alt | Set to **5 V**. Wire GND + MPPT-TX→adapter-RX only. |
| **8-channel USB logic analyzer** (sigrok-compatible) | Sessions B, C | Cheap FX2-based clone or genuine Saleae. Confirm **5 V input tolerance** before use — see §2.3. |
| **PulseView** (sigrok GUI) | Sessions B, C | macOS build via `brew install --cask pulseview` or sigrok.org. UART + break decode. |
| **Digital multimeter** | Session B | Pin voltage identification. |
| USB-C hub / adapter | all | MacBook USB-C → USB-A for the instruments. |

### 2.2 Optional but useful
| Tool | Use |
|---|---|
| Benchtop or USB oscilloscope | Alternative to the analyzer for the §B.4 single-ended-vs-differential call; shows edge detail the analyzer can't. |
| Python 3 + `pyserial` | Scripted VE.Direct capture/logging in Session A. |
| 6P4C breakout / RJ-style breakout board | Non-destructive access to Oview pins without cutting the pigtail. |

### 2.3 Logic-analyzer voltage caveat — check this before Session B
Most FX2 clones are **3.3 V logic-threshold parts that tolerate 5 V input** — fine
for reading a 5 V BusT4 line (a 5 V high still registers). But some bargain units
clamp near 3.6 V. Confirm the datasheet or product page says **5 V-tolerant
inputs.** If yours isn't, put a **1 kΩ series resistor** in each probe lead — at
these speeds it costs nothing and protects the input. The genuine Saleae Logic 8
is 5 V-tolerant natively.

---

## 3. macOS toolchain setup — do this before bench day

1. Install PulseView: `brew install --cask pulseview` (or download from
   sigrok.org). Launch once; confirm it opens without the analyzer attached.
2. Plug in the logic analyzer. In PulseView, **Connect to Device** → select
   *fx2lafw* (clones) or *Saleae Logic* → confirm channels D0–D7 appear.
3. Install serial tooling for Session A:
   - `screen` is built in, or
   - `pip3 install pyserial` for scripted capture.
4. Identify the serial device name: with the VE.Direct-USB cable plugged in,
   `ls /dev/tty.usbserial-*`. Note the exact name.
5. **Dry-run the analyzer** on a known signal (e.g. probe a 3.3 V GPIO toggling,
   or the VE.Direct line from Session A) so you learn PulseView's capture/trigger
   UI on something harmless before Session B.

---

## SESSION A — VE.Direct characterization (do first)

**Goal:** validate the read toolchain, capture real MPPT frames, and resolve the
Wire Format §6.5 field encodings against live data.
**Risk:** low. VE.Direct is 5 V, unpowered from the adapter side, transmits
unsolicited. No high-voltage pin on this connector's data side.

### A.1 Connect
- **Victron cable:** plug JST-PH into the MPPT VE.Direct port, USB into the Mac.
- **Generic adapter:** set to 5 V. Wire only: adapter GND ↔ VE.Direct pin 1 (GND),
  adapter RX ↔ VE.Direct pin 3 (device TX). **Leave adapter TX and pin 4 (V+)
  unconnected.** Meter pin 1 vs. pin 4 first to confirm which is GND, per PRD §4.3
  — the crossover-cable warning applies.

### A.2 Capture
```
screen /dev/tty.usbserial-XXXX 19200
```
or scripted:
```python
import serial
s = serial.Serial('/dev/tty.usbserial-XXXX', 19200, timeout=2)
with open('vedirect_capture.txt', 'wb') as f:
    while True:
        line = s.readline()
        print(line.decode(errors='replace'), end='')
        f.write(line)
```
Exit `screen` with `Ctrl-A` then `K`.

### A.3 What you should see
Repeating ~1 Hz blocks of tab-separated `LABEL<TAB>VALUE` lines, each block
terminated by a `Checksum` field. Human-readable ASCII. If you see garbage,
suspect wrong baud, TX/RX swapped, or the crossover-cable pin mapping.

### A.4 Record — feeds Wire Format §6.5 and W1
For **every** label the MPPT emits, capture:

| Capture | Why |
|---|---|
| Complete list of labels present | Resolves W1 — "all available fields" becomes a concrete list |
| Units and value ranges observed | Confirms the §6.5 unit choices (e.g. `VPV` in 10 mV, `H19` as `uint32`) |
| `V`, `I`, `VPV`, `PPV`, `IL`, `CS`, `ERR`, `MPPT`, `H19`–`H22` present? | These are the §6.5 MPPT-block fields; note any missing or extra |
| Values under different conditions | Panel covered vs. sunlit; load on vs. off — see the value swing |
| `CS`, `ERR`, `MPPT` numeric codes | These pass through unmodified (§6.5); record the raw numbers seen |

Deliberately provoke states so the encodings aren't guessed:
- Shade the panel → watch `CS` change (float → bulk, or → off).
- If safe and supported, toggle a load → watch `IL` and the load-on flag.

### A.5 Session A exit criteria
- [ ] Full label list recorded and committed to `/docs`.
- [ ] Each Wire Format §6.5 MPPT field confirmed present, or flagged absent.
- [ ] Any labels present but *not* in §6.5 noted for a possible `ver` bump (W1).
- [ ] `CS`/`ERR`/`MPPT` raw code values recorded for the bridge's MQTT text mapping.

---

## SESSION B — BusT4 physical-layer discrimination (resolves D13)

**Goal:** identify the Oview pinout, count data lines, and determine
single-ended vs. differential — the measurement that selects Branch A (BSS138) or
Branch B (SN65HVD230).
**Risk:** high — 24–28 V present. Observation only.

### B.1 Access the pins non-destructively
Use a 6P4C breakout or carefully back-probe the jack. Do **not** cut the pigtail
yet — the pinout isn't known. Label the four positions 1–4 consistently and
record your numbering convention with a photo (PRD §4.2.2 step 5).

### B.2 Find VCC and GND — meter, power on, gate idle
With the 1050 powered and the gate idle, meter each of the four pins against
battery negative:

| Reading | Interpretation |
|---|---|
| **24–28 V** | **VCC. Mark it. Do not probe it with the analyzer.** |
| ~0 V, continuous | Candidate GND |
| Small varying voltage / idles ~2.5 V or ~5 V | Candidate data line |

Record all four. **Do not proceed to the analyzer until VCC is positively
identified and flagged.**

### B.3 Attach the analyzer — data and GND pins only
- Analyzer GND ↔ the Oview GND (single-point, same reference as the 1050).
- One analyzer channel to each **candidate data pin.** **Do not** connect any
  channel to the VCC pin.
- If §2.3 flagged your unit as not 5 V-tolerant, series 1 kΩ in each probe lead.

### B.4 The discrimination capture
Trigger a capture in PulseView, then operate the gate from a wall button or
handheld remote so the 1050 puts traffic on the bus. Examine the waveform:

| Observation | Physical layer | Decision |
|---|---|---|
| One pin swings **0 → 5 V**, clean UART edges, referenced to GND; other data pin idle or is the return | **Single-ended** | **Branch A — BSS138** (PRD §4.2.3) |
| Two pins swing **oppositely around a ~2.5 V common mode**; both sit near 2.5 V when idle (recessive) | **Differential** | **Branch B — SN65HVD230** (PRD §4.2.4) |

Also settle the **line count** (PRD §4.2.2 step 2):
- Activity on **one** data pin (besides GND) → shared bidirectional line.
- Activity on **two** → separate TX/RX.

This distinction matters for Branch A wiring: a single shared line needs the
BSS138's open-drain contention tolerance (PRD §4.2.3), and rules out anything
push-pull without an output-enable.

### B.5 Assign direction
Operate the gate by an **external** cause (remote/wall button) so the 1050 is the
one talking. The line carrying 19200-baud traffic in that moment is the **1050's
TX** → the gate node's RX. The other data line is the 1050's RX.

### B.6 Record — completes PRD §4.2.2 table
| 6P4C position | Measured voltage | Signal | GateLink connection |
|---|---|---|---|
| 1 | | | |
| 2 | | | |
| 3 | | | |
| 4 | | | |

Physical layer: ☐ single-ended (Branch A)  ☐ differential (Branch B)
Data lines: ☐ one (shared)  ☐ two (separate)

Photograph the jack alongside the filled table; commit both to `/docs`.

### B.7 Session B exit criteria
- [ ] VCC pin positively identified and documented.
- [ ] Full pinout table filled.
- [ ] **D13 resolved:** branch selected on waveform evidence, not inference.
- [ ] Line count determined.
- [ ] TX/RX direction assigned.
- [ ] Photos + table in `/docs`.

> After Session B the BOM is final: Branch A uses BSS138 channels 1–2 for BusT4;
> Branch B moves BusT4 to the SN65HVD230 and leaves BSS138 channels 1–2 spare.

---

## SESSION C — BusT4 protocol decode (feeds D3, W2, break timing)

**Goal:** decode actual BusT4 frames, confirm the break timing, and determine
whether loop state and movement cause come over the bus.
**Prerequisite:** Session B complete; branch known; still **read-only.**

### C.1 Configure the decoder
With the physical layer known from Session B:
- **Single-ended:** add PulseView's **UART decoder** on the 1050-TX channel,
  19200 baud, 8N1. Stack a **timing/annotation** view to see the inter-burst gap.
- **Differential:** the analyzer reads one side of the pair against common mode;
  add the UART decoder on the dominant-transition channel. (If decode is
  marginal, this is where the optional scope earns its place.)

### C.2 Confirm the break
Measure the idle-low interval preceding each burst. **Expected 519–590 µs** per
PRD §4.2.1. Record the actual measured range — this is a firmware parameter
(Wire Format / gate BusT4 client) and worth pinning precisely, not just
confirming.

### C.3 Characterize frames by cause — the core of Session C
Operate the gate by each cause and capture the resulting bus traffic separately.
The point is to see which fields in the emitted frames change with each cause:

| Trigger the gate via | Watch for |
|---|---|
| Handheld remote | Command frame shape; movement-cause bits (W2) |
| Wall button | Movement-cause difference vs. remote |
| **Vehicle on inside loop** | Loop-state bits; which frame carries them |
| **Vehicle on outside loop** | Loop-state bits on the other loop |
| **Oview (if available)** | Movement cause = Oview encoding |

### C.4 The `INF_IO` question — resolves D3
Per PRD §5.4.3, the `makstech` component uses an `INF_IO` request to read the
1050's I/O state. Determine empirically whether loop inputs appear there:

1. Identify `INF_IO` response frames in the capture (cross-reference the
   `makstech`/`xdanik` source for the command/response bytes).
2. With a vehicle (or a suitable metal mass / loop simulator) on the **inside**
   loop, capture and note which bit(s) change.
3. Repeat for the **outside** loop.
4. If distinct bits track each loop → **BusT4 acquisition path is viable (D3
   resolves toward BusT4).** If loop state never appears on the bus → GPIO
   fallback is required (PRD §5.4.3 GPIO path).

> Note the timing caveat from PRD §5.4.3: even if loop state is on the bus,
> direction detection needs sub-second ordering resolution. Record the **native
> rate** at which the 1050 updates `INF_IO` — it bounds how fast polling can
> resolve entry-vs-exit and may still argue for GPIO regardless of availability.

### C.5 Record — feeds Wire Format §6.5, §7.4; PRD D3, W2
- [ ] Measured break duration (min/typ/max over several bursts).
- [ ] Command frame bytes for each command issued.
- [ ] Movement-cause field location and observed values per cause (populates
      Wire Format §7.4, currently provisional).
- [ ] Loop-state field: present on bus? which frame? which bits? (D3)
- [ ] `INF_IO` native update rate (direction-resolution bound).
- [ ] Raw captures (`.sr` PulseView session files) committed to `/docs/captures`.

### C.6 Session C exit criteria
- [ ] Break timing confirmed and recorded as a firmware constant.
- [ ] **D3 resolved:** BusT4-loop-state viable, or GPIO fallback confirmed
      necessary.
- [ ] Movement-cause encoding documented (or confirmed not exposed → §7.4 stays
      `UNKNOWN`).
- [ ] Command frame formats captured for the port from `makstech`/`xdanik`.

---

## 4. After the bench sessions — what to update

| Document | Update |
|---|---|
| PRD §4.2.2 | Filled pinout table + photo |
| PRD D13 | Resolved — branch selected |
| PRD D3 | Resolved — BusT4 vs. GPIO for loops |
| PRD §4.2.1 | Break timing confirmed with measured value |
| PRD BOM §4.1 | Finalized per branch |
| Wire Format §6.5 | MPPT field encodings confirmed against live frames |
| Wire Format §7.4 | Movement-cause values filled or confirmed absent |
| Wire Format W1, W2 | Closed |

Only after these are updated does wire-format refinement and the W4 test-vector
work resume, per the sequencing agreed before this bench detour.

---

## 5. Quick reference — session-to-tool-to-outcome

| Session | Tool | Bus | Risk | Resolves |
|---|---|---|---|---|
| **A** | VE.Direct-USB cable | VE.Direct (5 V UART) | Low | §6.5 fields, W1 |
| **B** | Logic analyzer + meter | BusT4 (unknown) | **High (24–28 V)** | Pinout, line count, **D13** |
| **C** | Logic analyzer + UART decode | BusT4 (known branch) | Medium | Break timing, **D3**, W2 |

Do them in order. Session A proves the toolchain; Session B is observation-only
near high voltage; Session C only starts once the physical layer is known.
