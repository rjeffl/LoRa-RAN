---
name: lran-task
description: Start an LRAN task group from a node's HANDOFF.md. Use when the operator says "Continue from docs/<node>/HANDOFF.md: task X", "start group N", "start the <name> task", or names a task id from a handoff table.
argument-hint: "<node> <task>, for example: gatelink L7"
---

# Start an LRAN task

The operator names a node and a task. The goal is to start work with only the sections
that task needs in context. CLAUDE.md's *Workflow* sets the budget: aim to finish by 20 %
of context. Reading is the biggest cost, so this procedure keeps the reading narrow.

## 1. Find where the handoff really is

```bash
git fetch origin -p && git status -sb | head -1
gh pr list --state open
```

A draft PR for this node means its handoff and task documents live on **that branch**, and
`main`'s copy is stale. Check out the branch before reading. Two machines push to this
repository, so trust nothing local until the fetch has run.

## 2. Read the handoff by section, not whole

```bash
python3 tools/docs/section.py docs/<node>/HANDOFF.md                      # outline
python3 tools/docs/section.py docs/<node>/HANDOFF.md "Start here" --shallow
```

Then read only the sections that the *Start here* table names for this task. Read each
cited plan or spec section the same way:

```bash
python3 tools/docs/section.py docs/<node>/LRAN-<Node>_Node-Implementation-Plan.md 8.2
python3 tools/docs/section.py docs/shared/LRAN-Protocol-Specification.md 7.2.3
```

Don't read a plan or the specification whole, and don't search for line ranges with
`sed -n`. If the handoff names a file without a section, run the outline first and pick
the section from it. If the task needs a whole-document reconciliation, hand the reading
to an Explore subagent and ask for a discrepancy list with section numbers. Make the edits
here, from that list.

Report the context in use once the reading is done (`get_usage`). If it's past 20 %, say
so before writing code, as CLAUDE.md asks.

## 3. Branch and load the writing rules

```bash
git switch -c <milestone-branch> origin/main     # named for the milestone: p4-schemas
```

Skip this if step 1 found the branch. Invoke the `write-clearly` skill before writing any
prose: docs, comments, commit messages and the PR description.

## 4. Say what's needed from the operator

Before you start, list the boards that must be connected, whether the broker must be up,
and any secrets.h field the task reads. Bench work then goes through
`tools/bench/bench.py`; its README has the loop. Don't write a new serial or MQTT script
in a scratchpad.

## 5. Work, then close with /lran-close

Append dated findings to `docs/<node>/engineering-log.md` as they happen. When the
acceptance criteria are met, or the session nears its budget, run `/lran-close`.
