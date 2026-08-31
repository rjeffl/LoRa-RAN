# LRAN — repo-wide context

**This file is authoritative.** Where it conflicts with a `CLAUDE.md` under
`/firmware/<node>/`, this file wins. Subordinate files carry only what is specific to
their target; if guidance applies in two places, it belongs here.

## What this is

LoRa Remote Automation Network — a property-wide 915 MHz LoRa network linking Home
Assistant to remote nodes. A mains-powered bridge in the house speaks LoRa to nodes and
MQTT to HA. GateLink controls and monitors a driveway gate ~500 ft away; WellLink is
planned. Nodes are solar/battery powered and **have no OTA** — a firmware change means a
USB reflash in the field.

## Document set — read before writing code

| Document | Authority over |
|---|---|
| `LRAN-System-PRD` | Architecture, node roles |
| `LRAN-Protocol-Specification` | **Every byte on the wire and every MQTT topic.** No other document may redefine a frame layout, enum value, schema ID or topic |
| `LRAN-Protocol-Library-Implementation-Plan` | `/lib/lran-protocol/` API and tests |
| `LRAN-Bridge_Node-PRD` / `-Implementation-Plan` | Bridge requirements and build |
| `LRAN-GateLink_Node-PRD` / `-Implementation-Plan` | GateLink requirements and build |
| `LRAN-Decision-Register` | D-numbers referenced throughout |

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

## Layout

```
lib/        lran-protocol, lran-config, lran-sim, vedirect   (shared)
firmware/   bridge, simnode, gatelink, rangetest             (separate PIO projects)
tools/      simctl, vectors                                  (Python 3)
docs/       <node>/engineering-log.md
ha/         example discovery payloads
```

Each firmware is its own PlatformIO project and reaches shared code via
`lib_extra_dirs = ../../lib`. Framework is `arduino` with ESP-IDF components reachable
(mbedTLS). Every project also defines a `native` environment running the Unity tests.

## Build and test

```bash
pio run -d firmware/bridge -e bridge          # build
pio run -d firmware/bridge -e bridge -t upload
pio test -d firmware/bridge -e native         # host tests
python tools/vectors/check.py                 # W4 vectors
python tools/simctl/simctl.py --port /dev/ttyUSB0
```

**`main` stays buildable.** A PR builds every firmware target *and* the native tests
before merge.

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

## Secrets

`secrets.h` is **gitignored**. `secrets.h.example` is committed and documents every field.
A missing field should fail the build with a clear message.

**Never commit, echo, log or paste `LRAN_MASTER_KEY`, WiFi credentials, MQTT credentials
or the OTA password.** A leaked master key means re-provisioning every node, which for
GateLink means a USB reflash at the gate.

## Style

- C++17. `-Wall -Wextra -Werror`.
- `snake_case` for functions and variables, `PascalCase` for types, `kCamelCase` for
  constants, `lower_snake.cpp` for files.
- Comment *why*, not *what*. Where a value comes from a document, cite the section:
  `// spec 7.2.9 - uint32 because 16 bits saturates at 18h`.
- License header on every file: MIT, 2026. Copyright holder is **D31, still open** — use
  the placeholder already in the template rather than inventing one.
- No `TODO` without an identifier: `// TODO(W6): confirm pack_ma sign under load`.

## Working style

Prefer asking to guessing when a requirement is ambiguous — the documents are maintained
and a gap in them is worth reporting rather than patching locally. When a document turns
out to be wrong, say so; several current sections exist because a review caught an error
rather than working around it.

