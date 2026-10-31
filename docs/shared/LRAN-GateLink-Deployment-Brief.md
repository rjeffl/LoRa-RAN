# LRAN — GateLink deployment brief

**Written 2026-10-31**, from `main` at `17b505a`, the three node handoffs (bridge, GateLink,
HA), the engineering logs to 2026-10-30, Decision Register v0.35, Protocol Spec v0.19 and the
production `secrets.h` the operator created that day. This brief is a dated record. The
handoffs and the register stay authoritative for status. Where this brief disagrees with
them, check the evidence it cites before acting on either.

## Summary

**GateLink is close to deployable, and nothing found here is a design problem.** CI is green
on `main`, and the local checks job passes. The soaks ran about 55 h without a reset. Every
bench criterion reachable without the gate has passed, except the items below.

What stands between the bench and a working gate falls into four groups:

1. **Three operator actions before the node leaves the bench** (B1–B3): the M7 harness fix,
   taking the simnodes off the air, and the production builds from the new `secrets.h`.
2. **A GateLink firmware freeze** (F1–F9). GateLink has no OTA, so every firmware item left
   after install costs a USB reflash at the gate. The operator decides which items go in.
3. **The at-gate sequence**: GL2, M3, M23, GL6, B6 with H10, GL7, GL8, then B7 and GL9. The
   order is fixed by the plan's dependencies.
4. **Bridge and HA work** that can follow install, because both update without a walk.

## How the findings are classified

The categories you asked for are kept. Two changes make them sort better for this project:

- **Classify by the cost of changing it later**, not only by importance. LRAN's own rule is
  that a GateLink firmware change is a walk to the gate, while the bridge takes OTA and HA
  takes a file copy. One gap can be urgent on GateLink and routine on the bridge.
- **Separate "at the gate" from "before the gate"**. The at-gate items are a sequence with
  dependencies, not a priority list, so they get their own table in order.

| Class | Meaning | Changed later by |
|---|---|---|
| **Blocking** | Safety, security or equipment risk. Do before GateLink leaves the bench | — |
| **Firmware freeze** | GateLink firmware to finish or explicitly defer before the production flash | USB reflash at the gate |
| **At-gate sequence** | Ordered work that needs the installation | A visit |
| **Before deploy, recommended** | Cheap now, and awkward or misleading later | OTA, file copy or bench |
| **Post-deploy verify** | Questions only the installed system can answer. Watch, and record in the logs | — |
| **Release** | Needed before a public GitHub release, not for deployment | Commit |
| **Future** | Improvements and later nodes | — |

Each row names its **owner**: *operator* (hands, hardware, decisions) or *session* (a Claude
Code task).

## Blocking

| # | Item | Owner | Source |
|---|---|---|---|
| **B1** | **Fix the warm section of the MPPT's battery harness** (about 0.22 Ω). At the 75/15's 15 A that is about 50 W in the harness, and about 1.2 W already at a 30 W panel's 2.3 A. The handoff says to fix it before the MPPT charges through it. The MPPT and the pack may already be back at the gate, so confirm which harness they charge through now | operator | GateLink log 2026-10-10, *M7*; GateLink handoff, *The next job* |
| **B2** | **Take the simnodes off the air, or confirm neither holds node ID `0x01`.** The XIAO simnode held `f1` in `ROLE_GATELINK`. Nothing a node sends the bridge carries a MAC the bridge verifies (spec §9.2). A simnode left running as `0x01` would therefore publish gate state and events into production HA as GateLink. A new master key does not stop it | operator | Bridge handoff, *Hardware state* and *Waits on GateLink*. **Inferred** from those two facts, not observed |
| **B3** | **Build and flash the bridge and GateLink from the production `secrets.h`, together.** Once one runs the new master and the other the old, they cannot authenticate. Build from a committed, clean tree so the banner does not read `-dirty`. GateLink's production flash is its last on the bench | operator | Bridge handoff, group 8 items 6 and 7; GateLink handoff, *Open* |

### The production `secrets.h`, as checked on 2026-10-31

These checks compared values without printing them:

| Field | Finding |
|---|---|
| `LRAN_MASTER_KEY` | **New.** Differs from the sandbox and the template, and is not all-zero |
| `LRAN_GATELINK_NODE_KEY` | **Matches** `tools/provision/node_key.py --node 0x01` derived from the new master |
| `MQTT_HOST`, `MQTT_USER`, `MQTT_PASSWORD` | Differ from the sandbox: the production broker |
| `OTA_PASSWORD` | Differs from the sandbox |
| `WIFI_SSID`, `WIFI_PASSWORD` | Same as the sandbox. The operator confirmed on 2026-10-31 that the bench network is the production network |
| `MQTT_PORT` | The template's value |
| `.gitignore` | Ignores `secrets.h` and `secrets.sandbox.h`. The `.gitignore` change is not committed yet |

Three consequences follow from the split:

- **The bridge's first production upload over OTA authenticates with the sandbox password**,
  because the running image checks the password it was built with. Upload over USB, or pass
  the sandbox `OTA_PASSWORD` for that one upload.
- **`bench.py` and `node_key.py` default to the root `secrets.h`**, which is now production.
  Bench work against the sandbox broker needs `--secrets secrets.sandbox.h`. The three
  handoffs and `tools/bench/README.md` say so from 2026-10-31.
- **The production master is backed up with the build Mac**: hourly to a Time Machine
  drive on the local network, weekly to an onsite external drive, and every two to three
  weeks to an offsite one (operator, 2026-10-31). Losing it would mean re-provisioning
  every node, GateLink at the gate included. Each of those drives now holds the master, so
  each should be encrypted. This brief does not know whether they are.

## Firmware freeze — GateLink

GateLink's handoff puts all of these in Tier 3, before deploy. Each row is one decision for
the operator: build it now, or defer it and accept a gate visit later.

| # | Item | Recommend | Source |
|---|---|---|---|
| **F1** | **`ACTUATOR_FAULT` for a failed relay write** (D78). Without it, a relay write that fails at the I²C expander is reported as a success | Build | Spec §8.2; GateLink handoff, *Start here* |
| **F2** | **Schema `0x13` with `ah_drawn_dah`** (D82, PRD R-6.4c). The bridge side is built (group 9) | Build. Its premise, that the BMS's remaining capacity falls under GateLink's 44 mA draw, is unmeasured. PRD R-6.4c names the check: the real pack overnight under GateLink's load alone, about 0.5 Ah in 12 h. Run it on the bench before the freeze if the pack is still available | Spec §7.2.8; GateLink handoff, *Open* |
| **F3** | **`MPPT_ERROR` and `BMS_ALARM` are never sent.** `lib/lran-node` names both, and nothing in `firmware/gatelink/src` emits either. `MPPT_ERROR` needs only VE.Direct's `ERR`, which GateLink already parses (D86). `BMS_ALARM` waits on the `0x8D` decode, but D86 allows passing `bms_alarms` through | Build `MPPT_ERROR`. Decide `BMS_ALARM` | Spec §8.9; HA handoff, *Open*; checked in source 2026-10-31 |
| **F4** | **True entropy for a context roll's `ctx_id`** (D87, spec §10.1, PRD R-3.5h). Roll draws call `esp_random()` between BLE windows, when it is pseudo-random | Build. It is a spec requirement | GateLink handoff, *Open* |
| **F5** | **The relay at a watchdog reset.** A hang 100 ms into a 500 ms pulse holds the relay for about 1 s. Any fix is a design question, since releasing the relay at the panic would mean I²C from the panic handler. The handoff asks for a written proposal | Write the proposal; the operator decides | GateLink log 2026-10-07; GateLink handoff, *Start here* |
| **F6** | **The SD library's `log_w`** writes to `Serial` from `lora_task`, breaking `firmware/gatelink/CLAUDE.md`'s rule | Build. Small | GateLink handoff, *Open* |
| **F7** | **The BMS low-temperature path has no hysteresis.** A pack sitting at the threshold could send a STATUS and an event on every poll, up to every 300 s. That pack state is the cold morning this feature exists for | Build | GateLink handoff, *Open* |
| **F8** | **The pack is matched by its compiled-in name, `XDZN_001_49A1`.** A replacement pack means a reflash at the gate. Matching any `XDZN_001_` is the operator's call | Decide before the freeze | GateLink handoff, *Open* |
| **F9** | **Observability at the gate.** The BMS drain-timeout counters clear at power-down, and the console cannot print `/bms.log`. Writing the `bms` counters to the card, as the handoff suggests, lets a field visit read them | Consider. It decides whether the NimBLE fix can be confirmed in the field | GateLink handoff, *Open* |

These lower-value items can wait for any later reflash:

- `boot_count` and `state.json` are not built, so STATUS sends `boot_count` 0 (plan §6.4).
- The `BOOT` event's `input_bits` is always 0.
- The console's `sd` command hangs the node with no card fitted.
- `bms_task` should read its clock after the blocking write.
- The RTC reads `2049-12-01`. Nothing on the wire reads it yet.

## At-gate sequence

Dependencies follow GateLink plan §8.2 and §7.4. Rows with no LRAN hardware can run any
time before.

| Step | Work | Owner | Gates on | Notes |
|---|---|---|---|---|
| 1 | **Input board R9 fitted and measured**: J3 pin 2 to ground with the 1050 powered and its alarm off | operator | — | Falsified if Q4's gate reads 0.8 V or more (GateLink log 2026-10-25) |
| 2 | **GL2**: rewire the keyswitch and pushbutton *before* reprogramming AUX; reprogram the 1050; verify by hand; write `docs/gatelink/1050-config.md` | operator, session for the doc | — | Plan §7.4 steps 1–6 |
| 3 | **M1** branch current in four states, **M2** MOVING through the countdown, **M8** the lamp output | operator | GL2 | M1 can change the power budget. M2 sets `hold_confirm_ms` |
| 4 | **M3** IN5 and IN6 idle and asserted voltages, **before connecting them** | operator | — | `kInputsActiveLow` `0x1C` rests on a description. A wrong polarity inverts the FIRE alert |
| 5 | **IN1 and IN2 harnesses rewired sinking**, and the K3 and K4 wake check on Radio Open and Close | operator | GL2 | GateLink handoff, *The next job* |
| 6 | **M23** BLE RSSI from the mounting position: three positions, two orientations, and LoRa keyed and unkeyed. **Before committing the mounting hardware** | operator, session | — | Closes D28. A poor reading points at the SmartShunt or D30 fallback |
| 7 | **Expansion board §11 step 6** | operator | install | Waits for the gate |
| 8 | **GL6**: inputs live, relays physically disconnected, real cycles from the keypad and the remote | operator, session | GL2, GL3 | `hold_confirm_ms` must reject the transient 1/1 |
| 9 | **B6**: bridge on the production broker, `deployed` = 1 on `lran/gatelink/config/set`; **H10** port to production HA | operator, session | GL6 | See *B6 and H10* below |
| 10 | **GL7**: relays wired with dry-run on, every command path from HA; both manual UNLOCK paths; **`RELEASE_HOLD` (K2) releases a `keypad_fire` hold**; then dry-run off, each command with a clear line of sight | operator | GL6 | Press the Gate dashboard's *Open gate* and *Close gate* tiles with dry-run on first. `cmd/ack` should read `result` 16 |
| 11 | **GL8**: every entity present, held-open and FIRE once only across an HA restart, configuration `number` entities read and write | session | GL3–GL7 | Production broker |
| 12 | **B7** soak; **GL9** field soak: overnight ΔSOC against budget, D29's enclosure log begun, error paths exercised | operator, session | GL8, B6 | |

### B6 and H10

| Item | Note |
|---|---|
| Bridge reflash against production | B3 above. Bridge NVS keeps its overrides across a reflash. Confirm `deployed` 1 for GateLink and `simnode_diag_enable` 0 afterwards |
| `ha-prod` registered for ha-mcp | User scope, never the repository (HA handoff) |
| HACS, then Fluvy v1.11.3, theme and *Scope* `everywhere` | HA plan §12 step 3 |
| Production `site.yaml` with real notify targets | HA plan §12 step 2 |
| **Production home location** | The sandbox's `sun.sun` looks wrong for the site, so `lran_3_open_at_night` would fire at the wrong hour if production is the same |
| `tools/ha/check_entities.py --server ha-prod` | Rename production entities to match the repository, not the YAML |
| Puppet on production | Its port takes no authentication. The operator decides |
| Retained `availability` | Never publish one by hand on production |
| HEX timeouts | Read the real round trips before raising `hex_timeout_ms` or `hex_rsp_timeout_ms` (bridge handoff, group 8) |
| BF-30 register scales | B6 reads the real MPPT, confirming them and whether `0xEDEA` carries the system voltage |
| SMTP | `input_boolean.lran_email_enabled` when the operator is ready (HA plan §12 step 7) |

## Before deploy, recommended

These update without a walk, so none blocks. Each costs more after install, because it
shows wrong values in production or names something permanently.

| # | Item | Owner | Source |
|---|---|---|---|
| R1 | **Spec v0.19's bridge side** (D84, D85): publish `null` for `cell_temp_c` `INT8_MIN`, and for `alarms` and the four `bms_flags` while `bms_alarms` is `UINT16_MAX`. Today HA shows `-128`, `65535` and `charge_fet: false` | session | Bridge handoff, group 5 |
| R2 | **The BMS's four temperatures have no HA entity.** The operator's rule is that the lowest of the four governs low-temperature charging. HA should show them and a derived minimum. **Each new entity's name is permanent in production's registry**, so add them before B6 | session | HA handoff, *Open* |
| R3 | **The Request configuration button left `config/state`'s node rows null** where a `get_all` filled them. Capture `cmd/ack` and the frame log | session | Bridge log 2026-10-28 |
| R4 | **GateLink's 20 parameter ranges are "proposals"** in the GateLink handoff, while the bridge handoff records the operator approving GateLink's rows on 2026-10-15. Confirm which is current; the names become permanent at B6 | operator | Both handoffs |
| R5 | **The Fluvy review's open decisions**: SOC gauge bands, alert snooze, the Restore defaults dialog, mixed-unit charts | operator | `docs/ha/briefs/2026-10-30-fluvy-port-review.md` |
| R6 | **H8**, the approach prompt. The trigger can be verified only on production | session | HA plan §9.5 |
| R7 | **A state alert already past its threshold at start or reload does not fire**: `lran_2_soc_low`, `lran_2_enclosure_hot`, `lran_2_cell_spread`. Left open by the operator on 2026-10-27 | operator decides | HA handoff, *Open* |
| R8 | **The test-only `input_boolean` override package** the operator chose on 2026-10-27, for banner checks | session | HA handoff, *Open* |

## Post-deploy verify

Each of these needs the installed system to answer. Most have a check named in the source.

- **R-6.4b on a pack that refuses charge.** A full pack stood in for one. The first cold
  sunny morning at the gate is the test, and its 1 V PV margin came from SmartSolar manuals,
  not the 75/15's.
- **The pack's real low-temperature cut-off and `0x8D`'s bits.** A capture on a cold
  morning sets `charge_inhibit_temp_c` and starts the decode R-3.4a and R-3.4c need.
- **H7's SOC and yield figures with real values.** Rerun `automation.lran_4_morning_report`.
- **R-4.4b's PER at the gate as the bridge's WiFi traffic rises.** That PER is the check that
  would reopen M22. A rate measured at one metre says nothing about 87 m.
- **The health hash** (`node/health/state`): a frame that moves only counters, RSSI or SNR
  should publish nothing.
- **A QoS 1 retransmission across a reconnect**, at B7's broker restart.
- **A node document lost on reconnect**, which waited for GateLink (B7 rehearsal).
- **The bridge's `rx_no_interrupt` and `rx_wake_empty`**, which rose in two of three soaks.
- **Event-to-MQTT latency of up to 6 s**, seen on the bench and not explained.
- **M9, M10, M11**: real EXIT→SAFETY gaps, exit-wand hold behaviour, loop detector inputs.
  They tune `detect_sequence_window_ms` and the debounce.
- **M13 and D29**: the seasonal enclosure log, and a fan only if the maxima justify one.
- **W17** closes after deploy, by operator decision (D59).

## Release (GitHub), not needed to deploy

- The whole-document style passes, on their own branch, when the operator asks.
- **The bridge handoff was stale**: it said the bridge runs `c9d3667` and that F1–F4 wait on
  an OTA. The bridge log has OTAs of `57997fa` and then `addfd6b` on 2026-10-28. Corrected
  on 2026-10-31, in the branch that carries this brief.
- **The GateLink handoff's Tier 3 still waits for the input board to "arrive".** The v1.0
  board was benched on 2026-10-24, and v1.1 adds R9.
- Five `CLAUDE.md` passages describe what their files used to say (2026-10-03 prompt audit).
- Two `TODO` markers name finished work: `rx_ladder.h`'s `TODO(BF-19a)` and
  `per_measure.py`'s header. `lib/lran-config`'s `bandwidth_khz` comment predates Envelope A.
- `rssi_report.py`'s periodicity verdict is untrustworthy on long captures.
- osh-labs deviations reported upstream (`lib/vedirect/osh-labs-deviations.md`).
- `docs/archive/` holds 22 superseded revisions, about 1.3 MB; decide what a public
  repository carries.
- **Scan the full git history for secrets before the repository goes public.** The bench
  master and a node key derived from it appeared in a session transcript, and the sandbox
  broker's address appears in the docs. Both retire with the production master, but a scan
  should confirm nothing else was committed.

## Future

- **Per-node keys.** Spec §9.1 derives every key from one master, so one leaked node key
  costs the whole fleet. A protocol question.
- **Node-to-bridge authentication.** Today nothing a node sends carries a MAC the bridge
  verifies. B2 is the near-term consequence.
- WellLink: D19's power source, W14, schema `0x20` and a BF-27 WellLink simulator.
- Fleet-wide `antenna_gain_dbi` and envelope rows (PRD R-4.3i), in node-common.
- A counter that tells a preamble deferral from a header deferral.
- M26's Z-Wave and Insteon inventory, for §5.4's attribution only.
- Why the expander's direction write is lost, the console's spliced lines, the 3.3 V LED
  lit from USB, and the unexplained RST pull-up. None affects deployment.
- Fluvy updates need a render of every view on the sandbox before production takes them.
