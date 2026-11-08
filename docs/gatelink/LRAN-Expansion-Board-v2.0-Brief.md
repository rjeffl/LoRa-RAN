# LRAN — GateLink expansion board v2.0 brief

**Document:** `LRAN-Expansion-Board-v2.0-Brief`
**Version:** 0.3
**Status:** The schematic and the PCB layout are re-reviewed. Before the board is ordered, the layout needs P17's
label fix and its Gerbers, under *Rerun at 16:42*. Nothing here has been built, and no value here has been measured
on a v2.0 board.
**Last updated:** 2026-11-08

## Summary

**Expansion board v2.0 is a fabricated PCB that replaces the hand-built rev 0.3 carrier.**
[`gatelink-expansion-board-v2.0.net`](./gatelink-expansion-board-v2.0.net) is the netlist from the re-review,
exported from the 16:38 schematic on 2026-11-08. It carries the schematic changes from all seven of *Corrections to
make*.
The netlist reviewed on 2026-11-06, with those defects, is in git history.

The circuit is the one [`gatelink-expansion-board`](./gatelink-expansion-board.md) rev 0.3 describes. Every radio
net, the SPI bus, the bus header and the power path match it. v2.0 changes parts and connectors for availability
and for PCB assembly, and the operator renumbered the reference designators by position on the board. Rev 0.3 stays
the design record until it is revised for v2.0. This brief lists what that revision must carry.

## What changed from rev 0.3

| Function | Rev 0.3 (hand-built) | v2.0 (PCB) |
|---|---|---|
| 12 V → 5 V | SparkFun BabyBuck, AP63357, on a header (J5 in v1.0's KiCad) | U1, Murata OKI-78SR-5/1.5-W36-C, a switching drop-in for a TO-220 7805 |
| 5 V → 3.3 V | AMS1117-3.3 module on a 3-pin header | J6 keeps the module header. U2 is an alternate footprint for a TO-220 LDO, the Microchip MCP1825S. The operator fits one of the two |
| VE.Direct | 2×2 header and socket, J4 in the doc | J1, a 4-way 5.08 mm screw terminal |
| Level shifter | Channels 3 and 4, with the module mounted inverted | Channels 1 and 2, with the module upright on J3 and J5 |
| PORT.A and PORT.C | 3-pin 2.54 mm headers with pigtails | J10 and J7, 4-pin 2.0 mm, cabled with Grove leads |
| Bus header | Right-angle 2×8, J1 in the doc | J12, the same header |

**The level-shifter change corrects a v1.0 wiring error.** v1.0's KiCad schematic mapped the module's pins wrongly,
and the operator compensated by mounting the module inverted (operator, 2026-11-06). v2.0 uses the module's correct
orientation, so the channels in use moved from 3 and 4 to 1 and 2. Rev 0.3 §2 and §6 still name channels 3 and 4.

### Reference designators

| Part | Rev 0.3 doc | v2.0 |
|---|---|---|
| Bus header | J1 | J12 |
| PORT.A | J2 | J10 |
| PORT.C | J3 | J7 |
| VE.Direct | J4 | J1 |
| Shifter, 5 V side | — | J3 |
| Shifter, 3.3 V side | — | J5 |
| 3.3 V LDO module | — | J6 |
| Wio sockets | — | J9 (5V, GND, 3V3, D10, D9, D8, D7), J11 (D0 to D6) |
| Test points | — | J2 12 V, J4 5 V, J8 3.3 V |
| 47 µF on 12 V | C10 | C1 |
| 100 µF at the Wio | C4 | C4 |
| VE.Direct series 100 Ω | R2 | R1 |
| NSS pull-up 10 kΩ | R3 | R2 |
| RST pull-down 10 kΩ | R4 | R3 |
| 5 V regulator | BabyBuck module | U1 |
| 3.3 V regulator | AMS1117 module | J6 or U2 |

## How the Grove ports map

**The StamPLC's two Grove sockets share one pin order, and the carrier's connectors reverse it.** From the CPU
schematic, [`K141_sch_StamPLC_V10_CPU.pdf`](./K141_sch_StamPLC_V10_CPU.pdf), PORT.A (J6 there) is 1 GND, 2 EXT_5V,
3 G2, 4 G1, and PORT.C (J7 there) is 1 GND, 2 EXT_5V, 3 G5, 4 G4. The carrier puts GND on pin 4, so the cable joins
carrier pin *n* to StamPLC pin 5 − *n*:

| Carrier pin | StamPLC pin | PORT.A | PORT.C |
|---|---|---|---|
| 1 | 4 | G1 | G4 |
| 2 | 3 | G2 | G5 |
| 3 | 2 | 5 V, not connected | 5 V, not connected |
| 4 | 1 | GND | GND |

J10 matches this table: pin 1 is DIO1 on G1, and pin 2 is RST on G2. J7 does not, as *Corrections to make* says.
Wire colours do not settle the mapping, because the StamPLC swaps yellow and white (operator).

**HY2.0 sockets take Grove cables once the locking tab is removed** (operator). KiCad has no HY2.0 footprint, so
J7 and J10 use the JST PH 2.0 mm footprint, which has the same pad layout.

## Corrections to make

1. **Swap the `/G4` and `/G5` net labels at J7.** The wiring is right and the labels are wrong. J7.1 is labelled
   `/G5` and reaches G4, and J7.2 is labelled `/G4` and reaches G5. After the swap, the netlist shows what the
   board already does: R1 sits in series with channel 1, on the path to G4.
2. **Order U2 as the 3.3 V part.** The value field gives MCP1825S-3002E. In Microchip's ordering code, `3002`
   selects a 3.0 V output and `3302` selects 3.3 V. At 3.0 V, the StamPLC's 3.3 V SPI and control lines would drive
   the Wio above its own supply. The TO-220 part is MCP1825S-3302E/AB. Check its pin order, VIN, GND and VOUT,
   against U2's footprint before ordering.
3. **Replace U2's symbol.** It is still `LM7805_TO220`, with the description "Fixed Output 5V".
4. **Add a 10 kΩ pull-down from DIO1 to GND.** Rev 0.3 §7.1.1 recommends it where there is room, and the PCB
   has room. Firmware's `INPUT_PULLDOWN` on G1 stays.
5. **Add decoupling for the Murata module**, which carries no capacitors the BabyBuck had. The BabyBuck had
   2× 4.7 µF on its input and 2× 22 µF on its output. On v2.0, the 5 V rail has only C2, 10 nF.
   - 10 to 22 µF on +5V at U1's output. Placed near U2, it is also U2's input capacitor. The MCP1825 needs
     about 1 µF there.
   - 1 to 10 µF, 50 V ceramic, at U1's input pins. C1 keeps its damping role from rev 0.3 §7.5.
   - Check both against the OKI-78SR datasheet's recommended capacitors and its maximum capacitive load.
6. **Rename `/RS_SW` to `RF_SW`.** Done (operator, 2026-11-06).
7. **Correct the level-shifter header labels.** J3 is the 5 V side and J5 the 3.3 V side, and the reviewed netlist
   had them reversed. Done (operator, 2026-11-06). A module inserted rotated 180° would short +5V to GND through
   J3.3, so the silkscreen also needs a pin-1 mark and the module's outline.

## Decided in the review

- **The VE.Direct crossover is fixed at the screw terminal.** Swapping the two signal wires at J1 is quick, so the
  board does not need to know which end of the cable crosses (operator). What must not move is R1: it stays in
  the path to G4, the ESP32's receive pin, as rev 0.3 §7.4 requires. Label J1.3 and J1.4 on the silkscreen with
  the ESP32 pin each reaches, so the installer wires by signal, not by pin number.
- **J12's pin order is mirrored on purpose.** KiCad has no right-angle 2×8 footprint that mates this way, so the
  operator mirrored the pinout to match v1.0's footprint. Rev 0.3 §3's continuity check of pin 1 against the
  seated StamPLC still applies to the first board.
- **Footprints that differ from the part only in the BOM stay.** J3, J6, J9 and J11 are sockets on PinHeader
  footprints. The hole pattern is the same, and this is not a production board (operator).
- **D1 keeps its footprint.** The `D_TVS` symbol is bidirectional, while the part is the unidirectional P6KE18A.
  Its pin 1 is on +12V, and the DO-15 footprint's pin 1 is the square pad at the band end, so the band already
  marks the cathode on +12V. Confirm that in the PCB view. To make the symbol match the part, use a P6KE18A symbol
  if KiCad's `Diode` library has one, or `Device:D_Zener` with K on +12V. Either uses the same footprint.
- **No DNP markings.** The operator assembles the board and chooses between J6 and U2 at the bench.
- **Mounting holes are on the PCB**, for the standoffs rev 0.3 §3 and §7.6 require.

## PCB review, 2026-11-08

The layout passes DRC with schematic parity in KiCad 10.0.7 (engineering log, 2026-11-08). These findings come from
the schematic, the layout and its 3D renders. None is a measurement.

**Status of *Corrections to make*:** 1, 2 and 3 are still open. J7.1 is still on `/G5`, and U2 is still
`LM7803_TO220` on a 78xx symbol. 4 and 5 are in, as R4, C6 (22 µF on +5V) and C7 (10 µF on +12V). 6 is done. 7's
labels are done, and its silkscreen is not.

### Fix before ordering

| # | Finding | Fix |
|---|---|---|
| P1 | U1's TO-220 footprint is smaller than the OKI-78SR. KiCad's `Converter_DCDC_Murata_OKI-78SR_Vertical` puts the body 5.4 mm above the pin row and 3.4 mm below. At U1's position, the body covers C7's pad 1 and overlaps C6 | Use that footprint, move C6 and C7 clear, and rerun DRC |
| P2 | Corrections 1 to 3 are open | Swap J7's labels. Set U2's value to MCP1825S-3302E/AB and its symbol to match, and confirm its pin order against the datasheet. Set U1's value to OKI-78SR-5/1.5-W36-C |
| P3 | J1.2 is GND. The VE.Direct cable's +5 V wire, unconnected under rev 0.3 §6, lands on it easily and shorts the MPPT's supply | Make J1.2 a no-connect terminal to park that wire, or use a 3-way terminal |
| P4 | J1 has no silkscreen labels, though *Decided in the review* asks for them | Label J1.3 "MPPT TX → G4" and J1.4 "MPPT RX ← G5" |
| P5 | Correction 7's silkscreen is missing: J3 and J5 have no module outline, pin-1 mark or HV/LV labels | Add them |
| P6 | The Wio fits its sockets rotated 180°, which puts its GND on RF_SW and its 3V3 on NSS | Draw the module's outline, with its USB end toward the bottom edge |
| P7 | Silkscreen text uses 0.1 mm lines, below JLCPCB's 0.15 mm minimum | Use 1.0 to 1.2 mm text with 0.15 to 0.18 mm lines |

### Recommended

| # | Finding | Fix |
|---|---|---|
| P8 | U2's nearest output capacitor is C4, about 15 mm away | Add a 1 to 4.7 µF ceramic from U2 pin 3 to GND. J6's module shares it |
| P9 | GND is 1 mm traces with no pour | Pour GND on B.Cu, stitched to F.Cu |
| P10 | J12's outer row is about 3.2 mm from the edge, against about 5.7 mm on v1.0. Its nets and orientation match v1.0's J8 | Check that the right-angle header clears the edge and mates as v1.0 did |
| P11 | J2, J4 and J8 use a 1.00 mm-pitch footprint with a 0.5 mm drill, too small for a test loop | Use `TestPoint_THTPad_D2.0mm_Drill1.0mm` |
| P12 | ERC reports 43 errors: net labels used as notes, and no power flag on /VIN or +12V | Turn the notes into text and add the flags, so a real error stands out |

**D1:** the square pad, at the band end, is on +12V, so the cathode is right. Order the P6KE18A.

**For the JLCPCB order:** two layers, 1.6 mm, lead-free HASL. Choose where JLCPCB prints its order number, or pay to
remove it.

## PCB re-review, 2026-11-08

The operator revised the schematic and the layout after the first review, and this pass read the files saved at
15:25. The layout passes ERC with no errors or warnings. It passes DRC with no violations and schematic parity, both
as saved and after a zone refill. No Gerbers have been generated yet.

**The schematic carries all seven of *Corrections to make*.** J7.1 is on `/G4`, and U2 is MCP1825S-3302E/AB.
U2's TO-220 pads are on VIN, GND and VOUT in pin order 1 to 3. Correction 2's check of that order against the
MCP1825S datasheet is still owed. U1's value is OKI-78SR-5/1.5-W36-C. Correction 7's silkscreen is under P5 and P7
below.

**The operator assembles the board by hand** (operator, 2026-11-08). The level shifter and the Wio have no stock
footprint, and the level shifter's module does not mark its pin 1. The silkscreen labels carry each module's
orientation instead, and pick-and-place conventions do not apply.

| # | Status at the re-review |
|---|---|
| P1 | Fixed. U1 uses `Converter_DCDC_Murata_OKI-78SR_Vertical`, and its courtyard clears C6, C7 and D2 |
| P2 | Fixed |
| P3 | Fixed in the netlist. J1.1 is a no-connect terminal and J1.2 is GND. J1.1 has no label, which P14 adds |
| P4 | Partly fixed. J1.3 reads TX and J1.4 reads RX, named from the MPPT's side, while their nets, `/VE.RX` and `/VE.TX`, are named from the ESP32's side |
| P5 | Fixed. J3 and J5 carry HV1 to HV4, HV, LV1 to LV4, LV and GND labels, and each matches its pin in the netlist. The module outline is drawn with 0.1 mm lines |
| P6 | Partly fixed. The Wio's outline is drawn, with 0.1 mm lines and no mark for the USB end. P13 covers the label inside it |
| P7 | Partly fixed. The new labels use 0.15 mm lines. The title, Port.A, Port.C, the date, Level Shift and Wio SX1262 still use 0.1 mm, as do both module outlines. The project's default graphic line width is still 0.1 mm |
| P8 | Fixed. C8, 4.7 µF, sits about 3 mm from U2 pin 3 |
| P9 | Fixed, with the GND pour on F.Cu |
| P10 | Unchanged. It stays a first-board check |
| P11 | Fixed. The test points use `TestPoint_THTPad_2.0x2.0mm_Drill1.0mm` |
| P12 | Fixed. ERC is clean |

### Fix before ordering

| # | Finding | Fix |
|---|---|---|
| P7 | Six text items and both module outlines use 0.1 mm lines, so the outlines P5 and P6 asked for may not print. Changing the default in Board Setup does not change items already placed | Set every F.SilkS item to 0.15 mm or more with *Edit Text and Graphics Properties* |
| P13 | A "D1" label sits inside the Wio outline beside J9.1, which is the Wio's 5V pin. The Wio's D1 is J11.2, on the other row. Read as an orientation key, the label points at the wrong pin, and a rotated Wio puts its GND on RF_SW and its 3V3 on NSS. It also repeats diode D1's reference | Label J9.1 "5V" and J11.1 "D0", and mark the USB end |
| P14 | J1.1 has no label, so nothing tells the installer to park the VE.Direct cable's +5 V wire there | Label J1.1 "+5V NC" |

### Recommended

| # | Finding | Fix |
|---|---|---|
| P4 | The bare TX and RX labels read the opposite way from their net names | Label J1.3 "MPPT TX" and J1.4 "MPPT RX" |
| P15 | The saved stackup totals 1.65 mm. The thickness is chosen on the JLCPCB order, so this does not change the board | Set 1.6 mm in Board Setup's *Physical Stackup* |
| P16 | D1's value reads P6KE18, and *PCB review, 2026-11-08* orders the P6KE18A | Set the value to P6KE18A |

The operator reported the stackup and the silkscreen widths as corrected. The saved files read as above, so the
corrections may sit in KiCad unsaved.

### Rerun at 16:42

The checks ran again on the PCB saved at 16:42 and the schematic saved at 16:38. ERC reports no errors or warnings.
DRC reports no violations and schematic parity, both as saved and after a zone refill. The netlist differs from the
15:14 export only in D1's value.

**The operator closed three items** (operator, 2026-11-08):

- **P3 and P14.** J1.1 now reads NC. The installed VE.Direct cable has its +5 V wire removed, so no wire needs
  parking there.
- **P10.** J12's 3.2 mm from the edge matches the hand-built perfboard exactly. The v1.0 KiCad layout was drawn
  after that board was built, and does not match it. The continuity check under *First-board checks* stays.
- **P6 and P13.** The Wio now has a label beside each pin in use, which is enough for hand assembly. The "D1"
  label is gone.

| # | Status at the rerun |
|---|---|
| P4 | Fixed. J1 reads NC, GND, MPTX and MPRX, and MPTX is the terminal that reaches G4 through R1 |
| P7 | Fixed. Every board text item and both module outlines use 0.15 mm lines, as do the project defaults |
| P15 | Fixed. The stackup totals 1.58 mm, and the order sets 1.6 mm |
| P16 | Fixed. D1's value is P6KE18A |

| # | Finding | Fix |
|---|---|---|
| P17 | The Wio label beside J11.6 reads RS_SW, and the net is `/RF_SW` (correction 6) | Relabel it RF_SW |

**Still owed before ordering:** the Gerbers, and correction 2's check of U2's pin order against the MCP1825S
datasheet.

## The Murata regulator

**No reason found to change from the OKI-78SR, with the capacitors in correction 5 added.** It is a buck like the
BabyBuck, so rev 0.3 §4's buck-then-LDO rationale stands. Where it differs:

| Property | BabyBuck | OKI-78SR-5/1.5-W36-C |
|---|---|---|
| Minimum input | 6 V | About 7 V. A 12.8 V bank sagging under 1 V on motor start keeps wide margin |
| Maximum input | 32 V | 36 V. D1's 25.2 V maximum clamp stays below both |
| On-module capacitors | 2× 4.7 µF in, 2× 22 µF out | Correction 5 replaces them |
| No-load current | About 209 µA | Probably a few mA. Read the datasheet figure and update rev 0.3 §4's budget |

## Still to decide

**The 2026-11-08 layout settles all four items:** C1 uses a 6.3 mm footprint, C2, C3 and C5 are 100 nF, D2 is fitted, and
F1's 5.08 mm pitch fits a typical radial PPTC such as the MF-R050. Check F1's pitch against the part bought. The
items as the review left them:

- **C1's can against its footprint.** A 47 µF, 50 V electrolytic is usually 6.3 mm in diameter, and the footprint
  is `CP_Radial_D5.0mm_P2.50mm`. Rev 0.3 §2 requires 25 V at least and specifies 50 V.
- **C2, C3 and C5's value.** The v1.0 and v2.0 schematics give 0.01 µF (10 nF), and rev 0.3 §2 specifies 100 nF.
  Settle on one value so the two records agree.
- **F1's footprint against the polyfuse.** `Fuse_Littelfuse_395Series` is a fuse footprint, so check the PPTC's
  lead pitch against it.
- **Whether to add a footprint for D2**, a 1N5819 from the 3.3 V regulator's output to its input. Rev 0.3 §7.5
  argued for it at a 2:1 ratio of output to input capacitance, and v2.0 has about 110 µF against about 20 µF once
  correction 5 is made.

## What the rev 0.3 document needs

When v2.0 is built, revise [`gatelink-expansion-board`](./gatelink-expansion-board.md) for it, rather than keeping
two design records:

- §2 and §4: the Murata and MCP1825S, the new capacitors, and the power budget.
- §2, §6 and §9: shifter channels 1 and 2, and the new reference designators.
- §6: J1 as a screw terminal, with the crossover handled there.
- §5 and §6: the Grove pin mapping above.
- §7.1.1: the DIO1 pull-down as fitted, not optional.
- §10 and §11: a first-board checklist for the PCB.

## First-board checks

- J12 pin 1 rings out to the StamPLC's bus pin 1 with the boards seated.
- With no Wio or shifter seated, +5V reads 5 V and +3V3 reads 3.3 V, not 3.0 V.
- J10.1 rings out to G1, J10.2 to G2, J7.1 to G4 and J7.2 to G5, through the cables to be installed.
- R1 sits between the J1 terminal marked for the MPPT's TX and G4.
- VE.Direct text frames arrive at 1 Hz, as in rev 0.3 §11 step 5.

**Falsified by:** a continuity reading in the checks above that differs from this brief, or a 3.3 V rail that
reads other than 3.3 V with U2 fitted.
