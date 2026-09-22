# LRAN conventions rationale — why the repo-wide rules are what they are

**Document:** `LRAN-Conventions-Rationale`
**Version:** 0.1
**Status:** **Reference.** This document holds the reasoning behind the rules in the
repo-root [`CLAUDE.md`](../../CLAUDE.md). It binds nothing on its own: where it and
`CLAUDE.md` disagree about what a rule *is*, `CLAUDE.md` wins.
**Parent document:** [`CLAUDE.md`](../../CLAUDE.md)
**Last updated:** 2026-09-22

Read this when a rule looks wrong, arbitrary, or worth changing. Each section names the
rule, then gives the failure that produced it. Several rules exist because the obvious
alternative was tried and cost someone an afternoon.

`CLAUDE.md` is loaded into context on every session, so its length is charged on every
turn. The reasoning here is read once, by a person who has a reason to question a rule.
That is why the two are separate files.

---

## Contents

- [Wire format and framing](#wire-format-and-framing)
- [Radio and regulatory](#radio-and-regulatory)
- [The test vectors](#the-test-vectors)
- [Why Layout carries only `[built]` and `[planned]`](#why-layout-carries-only-built-and-planned)
- [Why the command catalogue is not in `CLAUDE.md`](#why-the-command-catalogue-is-not-in-claudemd)
- [Writing rules](#writing-rules)
- [Documents are guidance, and they are works in progress](#documents-are-guidance-and-they-are-works-in-progress)
- [Changelog](#changelog)

---

## Wire format and framing

### Never `memcpy` a struct to or from the wire

Serialize field by field, explicitly, little-endian.

Host tooling uses a different compiler and a different architecture from the nodes.
Layout-dependent code works on two ESP32s and breaks the moment the bench tooling is
written. The failure arrives late, in a tool nobody was debugging, and it looks like a
protocol bug rather than a struct-packing bug.

### Never vary `seq` on a command retry

The retry reuses the same `seq`, so the node's `(ctx_id, seq)` deduplication returns the
cached ACK.

Incrementing `seq` looks like a fix for a stuck command. At the gate it is a second relay
pulse: the first command was received and acted on, and the retry is a new command as far
as the node can tell. The gate opens twice, or opens and then closes.

### Never discard a frame silently

Every discard increments a named counter and maps to one `Status` value and one §14 stage.
A frame that vanishes without a counter is indistinguishable from a frame that never
arrived, which makes a link problem and a parser problem look identical from the bridge.

### Reserved fields and bits are written zero and ignored on receive

The one exception is `hdr_flags` bit 7 (`CRITICAL_EXT`), which is validated.

Validating the rest as zero breaks forward compatibility and the header extension space. A
node built today would reject a frame from a bridge built after the next revision, and the
nodes are the side that cannot be reflashed without a walk.

### Sentinels, not zero, for unavailable

`INT16_MIN`, `UINT16_MAX`, `UINT32_MAX`. A consumer must be able to tell "0 A" from "no
reading." Zero is a plausible measurement for most of what the nodes report, so zero as a
"missing" marker makes a dead sensor look like a working one reading nothing.

### No dynamic allocation in `/lib/` or in any node firmware

Fixed buffers, caller owned, sizes derived from `LRAN_MAX_FRAME`. A heap fragmentation
failure on a solar node months into deployment is a walk to the gate with a laptop.

### `/lib/lran-protocol/` must keep building in the `native` environment

No Arduino header, no ESP-IDF header, no `millis()`, no `Serial`. Time is passed in as an
argument.

The native build is what makes the host Unity suite and the vector generator possible. When
it breaks, the tests stop being runnable without hardware, and the loop that catches codec
errors in seconds becomes a flash-and-watch loop. That is a defect to fix now, not a
nuisance to route around.

### Anything timing-related is runtime-configurable

No timing constant is fixed at compile time in a node that cannot be reflashed without a
walk to the gate. GateLink is ~87 m (285 ft) from the house and has no OTA.

---

## Radio and regulatory

### RadioLib is the SX1262 driver everywhere (D32), and its version is pinned in every `platformio.ini`

A driver shared by four firmwares is not a thing to let float.

**The pin now has a consumer that depends on the driver's internals.**
`firmware/range-test/src/pa_config.cpp` mirrors RadioLib's file-static `paOptTable` so the
applied PA configuration can be logged; the SX1262's PA config cannot be read back from the
chip. A RadioLib bump that changes that table is silent in every other check in the repo,
which is why `python3 tools/rangetest/check_pa_table.py` exists and why it runs after any
version change.

### The radio config struct is injected, never `#define`d

Spec §12.2. The pin map, TCXO reference voltage and DIO2-as-RF-switch flag are passed as a
config struct.

The two voltage and switch settings fail *silently* on the Heltec V3. A wrong value
presents as a radio that will not calibrate, not as an error naming the setting. Injecting
them as data makes the value visible at the call site and testable on the host.

### TX power is capped by D33

At or below the FCC §15.249 EIRP ceiling: about −1 dBm EIRP, which is **−4 dBm conducted
with the fitted 3.0 dBi antenna**. Single fixed channel, no hopping.

Record conducted power and antenna gain separately. The ceiling is EIRP, and a combined
figure cannot be audited — a reader cannot tell which half of it was measured and which was
assumed.

**D33 was reopened by M21 on 2026-09-06.** The ceiling stands, but the reasoning changed:
`BW` and the rule section are now one decision, and the project's frame is **§15.23
home-built**. No node may be represented as FCC certified anywhere — not in a README, a
LICENSE header, an enclosure label or HA device metadata. Protocol Spec §18.2 is
authoritative.

**Do not derate below −4 dBm conducted for conservatism.** −9 dBm is the SX1262's hard
floor, and the site measured 12.5–25 % PER there at SF7. Backing off "to be safe" lands on
a link that does not work.

**D1 closed 2026-09-10, and D33 closed with it**, on Envelope A: 917.4 MHz, SF9, BW 125 kHz,
CR 4/5, −4 dBm conducted with the fitted 3.0 dBi antenna. `backoff_max_ms` was raised to
**1500** because a maximum-length `PING` at SF9 runs 1107 ms. Protocol Spec §12.1 and §12.3
state the envelope; Decision Register §3.4 is the status of record.

---

## The test vectors

Regenerating the W4 vectors after a protocol change is not optional (spec §13.2).

The generator is written from the specification with the codec off limits, and that
independence is the entire value. When a vector and the codec disagree, one of them is
wrong and the disagreement is the only signal that says so. Editing the vector to match the
codec destroys the check and leaves the repo with two copies of the same bug.

---

## Why Layout carries only `[built]` and `[planned]`

`CLAUDE.md`'s Layout section used to carry test counts, task ranges, dates and "the next
target." Every one of them was **wrong more often than right** — stale by the time anyone
read it, because nothing in the build updated them.

A governing file that is reliably wrong in its details teaches readers to distrust the parts
that are not. So Layout carries the one fact that changes rarely and visibly: whether a path
exists. Current counts and status live where they are produced — the build and test commands
themselves, `docs/<node>/HANDOFF.md`, and the engineering logs.

The same test governs the rule that [a document must not record where a branch currently
points](../README.md#a-document-must-not-record-where-a-branch-currently-points).

---

## Why the command catalogue is not in `CLAUDE.md`

`CLAUDE.md` once listed every build, test and check command in the repo — about 37 of them.
Three copies of that list existed: `CLAUDE.md`, `README.md`, and the per-firmware
`CLAUDE.md` files. On 2026-09-22 they disagreed. `CLAUDE.md` was missing
`tools/checks/no_mbedtls_hkdf.py`, `tools/simctl/test_per_measure.py` and
`tools/checks/bridge_partitions.py`, all three of which CI had been running; `README.md` was
missing the `lran-link`, `lran-sim`, bridge, simnode and chan-capture entries entirely.

[`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) is the catalogue instead,
because it is executed rather than read. A command that stops working fails a job; a command
that is added appears there by necessity. A prose list has neither property.

---

## Writing rules

### Why a whole-document prose review waits to be asked for

Reviewing three thousand lines of specification to land a two-line correction buries the
change and spends the session on wording. The operator decides when a document is worth that
pass, and on which branch.

*Decided 2026-09-19, after a v0.13 edit turned into a review of six governing documents.*

### Why a spec revision's style pass goes on its own branch

A version bump already drags a citation sweep across the document set. Wording changes on
top of that make the diff unreadable, and a reviewer cannot tell a renamed field from a
reworded sentence.

*Agreed 2026-09-16.*

### Why dated records keep their tense and their wording

An engineering-log entry, a committed trace or a handoff file describes a moment. Rewriting
one into the present tense destroys the thing that made it useful: the reader can no longer
tell what was known when. Correct a dated record with a new dated entry or a
marked-superseded note.

### Why uncertainty is preserved exactly

"Suspected", "unverified", "measured once", "D31 still open" — a hedge in this repo is
usually load-bearing and often the whole point of the sentence. Removing it converts a known
gap into an apparent fact, and the next reader builds on it.

---

## Documents are guidance, and they are works in progress

**`CLAUDE.md` included.** The governing set — `CLAUDE.md`, the PRDs, the implementation
plans, the task documents — was written **before any firmware was built or any hardware was
in hand**. It was the first pass at a structure to work inside, not a specification derived
from a working system. Much of it has held up. Some of it was a guess that development has
since tested.

So: if something in a reference document looks incorrect, misplaced, inefficient or simply
overtaken, say so and propose the change. Do not work around it silently, and do not treat
it as settled merely because it is written down. Update it in the same commit as the work
that proved it wrong, and record what changed and why.

Two things this latitude does **not** license:

- **The protocol specification is still binding.** If code and the specification disagree,
  the specification is right. Raise the discrepancy; do not adjust the spec to match the
  code.
- **A dated record is not a draft.** Correct it with a new dated entry or a
  marked-superseded note.

Two worked examples, both from range-test pass 2 (2026-09-05). Bridge Impl Plan §10.8.1
rested on a premise about the Wio module's pad assignment that turned out to be false for
the board that arrived; the section even said what would follow if the premise stopped being
true, and still had to be found by audit rather than announcing itself. And pass 1's own
task text predicted the RF-switch divergence correctly while telling pass 2 to populate its
config from a document describing a *different product*.

The first of those produced the rule that [a load-bearing premise must name the check that
would falsify it](../README.md#a-load-bearing-premise-must-name-the-check-that-would-falsify-it).

---

## Changelog

| Version | Date | Change |
|---|---|---|
| **v0.1** | 2026-09-22 | Initial release. Reasoning relocated from the repo-root `CLAUDE.md`, which went from 383 lines to under 200 so that per-turn context cost fell by about 57 %. No rule changed; the two document-authoring meta-rules moved to [`docs/README.md`](../README.md) instead of here. The command-catalogue section records the three-way drift found on 2026-09-22 between `CLAUDE.md`, `README.md` and CI |
