# LRAN — GateLink input board v1.2 brief

**Document:** `LRAN-Input-Board-v1.2-Brief`
**Version:** 0.1
**Status:** Recommendations for review. Nothing here has been built, and no value here has been measured on a v1.2 board.
**Last updated:** 2026-11-03

## Summary

**Input board v1.2 is a PCB, and these are the changes to review before ordering it.** D89 deploys
GateLink on the repaired v1.1 board. D91 tracks v1.2's content and the date it is built. The
recommendations come from the input-board session of 2026-11-03, after the operator found Q1 and Q2
faulty. That session said rev 1.2 could be reworked on the existing perfboard. The operator rejected that:
the changes are more extensive than a hand-built perf board will survive, so v1.2 starts from scratch.

The cause of the Q1 and Q2 failures is not established, so these changes do not rest on a known cause.
M27 tracks the tests that could narrow it.

## What the stages drive

Q1 to Q4 serve IN3 to IN6. The 2N7000s buffer the 1050's signals to the StamPLC's opto inputs.

| Input | FET | Source at the 1050 | Undriven | Fault seen |
|---|---|---|---|---|
| IN3 | Q1 | 1050 input, pulled high internally to 5 V, reaching 4.6 to 4.7 V, and driven low by an external device | Pulled high by the 1050 | Suspect (engineering log, 2026-11-03) |
| IN4 | Q2 | The same type as IN3 | Pulled high by the 1050 | Replaced 2026-11-02 |
| IN5 | Q3 | The same type as IN3 | Pulled high by the 1050 | None ever observed |
| IN6 | Q4 | A FET driving the output to 12 V | No apparent pulldown when off, so R9 holds the gate low | None recorded |

The operator's history bounds the failures. All stages tested good when the board was built, so the failures
came after installation. The stages have never been connected to the 1050.

## v1.1 as built

From [`Gatelink-Input-Board.net`](./Gatelink-Input-Board.net). Impl Plan §3.2 differs from this and has not
been reconciled.

| Item | v1.1 |
|---|---|
| Gate series resistor | 1K (R1 to R4) |
| 1 MΩ | Drain to ground (R5 to R8) |
| Gate pulldown | Q4 only (R9, 10K) |
| Gate zener | None |

## Recommended changes

| Priority | Change | Reason given |
|---|---|---|
| 1 | Add a 15 V zener gate to source on all four stages, such as a 1N4744A | Clamps ESD and rail overvoltage. It sits above the 13.1 V Q4 can see and well above the 1050's 5 V lines |
| 2 | Move the 1 MΩ to gate-source on Q1 to Q3. R8 on Q4 can go, because R9 already does this job | Gives the gate a discharge path, and makes a broken wire read asserted, as the plan intends |
| 3 | Change R1 to R3 to 10 kΩ | Limits zener current and reduces the risk to the 1050's lines |
| 4 | Change R4 to 4.7 kΩ and keep R9 at 10 kΩ | Gives Q4 more protection while its gate still sees about 8.2 V on an alarm |
| 5 | Add 1 to 10 nF gate to source | Absorbs fast spikes, and is far quicker than the 100 ms input poll |
| 6 | Use machine-pin TO-92 sockets, or at least carry spare FETs | Makes a repair at the gate quick, though sockets can corrode outdoors |
| 7 | Clean the flux and conformal coat the board | Prevents leakage at the new high-impedance gates |

**Change 3 can also reach v1.1**, because the operator can replace the series resistors without a full
rebuild. The engineering log, 2026-11-03, *IN5 and IN6 have no inhibit today*, has the estimates. D89 records
the choice.

**Buy the FETs from an authorized distributor.** Poor-quality parts are one of the candidate causes.

**Change the bench method.** Use a toggle switch for each input instead of a moving lead, and drive IN3 to
IN5's gates from about 5 V. That removes the hand contact and tests the margin the gate will see in service.

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

## What the review still has to decide

The session did not cover these:

- The PCB's outline, mounting and connectors in the enclosure.
- Whether the sockets in change 6 stay, given the outdoor corrosion risk.
- Whether v1.2 needs a way to disconnect a failed stage, given D90's firmware inhibit and that an inhibit
  cannot protect the 1050.
- The date v1.2 is built and swapped in, which the operator picks later.

**Falsified by:** a v1.2 stage that reads wrongly at the 1050's 4.6 V, or an M27 result that points away from
the gate, which would lower the value of changes 1 and 2.
