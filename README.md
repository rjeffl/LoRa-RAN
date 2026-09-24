# LRAN — LoRa Remote Automation Network

LRAN connects Home Assistant to equipment that sits beyond the reach of the house WiFi,
such as a driveway gate or a well. It uses LoRa, a long-range, low-power radio, on the
915 MHz band. A bridge in the house passes messages between the radio network and Home
Assistant.

> **This project is under development and has not been released.** The bridge firmware
> is in bench testing, and the firmware for the remote nodes hasn't been written yet. The
> design can still change, and nothing here is ready to install. [Status](#status) gives
> the current state.

## Contents

- [What it does](#what-it-does)
- [How it works](#how-it-works)
- [Hardware](#hardware)
- [Radio and regulatory](#radio-and-regulatory)
- [Status](#status)
- [Repository layout](#repository-layout)
- [Build and test](#build-and-test)
- [Documentation](#documentation)
- [How the project is developed](#how-the-project-is-developed)
- [License](#license)

## What it does

The first remote node is **GateLink**, which runs a driveway gate about 87 m (285 ft) from
the house. Through GateLink, Home Assistant is designed to:

- open and close the gate, and hold it open;
- show whether the gate is open, and raise an alert when it stays open;
- detect vehicles passing through the gate, and the direction each one travels;
- report the state of the node's solar charger and battery;
- read and change the solar charger's settings.

The second node, **WellLink**, will monitor the water level in a shallow well. It's planned,
and only its requirements are drafted. The network was designed from the outset to carry
more than one node.

LoRa suits this job because it sends small messages over long distances with little power.
GateLink runs on solar power and a battery, and no LRAN radio frame is longer than a few
hundred bytes.

## How it works

```
   Remote nodes                    House                     Home network

 +---------------+           +----------------+          +------------------+
 |   GateLink    |           |                |          |                  |
 | gate control, |<--------->|                |   WiFi   |  Home Assistant  |
 | solar, battery|   LoRa    |     Bridge     |<-------->|  and its MQTT    |
 +---------------+  915 MHz  |                |   MQTT   |  broker          |
 +---------------+           |                |          |                  |
 |   WellLink    |<--------->|                |          +------------------+
 |   (planned)   |           +----------------+
 +---------------+
```

**Every node talks only to the bridge.** Nodes don't relay messages for each other, so the
network has no mesh and no multi-hop routing.

**The bridge polls each node on a schedule.** A node also sends an event as soon as
something happens, such as a vehicle arriving at the gate. The bridge publishes what it
hears over MQTT, a lightweight publish-and-subscribe messaging protocol that Home Assistant
supports. Home Assistant discovers each node as a device automatically, with entities for
controls, sensors and settings. The bridge also tracks whether each node is responding and
marks a silent node unavailable.

**The bridge has no code specific to gates.** Each message declares its format, and the
bridge decodes it from a table of registered formats. Adding a new kind of node means adding
an entry to that table and a matching Home Assistant template. The bridge's scheduling,
availability tracking and MQTT handling don't change.

**Commands are authenticated.** Each command that operates equipment carries a
cryptographic signature made with a key unique to the node it's addressed to. A node
ignores a command whose signature doesn't match. Because every node has its own key, a
compromised well sensor can't be used to open the gate. The messages aren't encrypted, so
anyone listening on the channel can read them, but they can't forge a command.

**A retried command runs once.** Radio messages get lost, so the bridge resends a command
when the node doesn't acknowledge it. The node recognizes the resend as a copy and repeats
its acknowledgment instead of acting again. A lost acknowledgment therefore never triggers
the gate twice.

**Remote nodes can't be updated over the air.** The bridge is on mains power and the home
network, so it takes firmware updates over WiFi. GateLink and WellLink have neither, and a
firmware change means a walk to the node with a laptop and a USB cable. That constraint
shapes the design. Every timing value a node uses is a setting that Home Assistant can
change while the node runs. And because each message declares its format, adding a node
means reflashing only the bridge and the new node, never the existing ones.

The [protocol specification](docs/shared/LRAN-Protocol-Specification.md) defines every
message and every MQTT topic.

## Hardware

| Node | Hardware | Power | Status |
|---|---|---|---|
| **Bridge** | Heltec WiFi LoRa 32 V3: an ESP32-S3 microcontroller with a Semtech SX1262 LoRa radio | Mains | Built, in bench testing |
| **GateLink** | M5Stack StamPLC industrial controller with an SX1262 radio. Relays drive the gate operator, and isolated inputs read its sensors. It reads the Victron solar charger over a serial link and the battery's management system over Bluetooth LE | Solar, with a 12 V LiFePO4 battery | Designed; firmware not started |
| **WellLink** | Not yet chosen | Not yet decided | Planned |
| **Simulated node** | Heltec V3, or a Seeed XIAO ESP32S3 with a Wio-SX1262 radio | Bench | Built. A test instrument that imitates up to four nodes from one board |

The simulated node is permanent test equipment. It lets the bridge be tested against
realistic traffic and injected faults without anyone walking to the gate.

## Radio and regulatory

LRAN uses one fixed channel in the 915 MHz band, with no frequency hopping. Transmit power
is set at or below the limit that FCC Part 15, §15.249, places on radiated power in this
band, which works out to less than one milliwatt.

**No node in this project is FCC certified, and none may be represented as certified.** The
SX1262 radio modules carry their own FCC grants, but those grants don't transfer to a device
built around them. This project operates as home-built equipment under FCC §15.23.
[Protocol specification](docs/shared/LRAN-Protocol-Specification.md) §18.2 is the
authoritative statement.

## Status

**As of 2026-09-23.** Each node's implementation plan lists its milestones and their
acceptance criteria.

**Built and tested:**

- **The protocol library**, which builds and checks every message, signs commands, and
  splits long messages across several radio frames. Its tests run on a desktop computer
  and on the target board. They check the library against test vectors that a separate
  Python generator produces from the specification without reading the C++ code.
- **The range test.** Field measurements at the gate and well sites chose the radio
  settings and the transmit power in September 2026.
- **The bridge's radio link, polling, availability tracking and command path**, tested on
  the bench against the simulated node. The command tests cover retries, lost
  acknowledgments and a scripted catalogue of injected faults.
- **The bridge's over-the-air updates**, including rollback from a bad firmware image.
- **Home Assistant discovery.** The bridge publishes each node as a device, and the
  example discovery messages in `ha/` are generated from the firmware.
- **The simulated node**, its scripting tool, and a listen-only receiver that records
  other traffic on the radio channel.

**In progress:** the rules that decide what the bridge publishes to Home Assistant and
when. For example, a gate event is delivered once and isn't replayed after Home Assistant
restarts. The rules are built and pass their host tests, and they haven't yet been shown
working in Home Assistant.

**Not started:** the GateLink firmware and hardware build, the WellLink design, and field
installation.

**Next:** finish the bridge's Home Assistant integration, then build GateLink and connect
it to the bridge, then run a long soak test in the field.

Four design decisions are open: WellLink's power source, and three GateLink hardware
questions about the charger's serial interface, Bluetooth signal strength at the mounting
position, and enclosure temperature in summer. The
[decision register](docs/shared/LRAN-Decision-Register.md) records the status of every
decision.

## Repository layout

| Path | Contents |
|---|---|
| `lib/` | Shared libraries: the protocol, the radio link, the simulated node, and runtime configuration |
| `firmware/bridge/` | Bridge firmware |
| `firmware/simnode/` | Simulated node firmware |
| `firmware/range-test/` | Range-test instrument, for two board types |
| `firmware/chan-capture/` | Listen-only receiver for surveying the radio channel |
| `tools/` | Test-vector generator, repository checks, the simulated node's console driver, range-test and channel analysis, and Home Assistant helpers |
| `ha/` | Home Assistant discovery messages, generated from the bridge firmware |
| `docs/` | Requirements, specification, decisions, implementation plans and engineering logs |
| `wattcycle-reader/` | A self-contained proof of concept that reads the battery's management system over Bluetooth LE. It isn't part of the LRAN build |

GateLink and WellLink will get their own directories under `firmware/` when their firmware
work starts.

## Build and test

Each firmware directory is its own [PlatformIO](https://platformio.org/) project. Every
project also has a `native` environment that runs its unit tests on the host computer, with
no board attached.

```bash
pio test -d lib/lran-protocol -e native       # protocol library tests on the host
pio test -d firmware/bridge -e native         # bridge tests on the host
python3 tools/checks/run_ci_local.py          # the repository checks CI runs
```

The bridge and the simulated node need a `secrets.h` at the repository root. Copy
[`secrets.h.example`](secrets.h.example), which documents every field, and fill it in.
`secrets.h` is ignored by git and must never be committed.

[GitHub Actions](.github/workflows/ci.yml) runs the repository checks on every change. It
runs the unit tests and builds every firmware target when a change reaches the code, and
builds everything once a week. CI builds without secrets, using the committed template.

## Documentation

The documents are the project's design record. Start at [`docs/README.md`](docs/README.md),
which maps the full set.

| Document | Covers |
|---|---|
| [System PRD](docs/LRAN-System-PRD.md) | Architecture, node roles and repository layout |
| [Protocol specification](docs/shared/LRAN-Protocol-Specification.md) | Every message on the radio and every MQTT topic |
| [Decision register](docs/shared/LRAN-Decision-Register.md) | Design decisions and the measurements they depend on |
| [Bridge PRD](docs/bridge/LRAN-Bridge_Node-PRD.md) and [implementation plan](docs/bridge/LRAN-Bridge_Node-Implementation-Plan.md) | Bridge requirements, milestones and the simulated node |
| [GateLink PRD](docs/gatelink/LRAN-GateLink_Node-PRD.md) and [implementation plan](docs/gatelink/LRAN-GateLink_Node-Implementation-Plan.md) | GateLink requirements and milestones |
| [Range test](docs/rangetest/) | Range-test tasks, field procedure and findings |

Each engineering log, at `docs/<node>/engineering-log.md`, is a dated record of what was
tried, measured and decided, and why. Its entries are corrected with a new entry rather than
rewritten.

## How the project is developed

LRAN is a personal project, developed with Claude Code against the maintained document set.
[`CLAUDE.md`](CLAUDE.md) at the repository root holds the working rules. It covers the
branch-per-milestone workflow, the coding rules, and the requirement that every commit cite
the requirement or decision it serves.

## License

MIT. See [`LICENSE`](LICENSE). Copyright (c) 2026 Robert J. Lee.

[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists the third-party components and
their attribution requirements. The repository contains none of their code. PlatformIO
fetches each one at build time, at the version pinned in the project's `platformio.ini`.
Publishing this source doesn't distribute those components, but **distributing a compiled
firmware image does**, so ship the notices file with one.
