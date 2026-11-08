# LRAN — GateLink input board v2.0 brief

**Document:** `LRAN-Input-Board-v2.0-Brief`
**Version:** 0.4
**Status:** The schematic and the PCB layout are re-reviewed, and every review item but I1 is closed. The board
needs fresh Gerbers before it is ordered (*Final check at 16:56*). Nothing here has been built, and no value here has been measured on a v2.0 board.
**Last updated:** 2026-11-08

## Summary

**Input board v2.0 is a PCB, and [`gatelink-input-board-v2.0.net`](./gatelink-input-board-v2.0.net) is its
reviewed schematic.** The operator renamed it from v1.2 on 2026-11-04 because it is a completely different board,
so records up to that date call it v1.2. D89 deploys GateLink on the repaired v1.1 board. D91 tracks the date v2.0
is built. The recommendations come from the input-board session of 2026-11-03, after the operator found Q1 and Q2
faulty. That session said the new revision could be reworked on the existing perfboard. The operator rejected that:
the changes are more extensive than a hand-built perf board will survive, so v2.0 starts from scratch.

The cause of the Q1 and Q2 failures is not established, so these changes do not rest on a known cause.
M27 tracks the tests that could narrow it.

## What the stages drive

Q1 to Q4 serve IN3 to IN6. The 2N7000s buffer the 1050's signals to the StamPLC's opto inputs.

| Input | FET | Source at the 1050 | Undriven | Fault seen |
|---|---|---|---|---|
| IN3 | Q1 | 1050 input, pulled high internally to 5 V, reaching 4.6 to 4.7 V, and driven low by an external device | Pulled high by the 1050 | Gate leaks in both states (M27) |
| IN4 | Q2 | The same type as IN3 | Pulled high by the 1050 | Replaced 2026-11-02 |
| IN5 | Q3 | The same type as IN3 | Pulled high by the 1050 | Gate leaks about 0.24 mA at 13.2 V (M27). It still switches |
| IN6 | Q4 | A FET driving the output to 12 V | No apparent pulldown when off, so a 10K holds the gate low: R9 on v1.1, R8 on v2.0 | None recorded |

The operator's history bounds the failures. All stages tested good when the board was built, so the failures
came after installation. The stages have never been connected to the 1050.

## v1.1 as built

From [`Gatelink-Input-Board.net`](./Gatelink-Input-Board.net). Impl Plan §3.2 differs from this and has not
been reconciled.

| Item | v1.1 |
|---|---|
| Gate series resistor | 1K (R1 to R4). D89 changes R1 to R3 to 10 kΩ at the Q1 swap |
| 1 MΩ | Drain to ground (R5 to R8) |
| Gate pulldown | Q4 only (R9, 10K) |
| Gate zener | None |

## The recommendations, and what v2.0 does

| Priority | Change | Reason given | In v2.0 |
|---|---|---|---|
| 1 | Add a 15 V zener gate to source on all four stages, such as a 1N4744A | Clamps ESD and rail overvoltage. It sits above the 13.1 V Q4 could see on v1.1 and well above the 1050's 5 V lines | D1 to D4, 1N4744A |
| 2 | Move the 1 MΩ to gate-source on Q1 to Q3. Q4 needs only its 10K | Gives the gate a discharge path, and makes a broken wire read asserted, as the plan intends | R5 to R7 are 1M, and the drain 1 MΩ resistors are gone |
| 3 | Change R1 to R3 to 10 kΩ | Limits zener current and reduces the risk to the 1050's lines | R1 to R3 are 10K |
| 4 | Change R4 to 4.7 kΩ and keep a 10 kΩ pulldown | Gives Q4 more protection while its gate still sees about 8.2 V on an alarm | **Changed:** R4 is 10K, matching the other stages, and R8 is the 10K pulldown |
| 5 | Add 1 to 10 nF gate to source | Absorbs fast spikes, and is far quicker than the 100 ms input poll | C1 to C4, 10 nF |
| 6 | Use machine-pin TO-92 sockets, or at least carry spare FETs | Makes a repair at the gate quick, though sockets can corrode outdoors | **No sockets** (operator). Carry spare FETs |
| 7 | Clean the flux and conformal coat the board | Prevents leakage at the new high-impedance gates | A build step, not in the netlist |

**Change 3 also goes on v1.1 under D89**, so the deployed board has 10 kΩ on R1 to R3. The rest of this table
is for v2.0.

**Buy the FETs from an authorized distributor.** Poor-quality parts are one of the candidate causes.

**Change the bench method.** Use a toggle switch for each input instead of a moving lead, and drive IN3 to
IN5's gates from about 5 V. That removes the hand contact and tests the margin the gate will see in service.

## Q4's resistors

**R4 can match the other stages, but Q4's pulldown cannot be 1M.** The alarm output does not pull down when off, so
the pulldown alone sets how much leakage turns Q4 on. These figures are calculations:

| R4 / pulldown | Gate at 12 V | Gate at 14.4 V | Leakage that puts 0.8 V on the gate |
|---|---|---|---|
| 10K / 1M, the first draft | 11.9 V | 14.3 V | 0.8 µA |
| **10K / 10K, v2.0** | **6.0 V** | **7.2 V** | **80 µA** |
| 4.7K / 10K, change 4 | 8.2 V | 9.8 V | 80 µA |

At 6.0 V, Q4 has more gate drive than Q1 to Q3 get from the 1050's lines. 4.7K adds drive the stage does not need,
and passes more current than 10K in a fault. Q1 to Q3 cannot take a 10K pulldown, because a 10K divider would put
about 2.4 V on their gates, below the 3 V maximum threshold.

## The drain resistors

**v2.0 drops v1.1's four drain 1 MΩ resistors.** The plan puts the 1 MΩ from gate to source, and R5 to R7 now do
that job. On v1.1, each passes about 13 µA with its FET off, which fits the roughly 80 mV drop in M27's off-state
readings. An open drain wire reads de-energized without them, and in-circuit meter readings no longer show their
1 MΩ.

## How J1 is wired

**J1's two pins are both `Gnd`, and they carry different returns** (operator, 2026-11-04):

- One pin takes the 1050's GND, which is the ground reference for all four sources. The 1050's 12 V positive goes to
  the StamPLC's `EXCON_COM`, and reaches J4 and J5 through IN3 to IN6.
- The other pin goes to the common terminal of all the StamPLC's relays. It gives the relays the ground reference
  to pull their 1050 inputs low to assert them.

**This is a wire routing choice, and it saves another wire run to the 1050.** Electrically, both pins are the 1050's
common ground (operator).

## Candidate causes

The session ranked these causes, most likely first, and none is proven:

1. ESD into an unprotected gate, from a hand-held lead.
2. Poor-quality parts. Two suspect parts out of four is consistent with this, but it needs a batch test.
3. Rail overvoltage, if a battery disconnect let the panel's open-circuit voltage reach the 12 V rail. No
   evidence shows that this happened.
4. A slipped lead on a drain, which would more likely damage the channel than the gate.

The session rated heat and contact bounce unlikely. A stage dissipates at most about 50 mW.

## Tests that could narrow the cause (M27)

1. **Q1 in circuit, before it comes out:** with the lead at 12 V and then at 0 V, measure gate pin to source,
   drain to source, and across R1. More than a few mV across R1 means the gate is leaking.
2. **Removed Q2:** measure drain to gate, then gate to source.
3. **Gate leakage on Q1 and unused parts from the same batch:** apply 12 V to the gate through 1 MΩ and measure
   across that resistor. It should read about 10 mV or less, which is 10 nA.
4. **Threshold on the same parts:** a cheap transistor tester is enough. Faults in unused parts would point to
   the batch.

**Test 1 ran on 2026-11-03, on all four stages.** Q1's gate leaks in both states, and Q3's leaks with its input
high. Q4, the only stage with a gate pulldown, shows no leak near Q3's size. That result points at the gate, so it
keeps the case for changes 1 and 2, though four parts on one board are not proof (engineering log, 2026-11-03,
*M27's in-circuit test*). Q3 comes out with Q1 under D89. Tests 2 to 4 are still owed, and they now include the removed Q1 and Q3.

## What the review still has to decide

- **The PCB's fit in the enclosure.** The layout is 60 × 40 mm with two M3 holes, and its fit is not checked.
- **C1 to C4's footprint against the part: settled.** The footprint's 2.50 mm pitch against the part's 2.54 mm leads
  makes no practical difference (review, 2026-11-08).
- **The date v2.0 is built and swapped in**, which the operator picks later.

**v2.0 draws no way to disconnect a failed stage.** The operator accepts lifting its field wire at J1 for that.
This does not replace D90's firmware mask, which stays decided. A lifted SAFETY, EXIT or FIRE wire reads asserted, so
the mask is still what stops that reading from acting.

## PCB review, 2026-11-08

The layout passes DRC with schematic parity in KiCad 10.0.7, and its nets match the stages above (engineering log,
2026-11-08). The connectors were consolidated after the 2026-11-04 netlist: J1 is a 6-way terminal, Gnd, Gnd,
Safety, Exit, Fire and Alarm, and J2 a 4-way terminal, IN3 to IN6.
[`gatelink-input-board-v2.0.net`](./gatelink-input-board-v2.0.net) is the 2026-11-08 export.

| # | Finding | Fix |
|---|---|---|
| I1 | The Gerbers in `Gerbers/` predate the PCB's last save by four minutes | Regenerate them before ordering |
| I2 | The terminal names are on B.SilkS only | Repeat them on F.SilkS, between the terminals and R1 to R4 |
| I3 | Silkscreen text uses 0.1 mm lines, below JLCPCB's 0.15 mm minimum | Use 1.0 to 1.2 mm text with 0.15 to 0.18 mm lines |
| I4 | The stackup gives 1.65 mm | Set 1.6 mm, JLCPCB's standard thickness |
| I5 | ERC warns of one dangling wire stub near (138.43, 90.17) mm | Delete it. It changes no net |

Every copper and drill size is within JLCPCB's standard two-layer process.

## PCB re-review, 2026-11-08

The operator revised the layout after the first review, and this pass read the PCB saved at 15:28. It passes ERC with
no errors or warnings, and DRC with no violations and schematic parity. The circuit is unchanged:
[`gatelink-input-board-v2.0.net`](./gatelink-input-board-v2.0.net) matches a fresh export net for net.

| # | Status at the re-review |
|---|---|
| I1 | Open. The Gerbers in `Gerbers/` are from 2026-11-05, and the operator has not regenerated them yet |
| I2 | Fixed, but I6 found the J2 labels wrong |
| I3 | Open. All 22 silkscreen text items, top and bottom, use 0.1 mm lines, and the project's default text thickness is still 0.1 mm |
| I4 | Open. The saved stackup totals 1.65 mm |
| I5 | Fixed. ERC reports no dangling wire |

| # | Finding | Fix |
|---|---|---|
| I6 | **J2's F.SilkS labels read IN1, IN2, IN3 and IN4.** Its nets and its B.SilkS labels are IN3 to IN6, so an installer working from the top would land the IN3 and IN4 wires on the IN5 and IN6 terminals | Relabel them IN3 to IN6 |
| I7 | J1.6 reads ALR on F.SilkS and ALRM on B.SilkS | Use one name on both sides |

The operator reported the stackup and the silkscreen widths as corrected. The saved files read as above, so the
corrections may sit in KiCad unsaved. Changing the default in Board Setup does not change text already placed, and
*Edit Text and Graphics Properties* applies a new width to every item at once.

### Rerun at 16:27

The checks ran again on the PCB saved at 16:27. ERC reports no errors or warnings. DRC reports schematic parity and
one silkscreen warning, I8. The netlist is unchanged.

| # | Status at the rerun |
|---|---|
| I1 | Open. The Gerbers are still from 2026-11-05 |
| I3 | Fixed. All 22 text items use 0.15 mm lines, as do the project defaults |
| I4 | Fixed. The stackup totals 1.58 mm, and the order sets 1.6 mm |
| I6 | Fixed. J2's top labels read IN3 to IN6 |
| I7 | Fixed. J1.6 reads ALRM on both sides |

| # | Finding | Fix |
|---|---|---|
| I8 | DRC's `silk_overlap` warning: the top of the F.SilkS SFTY label touches J1's outline. It is legible in the 3D render, and the clipped stroke is cosmetic | Move the label about 0.5 mm away from J1, or accept the warning |

### Final check at 16:56

The checks ran again on the PCB saved at 16:56. ERC reports no errors or warnings. DRC reports no violations and
schematic parity, so I8 is fixed: the SFTY label clears J1's outline. The stackup now totals 1.6 mm, and the netlist
is unchanged. I1 is the only open item: the Gerbers are still from 2026-11-05.

## First-board checks

- Q1 to Q3 each switch cleanly from about 4.6 V through their 10K.
- Q4 switches on from a 12 V input, and its gate reads under 0.8 V with the input open.
- Repeat Q4's off-state reading on the 1050 at GL2, with its alarm off.

**Falsified by:** a v2.0 stage that reads wrongly at the 1050's 4.6 V, Q4's gate reading 0.8 V or more with the 1050
powered and its alarm off, or an M27 result that points away from the gate, which would lower the value of changes 1
and 2.
