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

### 2. `tx_conducted_dbm` and `tx_power_dbm` are one parameter under two names

**Found 2026-09-20**, in the same count.

The PRD calls the transmit power `tx_conducted_dbm`. Protocol Library Plan §4 declares it as
`tx_power_dbm`, at `param_id` `0x0114`.

**Root `CLAUDE.md` asks for one term per concept**, and this one is not only a term: a
parameter's name in that table becomes its Home Assistant `object_id`, which is permanent
once published (Protocol Spec §16.7).

**Correct statement:** Protocol Library Plan §4. The PRD's name has no other consumer.

## Fixed

None yet.
