# Range test — session handoff

**Written 2026-09-23, by a session that did no range-test work.** It shortened the
2026-09-09 handoff, whose sessions ended with nothing owed here, and it replaces that file
wholesale. The full 2026-09-09 text stays readable with `git show
296805d:docs/rangetest/HANDOFF.md`: B1b's results, the A/B split, the responder swap, §7.6,
the NVS collision, the well bearing, M20's results and W9. The engineering log has the full
account of each.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees
> with the documents below, they win — check the log's last entry against the date above
> before trusting anything here.

## Start here

**A session does one task group, and reads only what that task needs.** Nothing is owed
here, so a range-test session happens only when the operator opens one of these:

```text
Continue from docs/rangetest/HANDOFF.md: repeat the Wio A/B substitution.
Continue from docs/rangetest/HANDOFF.md: add the capture.py --note placeholder guard.
```

| Task | Read |
|---|---|
| **The A/B repeat** (field) | *The next job*; *Hardware state*; [`traps.md`](./traps.md); [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md); the engineering log's 2026-09-09 A/B entry |
| **The placeholder guard** (no board) | *The next job*; `tools/rangetest/capture.py`; [`CAPTURE-PY.md`](./CAPTURE-PY.md) |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**Nothing is owed. This directory has no queued measurement or firmware work.** D1 and D33
closed on 2026-09-10 against data this directory produced (Decision Register §3.4.1), and
W7 closed with them. M6, M20 and M21 closed earlier. **Do not schedule a well walk**, and
**do not re-run the §7.6 role permutations.**

Two optional tasks would each answer something real, and nothing blocks on either:

- **Repeat the A/B substitution.** 2026-09-09 got the Wio at TX ≈ −6.0 dB and RX ≈ −3.2 dB
  against the Heltec, from **one** pair of sweeps at G2's mount, ~±1 dB on each. Use the
  same mount, leave the initiator untouched, **alternate Wio–Heltec–Wio** so drift shows in
  the data, and photograph the mount. Three sweeps make ±1 dB defensible. The deployed
  margin figures already carry the Wio's penalty.
- **A `capture.py` guard against `--note` placeholders.** Refuse, or warn loudly, on a note
  containing `<...>`. Two runs in three went out with placeholders unedited, and bold text
  under the command has failed twice, so this is a tooling fix rather than a discipline one.

**Four rules decide whether a field run is worth anything.** `FIELD-PROCEDURE.md` has the
procedure.

1. **Power the third board OFF.** Not in a backpack, not in `SURVEY` — off.
2. **Clear the responder's position log**, and see `# position log cleared` come back.
3. **Connect antennas before power**, every time.
4. **Reset the initiator whenever you change responders.** A fresh responder starts a sweep
   with no button press.

## Hardware state

**No board runs range-test firmware now.** The bridge handoff's *Hardware state* table is
the current record for all three. **Reflash each board with `firmware/range-test` before any
range work, and record that in the bridge handoff first**, because the bridge bench loses
the board.

| Board | Env | Range-test role | Runs now |
|---|---|---|---|
| Heltec V3, **handheld dev-board case** | `heltec` | B1b's **Heltec #1** | simnode firmware, since 2026-09-14 |
| Heltec V3, **Meshtastic flat case** | `heltec` | B1b's **Heltec #2**, the initiator | bridge firmware, since 2026-09-13 |
| XIAO ESP32S3 + Wio-SX1262 **Kit** | `xiao` | B1b's walking responder | simnode firmware (`simnode-xiao-wio`) |

**Tell the two Heltecs apart by enclosure.** The settings dump reads `heltec` for both, and
both CP2102 bridges report `SER=0001`. **Read `board=` off the settings dump** — a wrong
board selection is silent and writes the wrong pin map and antenna gain into a
normal-looking CSV. **The role is not persisted**; every board asks at boot.

**NVS on a bench board is not a backup.** Every survey site is committed in
`data/2026-09-05-survey-campaign-r11.csv` and `data/2026-09-05-survey-campaign.csv`.

## Git state — ask git, do not read it here

```bash
git fetch origin -p                            # prune deleted remote branches first
git log --oneline -1 origin/main                # where main actually is
gh pr list --state open                         # what is open, if anything
git log --branches --not --remotes --oneline    # local-only work; empty is good
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository. The
bridge [`traps.md`](../bridge/traps.md#git-branches-and-merging) has the merge and push
gotchas.

## Open, and not closable from this firmware alone

- **The Wio's TX/RX split** — measured once by B1b's A/B. Not closed, because it wants the
  repeat above. §7's ruling against more **role permutations** stands.
- **§2.3.1 sub-question (b)** — the sleep-current cost of holding `RF_SW` high. Untouched. A
  meter on the carrier, not a walk.
- **`gatelink-expansion-board.md` §10 ring-out** — the header board's pads against the
  carrier's nets. **The Kit cannot close it.** It is the tracked check for Bridge Impl Plan
  §10.8.1's remaining premise.
- **Not range-test work:** **M22** belongs to the bridge; **M23** belongs to GateLink; the
  24 in trunk partly blocking the gate-to-house line is GateLink's siting question, and no
  node document carries it yet.
