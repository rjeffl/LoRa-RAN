# `<node>` — session handoff

<!--
  TEMPLATE. Copy to docs/<node>/HANDOFF.md, then delete every HTML comment as you fill
  the section it explains. A comment left in place means that section was not thought about.

  WHAT THIS FILE IS FOR. One question: a session that knows nothing opens it and learns
  what to do next, and what will waste their day. Nothing else earns a place here.

  THE PLACEHOLDER RULE, AND THIS REPOSITORY HAS BEEN BITTEN TWICE. Every blank below is
  `TODO(handoff): ...`, never `<angle brackets>` — two field captures went out with
  `<...>` unedited because a placeholder read as a value. Before committing:

      grep -n "TODO(handoff)" docs/<node>/HANDOFF.md      # must be empty
      grep -c "<!--" docs/<node>/HANDOFF.md               # must be 0

  SORT EVERY FACT BEFORE YOU WRITE IT (root CLAUDE.md, Workflow):

    Derivable   where main points, what is open, test counts, task ranges
                -> a command in this file, never prose. It cannot be kept true.
    Predictive  "once this merges, X"
                -> the PR description. Written here it is false at commit time.
    Durable     what a run measured, a trap, a gotcha, permanent-history commits
                -> here. This is what a handoff is.
-->

**Written TODO(handoff): date, at the end of the session that TODO(handoff): what it did.**
It replaces the previous file wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## Start here

<!--
  THE SECTION A SESSION READS FIRST, AND OFTEN THE ONLY ONE. A session does one task group
  and reads only what that task needs (root CLAUDE.md, Workflow). Give each queued task a
  ready-to-paste opening line and the sections it needs - not "read the whole file".
  Keep it under about twenty lines. Durable reference goes in traps.md, not here.
-->

**A session does one task group, and reads only what that task needs.** Open it with one
of these lines, then read this section and the sections the table names:

```text
Continue from docs/TODO(handoff)/HANDOFF.md: TODO(handoff): the task.
```

| Task | Read |
|---|---|
| TODO(handoff): the task | TODO(handoff): the sections of this file and the documents it needs |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

<!--
  One answer, not a survey. A cold session reads this and starts work.
  If the next job is a decision rather than a task, say so and say where it starts -
  "D1 does not start in this directory" saved a whole session once.
  If a procedure document exists for it, link it here and nowhere else first.
-->

TODO(handoff): the one next thing, and where its procedure lives.

## What the last session established

<!--
  Results a future reader needs and cannot recompute. Numbers with units.
  Include the negative results and the things that surprised you - they are the ones
  that get re-derived expensively otherwise.
  Say what a result does NOT support. That sentence has earned its place more than once.
-->

TODO(handoff): results, with what each one does not support.

## Read these, in this order

<!--
  A table with a WHY column. A bare list of filenames gets skipped.
  Order it for a cold reader, not by importance to you.
-->

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | TODO(handoff) | TODO(handoff) |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | TODO(handoff): milestone identifiers only |
| Queue | TODO(handoff): what is owed, or **empty** |

<!--
  NO COUNTS AND NO SHAs IN THIS TABLE. Both were wrong more often than right.
  List the commands that produce them instead, below.
-->

```bash
TODO(handoff): the build and test commands that work today, one per line, commented
```

**CI runs these on every push and pull request.** Run them locally when you are about to
spend bench time on the result; otherwise a green run already answers it.

## Git state — ask git, do not read it here

> **Where `main` points, what merged last, which branches exist and whether a PR is open
> are deliberately not written in this file.** A written SHA is wrong the moment the branch
> carrying it merges, and it is wrong in the worst direction — confidently, in a file whose
> whole value is being trustable cold.

```bash
git fetch origin -p                            # prune deleted remote branches first
git log --oneline -1 origin/main                # where main actually is
gh pr list --state open                         # what is open, if anything
git log --branches --not --remotes --oneline    # local-only work; empty is good
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository.

**Permanent history is citable; moving state is not.** A commit SHA naming work a reader
should go and read is durable and belongs here. "`main` is at `abc1234`" is not.

<!-- Durable push and auth gotchas go here - the workflow-scope refusal, an askpass hang. -->

## Hardware state

<!--
  WRITE THIS ONE OUT, AS THE TABLE BELOW. It is the section that cannot be derived, and a
  wrong device or a stale stored state is SILENT - it produces a normal-looking artifact
  with the wrong configuration in it.

  THIS TABLE IS PER SUBPROJECT AND IS NOT SHARED. Another subproject's table describes its
  devices in ITS roles, so it does not transfer and must not be cited in place of this one.
  Where the same physical device appears in two subprojects, each names it in its own terms
  and both say so; copying the other's row is how a wrong pin map reached two documents
  before (root CLAUDE.md, "a load-bearing premise must name the check that would falsify it").

  Add or drop columns to fit the subproject. Keep "Told apart by" whatever else changes.
-->

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| TODO(handoff): what it is, and the physical detail that identifies it | TODO(handoff): the short name the rest of this file uses | TODO(handoff): something that does not expire | TODO(handoff): build env, and what it was last flashed from | TODO(handoff): what is in NVS / on disk, and what losing it would cost | TODO(handoff): powered, parked, deployed, or off - and where it physically is |

**Identify a device by something that does not expire.** Serial numbers collide, port names
are not identities, and a software discriminator disappears the moment someone erases the
thing it keys on. A physical difference — an enclosure, a label, a fitted antenna — does not.

<!--
  Below the table, say per subproject:
    - what a wrong selection would silently produce, so the reader knows the cost;
    - which stored state is the only copy, and which is committed somewhere;
    - anything that must be powered down during a measurement, and why.
-->

TODO(handoff): what a wrong selection produces silently, and which stored state is the only
copy.

## Behaviour that changed, and will make older artifacts read differently

<!--
  Version strings, defaults, pin maps, output formats. A reader comparing a new capture
  to an old one needs to know what moved between them and what did not.
  Say plainly that older artifacts are CORRECT for when they were made. Do not re-stamp them.
-->

TODO(handoff): what changed, from when, and what it does not invalidate.

## Traps that cost real time here

<!--
  One line each, index style, and only the traps the next job meets first. The full set
  lives in docs/<node>/traps.md, which is kept current; the engineering log holds the
  full account.
  Earn a place by having cost an hour. Prefer traps whose symptom points at the wrong
  cause - those are the expensive ones.
-->

- TODO(handoff): the trap, and the symptom it presents as.

## Open, and not closable from here

<!-- Identifier, one line on why it is open, and what would close it. -->

- TODO(handoff)

### Closed, and not to be reopened by habit

<!--
  Things that got re-litigated or re-run. Say CLOSED, the date, and where the result lives.
  If something is not closable by construction, say that instead of leaving it open -
  a permanently open item reads as a task nobody got to.
-->

- TODO(handoff)
