# LRAN documentation

System-level, shared and node-specific documents for the LoRa Remote Automation Network.

**Start here:** [`LRAN-System-PRD`](./LRAN-System-PRD.md) — architecture, node roles, repo
layout, and §12's document set table, which carries the current version of every live
document. **A node document citing an older protocol version than the specification's own
has not been reconciled with the intervening revisions.**

| Folder | Contains |
|---|---|
| *(this level)* | [`LRAN-System-PRD`](./LRAN-System-PRD.md) — the parent document |
| [`shared/`](./shared/) | Documents binding on more than one node: the [Protocol Specification](./shared/LRAN-Protocol-Specification.md) (**authoritative for every byte on the wire and every MQTT topic**), the [Decision Register](./shared/LRAN-Decision-Register.md) (**the only place a decision's status is recorded**), the [Protocol Library Implementation Plan](./shared/LRAN-Protocol-Library-Implementation-Plan.md), the [P8 `CommandGate` decision brief](./shared/LRAN-P8-CommandGate-Brief.md) (**superseded** — D34 amended 2026-09-11; the register's §3.2.1 is the record), the [D1 PHY decision brief](./shared/LRAN-D1-PHY-Decision-Brief.md) (**superseded** — D1 closed 2026-09-10; the register's §3.4 is the record), the [site RF inventory](./shared/LRAN-Site-RF-Inventory.md) (**reference** — the property's Z-Wave, Insteon, YoLink and Davis radios), and the [D1 frequency change brief](./shared/LRAN-D1-Frequency-Change-Brief.md) (**draft for decision** — 917.4 to 917.2 MHz) |
| [`bridge/`](./bridge/) | Bridge Node (`0x00`) PRD, implementation plan and [firmware task list](./bridge/LRAN-Bridge-Firmware-Tasks.md) and [session handoff](./bridge/HANDOFF.md). The plan also owns `lran-simnode` (§10) |
| [`gatelink/`](./gatelink/) | GateLink (`0x01`) PRD, implementation plan, and the expansion board design |
| [`welllink/`](./welllink/) | WellLink (`0x02`) PRD — **placeholder**, scope and reserved allocations only |
| [`rangetest/`](./rangetest/) | Range test firmware tasks (pass 1 and pass 2) and engineering log. Answered **M6**, **M20** and **W9**, and gathered D1's inputs |
| [`protocol-lib/`](./protocol-lib/) | Engineering log for `/lib/lran-protocol/` |
| [`archive/`](./archive/) | Superseded revisions and the Research Archive. **Retained, not deleted:** old material references these by name, and a reader following such a reference needs to land on an explanation rather than a gap |

## Conventions

- **The protocol specification is authoritative** for anything on the wire. No other
  document may redefine a frame layout, an enumeration value, a schema ID or a topic.
- **Requirements documents** (`*-PRD`) state goals and requirements. **Implementation
  plans** are what is handed to Claude Code for a target.
- **Engineering logs** are dated running records — what was tried, measured, decided and
  why. One per node, at `<node>/engineering-log.md`, created at that node's bring-up.
  **Split a log when a reader resuming work can no longer find the latest entries
  quickly.** At a milestone boundary, move whole entries, unedited, into
  `engineering-log-<first date>_<last date>.md` beside it, and name that file at the top of
  the live log. Keep in the live file every entry that an open investigation still relies
  on. Moving an entry does not rewrite it. The bridge log was split this way on 2026-09-18.
- Every document carries a version, a status and a `Last updated` date in its header, and
  a changelog as its final section. Both are updated in the same commit as the change.

### A document must not record where a branch currently points

`docs/<node>/HANDOFF.md` files, and any other document, name no `origin/main` SHA, no
"merged through #N", no current branch and no open-PR status. These went stale on every
merge, and the correction could not ride along with the work that caused it: the branch
being described is the branch doing the describing, so each fix needed its own branch and
PR. Two of the five commits before 2026-09-09 on the range-test handoff exist for nothing
else.

Sort a fact into one of three places, by whether it can be kept true:

| The fact is | Where it goes |
|---|---|
| **Derivable** — where `main` points, what is open, which branches exist, whether anything is local-only | A command in the document, never prose. `git fetch origin -p` first; two machines push here |
| **Predictive** — "once this merges, `main` carries X" | The PR description. It is read at review time and is about a proposed state by nature. A handoff written this way is *false when committed*, which is worse than silent |
| **Durable** — what a run measured, a trap, a push gotcha, "read these two commits in order" | The document. This is what a handoff is for |

Permanent history is citable; moving state is not. `0f21c23` will always be that commit, so
cite it freely. "`main` is at `9fee445`" describes where a pointer sat one afternoon.

Start a node's handoff from [`HANDOFF-TEMPLATE.md`](./HANDOFF-TEMPLATE.md). It carries the
sections in reading order, this rule applied inline, and what each section is for written
where it is needed. Its blanks are `TODO(handoff):`, not `<angle brackets>`, because two
field captures went out with `<...>` unedited — so a half-filled handoff is greppable:

```bash
grep -n "TODO(handoff)" docs/<node>/HANDOFF.md   # empty before commit
grep -c "<!--" docs/<node>/HANDOFF.md            # 0 before commit
```

**Commit the handoff on the session's own branch while that branch is still open.** On a
branch of its own, a handoff needs its own PR and review, and it can describe work that
reaches `main` only through another PR. #85 had to tell its reviewer to merge #83 first,
because the handoff cited commits that only #83 carried. On the branch whose work it
describes, the handoff merges together with that work. When a session's work spans several
open branches, put the handoff on the one meant to merge last, and say so in that PR's
description. Open a `docs/handoff-*` branch only when everything the session did has
already merged.

### A load-bearing premise must name the check that would falsify it

If a document's argument rests on a factual premise — *"these two boards share a pad
assignment"*, *"this counter cannot move"* — then say what would prove it false, and point
at the place that check is actually tracked: an `M-*` item, a verify-before-build checklist,
a test. Prose that states a falsification condition and tracks it nowhere reads like
diligence and behaves like nothing.

The case that produced this rule: Bridge Impl Plan §10.8.1 wrote *"if it ever stops being
true, §2.3's claim collapses"* — and when it did stop being true, nothing surfaced it. It
was found by an audit somebody thought to ask for, after the wrong pin map had already been
copied into two other documents.
