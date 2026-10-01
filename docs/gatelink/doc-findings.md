# GateLink — documentation findings, open

**Defects found in GateLink's documents by work on other branches, deferred to the GateLink
milestone rather than fixed where they were found.** The operator's direction on 2026-09-20:
GateLink's implementation will surface more of these, so they are fixed in one pass rather
than one branch at a time.

**This file is a list of open findings, not a record.** A finding leaves it when it is
fixed, and the fix names it in its commit message. It is not a changelog, and nothing here
is a decision — a decision goes in
[`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md).

**Each entry names where the correct statement lives**, so the fix is a reconciliation
rather than a rewrite.

## Open

### 2. `tx_conducted_dbm` and `tx_power_dbm` are one parameter under two names

**Found 2026-09-20**, in the W10 parameter count that found finding 1.

The PRD calls the transmit power `tx_conducted_dbm`. Protocol Library Plan §4 declares it as
`tx_power_dbm`, at `param_id` `0x0114`.

**Root `CLAUDE.md` asks for one term per concept**, and this one is not only a term: a
parameter's name in that table becomes its Home Assistant `object_id`, which is permanent
once published (Protocol Spec §16.7).

**Correct statement:** Protocol Library Plan §4. The PRD's name has no other consumer.

### 3. PRD R-4.3b forbids the RF-switch line the chosen module needs

**Found 2026-10-01**, reconciling the Implementation Plan with
[`gatelink-expansion-board`](./gatelink-expansion-board.md) for plan v0.18.

R-4.3b says the module *"SHALL NOT require separate TXEN/RXEN lines."* The carrier's module,
the Wio-SX1262 for XIAO, needs one: Seeed does not tie DIO2 to the RF switch, so the board
spends G40 on `setRfSwitchPins(rf_sw, RADIOLIB_NC)` alongside DIO2-as-RF-switch.

**Correct statement:** expansion board §6 and §7.3, confirmed 2026-09-05 against both Wio
products' board support. R-4.3b's reason, a pin budget with no room, was true of the
earlier pin map and is answered by rev 0.3's.

### 4. PRD R-4.3d and D26 describe a regulator the carrier does not use

**Found 2026-10-01**, in the same reconciliation.

R-4.3d asks for a local 3.3 V regulator with dropout ≤300 mV, because `EXT_5V` sits near
4.76 V. D26 excludes the AMS1117 for the same reason. The expansion board does not use
`EXT_5V`: it feeds an AMS1117 from its own 12 V → 5 V buck on Bus pin 1.

**Correct statement:** expansion board §4. D26's finding, that the StamPLC exposes no 3.3 V
rail, still holds; its consequence needs a register amendment, not an edit to the closed
row.

### 5. PRD R-4.3f requires a DIN-mounted carrier, and the carrier is cantilevered

**Found 2026-10-01**, in the same reconciliation.

R-4.3f and D27's sub-item **M15** call for a DIN-rail carrier with the perfboard cut to it.
The expansion board mates the carrier directly to the StamPLC on a right-angle header,
cantilevered, with a standoff at the far end (§3).

**Correct statement:** not yet decided. Either R-4.3f changes to accept the standoff, or
the carrier gains a DIN mount. The operator decides.

### 6. VE.Direct is "5 V" in the plan and 3.3 V in a measurement

**Found 2026-10-01**, in the same reconciliation.

Implementation Plan §4.2 says all Victron MPPTs are 5 V devices. Expansion board §7.4
measured this unit's TX pin idling at 3.25 V, and concludes it needs no translation. An idle
level does not settle D25, which is about the low level against a 10 kΩ load.

**Correct statement:** not yet established. **Measurement M4** settles D25. The plan keeps
both statements until it runs.

### 7. Milestone numbers collide with measurement numbers

**Found 2026-10-01**, in the same reconciliation.

The Implementation Plan's milestones are M0–M9, and the Decision Register's measurements
are M1–M26. Milestone M0 depends on measurement M4, and milestone M5 settles measurement
M7. Plan v0.18 writes "measurement M*n*" to tell them apart, which is a workaround.

**Correct statement:** none yet. Renaming the milestones (to `GL0`–`GL9`, say) reaches the
bridge's documents, which cite "GateLink M6". The operator decides.

### 8. What the StamPLC's INA226 measures is stated two ways

**Found 2026-10-01**, in the same reconciliation.

Expansion board §7.7 says the INA226 measures the **bank's** voltage and current. PRD R-4.4b,
Implementation Plan §9.8 and V-11 use it for the **node's** own supply.

**Correct statement:** not yet established. **Measurement M12**, at milestone M1, finds
which current passes through its shunt.

### 9. System PRD §3.5 requires `/lib/lran-platform/`, and the plan no longer builds it

**Found 2026-10-01**, in the same reconciliation.

System PRD §3.5 says `/lib/lran-platform/` SHALL abstract the StamPLC for GateLink and
AquaLink. The operator chose a firmware-local board layer on 2026-10-01 (Implementation Plan
v0.18 §5.3), because no firmware in this repository needs to share it yet. §3.5 also asks
for one SX1262 driver; the fleet has three, each with injected pins.

**Correct statement:** Implementation Plan v0.18 §5.3. §3.5 needs amending, and AquaLink's
own project should hear of it.

## Fixed

### 1. PRD §5.3.1 lists the LoRa PHY parameters as not runtime-configurable

**Found 2026-09-20**, while counting parameters for **W10** on branch `spec-v0.13`.

§5.3.1 puts the PHY under *"what is not runtime-configurable"*, reasoning that *"changing
these from HA means changing the link you are changing them over; one mismatch and the node
is unreachable until someone walks to it with a laptop."*

**D56 decided the opposite on 2026-09-19**, and Protocol Spec §12.4's commit-and-revert is
the answer to exactly that objection: the old settings are persisted before the radio is
retuned, confirmation is a frame *received* on the new settings, and both ends revert on
silence. Protocol Library Plan §4 declares all six rows for every node, `READ_ONLY` until
**BF-33** builds the path.

**Correct statement:** Decision Register D56, Protocol Spec §12.4, Protocol Library Plan §4.

**Why it matters beyond wording.** Those six rows are 42 of the 171 bytes a GateLink
readback occupies, against 193 available. A reader who takes §5.3.1 at its word counts the
readback 42 bytes light.

**Fixed 2026-09-23** in GateLink PRD v0.9, on branch `spec-v0.13-d58`. The operator brought
it forward to the protocol specification's v0.13 citation sweep, because the PRD could not
cite v0.13 while contradicting D56.
