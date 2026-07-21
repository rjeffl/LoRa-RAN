# Requirements Specification
## LoRa-Based Driveway Gate Control & Status System

**Status:** Draft v1 — for review
**Last updated:** 2026-07-19

---

## 1. Overview

This system links a Home Assistant installation to an existing Nice 1050 gate
operator located at a remote driveway gate, using a point-to-point LoRa radio
link. It replaces the need for a wired or Wi-Fi connection to the gate by
communicating commands (open/close/stop) and status (gate state, faults) over
LoRa between two custom nodes:

- **Gate Node** — installed at the gate, connects directly to the Nice 1050's
  BusT4 (Opera protocol) port
- **House Node** — installed at the house, bridges LoRa traffic to a Node.js
  service, which exposes the gate to Home Assistant

## 2. Scope

**In scope:**
- Sending open / close / stop commands from Home Assistant to the gate
- Reporting gate status (state, and fault/obstruction if exposed by the board)
  back to Home Assistant
- Reliable operation over a sub-300m LoRa link
- Basic protection against trivial replay/spoofing on the LoRa link

**Out of scope (v1):**
- Remote access outside the home network (handled by Home Assistant itself)
- Support for gate operators other than the Nice 1050
- Camera/video integration at the gate
- Mobile app development (Home Assistant's own apps/dashboards are assumed sufficient)
- Over-the-air (OTA) firmware updates — updates are performed via USB on both nodes

## 3. System Architecture

```
Home Assistant  <--MQTT-->  Node.js Bridge Service  <--WiFi-->  House Node (Heltec V3)
                                                                              |
                                                                          LoRa (sub-GHz)
                                                                              |
                                                                     Gate Node (Heltec V3)
                                                                     /                    \
                                                          BusT4 (Opera)                VE.Direct
                                                                |                          |
                                                     Nice 1050 control board      Victron MPPT 75/15
                                                                                  solar charge controller
                                                                                          |
                                                                                  12V FLA battery
```

The gate node itself is also powered from the 12V FLA battery (not shown as a
data line above — see 3.1). The house node's USB port is used only for power
and initial provisioning (e.g. Wi-Fi credentials, broker address) — not as a
runtime data path. This lets the house node be sited wherever LoRa reception
is best, independent of where the Node.js bridge/Home Assistant host lives.

### 3.1 Gate Node
- Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
- CAN transceiver breakout module, wired to the BusT4 connector's CAN-Rx/CAN-Tx
  lines (BusT4 uses CAN transceiver chips for its differential signaling, but
  the protocol on the wire is a modified UART, not standard CAN frames)
- Logic-level shifter between the CAN transceiver (5V logic) and the ESP32 UART (3.3V)
- Second UART connection to the Victron MPPT 75/15's VE.Direct port, also via
  a level shifter/optocoupler (VE.Direct runs at 5V on MPPT devices; the
  signal is inverted when passed through an optocoupler, which the ESP32's
  UART can be configured to handle in software)
- Powered directly from the site's 12V FLA battery (stepped down via a buck
  converter to what the Heltec board needs), per the requirement to draw all
  gate-side power from the battery where possible, rather than from the
  BusT4 connector's VCC pin
- Translates between BusT4 command/status frames and LoRa packets
- Caches the latest VE.Direct telemetry from the MPPT and includes it in
  status responses (poll-driven or event-triggered — see F9–F11), not on a
  periodic schedule
- Runs with Wi-Fi and Bluetooth radios disabled, and the board's Vext power
  rail (OLED display and other Vext-powered peripherals) held off, during
  normal (unattended) operation — see N4b/N4c. There is no OTA update path;
  firmware updates are done via USB, so the radios can stay off permanently
  rather than needing a provisioning/update mode that re-enables them
- A local debug mode that re-enables the display for on-site troubleshooting
  would be useful, but the exact mechanism (button press, jumper, build flag)
  is left open to work out during firmware development rather than specified here

### 3.2 House Node
- Heltec WiFi LoRa 32 V3
- Connects to the network over Wi-Fi to communicate with the Node.js bridge
  service — no runtime dependency on USB
- USB is used only for power and initial provisioning (Wi-Fi credentials,
  broker address, etc.), so the node can be sited in whatever spot gives the
  best LoRa reception, independent of the bridge/HA host's location
- Powered by USB — no battery constraints, so its OLED display can stay on
  continuously without a power trade-off
- Translates between LoRa packets and messages to/from the bridge service
  (exact wire protocol — MQTT client on-device vs. a custom protocol to the
  bridge — is an open question, see section 10)

### 3.3 Node.js Bridge Service
- Runs on a host on the local network (e.g. the same machine as Home
  Assistant, or a Raspberry Pi) — no longer requires a physical/USB
  connection to the house node now that it communicates over Wi-Fi
- Communicates with the House Node over the network; publishes gate state
  and diagnostics to MQTT; subscribes to command topics
- Recommended integration path: MQTT with Home Assistant's MQTT discovery, so
  the gate appears as a `cover` entity without manual YAML configuration
  — **assumption, confirm before implementation**

### 3.4 Home Assistant
- Represents the gate as a `cover` entity (open/close/stop/state) via MQTT
- Surfaces diagnostic sensors (link quality, last-seen timestamp) as separate
  entities

## 4. Functional Requirements

| ID | Requirement |
|----|-------------|
| F1 | HA shall be able to send Open, Close, and Stop commands to the gate |
| F2 | The gate node shall translate each command into the corresponding BusT4 command frame |
| F3 | The gate node shall report gate state (open / closed / opening / closing / stopped) to the house node |
| F4 | If the Nice 1050 exposes fault/obstruction/position/limit-switch status over BusT4, the system shall relay it to HA as a binary/problem sensor |
| F4a | The gate node shall poll and report the state of the controller's discrete inputs over BusT4, if individually queryable — including the vehicle-loop detector input — as a best-effort capability, not a guaranteed one (see open questions 1 and 7). **Fallback:** if BusT4 doesn't expose loop-detector state, the loop detector's dry-contact relay output can instead be wired directly to a spare gate node GPIO (pulled to ground when active) — noted here for future implementation, not committed as part of the v1 build |
| F4b | The gate node shall attempt to report a "trigger reason" for gate movement (e.g. loop detect, remote control, automation command) by correlating input-state transitions and remote-control (OXI) events with movement start, since this is not expected to be a single field the protocol provides directly |
| F5 | The house node shall report a heartbeat/last-seen status so HA can flag the link as unavailable if it goes silent |
| F6 | Every command shall receive an acknowledgment (success/failure) back to HA within a defined timeout, so the person requesting the action knows whether it worked |
| F7 | The gate node shall read telemetry from the Victron MPPT 75/15 over VE.Direct (at minimum: battery voltage, charge current, PV power, charge state, and error code) and relay it to HA via the house node |
| F8 | HA shall surface MPPT/battery telemetry as diagnostic sensors, so battery health and solar charging can be monitored remotely without a site visit |
| F9 | The gate node shall cache the most recently received VE.Direct frame locally, overwriting it on each new push from the MPPT (nominally every second) |
| F10 | The gate node shall transmit a status message over LoRa only when: (a) it receives a poll request from the house node, (b) the BusT4-reported gate state changes, or (c) the VE.Direct ERR field becomes non-zero (any active MPPT fault) |
| F11 | The house node/bridge shall poll the gate node at a configurable interval (default: somewhere in the 1–5 minute range) to refresh cached status and confirm the link is alive; this poll doubles as the heartbeat check in F5 — a missed poll response beyond a defined threshold marks the link unavailable in HA |

## 5. Non-Functional Requirements

| ID | Category | Requirement |
|----|----------|-------------|
| N1 | Range | Reliable link over the site's sub-300m gate-to-house distance, including typical obstructions (trees, structures) |
| N2 | Latency | Target: command received by HA to command executed at the gate within **2 seconds** under normal conditions — *placeholder, confirm this is acceptable* |
| N3 | Reliability | Commands shall be retried on failure (no ack received) up to a defined retry limit before reporting failure to HA |
| N4 | Power | Gate node and all its peripherals shall draw power from the site's 12V FLA battery where possible, rather than from the Nice board's BusT4 VCC pin |
| N4a | Power | Gate node's own power draw shall be low enough to avoid materially affecting the battery's charge/discharge cycle, given it now shares the battery with the gate operator itself |
| N4b | Power | Gate node firmware shall disable unused onboard radios (Wi-Fi, Bluetooth) during normal operation, since only the LoRa radio is needed |
| N4c | Power | Gate node firmware shall hold the Heltec board's Vext control pin in the OFF state during normal operation, cutting power to the onboard OLED display and any other Vext-supplied peripherals |
| N5 | Security | COMMAND messages (house → gate) shall include a shared-secret-derived check and a sequence counter, so a captured command cannot be replayed to re-trigger gate movement. POLL/STATUS/ACK messages are unauthenticated by design — the security-critical direction is the one that can move the gate, and this is a lightweight, not cryptographically strong, protection consistent with "basic protection" rather than full authenticated encryption |
| N6 | Environmental | Gate node enclosure shall be rated for outdoor exposure (temperature, moisture) at the installation site |
| N7 | Maintainability | LoRa packet format and BusT4 command mapping shall be documented in the repo so firmware can be modified without re-reverse-engineering the protocol |

## 6. Hardware

- 2x Heltec WiFi LoRa 32 V3
- 1x CAN transceiver breakout (gate node, for BusT4)
- 1x logic-level shifter/optocoupler, 5V↔3.3V (gate node, BusT4 side)
- 1x logic-level shifter/optocoupler, 5V↔3.3V (gate node, VE.Direct side — can typically reuse the same type of interface Victron sells as "VE.Direct to USB/RS232", or a simple optocoupler circuit)
- 1x buck converter, 12V battery → 5V/3.3V, to power the gate node from the FLA battery
- Weatherproof enclosure for the gate node
- BusT4 cable/connector — likely via a breakout such as the Nice IBT4N adapter, to avoid modifying the gate control board's connector directly
- VE.Direct cable from the Victron MPPT 75/15 to the gate node
- Possible future addition: a spare gate node GPIO wired to the loop detector's dry-contact relay output (pulled to ground when active), as a fallback if BusT4 doesn't expose loop state directly — not part of the v1 build

## 7. Software Architecture

- **Gate node firmware:** handles BusT4 UART framing (19200 baud, 8N1, with
  break signal before each packet) and command translation; separately reads
  and parses VE.Direct text-mode frames (also 19200 baud, on a second UART)
  from the MPPT; sends/receives LoRa packets to/from the house node. The
  ESP32-S3 on the Heltec V3 has multiple hardware UARTs, so BusT4 and
  VE.Direct can run on separate peripherals concurrently
- **House node firmware:** LoRa ↔ Wi-Fi bridge (protocol to the bridge service TBD, see section 10); no BusT4 or VE.Direct logic; OLED display can remain active since there's no power budget to protect
- **Node.js bridge service:** network ↔ MQTT translation, sequence/ack tracking,
  retry logic, exposes configuration (MQTT broker, retry counts, timeouts)
- **Firmware updates:** no OTA path in v1 — both nodes are updated via USB

## 8. LoRa Packet Design (draft — to be finalized during implementation)

The SX1262 radio (via RadioLib or similar) already handles preamble, sync
word, and PHY-layer CRC. Everything below is the *application payload*
carried inside that radio frame — it doesn't duplicate framing the radio
already does, but it does add authentication the radio's CRC doesn't provide.

### 8.1 Common Header (every packet)

| Field | Size | Notes |
|---|---|---|
| Protocol version | 1 byte | future-proofing |
| Message type | 1 byte | `COMMAND` / `POLL` / `STATUS` / `ACK` |
| Sender ID | 1 byte | `HOUSE` / `GATE` |
| Sequence counter | 4 bytes | monotonic, tracked separately per sender. 4 bytes avoids wraparound even at frequent polling over a multi-year lifetime |
| Auth tag | 4–8 bytes | truncated HMAC-SHA256 over the rest of the packet, using a pre-shared key installed during USB provisioning |

**Resolved:** the sequence counter is memory-only and resets on a gate node
power cycle — persistence to flash was judged unnecessary since power
outages at this site are rare outside of a battery failure (which would
likely take the gate node down entirely anyway). This leaves a small replay
window immediately after a reboot; accepted as a reasonable trade-off given
the low likelihood and low value of exploiting it. The auth tag applies to
**COMMAND messages only** — POLL, STATUS, and ACK are sent unauthenticated,
since they can't themselves move the gate.

### 8.2 COMMAND (house → gate)
- Command code (1 byte): `OPEN` / `CLOSE` / `STOP`

### 8.3 POLL (house → gate)
- No payload — header only; gate always returns full status

### 8.4 STATUS (gate → house)

| Field | Size | Notes |
|---|---|---|
| Message trigger reason | 1 byte | `POLLED` / `GATE_STATE_CHANGE` / `MPPT_ALERT` — why this LoRa message was sent |
| Gate state | 1 byte | open / closed / opening / closing / stopped / unknown |
| Gate fault flags | 1 byte | bitmask — pending open question 1 (whether BusT4 exposes fault/obstruction at all) |
| Gate input flags | 2 bytes | bitmask of discrete input states, including the loop detector, if individually queryable over BusT4 — best-effort, see F4a and open question 7 |
| Movement trigger reason | 1 byte | best-effort: loop detect / remote (OXI) / automation command / unknown — inferred by the gate node rather than read as a single protocol field, see F4b |
| Battery voltage | 2 bytes | mV, from VE.Direct `V` |
| Charge current | 2 bytes | mA, from VE.Direct `I` |
| PV power | 2 bytes | W, from VE.Direct `PPV` |
| Charge state | 1 byte | raw VE.Direct `CS` byte, unmapped — translated to a label by the Node.js bridge |
| MPPT error code | 1 byte | raw VE.Direct `ERR` byte, unmapped |

### 8.5 ACK (gate → house, in response to a command)
- Acknowledged sequence number (4 bytes)
- Result code (1 byte): success / failure

Kept as its own message type rather than folded into the next status
message, so command latency (N2) doesn't depend on an unrelated status
update happening to follow it.

STATUS messages are sent only in response to a POLL, or unprompted when
triggered by F10(b) or F10(c) — never on a fixed schedule. This keeps
normal-condition airtime and gate node power draw low, since the gate and
battery both sit idle most of the time.

Total worst-case packet size is comfortably under LoRa's practical payload
limits at this range, even with the added I/O and movement-reason fields.

## 9. Failure Modes & Edge Cases

| Scenario | Expected behavior |
|----------|--------------------|
| Gate node loses BusT4 comms with the Nice board | Report a fault status to HA rather than silently failing |
| LoRa link drops (out of range / interference) | House node marks status "unavailable" after a defined silence window; queued commands should not fire once link returns without re-confirming gate state |
| Duplicate/replayed packet received | Rejected via sequence counter check on COMMAND messages (see N5). Note: the counter is memory-only (open question 9, resolved), so a captured command from before a gate node reboot could theoretically replay successfully in the brief window right after power-up — accepted trade-off given how rarely the site loses power |
| Power loss at the gate node | Node resumes and reports current gate state on power-up; command sequence counter resets to zero (see above) |
| Node.js bridge restarts | Should reconcile current gate state from the house node rather than assuming a stale state |
| Command sent with no ack received | Retried per N3, then reported as failed to HA |

## 10. Open Questions / Assumptions to Confirm

1. Does the Nice 1050 expose fault/obstruction/position status over BusT4, or only command acceptance? Community reverse-engineering documents position tracking and limit-switch confirmation on other Nice Gate&Door boards (Robus, Walky, Road 400) — whether the same holds for the Apollo-branded 1050 is unconfirmed (see question 7).
2. Confirm the 2-second latency target (N2) is acceptable for daily use.
3. Confirm MQTT + HA discovery is the preferred integration path over a custom HA integration/add-on.
4. Confirm what "basic protection" should cover in practice — this spec assumes shared-secret + sequence counter (N5); full authenticated encryption (e.g. AES-CCM) is explicitly out of scope for v1 but could be revisited later.
5. Confirm the actual current draw of the gate node (LoRa TX peaks especially) against the battery's capacity and the MPPT 75/15's output, to make sure N4a is met in practice.
6. What protocol does the house node use to talk to the Node.js bridge over Wi-Fi — an MQTT client running directly on the ESP32 (publishing straight to the same broker Home Assistant uses), or a custom protocol (TCP/UDP/HTTP) to the bridge, which then handles MQTT itself? This affects how much logic lives in house node firmware vs. the Node.js service.
7. Is the Nice 1050 (the Apollo-branded North American board) actually compatible with the BusT4 command set documented by community projects, which explicitly target Robus/Walky/Road 400/Spin? This is unconfirmed and is the single biggest technical risk to the BusT4 side of the project. Recommend verifying with a bus sniff (logic analyzer between the board and a known-good Nice accessory) against the live board before committing firmware design to it.
8. Are individual discrete inputs — specifically the vehicle-loop detector — queryable over BusT4 on this board, the way limit-switch state appears to be queryable on other boards? If not, F4a falls back to wiring the loop detector's dry-contact relay directly to a spare gate node GPIO (see F4a) rather than going without.

## 11. Acceptance Criteria (v1)

- [ ] HA can open, close, and stop the gate reliably from within the 300m range
- [ ] Gate state shown in HA reflects reality within the latency target (N2)
- [ ] A captured/replayed LoRa packet does not re-trigger a command
- [ ] Gate node recovers gracefully from a power cycle
- [ ] House node/bridge restart does not cause spurious commands or lost state
- [ ] Battery voltage, charge current, and MPPT charge state are visible in HA and refresh at the configured poll interval
- [ ] Gate node transmits status immediately on a gate state change or MPPT error/alert (non-zero ERR), without waiting for a poll
- [ ] A poll request from the automation server reliably returns the most recently cached gate + VE.Direct status
- [ ] Gate node runs entirely from the 12V FLA battery with no separate power source
- [ ] Measured gate node current draw confirms Wi-Fi/Bluetooth are off and Vext is de-powered during normal operation
- [ ] House node operates over Wi-Fi with no USB connection required at runtime, once provisioned
- [ ] Firmware on both nodes can be updated via USB without requiring an OTA mechanism
- [ ] A bench bus-sniff session has confirmed the Nice 1050 responds to the documented BusT4 command set before firmware is written against it (see open question 7)
- [ ] Loop detector state and movement trigger reason are captured and surfaced in HA if the bench verification confirms they're available; otherwise this is documented as unavailable rather than silently omitted

## 12. References

These are community reverse-engineering efforts, not official Nice
documentation — treat all BusT4/Opera protocol details as unconfirmed for
this specific board until verified on the bench (see open question 7):

- pruwait/Nice_BusT4 — original ESP8266 ESPHome component for BusT4
- makstech/esphome-BusT4 — ESP-IDF ESPHome component, documents position tracking, I/O queries, and OXI (remote) event logging
- gashtaan/nice-bidiwifi-firmware — protocol insights from the factory BiDi-WiFi module
- bpietroiu/esphome-nice-bidiwifi — device-specific handling notes across Nice Gate&Door boards
