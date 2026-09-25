# LRAN — repo-wide context

**This file is authoritative** and states the rules; where it conflicts with a `CLAUDE.md`
under `/firmware/<node>/`, this file wins.
[`LRAN-Conventions-Rationale`](docs/shared/LRAN-Conventions-Rationale.md) says what failure
produced each rule — read it when one looks wrong, arbitrary, or worth changing.

## What this is

LoRa Remote Automation Network — a property-wide 915 MHz network linking Home Assistant to
remote nodes. A mains-powered bridge in the house speaks LoRa to nodes and MQTT to HA.
GateLink controls and monitors a driveway gate ~87 m (285 ft) away; WellLink is planned.
Nodes are solar/battery powered and **have no OTA**: a firmware change is a USB reflash in
the field.

## Document set — read before writing code

[`docs/README.md`](docs/README.md) has the full map and the document conventions. These
bind code:

| Document | Path | Authority over |
|---|---|---|
| `LRAN-Protocol-Specification` | `docs/shared/` | **Every byte on the wire and every MQTT topic.** No other document may redefine a frame layout, enum value, schema ID or topic. **Currently v0.15, `ver = 2`** |
| `LRAN-Decision-Register` | `docs/shared/` | **D1–D61** and measurement backlog **M1–M26**. The only place a decision's status is recorded |
| `LRAN-Protocol-Library-Implementation-Plan` | `docs/shared/` | `/lib/lran-protocol/` API and tests |
| `LRAN-Bridge_Node-`, `-GateLink_Node-`, `-WellLink_Node-PRD` / `-Implementation-Plan` | `docs/<node>/` | That node's requirements and build. The bridge plan also owns `lran-simnode` (§10) |
| `LRAN-Range-Test-Firmware-Pass1-Tasks`, `-Pass2-Tasks` | `docs/rangetest/` | Range test tasks R1–R11; answers W9, M6 and M20. Pass 2 is the second board profile |

Requirement identifiers (`R-*`, `BG-*`, `BS-*`, `V-B*`, `D*`, `W*`, `M*`) refer to those
documents. **Cite them in commits and PR descriptions.** **If code and the protocol
specification disagree, the specification is right** — raise the discrepancy rather than
adjusting the spec to match the code.

**Check the version.** A node document citing an older protocol version than the
specification's own header has not been reconciled with the intervening revisions — say so
rather than building against it. `tools/checks/spec_citation_version.py` reports that drift
but cannot tell you a document *is* reconciled: reconcile first, bump the citation second.

## Rules that hold everywhere

1. **Never `memcpy` a struct to or from the wire.** Serialize field by field, explicitly,
   little-endian.
2. **Never vary `seq` on a command retry.** The retry reuses the same `seq` so the node's
   `(ctx_id, seq)` deduplication returns the cached ACK. Incrementing it is a second relay
   pulse at the gate.
3. **No dynamic allocation in `/lib/` or in any node firmware.** Fixed buffers, caller
   owned, sizes derived from `LRAN_MAX_FRAME`.
4. **Never discard a frame silently.** Every discard increments a named counter and maps to
   one `Status` value and one §14 stage.
5. **Reserved fields and bits are written zero and ignored on receive.** The one exception
   is `hdr_flags` bit 7 (`CRITICAL_EXT`), which is validated. Do not validate the rest as
   zero.
6. **Sentinels, not zero, for unavailable.** `INT16_MIN` / `UINT16_MAX` / `UINT32_MAX`.
7. **`/lib/lran-protocol/` must keep building in the `native` environment.** No Arduino or
   ESP-IDF header, no `millis()`, no `Serial`; time is passed in as an argument. A broken
   native build is a defect to fix now, not a nuisance to route around.
8. **Anything timing-related is runtime-configurable.** No timing constant is fixed at
   compile time in a node that cannot be reflashed without a walk to the gate.
9. **RadioLib is the SX1262 driver everywhere (D32), and its version is pinned in every
   `platformio.ini`.** `firmware/range-test/src/pa_config.cpp` mirrors RadioLib's
   file-static `paOptTable`, so **run `python3 tools/rangetest/check_pa_table.py` after any
   RadioLib version change** — no other check here would catch it.
10. **The radio pin map, TCXO reference voltage and DIO2-as-RF-switch flag are injected as
    a config struct**, never `#define`d (spec §12.2). The voltage and switch settings fail
    *silently* on the Heltec V3.
11. **TX power is capped by D33**, at or below the FCC §15.249 EIRP ceiling, and **do not
    derate for conservatism**. Record conducted power and antenna gain separately. **No node
    may be represented as FCC certified anywhere** — the project's frame is §15.23
    home-built (M21, 2026-09-06); Protocol Spec §18.2 is authoritative.
12. **Never regenerate a W4 vector to match the codec.** When a vector and the codec
    disagree, find out which is wrong. Regenerating after a protocol change is not optional
    (spec §13.2).

**Current PHY envelope** — D1 and D33 both closed 2026-09-10 on Envelope A: 917.4 MHz, SF9,
BW 125 kHz, CR 4/5, −4 dBm conducted with the fitted 3.0 dBi antenna, `backoff_max_ms` 1500.
Protocol Spec §12.1 and §12.3 state them; **Decision Register §3.4 is the status of record.**

## Layout

`[built]` / `[planned]` is the only status here; counts and dates live where they are
produced — `docs/<node>/HANDOFF.md` and the engineering logs. **Do not assume a path exists.**

```
lib/        lran-protocol, lran-link, lran-sim, lran-config   [built]
            vedirect, bms-ble                               [planned]
firmware/   bridge/, range-test/, simnode/, chan-capture/    [built]
            gatelink/, welllink/                            [planned]
tools/      vectors/, checks/, simctl/, rangetest/, ha/               [built]
docs/       shared/ bridge/ gatelink/ welllink/ rangetest/ protocol-lib/ archive/
            <node>/engineering-log.md — protocol-lib, rangetest and bridge have one
ha/         discovery payloads, GENERATED from the firmware   [built]
wattcycle-reader/  BMS BLE proof of concept, self-contained, its own CLAUDE.md. Not
            part of the LRAN build; its TDT protocol write-up still needs lifting
            out into docs/gatelink/bms-protocol.md
```

Each firmware is its own PlatformIO project, reaching shared code via
`lib_extra_dirs = ../../lib`. Framework is `arduino` with ESP-IDF components reachable
(mbedTLS), and every project defines a `native` environment running the Unity tests.

## Build and test

```bash
pio test -d lib/<lib> -e native               # host Unity suite, any library
pio test -d firmware/<node> -e native         # host Unity suite, any firmware
pio run  -d firmware/<node> -e <env>          # target build; add -t upload to flash
python3 tools/checks/<check>.py               # repository invariants
python3 tools/checks/run_ci_local.py          # CI's checks job, read from ci.yml; --job native adds the suites
python3 tools/vectors/generate.py             # regenerate W4 vectors after a spec change
```

**[`.github/workflows/ci.yml`](.github/workflows/ci.yml) is the catalogue**, in three
parallel jobs: `checks` (invariants and host tools, seconds, no toolchain), `native` (the
Unity suites) and `firmware` (every target, then the checks reading a built image or the
installed RadioLib). `checks` runs on every change; `native` and `firmware` run only when
the diff reaches `lib/`, `firmware/`, the workflow or the few other paths ci.yml's `changes`
job lists, and a weekly scheduled run builds everything. **A build that starts reading a new
path adds it to that list in the same commit**, or a change to that path skips the build.
Read it rather than a list here — it is executed, so it cannot go stale.
A firmware's own `CLAUDE.md` names the commands specific to it.

**Run the checks job before you push.** `run_ci_local.py` takes seconds and runs the steps
ci.yml lists, so drift is found before a CI round trip. `git config core.hooksPath
tools/hooks` runs it on every push, per clone. It never runs a step that touches
`secrets.h`, because here that file is real.

**`main` stays buildable**, and **no secrets are needed or available in CI**: the bridge and
simnode need `secrets.h`, and CI copies the committed template (`LRAN-Bridge-Firmware-Tasks`
§1.2). A target needing a *real* secret needs a decision, not a secret in a workflow.

## Secrets

`secrets.h` is **gitignored**; `secrets.h.example` is committed and documents every field.
Copy it to the repo root and fill it in — a missing field fails the build with a message
naming that step. The simnode reads `LRAN_MASTER_KEY` alone, deriving the bridge's node keys
from it (decided 2026-09-14). **Never commit, echo, log or paste `LRAN_MASTER_KEY`, WiFi
credentials, MQTT credentials or the OTA password.** A leaked master key means
re-provisioning every node, which for GateLink means a USB reflash at the gate.

## Style

- C++17. `-Wall -Wextra -Werror`.
  - **One exception, and it is narrow.** Where a pinned third-party header emits a
    diagnostic you can neither fix nor suppress precisely — RadioLib fires an unconditional
    `#warning` under `ARDUINO_USB_CDC_ON_BOOT` — use the narrowest flag (`-Wno-error=cpp`,
    not `-Wno-error`), in the one environment needing it, reasoning written at the flag, and
    prefer downgrading over silencing. Anything wider is a discussion, not a build fix.
- `snake_case` for functions and variables, `PascalCase` for types, `kCamelCase` for
  constants, `lower_snake.cpp` for files.
- Comment *why*, not *what*, citing the section a value comes from:
  `// spec 7.2.9 - uint32 because 16 bits saturates at 18h`. No `TODO` without an
  identifier: `// TODO(W6): confirm pack_ma sign under load`.
- License header on every file: MIT, 2026, `// Copyright (c) 2026 Robert J. Lee` (D31,
  closed 2026-09-08).

## Writing

**Use the `nbj-write-clearly` skill for every prose artifact in this repo** — documents under
`docs/`, `README`s, engineering-log entries, commit messages, PR descriptions, code comments
and docstrings, and revisions to any of them. Invoke it before drafting, not as a cleanup
pass. Every passage you write or revise meets it: the lines the change touches, and the
sections it reaches into.

Precedence when the skill and this repo disagree: (1) this file and the governing documents
in `docs/`; (2) source facts — measurements, identifiers, spec section numbers, quoted text,
code, commands, pin names, enum values, which never drift for style; (3) the repo's voice,
which argues a point and says why, so do not flatten it into neutral reference prose; (4) the
skill. **Three repo rules override the skill outright.** Dated records keep their
tense and their wording — correct an engineering-log entry, a committed trace or a handoff
file with a *new* dated entry or a marked-superseded note. Precision beats familiarity:
where the accurate term is `hdr_flags` bit 7, EIRP or `(ctx_id, seq)` deduplication, use it.
And uncertainty is preserved exactly, because a hedge here is usually load-bearing.

**A whole-document prose review happens when the operator asks for one**, not because a
document was opened for an edit. When directed: fix the sections the change touches in the
same commit, fix the rest in a separate style-only commit on the same branch, and list
anything beyond what the branch should carry in the PR description. A spec revision's style
pass goes on its own branch. **Reading a document is not the same as restyling it** — a
wrong fact, a contradiction or drift from the specification is never a style finding. Fix it
in its own commit, with the evidence in the message.

## Workflow

- **One task group per session.** Start from the *Start here* section of the node's
  `HANDOFF.md` and read only what it names for the task. Do the task and the cleanup it
  produced — stale comments and document lines, closed `TODO(<id>)` markers, merged branches
  and worktrees. Close by opening the PR, merging it once the operator accepts it, and
  rewriting *Start here*. Something out of scope, a wrong document included, goes in one
  line under the handoff's *Open*, not into the session. Context in use is the budget:
  aim to wrap up by about 20 %, or 200k tokens. A task that must read large documents
  before it can start may run to 40 %, or 400k tokens; say so when the intake pushes past
  20 %.
- **Branch per milestone**, named for it: `p4-schemas`, `b0-simnode-bringup`.
- **Open the PR and write the description yourself.** State which acceptance criteria from
  the milestone table the branch satisfies **and which it does not** — a criterion not met
  is stated plainly, not omitted. Link the engineering-log entries made during the work.
- Append dated findings to `docs/<node>/engineering-log.md` as they happen — measurements,
  surprises, things that cost an hour. This answers "why is it like this" in eighteen months.
  Split a log at a milestone boundary once the latest entries get hard to find;
  [`docs/README.md`](docs/README.md#conventions) gives the procedure.
- Use a docs-as-code workflow throughout: commit a document change alongside the code or
  test result that motivated it.
- **A document must not record where a branch currently points**, and **a load-bearing
  premise must name the check that would falsify it** — both in
  [`docs/README.md`](docs/README.md#conventions), with the cases that produced them.

## Working style

Prefer asking to guessing when a requirement is ambiguous: the documents are maintained, and
a gap in them is worth reporting rather than patching locally.

**These documents are guidance, and they are works in progress — this file included.** The
governing set was written before any firmware was built or any hardware was in hand. If
something in a reference document looks incorrect, misplaced, inefficient or simply
overtaken, say so and propose the change rather than working around it silently. Update it
in the same commit as the work that proved it wrong. Two limits: the protocol specification
is still binding, and a dated record is not a draft. The rationale document has the two
cases behind this rule.
