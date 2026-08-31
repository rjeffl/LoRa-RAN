# LRAN documentation

System-level, shared and node-specific documents for the LoRa Remote Automation Network.

**Start here:** [`LRAN-System-PRD`](./LRAN-System-PRD.md) — architecture, node roles, repo
layout, and §12's document set table, which carries the current version of every live
document. **A node document citing an older protocol version than the specification's own
has not been reconciled with the intervening revisions.**

| Folder | Contains |
|---|---|
| *(this level)* | [`LRAN-System-PRD`](./LRAN-System-PRD.md) — the parent document |
| [`shared/`](./shared/) | Documents binding on more than one node: the [Protocol Specification](./shared/LRAN-Protocol-Specification.md) (**authoritative for every byte on the wire and every MQTT topic**), the [Decision Register](./shared/LRAN-Decision-Register.md) (**the only place a decision's status is recorded**), and the [Protocol Library Implementation Plan](./shared/LRAN-Protocol-Library-Implementation-Plan.md) |
| [`bridge/`](./bridge/) | LoRaBridge (`0x00`) PRD and implementation plan. The plan also owns `lran-simnode` (§10) |
| [`gatelink/`](./gatelink/) | GateLink (`0x01`) PRD, implementation plan, and the expansion board design |
| [`welllink/`](./welllink/) | WellLink (`0x02`) PRD — **placeholder**, scope and reserved allocations only |
| [`rangetest/`](./rangetest/) | Range test firmware tasks and engineering log. **The next firmware target** — it answers D1, M6, M20 and W9 |
| [`protocol-lib/`](./protocol-lib/) | Engineering log for `/lib/lran-protocol/` |
| [`archive/`](./archive/) | Superseded revisions and the Research Archive. **Retained, not deleted:** old material references these by name, and a reader following such a reference needs to land on an explanation rather than a gap |

## Conventions

- **The protocol specification is authoritative** for anything on the wire. No other
  document may redefine a frame layout, an enumeration value, a schema ID or a topic.
- **Requirements documents** (`*-PRD`) state goals and requirements. **Implementation
  plans** are what is handed to Claude Code for a target.
- **Engineering logs** are dated running records — what was tried, measured, decided and
  why. One per node, at `<node>/engineering-log.md`, created at that node's bring-up.
- Every document carries a version, a status and a `Last updated` date in its header, and
  a changelog as its final section. Both are updated in the same commit as the change.
