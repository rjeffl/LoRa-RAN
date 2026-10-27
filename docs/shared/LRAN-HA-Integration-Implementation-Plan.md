# LRAN Home Assistant Integration Implementation Plan

**Document:** `LRAN-HA-Integration-Implementation-Plan`
**Version:** 0.7
**Artifact:** `/ha/packages/`, `/ha/dashboards/` and the check that ties them to discovery
**Binding specification:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) **v0.18**
**Requirements:** [System PRD](../LRAN-System-PRD.md) §6.2 and §10, [GateLink PRD](../gatelink/LRAN-GateLink_Node-PRD.md) §3.2 and §7, [Bridge PRD](../bridge/LRAN-Bridge_Node-PRD.md)
**Status:** **Draft.** Operator choices taken 2026-10-25; H0, H1 and H2 done 2026-10-25; H3 done 2026-10-26; H4 partly done 2026-10-26; H5 done 2026-10-27
**Last updated:** 2026-10-27
**Handoff and log:** [`docs/ha/HANDOFF.md`](../ha/HANDOFF.md), [`docs/ha/engineering-log.md`](../ha/engineering-log.md)

> **Everything HA-side is built in the sandbox and ported to production as files.** The
> dashboards, helpers, scripts and automations live in this repository as YAML, so the
> production install is a copy and a reload, not a rebuild by hand. The bridge's discovery
> configs are the other half, and they are already generated from the firmware (`ha/README.md`).

---

## Table of contents

1. [Scope](#1-scope)
2. [Operator choices, 2026-10-25](#2-operator-choices-2026-10-25)
3. [Rules this plan inherits](#3-rules-this-plan-inherits)
4. [What the sandbox shows today](#4-what-the-sandbox-shows-today)
5. [Bridge-side work the dashboards need](#5-bridge-side-work-the-dashboards-need)
6. [Repository layout and installation](#6-repository-layout-and-installation)
7. [Shared plumbing](#7-shared-plumbing)
8. [Dashboards](#8-dashboards)
9. [Automations](#9-automations)
10. [History and the recorder](#10-history-and-the-recorder)
11. [Tasks](#11-tasks)
12. [Porting to production](#12-porting-to-production)
13. [Open questions](#13-open-questions)
14. [Changelog](#14-changelog)

---

## 1. Scope

This plan covers what Home Assistant does with the entities and events the bridge already
publishes: dashboards, helpers, notification routing and automations. It covers GateLink
and the bridge. WellLink gets the same treatment when its PRD defines entities; until then
its entities appear only on the Network view (§8.2).

The plan does not change any MQTT topic or payload. The specification owns those (spec
§16). Where a dashboard needs an entity the bridge does not publish, §5 lists it as bridge
work under the requirement that already asks for it.

GateLink Impl Plan **GL8** verifies the integration on the production broker. The tasks
here come before GL8, so that GL8 verifies the finished dashboards and automations
together with the entities. The bridge handoff asked for this plan before GL8.

## 2. Operator choices, 2026-10-25

| Question | Choice |
|---|---|
| Dashboard layout | **Two dashboards.** *Gate* for daily use; *LRAN* with Power, Network, Events and Maintenance views. On 2026-10-26 the operator moved Maintenance to a third, admin-only dashboard, because HA cannot gate a view (§8.2) |
| Card set | **Core cards only.** Nothing to install on production, and no HACS dependency to keep current |
| Gate control entity | **Fix it in the bridge.** Discovery publishes the D8 `cover` and hold `switch` (§5, task F1). Dashboards use the four command buttons until F1 lands |
| Automation tiers | **All five:** alerts, health, reminders, reports, convenience (§9) |
| Notification channels | **Companion-app push now; SMTP email later, behind a switch.** Email is built and tested, then held off by `input_boolean.lran_email_enabled` until the operator turns it on. **No SMS:** push replaces it |
| Units | **The bridge keeps publishing mV and mA**, GateLink's native units on the wire. The Gate dashboard shows V and A through HA-side conversion (§7.4). The LRAN dashboard shows mV and mA |

These choices are about the HA layer, not about the protocol or the architecture, so the
Decision Register does not carry them.

## 3. Rules this plan inherits

Each rule below is already a requirement. The plumbing in §7 exists to satisfy them once,
so no automation has to remember them.

| Rule | Source | How this plan meets it |
|---|---|---|
| Automations trigger on `lran/<node>/event/<name>`, never on a retained `binary_sensor` | System PRD §6.2; spec §16.3; R-3.2.4d | Every alert in §9 has an MQTT trigger. The binary sensors appear on dashboards only |
| Ignore `synthetic: true` | `ha/README.md`; BF-27; R-5.2d | The shared event filter in §7.2 drops it. `is not sameas true`, so a payload missing the key still alerts |
| Deduplicate a retransmitted event | Spec §16.3, keyed on §7.3's `(ctx_id, event_id)`; a bridge event keys on `(boot, event_id)` (D67) | The shared filter in §7.2 |
| Held-open and FIRE fire exactly once, with no replay on HA restart or discovery refresh | GateLink PRD **V-5** | Event triggers do not replay. The filter covers retransmission. A follow-up (`follow_up: true`) replaces the first push and sends no second email (§7.3) |
| FIRE is routed separately | Spec §8.9; R-3.2.5a | FIRE has its own automation and the critical notification channel (§9.1) |
| LRAN is never a safety authority. The held-open alert notifies; it does not close the gate | System PRD §10.1; GateLink PRD **S-8** | No automation opens or closes the gate by itself. Reminders and the approach prompt send an actionable notification, and a person taps it (§9.3, §9.5) |
| No `reboot` button | `ha/README.md`; spec §8.1 | The Maintenance view does not add one through a script either |
| A stale value is unavailable, not the last good value | System PRD §6.2; spec §16.4 | Cards show `unavailable` as it is. No template substitutes a cached value |

## 4. What the sandbox shows today

The sandbox HA ran **2026.10.3** at the survey and **2026.10.4** at H1, so the YAML here
targets that release's automation syntax (`triggers`, `conditions`, `actions`). The bridge (`lora_bridge_*`) and GateLink
(`gatelink_*`) are live. The entity IDs below come from the sandbox registry on 2026-10-25.

| Area | Entities |
|---|---|
| Gate | `sensor.gatelink_gate`, `sensor.gatelink_hold_source`, `sensor.gatelink_movement_cause`, `sensor.gatelink_last_direction`, `sensor.gatelink_last_vehicle`, `binary_sensor.gatelink_held_open`, `binary_sensor.gatelink_vehicle_while_held_open`, `binary_sensor.gatelink_safety_loop`, `binary_sensor.gatelink_exit_wand`, `binary_sensor.gatelink_fire_input`, `binary_sensor.gatelink_alarm_input`, `binary_sensor.gatelink_hard_shutdown_latch`; buttons `open`, `close`, `hold_open`, `release_hold` |
| Battery | `sensor.gatelink_battery` (SOC), `pack_voltage`, `pack_current`, `cell_1_voltage` … `cell_4_voltage`, `battery_cycles`, `charge_drawn_since_charging`, `bms_reading_age`, `bms_link_rssi`; `binary_sensor.gatelink_battery_protection`, `charging_inhibited`, `charging_inhibited_bms` |
| Solar | `pv_power`, `pv_voltage`, `solar_yield_today`, `solar_yield_total`, `mppt_charge_state`, `mppt_error`, `battery_voltage_mppt`, `battery_current_mppt`, `load_current`, and the MPPT configuration readback sensors |
| Environment | `enclosure_temperature`, `mppt_temperature`, `node_supply_voltage` |
| Link | `rssi`, `snr`, `missed_polls`, `last_seen`, `protocol_version`, the PHY settings; bridge `frames_received`, `frames_transmitted`, `frames_discarded`, firmware version, commit and OTA slot |
| Configuration | 55 `number` entities across the bridge, GateLink and WellLink; `switch.gatelink_deployed`, `buzzer_enable`, `mppt_write_enable`, `switch.lora_bridge_simnode_diag_enable`; `binary_sensor.gatelink_relay_dry_run`, `configuration_persisted` |

Three things in the sandbox need attention before a dashboard is built on it:

- **The registry holds `simnode_0` to `simnode_3` from bench sessions,** with 44
  unavailable entities. They are a sandbox artifact. Task H0 removes them, so that
  dashboards are not built on a registry that production will not have. **Done
  2026-10-25:** the registry held 48 entities on four devices. Deleting them from the
  registry alone was not enough, because the broker still held their 48 retained
  `homeassistant/+/lran_simnode*/config` configs and would have recreated them at the next
  restart. Clearing those configs removed the devices. The bridge's own
  `switch.lora_bridge_simnode_diag_enable` stays. Turning it on with a simnode running
  brings the devices back, because spec §16.6 keeps bench entities by design.
- **Voltages and currents are published in mV and mA,** and they stay that way (§2). Only
  the Gate dashboard converts them (§7.4).
- **The production entity IDs are a premise, not a fact.** They follow from the device
  names and entity names in discovery, and production will match only if nothing in its
  registry already claims one. Task H9's check is what would show they differ.

## 5. Bridge-side work the dashboards need

These gaps lie between GateLink PRD §7 and what discovery publishes. They are bridge tasks
under existing requirements, recorded here because the dashboards are where they show.
Each goes to the bridge task list (`LRAN-Bridge-Firmware-Tasks`) when it is scheduled.

| ID | Gap | Requirement | Effect on this plan until it lands |
|---|---|---|---|
| **F1** | No `cover` (`device_class: gate`) and no "Hold gate open" `switch`. Discovery publishes four buttons instead | **D8**; GateLink PRD §7.1 | The Gate dashboard uses buttons and the text gate state. Voice control (§9.5) waits for F1 |
| **F2** | Missing §7.2 entities: auto-close pending, held open by keypad, SOC source, pack temperatures, uptime, close-immediately switch, restore-defaults button, BMS polling switch | GateLink PRD §7.2; R-4.5b asks for all three temperature sources, and the pack's is absent | Power and Maintenance views leave a placeholder row for each |
| **F3** | No HA `event` entities for FIRE and vehicle-while-held-open | GateLink PRD §7.2 lists both as `event`, non-retained | None for automations, which subscribe to the MQTT topics directly. Without F3, the logbook shows these events only through the Events view's automation-written entries (§8.2) |

**F1 changes what an existing button does.** Once the cover exists, the buttons duplicate
it, and the PRD has no requirement for them. Whether discovery keeps them is part of F1.

## 6. Repository layout and installation

```
ha/
  discovery/            generated, unchanged (ha/README.md)
  automations/          the existing example, kept as the minimal reference
  packages/lran/        hand-written; one package per concern
    core.yaml           helpers, event filter, notification script (§7)
    templates.yaml      derived sensors (§7.4)
    alerts.yaml         tiers 1 and 2 (§9.1, §9.2), and one level selector per alert
    messages.yml        the text of every push, email and logbook entry (§7.5)
    reminders.yaml      tier 3 (§9.3)
    reports.yaml        tier 4 (§9.4)
    convenience.yaml    tier 5 (§9.5)
    site.yaml.example   the per-install values: push and email scripts, SMTP, recorder, zones
  dashboards/
    gate.yaml           the Gate dashboard (§8.1)
    lran.yaml           the LRAN dashboard: Power, Network, Events (§8.2)
    lran-maintenance.yaml  its Maintenance view, admin only (§8.2)
tools/ha/
  check_entities.py     every entity ID the YAML names exists in a live HA (H9)
```

**Packages, so production gets the whole set in one include.** Production's
`configuration.yaml` gains two blocks:

```yaml
homeassistant:
  packages: !include_dir_named packages/lran

lovelace:
  dashboards:
    lran-gate:
      mode: yaml
      filename: dashboards/lran/gate.yaml
      title: Gate
      icon: mdi:gate
      show_in_sidebar: true
    lran-detail:
      mode: yaml
      filename: dashboards/lran/lran.yaml
      title: LRAN
      icon: mdi:radio-tower
      show_in_sidebar: true
      require_admin: false
    lran-maintenance:
      mode: yaml
      filename: dashboards/lran/lran-maintenance.yaml
      title: LRAN maintenance
      icon: mdi:tune-variant
      show_in_sidebar: true
      require_admin: true
```

**YAML-mode dashboards, so the repository is the source.** A dashboard edited in the UI on
production would drift from the repository and be lost at the next copy. The cost is that
the dashboard cannot be edited in the UI. To try a change, edit the file in the sandbox and
reload.

**`site.yaml` is the only file that differs between installs.** It defines two scripts,
`script.lran_site_push` and `script.lran_site_email`, which call that install's notify
services, and it holds the SMTP account, the recorder settings (§10) and the approach zone.
It is not committed, because it names the operator's phone. `site.yaml.example` documents
every key. Thresholds are helpers (§7.1), so they need no entry here.

## 7. Shared plumbing

### 7.1 Helpers

| Helper | Purpose | Default |
|---|---|---|
| `input_boolean.lran_email_enabled` | Gates every email | **off** |
| `input_boolean.lran_alerts_muted` | Mutes tiers 2 to 4 for bench work. **Never mutes FIRE or vehicle-while-held-open** | off |
| `input_text.lran_recent_events` | The last eight event keys, for deduplication (§7.2) | empty |
| `input_number.lran_soc_warn_pct` | Low-SOC warning | 40 |
| `input_number.lran_soc_alarm_pct` | Low-SOC alarm | 25 |
| `input_number.lran_enclosure_temp_warn_c` | Enclosure over-temperature | 40, the host's rated maximum (R-4.5a) |
| `input_number.lran_cell_spread_warn_mv` | Cell imbalance | 50 |
| `input_number.lran_offline_warn_min` | Node offline before alerting | 10 |
| `input_number.lran_held_open_reminder_h` | Held-open reminder | 4 |
| `input_number.lran_pack_capacity_ah` | The pack's capacity, for the autonomy estimate (§7.4) | 100, R-6.1a |
| `input_boolean.lran_defaults_seeded` | Set once the defaults above are applied | off |
| `input_select.lran_alert_<alert>` | One per alert: `Off`, `Low`, `Normal`, `High` or `Critical` (§7.5). In `alerts.yaml`, beside the automations | the alert's `level` in `messages.yml` |
| `input_boolean.lran_alert_levels_seeded` | Set once the alert levels are applied, by `lran_0_seed_alert_levels` | off |
| `input_text.lran_gatelink_boot_times` | GateLink's boot times in the last hour, for `lran_2_node_boot`'s count (§9.2). In `alerts.yaml` | empty |
| `counter.lran_gatelink_traversals_today` | Vehicle traversals, reset at midnight | 0 |

**Every threshold is a starting value, not a measured one.** The SOC thresholds, the cell
spread and the offline delay should be revisited after a month of production history.
They are helpers so that tuning them needs no file change.

**The defaults are applied once, by automation `lran_0_seed_defaults`, not by `initial:`.**
HA applies `initial` at every start, which would undo a value tuned in the UI. The
automation runs at start while `lran_defaults_seeded` is off, and then turns it on. To
reapply the defaults, turn it off and restart.

**`lran_enclosure_temp_warn_c` is in °C, and HA shows temperatures in the install's unit
system.** The sandbox shows °F. The H5 automation that compares the two converts by the
sensor's unit.

### 7.2 The event filter

Every automation on an event topic first calls `script.lran_event_admit`, written once in
`core.yaml`, and goes on only if its response says `admit`:

1. Drop the event if `synthetic is sameas true`.
2. Form its key: `<node>:<ctx_id>:<event_id>` for a node's event, or
   `bridge:<boot>:<event_id>` for the bridge's (D67), then `:f` for a follow-up or `:e`
   otherwise.
3. Drop it if the key is in `input_text.lran_recent_events`. Otherwise, prepend the key and
   keep the last eight.

The response also carries `tag`, the key without its follow-up suffix, which §7.3 uses as
the push tag.

**The key carries the follow-up bit.** A follow-up reuses its first edge's `event_id`
(spec §7.3), so a key of `ctx_id:event_id` alone, as v0.1 of this plan had it, drops every
follow-up as a repeat. That is the defect spec v0.15 corrected in the bridge (BF-25).

**A script, not a template condition**, because the filter has to record the key it
admits, and a condition cannot write. The script runs `queued`, so two triggers carrying
the same event are checked one after the other.

**An event with no usable key is admitted and not recorded**, for example a bridge event
whose `boot` is null. A repeat of it can alert twice, which is better than withholding an
alert the filter cannot place.

**Eight keys, because a retransmission arrives within seconds.** The filter only has to
remember the recent past. A key from a new `ctx_id` after a reboot cannot collide with an
old one. The list holds fewer than eight only when the keys are long enough to overrun
`input_text`'s 255 characters. Seven keys with full-width `uint32` fields fit.

### 7.3 The notification script

`script.lran_notify` takes `alert`, `fields`, `tag`, and optional `msg`, `follow_up`,
`push_only` and `actions`. `alert` names the alert's selector and its `messages.yml` entry;
`msg` names a different entry, for a follow-up such as GateLink's return; `fields` carries
the event's raw values for the entry's placeholders (§7.5). A caller with no alert passes
`severity` (`critical`, `high`, `normal`, `low`), `title` and `message` instead.
Automations call it and never call a notify service directly. It calls `site.yaml`'s `lran_site_push` and
`lran_site_email` (§6), so the channels change in one place. Every alert writes a logbook entry, logged against `script.lran_notify` so the Events
view's logbook card, which lists entities, can show it (§8.2). **An alert held back by
`lran_alerts_muted` is still logged**, marked `(alerts muted, not pushed)`, as an `Off`
one is. S-14 asks for every enclosure excursion to be recorded, and muting is for the
phone, not the record. Before v0.7 a muted alert left no entry.

| Severity | Push | Email | Muted by `lran_alerts_muted` |
|---|---|---|---|
| `critical` | Critical alert: sounds through Do Not Disturb (iOS `critical: 1`; Android `channel: alarm_stream`) | Yes, if enabled | **No** |
| `high` | Time-sensitive push | Yes, if enabled | **No** |
| `normal` | Ordinary push | No | Yes, and logged as muted |
| `low` | None; logbook entry only | No | Yes, and logged as muted |
| `off` | None. The logbook entry is marked `(alert off, not pushed)` | No | — |

**An alert's severity is its selector's level**, `Off`, `Low`, `Normal`, `High` or `Critical`,
which the operator sets on the Maintenance dashboard (§7.5). `severity` applies only to a
caller with no selector. **FIRE and the held-open vehicle are never muted**, whatever
their level, so plan §7.1's promise does not depend on the selector.

**`tag` makes a follow-up replace the first alert.** R-3.2.4b sends a follow-up that
refines the same event with the traversal direction. The script uses the filter's `tag`
(§7.2) as the push tag, so the phone shows one notification, updated, rather than two.
Email goes out on the first event only, because an email cannot be replaced.
`push_only` sends no email either, for an alert about a failure the email path may share.

**At H1 the sandbox's `site.yaml` pushes to the Mac only** (`notify.mobile_app_jbook16_m5`),
and its email script writes a persistent notification, because the sandbox has no SMTP
account. The phone has joined the sandbox's companion app since the survey, as
`notify.mobile_app_jliphone16`. Critical alerts and actionable notifications have to be
checked on that phone before the FIRE route counts as verified (task H4), and real SMTP
before email is turned on (§12).

### 7.4 Derived sensors

Template sensors in `templates.yaml`. Each derived sensor reads `unavailable` when an input
is unavailable, as System PRD §6.2 requires.

| Sensor | Formula |
|---|---|
| `sensor.lran_gatelink_cell_spread` | max − min of the four cell voltages, mV |
| `sensor.lran_gatelink_net_power` | Pack voltage × pack current, W. Positive while the pack charges |
| `sensor.lran_gatelink_draw_current` | MPPT battery current − pack current, mA, clamped at 0: what everything on the battery bus draws |
| `sensor.lran_gatelink_draw` | The `integration` integration over the draw current, mAh, cumulative |
| `sensor.lran_gatelink_draw_7d` | The `statistics` integration's `change` in the draw over 7 days |
| `sensor.lran_gatelink_autonomy_days` | SOC × `lran_pack_capacity_ah`, over the mean daily draw across the days the 7-day window covers. Unavailable until it covers a day. **An estimate**, labelled as one on the card |
| `sensor.lran_gatelink_link_quality` | `poor` at 3 or more missed polls or SNR below −10 dB; `marginal` at 1 or more or below −5 dB; otherwise `good`. For the Gate dashboard's status chip |
| `sensor.lran_gatelink_enclosure_temp_max_24h`, `_min_24h` | The `statistics` integration over 24 h |
| `sensor.lran_gatelink_traversals` | The traversal counter's value, with `state_class: total_increasing`, so the Events view's 30 d chart has statistics to read (§8.2). The midnight reset reads as a new cycle |

**Net power and the draw read the pack, not the MPPT's load output.** v0.1 had net power
as PV power − pack voltage × load current. The gate controller and GateLink hang on the
battery bus, and the MPPT's load output reads 0, so that formula would have reported the
whole PV power as surplus. The MPPT's battery current and the pack's are both positive
into the battery (spec §7.2.2, §7.2.3). The two readings are not simultaneous, so a draw
sample can dip below zero, and it is clamped.

**Link quality's thresholds are starting values**, as §7.1's are. SF9 demodulates down to
about −12.5 dB SNR, so −5 dB leaves 7.5 dB of margin and −10 dB leaves 2.5. RSSI is not
part of the rule: SNR is the margin that matters on this link, and RSSI is on the Network
view.

**The Gate dashboard's V comes from rescaling template sensors**,
`sensor.lran_gatelink_pack_voltage_v` and `sensor.lran_gatelink_pv_voltage_v`. Each is the
raw mV value ÷ 1000, rounded to two decimals. §8.1 shows no current, so there is no A
sensor. They carry no device class. With `voltage`, HA suggests a display precision of
0 and shows 13.31 V as 13 V, and the template schema rejects
`suggested_display_precision`. They are excluded from the recorder (§10), so they add no history rows. Graphs read
the raw sensors. A per-entity display-unit override in HA's registry would do the same, but
a YAML copy does not carry it to production.

### 7.5 Alert levels and the message catalog

**The operator chose on 2026-10-26 to set each alert's delivery from the dashboard and to
keep every alert's wording in one file**, so that changing either needs no reading of
automation YAML.

**Each alert has a level selector**, `input_select.lran_alert_<alert>`, with the options
`Off`, `Low`, `Normal`, `High` and `Critical` (§7.3's table). `Low`, added in v0.7 for
H5's boot alert, logs and sends nothing. `Off` sends no push and no email, and the logbook
still records the event, marked as not pushed: an alert switched off is hidden from the
phone, not from the record. The selectors sit on the Maintenance
dashboard's *Alerts* card, under a warning that names any alert set to `Off`.

**`ha/packages/lran/messages.yml` holds the text.** Each entry is the alert's name, with
a `title`, a `message` and a default `level`. `{name}` in the text is replaced with the
field of that name the automation passes. A map under a field's name translates its raw
value into words, with `missing` for an empty value and `other` for an unlisted one.
`lran_notify` reads the file with `!include`, so an edit takes effect at a script reload.
The extension is `.yml` because HA loads every `*.yaml` in the directory as a package.

**An alert with two levels is two alerts**, because a selector holds one level: SOC low
and SOC alarm, and a boot and a boot loop (§9.2). The SOC pair shares a push tag, so the
alarm replaces the warning on the phone. Each boot keeps its own push, and the boot loop's
replaces itself at each further boot.

**The default levels are seeded once**, by `lran_0_seed_alert_levels`, with a flag of its
own, so an install seeded before the selectors existed still gets them. The card's
*Restore default levels* runs `script.lran_alert_levels_default`, which reads the levels
from `messages.yml`.

**A selector added after that seed starts at `Off`**, its first option, because the flag
is already on. The *Alerts* card's warning names it, so the gap shows. Set it on the card,
or run *Restore default levels*, which also resets every other alert. H5's nine selectors
were set that way on the sandbox, whose levels were all at their defaults.

**A missing entry does not withhold an alert.** `lran_notify` sends under the bare name,
and an alert with no selector falls back to the caller's severity. Because that fallback
is silent, `tools/checks/ha_messages.py`, in the checks job, fails on an alert with no
entry, an entry with a level and no selector, and a placeholder its automation does not
pass.

## 8. Dashboards

Both dashboards use core cards only: `tile`, `entities`, `glance`, `gauge`,
`history-graph`, `statistics-graph`, `logbook`, `conditional`, `markdown`, and `heading`
for section titles. Sections
view layout, so the Gate dashboard reflows to one column on a phone.

### 8.1 Gate (daily use, phone first)

From top to bottom:

1. **Alert banners**, each a `conditional` card shown only while its condition holds: FIRE
   input asserted; vehicle while held open; hard-shutdown latch; GateLink unavailable;
   charging inhibited; battery protection. Each is an `ha-alert` in a text-only markdown
   card. Charging inhibited reads GateLink's derived indication (R-6.4a), which still
   reads while VE.Direct is stale.
2. **Gate control.** A tile for the gate state, with the movement cause beneath. Buttons
   for Open, Close, Hold open and Release hold until F1 replaces them with the cover and
   hold switch. Each button asks for confirmation, because a press is a relay pulse at the
   gate. Hold source shows only while the gate is held.
3. **Last vehicle.** Time and direction, and today's traversal count.
4. **Power at a glance.** SOC gauge, pack voltage in V, PV power, net power, and the MPPT
   charge state.
5. **Link chip.** `link_quality` and `last_seen` as two half-width tiles. Tapping either
   opens the LRAN Network view, at `/lran-detail/network`, so §8.2's Network view takes the
   path `network`.

**`last_seen` is a tile of its own, not the link tile's secondary text.** A tile shows only
its own entity's state and attributes. Copying `last_seen` into an attribute of
`link_quality` would write a recorder row at every publish, which §10 excludes `last_seen`
to avoid.

**The SOC gauge has no severity bands.** A gauge's bands are fixed numbers, and the SOC
thresholds are helpers (§7.1), so the bands would drift from them.

### 8.2 LRAN (detail)

| View | Contents |
|---|---|
| **Power** | SOC, pack voltage and current, the four cells and their spread, cycles, protection and charge-inhibit flags. PV power and voltage, yield today and total, charge state, charger error. Enclosure and MPPT temperatures, and the pack temperatures once F2 lands. Graphs: `history-graph` for 24 h and 7 d; `statistics-graph` for 30 d and 1 y of SOC, enclosure temperature (min, mean, max) and daily yield |
| **Network** | Bridge frames received, transmitted and discarded. GateLink RSSI, SNR, missed polls and last seen, with 7 d history. PHY settings, protocol version, unsupported-version flag. Firmware version, commit and OTA slot. BMS BLE RSSI as a 30 d statistics graph, which is the trend M23 reads. WellLink rows, unavailable until it is deployed |
| **Events** | A `logbook` card of LRAN events and gate state changes. Traversals per day for 30 d. Node boots with their reset cause, which is how a boot loop shows (spec §8.14) |
| **Maintenance** | `require_admin`. The *Alerts* card: a level selector per alert, mute, email and *Restore default levels* (§7.5). The configuration `number` entities, grouped by node and by subsystem. The deployed, dry-run, buzzer and MPPT write-enable switches. Request status and request configuration. The MPPT configuration readback. Configuration persisted, shown first, because `off` means the node is running unsaved settings |

**Maintenance is a dashboard of its own, `lran-maintenance`, because it is admin only.** HA
gates a dashboard with `require_admin` and has no equivalent for a view. A view's only
gate is a `visible` list of user IDs, which differ on every install and so would not
survive the copy to production. The other three views stay in `lran-detail`. The
operator chose this on 2026-10-26.

**Traversals per day come from `sensor.lran_gatelink_traversals`** (§7.4). The counter has
no state class, so HA keeps no long-term statistics for it, and its recorder history ends at
`purge_keep_days`. **A node's reset cause reaches the Events view through the logbook**:
`lran_2_node_boot` (§9.2) logs it, and the Events view charts boots per day from
`sensor.gatelink_boot_count`. The logbook card lists entities, so `script.lran_notify`
logs each entry against itself (§7.3).

**The four command buttons are not on the LRAN dashboard.** Each press is a relay pulse at
the gate, and the Gate dashboard already carries them, each behind a confirmation.

**The Maintenance view carries an MPPT write-enable warning.** Charge-controller writes
are a battery-affecting path (GateLink PRD **S-11**). The switch shows with a markdown card
above it stating that, and the view is admin only.

## 9. Automations

Every automation has an ID of the form `lran_<tier>_<name>`, so production's automation
list groups them. **Every alert in tiers 1 to 3 gets a level selector and a `messages.yml`
entry** (§7.5), and its automation passes raw fields, not text. Tier 4's reports take their
text from `messages.yml` too.

### 9.1 Tier 1 — alerts

| ID | Trigger | Action |
|---|---|---|
| `lran_1_vehicle_while_held` | `lran/gatelink/event/vehicle_while_held_open` | `high`: hold source, direction when present. Follow-up replaces it (§7.3). **The primary operational requirement, R-3.2.4** |
| `lran_1_fire` | `lran/gatelink/event/fire_asserted` | `critical`. **Its own automation, so it can be routed apart from every other alert** (spec §8.9) |
| `lran_1_hard_shutdown` | `lran/gatelink/event/hard_shutdown` | `high` |
| `lran_1_bms_alarm` | `lran/gatelink/event/bms_alarm` | `high`, with `detail` |
| `lran_1_gatelink_offline` | `lran/gatelink/availability` is `offline` for `lran_offline_warn_min` | `high`. A second push when it returns, with how long it was gone |
| `lran_1_bridge_offline` | The bridge's own `availability` (its LWT) is `offline` for 5 minutes | `high`. **Push only**, because the HA host that would send the email may be what failed |

**The two offline alerts read `alerts.yaml`'s own MQTT binary sensors**,
`binary_sensor.lran_gatelink_online` and `binary_sensor.lran_bridge_online`, because a state
trigger can wait out a delay and an MQTT trigger cannot. The GateLink sensor takes the
bridge's availability as its own, so a bridge outage leaves it unavailable rather than off,
and one outage does not alert twice.

### 9.2 Tier 2 — health

| ID | Trigger | Action |
|---|---|---|
| `lran_2_soc_low` | SOC below `lran_soc_warn_pct`, then below `lran_soc_alarm_pct` | Alerts `soc_low`, `normal`, then `soc_alarm`, `high`. The alarm replaces the warning's push, and the warning stands down while the alarm holds |
| `lran_2_charge_inhibited` | `lran/gatelink/event/charge_inhibited` | `normal`, naming the path from `inhibit_path` (D81) |
| `lran_2_mppt_error` | `lran/gatelink/event/mppt_error` | `normal`, with the error name from `sensor.gatelink_mppt_error`, because spec §7.3 gives `MPPT_ERROR`'s `detail` no meaning |
| `lran_2_enclosure_hot` | Enclosure temperature above `lran_enclosure_temp_warn_c` for 15 min | `normal`. **S-14 asks for excursions to be logged and acted on**; §7.3 logs every alert, a muted one included |
| `lran_2_cell_spread` | `cell_spread` above `lran_cell_spread_warn_mv` for 30 min | `normal` |
| `lran_2_node_boot` | `lran/gatelink/event/boot` | Alert `node_boot`, `low`, with `reset_cause`. The third boot within an hour sends `node_boot_loop`, `high`, instead |
| `lran_2_phy_reverted` | `lran/+/event/phy_reverted` | `high`. A PHY revert means a change failed (spec §12.4.2). The bridge's report adds the reason and the node (spec §16.7.5) |

**The state alerts use template triggers, not `numeric_state`.** A `numeric_state` trigger
watches only its entity, so a threshold changed in the UI would take effect only when the
sensor next moved. A template trigger watches every entity it reads, so moving a threshold
past the reading fires the alert at once. H5 verified the state alerts that way. A sensor
that returns from unavailable still past its threshold alerts again, and the shared tag
replaces the earlier push.

### 9.3 Tier 3 — reminders

Each reminder sends an actionable push with a **Close gate** action. A separate automation
handles the `mobile_app_notification_action` event and presses `button.gatelink_close`, or
calls `cover.close_cover` after F1. **The gate closes only when a person taps the action**
(S-8).

| ID | Trigger |
|---|---|
| `lran_3_held_open_long` | Held open for `lran_held_open_reminder_h`; repeats at the same interval |
| `lran_3_open_at_night` | Gate not closed one hour after sunset |
| `lran_3_open_nobody_home` | Gate not closed and every `person` away for 15 min. **Production only:** the sandbox has no person entities with trackers |

### 9.4 Tier 4 — reports

| ID | When | Content |
|---|---|---|
| `lran_4_morning` | 07:00 daily, `normal` | SOC and its 24 h change, yesterday's yield, traversals yesterday, enclosure min and max, missed polls, any event in the last 24 h |
| `lran_4_weekly_battery` | Sunday 08:00, `normal` | SOC range over the week, cycles added, worst cell spread, BMS link RSSI trend, MPPT time in each charge state |
| `lran_4_count_traversal` | `lran/gatelink/event/vehicle_detected` | Increments the traversal counter. No notification |
| `lran_4_reset_counter` | Midnight | Resets it |

### 9.5 Tier 5 — convenience

| ID | Trigger | Action |
|---|---|---|
| `lran_5_approach_prompt` | A `person` enters the approach zone set in `site.yaml` | Push with an **Open gate** action. Like §9.3, the gate moves only when a person taps it |
| Voice control | Assist, through the cover | Waits for F1: Assist exposes a `cover`, not a set of buttons |

## 10. History and the recorder

**The detail graphs beyond 10 days come from long-term statistics, not the recorder's
history.** The recorder keeps state history for `purge_keep_days`, 10 by default. HA
keeps 5-minute and hourly statistics indefinitely for every sensor with a `state_class`,
and most GateLink power, temperature and link sensors have one. The 30 d and 1 y graphs in
§8.2 read those statistics.

Three recorder changes are proposed for production, all in `site.yaml` because the recorder
is install-wide:

- **Exclude `sensor.gatelink_last_seen` and `sensor.gatelink_bms_reading_age`.** Both
  count seconds since an update, so they change at every publish, and their history says
  nothing that `missed_polls` and availability do not.
- **Exclude the rescaling `sensor.lran_gatelink_*_voltage_v` template sensors** (§7.4).
  The raw sensors already hold that history. `site.yaml.example` carries this exclusion
  already filled in, because every install needs it.
- **Raise `purge_keep_days` to 30**, if the production database has room, so the Events
  logbook reaches back a month.

## 11. Tasks

Tasks run in order unless the table says otherwise. Each is built and checked in the
sandbox. "Verified" means exercised with a real or bench-injected event, never assumed
from the YAML.

| Task | Content | Depends on | Acceptance |
|---|---|---|---|
| **H0** | Remove the `simnode_*` devices from the sandbox registry | — | No `simnode` entity in the sandbox registry. **Met 2026-10-25** (§4) |
| **H1** | `core.yaml` and `templates.yaml`: helpers, event filter, notification script, derived sensors | H0 | A replayed event with the same key is dropped. A `synthetic: true` event is dropped. Email sends only with the switch on. **Met 2026-10-25** against the email stand-in; real SMTP is not verified (§7.3) |
| **H2** | Gate dashboard | H1 | Renders on a phone in one column. Each alert banner appears when its condition is forced, and only then. **Met 2026-10-25** with states forced through HA's REST API, not the broker |
| **H3** | LRAN dashboard, four views | H1 | Every entity in §4 appears on some view. Statistics graphs show data. **Met 2026-10-26**, with the Maintenance view a dashboard of its own (§8.2) |
| **H4** | Tier 1 alerts | H1 | Each alert verified once with a bench-injected event; held-open and FIRE verified not to repeat after an HA restart (**V-5**). FIRE's critical alert verified on a phone. **Partly met 2026-10-26**: the four event alerts, V-5 and FIRE on the phone pass; the two offline alerts are not yet exercised |
| **H5** | Tier 2 health | H4 | Each automation verified by forcing its trigger or lowering its threshold. **Met 2026-10-27**: the state alerts by moving thresholds, the event alerts by events injected on the sandbox broker |
| **H6** | Tier 3 reminders and the action handler | H4 | Tapping **Close gate** presses the close command with relay dry-run on (**V-8**) |
| **H7** | Tier 4 reports | H1 | Morning summary renders with real values. Counter increments on a bench traversal |
| **H8** | Tier 5 approach prompt | H6; production `person` entities | Tapping **Open gate** with dry-run on issues the open command. Voice control waits for F1 |
| **H9** | `tools/ha/check_entities.py`: every entity ID named in `ha/packages/` and `ha/dashboards/` exists in the HA it is pointed at | H3 | Passes against the sandbox. Run against production at the port (§12) |
| **H10** | Port to production | H1–H9; bridge **B6** | §12's procedure, then GL8 |

**F1 to F3 are bridge tasks** (§5). They can run alongside H1 to H7. When F1 lands, H2,
H6 and H8 switch to the cover and hold switch in the same change.

## 12. Porting to production

1. Run B6 first. Production entities exist only once the bridge has published discovery
   to the production broker.
2. Copy `ha/packages/lran/` to `packages/lran/` and `ha/dashboards/` to `dashboards/lran/`
   in the production config directory, matching the paths in §6. Write
   `site.yaml` from `site.yaml.example` with the production notify targets.
3. Add the §6 blocks to `configuration.yaml`, check the configuration, and restart.
4. Run `tools/ha/check_entities.py` against production. **A missing entity ID means the
   production registry named something differently.** Rename the production entity to
   match the repository, rather than editing the YAML for one install.
5. Verify tier 1 on production with relay dry-run on, as part of GL8.
6. Turn on `input_boolean.lran_email_enabled` when the operator is ready for email.

## 13. Open questions

- **Does the approach zone (§9.5) belong on this property at all?** It needs a phone
  tracker reliable enough that a missed prompt is the worst case. The operator decides at
  H8.

## 14. Changelog

| Version | Date | Change |
|---|---|---|
| 0.1 | 2026-10-25 | First draft. Operator choices recorded in §2, including units and no SMS; the sandbox survey in §4; bridge gaps F1–F3; tasks H0–H10 |
| 0.2 | 2026-10-25 | H0 and H1 done. §7.2's key carries the follow-up bit, which v0.1's would have dropped, and the filter is a script. §7.4's net power and draw read the pack, by operator choice, because the loads are on the battery bus. Defaults seeded once (§7.1); site scripts (§6); link-quality thresholds stated |
| 0.4 | 2026-10-26 | H3 done. §8.2's Maintenance view is its own admin-only dashboard, `lran-maintenance`, because HA gates dashboards, not views (§6). §7.4 adds `sensor.lran_gatelink_traversals` for the 30 d traversal chart. §7.3's logbook entries name `script.lran_notify`, so the Events view's logbook card can list them |
| 0.3 | 2026-10-25 | H2 done. §8.1's link chip is two tiles, so `last_seen` adds no recorder rows; the Network view's path is fixed as `network`. §8 adds the `heading` card. §7.4's V sensors drop their device class, which rounded them to whole volts |
| 0.5 | 2026-10-26 | H4 partly done. §7.3 adds `push_only`, for the bridge-offline alert. §9.1 states the two connectivity sensors the offline alerts read |
| 0.6 | 2026-10-26 | By operator choice, §7.5 adds a level selector per alert and `messages.yml`, the one file of alert text. §7.3's script takes `alert` and `fields`, and adds the `off` level. §8.2's Maintenance dashboard gains the *Alerts* card |
| 0.7 | 2026-10-27 | H5 done. By operator choice, every selector gains `Low` (§7.5), and an alert with two levels is two alerts (§9.2). §7.3 logs a muted alert, for S-14. §9.2's state alerts use template triggers, and §7.1 adds the boot-time helper. §7.5 states that a selector added after the seed starts at `Off` |
