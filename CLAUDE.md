# LRAN — repo-wide context

**This file is authoritative.** Where it conflicts with a `CLAUDE.md` under
`/firmware/<node>/`, this file wins. Subordinate files carry only what is specific to
their target; if guidance applies in two places, it belongs here.

## What this is

LoRa Remote Automation Network — a property-wide 915 MHz LoRa network linking Home
Assistant to remote nodes. A mains-powered bridge in the house speaks LoRa to nodes and
MQTT to HA. GateLink controls and monitors a driveway gate **~87 m (285 ft)** away; WellLink is
planned. Nodes are solar/battery powered and **have no OTA** — a firmware change means a
USB reflash in the field.

## Document set — read before writing code

| Document | Path | Authority over |
|---|---|---|
| `LRAN-System-PRD` | `docs/` | Architecture, node roles, repo layout |
| `LRAN-Protocol-Specification` | `docs/shared/` | **Every byte on the wire and every MQTT topic.** No other document may redefine a frame layout, enum value, schema ID or topic. **Currently v0.11, `ver = 2`** |
| `LRAN-Decision-Register` | `docs/shared/` | **D1–D34** and measurement backlog **M1–M23**. The **only** place a decision's status is recorded |
| `LRAN-Protocol-Library-Implementation-Plan` | `docs/shared/` | `/lib/lran-protocol/` API and tests |
| `LRAN-Bridge_Node-PRD` / `-Implementation-Plan` | `docs/bridge/` | Bridge requirements and build; the plan also owns `lran-simnode` (§10) |
| `LRAN-GateLink_Node-PRD` / `-Implementation-Plan` | `docs/gatelink/` | GateLink requirements and build |
| `LRAN-WellLink_Node-PRD` | `docs/welllink/` | Placeholder — reserved allocations only |
| `LRAN-Range-Test-Firmware-Pass1-Tasks` | `docs/rangetest/` | Range test pass 1 — tasks R1–R11. Answers D1; hosts W9, M6, M20 |
| `LRAN-Range-Test-Firmware-Pass2-Tasks` | `docs/rangetest/` | Range test pass 2 — the second board profile (XIAO + Wio-SX1262 Kit) |

**Check the version.** A node document citing an older protocol version than
`LRAN-Protocol-Specification`'s own header has not been reconciled with the intervening
revisions — say so rather than building against it. **`python3
tools/checks/spec_citation_version.py` checks this**, across documents, context files,
`platformio.ini` headers and the range-test boot banner. It reports drift; it cannot tell
you a document is reconciled, so **reconcile first and update the citation second** —
bumping the number alone is the failure the check exists to make visible.

Requirement identifiers (`R-*`, `BG-*`, `BS-*`, `V-B*`, `D*`, `W*`, `M*`) refer to those
documents. **Cite them in commits and PR descriptions.**

**If code and the protocol specification disagree, the specification is right.** Raise the
discrepancy rather than adjusting the spec to match the code.

## Rules that hold everywhere

1. **Never `memcpy` a struct to or from the wire.** Serialize field by field, explicitly,
   little-endian. Host tooling uses a different compiler and architecture; layout-dependent
   code works on two ESP32s and breaks the moment the bench tooling is written.
2. **Never vary `seq` on a command retry.** The retry reuses the same `seq` so the node's
   `(ctx_id, seq)` deduplication returns the cached ACK. Incrementing looks like a fix for
   a stuck command and is a second relay pulse at the gate.
3. **No dynamic allocation in `/lib/` or in any node firmware.** Fixed buffers, caller
   owned, sizes derived from `LRAN_MAX_FRAME`.
4. **Never discard a frame silently.** Every discard increments a named counter and maps
   to one `Status` value and one §14 stage.
5. **Reserved fields and bits are written zero and ignored on receive.** The one exception
   is `hdr_flags` bit 7 (`CRITICAL_EXT`), which is validated. Do not "helpfully" validate
   the rest as zero — that breaks forward compatibility and the header extension space.
6. **Sentinels, not zero, for unavailable.** `INT16_MIN` / `UINT16_MAX` / `UINT32_MAX`.
   A consumer must be able to tell "0 A" from "no reading."
7. **`/lib/lran-protocol/` must keep building in the `native` environment.** No Arduino
   header, no ESP-IDF header, no `millis()`, no `Serial`. Time is passed in as an argument.
   If the native build breaks, that is a defect to fix now, not a nuisance to route around.
8. **Anything timing-related is runtime-configurable.** No timing constant is fixed at
   compile time in a node that cannot be reflashed without a walk to the gate.
9. **RadioLib is the SX1262 driver everywhere** (**D32**), and **its version is pinned in
   every `platformio.ini`.** A driver shared by four firmwares is not a thing to let
   float. **The pin now has a consumer that depends on the driver's internals:**
   `firmware/range-test/src/pa_config.cpp` mirrors RadioLib's file-static `paOptTable` so
   the applied PA configuration can be logged (the SX1262's PA config cannot be read back).
   **Run `python3 tools/rangetest/check_pa_table.py` after any RadioLib version change** —
   a bump that changes that table is silent in every other check here. The radio pin map, TCXO reference voltage and DIO2-as-RF-switch flag are
   **injected as a config struct**, never `#define`d (spec §12.2) — the two voltage/switch
   settings fail *silently* on the Heltec V3, presenting as a radio that will not
   calibrate rather than as an error.
10. **TX power is capped by D33**, at or below the FCC §15.249 EIRP ceiling (~−1 dBm EIRP;
   **−4 dBm conducted with the fitted 3.0 dBi antenna**). Single fixed channel, no hopping.
   **Record conducted power and antenna gain separately** — the ceiling is EIRP and a
   combined figure cannot be audited. **D33 was reopened by M21 on 2026-09-06**: the
   ceiling stands but the reasoning changed, `BW` and the rule section are now one
   decision, and the project's frame is **§15.23 home-built** — **no node may be
   represented as FCC certified anywhere**, including a README, a LICENSE header, an
   enclosure label or HA device metadata. Protocol Spec §18.2 is authoritative.
   **Do not derate below −4 dBm conducted for conservatism**: −9 dBm is the SX1262's hard
   floor and the site measured 12.5–25 % PER there at SF7.
   **D1 closed 2026-09-10 and D33 closed with it**, on Envelope A: **917.4 MHz, SF9,
   BW 125 kHz, CR 4/5, −4 dBm conducted** with the fitted 3.0 dBi antenna, and
   `backoff_max_ms` raised to **1500** because a maximum `PING` at SF9 runs 1107 ms.
   Protocol Spec §12.1 and §12.3 state them; Decision Register §3.4 is the status of
   record.

## Layout

What exists today is marked; the rest is planned. **Do not assume a path is there.**

**`[built]` / `[planned]` is the only status this file carries, deliberately.** Test
counts, task ranges, dates and "the next target" used to live here and were **wrong more
often than right** — every one of them was stale by the time anyone read it, and a
governing file that is reliably wrong in its details teaches people to distrust the parts
that are not. Current counts and status live where they are produced: run the commands
below, and read `docs/<node>/HANDOFF.md` and the engineering logs.

```
lib/        lran-protocol                                    [built]
            lran-config, lran-sim, vedirect, bms-ble        [planned]
firmware/   bridge/, range-test/                             [built]
            simnode/CLAUDE.md                     [context file only, no project yet]
            gatelink/, welllink/                            [planned]
tools/      vectors/ [built]  checks/ [built]  simctl/ [planned]
docs/       shared/ bridge/ gatelink/ welllink/ rangetest/ protocol-lib/ archive/
            <node>/engineering-log.md — protocol-lib, rangetest and bridge have one
ha/         example discovery payloads                       [planned]
wattcycle-reader/  BMS BLE proof of concept. Complete, self-contained, its own
            CLAUDE.md. Not part of the LRAN build; the TDT protocol write-up still
            needs lifting out of it into docs/gatelink/bms-protocol.md
```

Each firmware is its own PlatformIO project and reaches shared code via
`lib_extra_dirs = ../../lib`. Framework is `arduino` with ESP-IDF components reachable
(mbedTLS). Every project also defines a `native` environment running the Unity tests.

## Build and test

These work today:

```bash
pio test -d lib/lran-protocol -e native       # host Unity suite
pio test -d lib/lran-protocol -e esp32s3      # same suite on a Heltec V3
python3 tools/vectors/check.py                # W4 vectors, self-check
python3 tools/vectors/generate.py             # regenerate after any protocol change
python3 tools/checks/spec_citation_version.py # binding citations vs. the spec header

pio test -d firmware/range-test -e native     # host Unity suite
pio run  -d firmware/range-test -e heltec     # Heltec V3 target build
pio run  -d firmware/range-test -e xiao       # XIAO ESP32S3 + Wio-SX1262 Kit target
python3 tools/rangetest/test_capture.py       # capture tool, PlatformIO's python
python3 tools/rangetest/test_survey_reintegrate.py   # M20 re-integration tool
python3 tools/rangetest/test_eirp_check.py    # findings 7.6 EIRP sanity check
python3 tools/rangetest/check_pa_table.py     # PA table mirror vs. pinned RadioLib

pio test -d firmware/bridge -e native         # bridge host suite, no secrets
pio run  -d firmware/bridge -e heltec         # bridge target - NEEDS secrets.h
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing
```

**`firmware/bridge/` is the one target that needs `secrets.h`.** Copy
`secrets.h.example` to the repo root and fill it in; the build fails with a message
naming that step. **CI copies the committed template** — placeholders only, and
`main.cpp` says so at boot — so no secret enters a workflow.

These are the shape the firmware targets take once they exist:

```bash
pio run  -d firmware/<node> -e <env>          # build
pio run  -d firmware/<node> -e <env> -t upload
pio test -d firmware/<node> -e native         # host tests
python3 tools/simctl/simctl.py --port /dev/ttyUSB0
```

**Regenerating the W4 vectors is not optional after a protocol change** (spec §13.2). The
generator is written from the specification with the codec off limits; that independence
is the entire value, so never "fix" a vector to match the codec — investigate which one is
wrong.

**`main` stays buildable.** A PR builds every firmware target *and* the native tests
before merge. **[`.github/workflows/ci.yml`](.github/workflows/ci.yml) enforces it** in
three parallel jobs: `checks` (the repository invariants and the host tools, seconds, no
toolchain), `native` (the library, range-test and bridge Unity suites) and `firmware`
(every target, then the checks that read a built image or the installed RadioLib).
**No secrets are needed or available.** The bridge is the one target that needs
`secrets.h`, and the decision its arrival forced is recorded in
`LRAN-Bridge-Firmware-Tasks` §1.2: CI copies the committed template. A new target that
needs a *real* secret still needs a decision, not a secret pasted into a workflow.

## Workflow

- **Branch per milestone**, named for it: `p4-schemas`, `b0-simnode-bringup`.
- **Open the PR and write the description yourself.** State which acceptance criteria from
  the milestone table the branch satisfies **and which it does not**. Link the
  engineering-log entries made during the work. A criterion not met is stated plainly, not
  omitted.
- Append findings to `docs/<node>/engineering-log.md` as they happen — measurements,
  surprises, things that cost an hour. Dated entries. This is the record that answers
  "why is it like this" in eighteen months.
- Use docs-as-code workflow with all repository documents. As doc updates are identified, make changes at the repo level for commit along with code or test results that support it.

### A document must not record where a branch currently points

**`docs/<node>/HANDOFF.md` files, and any other document, name no `origin/main` SHA, no
"merged through #N", no current branch and no open-PR status.** These went stale on every
merge, and the correction could not ride along with the work that caused it — the branch being
described is the branch doing the describing, so each fix needed its own branch and PR. Two of
the five commits before 2026-09-09 on the range-test handoff exist for nothing else.

Sort a fact into one of three places, by whether it can be kept true:

| The fact is | Where it goes |
|---|---|
| **Derivable** — where `main` points, what is open, which branches exist, whether anything is local-only | **A command in the document**, never prose. `git fetch origin -p` first; two machines push here |
| **Predictive** — "once this merges, `main` carries X" | **The PR description.** It is read at review time and is about a proposed state by nature. A handoff written this way is *false when committed*, which is worse than silent |
| **Durable** — what a run measured, a trap, a push gotcha, "read these two commits in order" | **The document.** This is what a handoff is for |

**Permanent history is citable; moving state is not.** `0f21c23` will always be that commit,
so cite it freely. "`main` is at `9fee445`" describes where a pointer sat one afternoon.

The same test already governs test counts and task ranges here — *"they were wrong more often
than right"* — and this is that rule reaching the one section that kept escaping it.

**Start a node's handoff from [`docs/HANDOFF-TEMPLATE.md`](docs/HANDOFF-TEMPLATE.md).** It
carries the sections in reading order, this rule applied inline, and what each section is for
written where it is needed. Its blanks are `TODO(handoff):`, not `<angle brackets>`, because
two field captures went out with `<...>` unedited — so a half-filled handoff is greppable:

```bash
grep -n "TODO(handoff)" docs/<node>/HANDOFF.md   # empty before commit
grep -c "<!--" docs/<node>/HANDOFF.md            # 0 before commit
```

## Secrets

`secrets.h` is **gitignored**. `secrets.h.example` is committed and documents every field.
A missing field should fail the build with a clear message.

**Never commit, echo, log or paste `LRAN_MASTER_KEY`, WiFi credentials, MQTT credentials
or the OTA password.** A leaked master key means re-provisioning every node, which for
GateLink means a USB reflash at the gate.

## Style

- C++17. `-Wall -Wextra -Werror`.
  - **One exception exists, and it is narrow.** A pinned third-party header may emit a
    diagnostic you can neither fix nor suppress precisely — RadioLib fires an
    unconditional `#warning` under `ARDUINO_USB_CDC_ON_BOOT`, and GCC issues `#warning`
    from libcpp where `#pragma GCC diagnostic` does not reach it. Where that happens: use
    the **narrowest flag** (`-Wno-error=cpp`, not `-Wno-error`), in the **one environment**
    that needs it, with the reasoning written at the flag. Prefer a flag that
    **downgrades** over one that silences — `-Wno-error=cpp` still prints the library's
    other warnings, so the signal is kept and only the enforcement dropped. Anything wider
    than this is a discussion, not a build fix.
- `snake_case` for functions and variables, `PascalCase` for types, `kCamelCase` for
  constants, `lower_snake.cpp` for files.
- Comment *why*, not *what*. Where a value comes from a document, cite the section:
  `// spec 7.2.9 - uint32 because 16 bits saturates at 18h`.
- License header on every file: MIT, 2026. Copyright holder is **Robert J. Lee** (**D31**,
  closed 2026-09-08): `// Copyright (c) 2026 Robert J. Lee`. `LICENSE` at the repo root
  carries the full MIT text.
- No `TODO` without an identifier: `// TODO(W6): confirm pack_ma sign under load`.

## Writing

**Use the `nbj-write-clearly` skill for every prose artifact in this repo** — documents
under `docs/`, `README`s, engineering-log entries, commit messages, PR descriptions, code
comments and docstrings, and revisions to any of them. Invoke it before drafting or
revising, not as a cleanup pass afterward. It carries the reader-first rules: result
first, named actor, condition before instruction, one term per concept, and a list of
stock machine-writing patterns to keep out.

Order of precedence when the skill and this repo disagree:

1. This file and the governing documents in `docs/`.
2. Source facts — measurements, requirement identifiers, spec section numbers, quoted
   text, code, commands, pin names, enum values. These never drift for style.
3. The repo's existing voice. The documents here argue a point and say why; do not flatten
   them into neutral reference prose.
4. The skill's own guidance.

Three places the repo's rules override the skill outright:

- **Dated records keep their tense and their wording.** An engineering-log entry, a
  committed trace or a handoff file describes a moment. Correct it with a new dated entry
  or a marked-superseded note — never by rewriting it into the present tense.
- **Precision beats familiarity.** Where the accurate term is `hdr_flags` bit 7, EIRP,
  or `(ctx_id, seq)` deduplication, use it. Do not substitute a plainer word that means
  something slightly different.
- **Uncertainty is preserved exactly.** "Suspected", "unverified", "measured once",
  "D31 still open" — a hedge in this repo is usually load-bearing and often the whole
  point of the sentence.

## Working style

Prefer asking to guessing when a requirement is ambiguous — the documents are maintained
and a gap in them is worth reporting rather than patching locally. When a document turns
out to be wrong, say so; several current sections exist because a review caught an error
rather than working around it.

### These documents are guidance, and they are works in progress

**This file included.** The governing set — this file, the PRDs, the implementation plans,
the task documents — was written **before any firmware was built or any hardware was in
hand**. It was the first pass at a structure to work inside, not a specification derived
from a working system. Much of it has held up. Some of it was a guess that development has
since tested.

So: **if something in a reference document looks incorrect, misplaced, inefficient or
simply overtaken, say so and propose the change.** Do not work around it silently, and do
not treat it as settled merely because it is written down. Update it in the same commit as
the work that proved it wrong, and record what changed and why — the same docs-as-code
rule the rest of this file asks for.

### A load-bearing premise must name the check that would falsify it

If a document's argument rests on a factual premise — *"these two boards share a pad
assignment"*, *"this counter cannot move"* — then **say what would prove it false, and
point at the place that check is actually tracked**: an `M-*` item, a verify-before-build
checklist, a test. Prose that states a falsification condition and tracks it nowhere reads
like diligence and behaves like nothing.

The case that produced this rule: Bridge Impl Plan §10.8.1 wrote *"if it ever stops being
true, §2.3's claim collapses"* — and when it did stop being true, nothing surfaced it. It
was found by an audit somebody thought to ask for, after the wrong pin map had already
been copied into two other documents.

Two things the "works in progress" latitude does **not** license:

- **The protocol specification is still binding.** *"If code and the protocol
  specification disagree, the specification is right"* stands. Raise the discrepancy;
  do not adjust the spec to match the code.
- **A dated record is not a draft.** Engineering-log entries, committed traces and handoff
  files describe a moment. Correct them with a *new* dated entry or a marked-superseded
  note. Rewriting one to match today destroys the thing that made it useful.

Worked examples, both from range-test pass 2 (2026-09-05): Bridge Impl Plan §10.8.1 rested
on a premise about the Wio module's pad assignment that turned out to be false for the
board that arrived — the section even said what would follow if it stopped being true, and
still had to be found by audit rather than announcing itself. And pass 1's own task text
predicted the RF-switch divergence correctly while telling pass 2 to populate its config
from a document describing a *different product*.

