# Home Assistant integration — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour, for the HA layer that
[`LRAN-HA-Integration-Implementation-Plan`](../shared/LRAN-HA-Integration-Implementation-Plan.md)
plans: `ha/packages/`, `ha/dashboards/` and the sandbox they are built in. Where this
disagrees with a document, the document is the current statement and this is what was true
on the day.

Entries before this file existed are in the bridge's log, which covers discovery and the
sandbox broker's earlier history.

---

## 2026-10-25 — H0 and H1: the simnodes cleared from the sandbox, and the shared plumbing installed

**H0. A registry delete alone would not have held.** The sandbox registry had four
`simnode_*` devices with 48 unavailable entities, 12 each. The broker still held their 48
retained `homeassistant/{button,sensor}/lran_simnode<N>_*/config` configs, so HA would have
recreated the devices at its next restart. An empty retained publish to each config
removed the entities and then the devices. The same pass cleared 19 retained
`lran/simnode<N>/…` state topics left from bench sessions: `availability`, `config/state`,
`diag/state`, `vedirect/charge/state`, `vedirect/write_enable/state`, and one
`vedirect/hex/audit`. Afterwards the device registry had 17 devices and none named
simnode. The one entity matching `simnode` is the bridge's own
`switch.lora_bridge_simnode_diag_enable`, which was off throughout. Turning it on with a
simnode running republishes the devices, because spec §16.6 keeps bench entities by
design.

**H1. Installed** in the sandbox at `/config/packages/lran/` through the Samba mount,
with `homeassistant: packages: !include_dir_named packages/lran` added to its
`configuration.yaml`. The config check was valid, and the error log showed nothing from
the packages after the restart. The seed automation set every threshold to its default on
first start. HA was at 2026.10.4.

**Two defects in plan v0.1 were found while writing it, and v0.2 fixes both:**

- **§7.2's key, `ctx_id:event_id`, would have dropped every follow-up.** A follow-up reuses
  its first edge's `event_id` (spec §7.3). That is the defect spec v0.15 corrected in the
  bridge, which has keyed on the follow-up bit since BF-25. The filter's key now ends in
  `:f` or `:e`. The push tag leaves the suffix off, so a follow-up still replaces its first
  alert.
- **§7.4's net power read the MPPT's load output**, which reads 0 because the gate
  controller and GateLink are on the battery bus. Net power is now pack voltage × pack
  current, and the draw for the autonomy estimate is the MPPT's battery current less the
  pack's. The operator chose that formula.

**The acceptance run: 22 checks, all passed**, against the sandbox over the websocket API,
calling the scripts directly rather than publishing to an event topic:

- `lran_event_admit` admitted a new GateLink event and dropped its replay. It admitted the
  follow-up with the same tag and dropped the follow-up's replay. The same `event_id`
  under a new `ctx_id` was admitted, and so was the same key on another node.
- A `synthetic: true` event was dropped and its key not recorded. A payload with no
  `synthetic` key was admitted.
- A bridge event keyed on `boot` was admitted, its replay dropped, and the next boot's
  admitted. One with `boot: null` was admitted every time and never recorded.
- After 20 keys with full-width `uint32` fields, the list held seven keys in 230
  characters, newest first.
- `lran_notify`: no email at `high` with `lran_email_enabled` off, one with it on, none
  for a follow-up and none at `normal`. With `lran_alerts_muted` on, `normal` sent no push
  and `critical` still did. `low` sent no push.

**What this run does not show.** Email went to the sandbox `site.yaml`'s stand-in, a
persistent notification, because the sandbox has no SMTP account, so real SMTP is not
verified. Pushes were counted from `lran_site_push`'s traces and went to the Mac. No push
reached the phone, and the critical and time-sensitive options are not yet checked on a
device (H4). No event came through MQTT, because the sandbox broker is carrying GateLink's
live bench soak 2, and an injected `lran/gatelink/event/*` would reach the soak's
subscribers too.

**Live readings when the packages came up:** cell spread 2 mV, net power 0.0 W, draw
current 0 mA, link quality `good`, pack 13.31 V. The draw is 0 because the MPPT and the
BMS both read 0 mA with no sun on the bench, and the BMS counts in 0.1 A (spec §7.2.3). So
the autonomy estimate stays unavailable until the 7-day window covers a day with a
nonzero draw.

## 2026-10-25 — H2: the Gate dashboard

`ha/dashboards/gate.yaml` is installed in the sandbox as `/config/dashboards/lran/gate.yaml`.
`configuration.yaml` there carries only the `lran-gate` half of plan §6's `lovelace`
block. H3 adds `lran-detail` with its own file, because a dashboard entry naming a missing
file is an error.

**The acceptance run, at a 375 × 812 phone viewport:**

- The view rendered in one column, top to bottom in plan §8.1's order.
- With no state forced, only the FIRE banner showed. That is the bench's truth: IN5 is
  undriven and inverts, so `binary_sensor.gatelink_fire_input` reads `on` (gatelink
  engineering log, the IN5 and IN6 edge test).
- Forcing FIRE `off` and the vehicle-while-held-open, hard-shutdown latch,
  charging-inhibited and battery-protection binary sensors `on` showed those four banners
  and hid FIRE's.
- Forcing `sensor.gatelink_gate` to `unavailable` showed the GateLink-unavailable banner.
  Forcing `binary_sensor.gatelink_held_open` `on` at the same time showed the hold-source
  tile, which was absent before and after.
- After every state was restored, only FIRE's banner showed again.

**The states were forced through HA's REST API** (`POST /api/states/<entity>`), not the
broker. That changes what HA shows until the next MQTT update and reaches none of the bench
soak's subscribers. The forced states are in the sandbox recorder's history for the
minutes they held.

**HA rounded the rescaled voltages to whole volts.** It suggested a display precision of 0
for `sensor.lran_gatelink_pack_voltage_v` and `_pv_voltage_v`, because they carried
`device_class: voltage`, so 13.31 V showed as 13 V. Adding
`suggested_display_precision: 2` to the template sensors passed
`/api/config/core/check_config`, but the template reload rejected it ("Invalid config for
'template'"). Every template sensor in the package fell to `unavailable` until the file
was reverted and reloaded. The fix drops the device class. The two sensors have no
`state_class`, and the recorder excludes them, so the class bought only an icon, which is
now set directly. They read 13.31 V and 0.01 V on the dashboard afterwards.

**The template reload reset `sensor.lran_gatelink_draw` to `unknown`.** The integration
sensor restarts with the reload and resumes once the draw current next updates; its
`max_sub_interval` is 5 minutes. The draw was 0 mAh before the reload, so nothing was lost.

**Two plan changes went into v0.3.** The link chip is two tiles, because a tile cannot show
another entity's state, and an attribute copy of `last_seen` would write the recorder rows
plan §10 excludes `last_seen` to avoid. The button tiles hide their state, which is the
time of the last press.

## 2026-10-26 — The dev flow moves to ha-mcp

**The sandbox is now worked through ha-mcp and the Samba mount, not the REST API.** The
ha-mcp app (8.7.0) runs on the sandbox and is registered at user scope on the Mac as
`ha-sandbox`. Production will be set up the same way. Files still go over Samba, because
the repository is their source and plan §12 ports them as a copy. The handoff's *Where
things stand* lists the tool for each step.

**The first config check through ha-mcp read `valid`** with no errors, from
`ha_get_system_health(include="config_check")`. The sandbox was at core-2026.10.4, and
`/config/packages/lran/` held `core.yaml`, `site.yaml` and `templates.yaml`, with `gate.yaml`
in `/config/dashboards/lran/`. Nothing was changed on the sandbox.

**One REST call has no ha-mcp equivalent.** H2 forced its banner states with
`POST /api/states/<entity>` (the 2026-10-25 H2 entry). That is a REST endpoint, not a
service or a WebSocket command, and none of ha-mcp's 77 tools sets a state. The Mac has no
`~/.lran_ha_token`, so H3's and H4's forced-state checks need a token here or another
route. The handoff's *Open* records the choice.

**`ha_config_set_dashboard` writes storage-mode dashboards only**, so the LRAN dashboards
stay YAML files copied over Samba. Editing one through ha-mcp would leave the repository
and production behind.

## 2026-10-26 — H3, the LRAN dashboard

**HA cannot make a single view admin only, so Maintenance became a dashboard of its own.**
Plan §8.2 asked for a `require_admin` Maintenance view. `require_admin` belongs to a
dashboard's entry in the `lovelace` block. A view's only gate is `visible`, a list of user
IDs, and a card's `visibility` conditions have a `user` condition but none for "is an
admin". User IDs differ per install, so a list would break at the copy to production. The
operator chose a third YAML dashboard, `lran-maintenance`, with `require_admin: true`. The
other three views stay in `lran-detail`, so `/lran-detail/network` still resolves for the
Gate dashboard's link tiles. Plan v0.4 records the change.

**A counter keeps no long-term statistics.** `counter.lran_gatelink_traversals_today` has
no `state_class`, so a 30 d traversal chart could only read recorder history, which ends at
`purge_keep_days` (10 on the sandbox). `sensor.lran_gatelink_traversals` mirrors the counter
with `state_class: total_increasing`. The midnight reset reads as a new cycle, and a
statistics graph of daily `change` gives traversals per day.

**`logbook.log` with no `entity_id` does not reach an entity-filtered logbook card.** The
card takes `entities` or `target`, and the Events view lists entities. `script.lran_notify`
now logs each entry against itself, so listing that script brings in every LRAN alert.

**Coverage was checked against the live registry rather than plan §4's prose.** A template
listed 168 `sensor`, `binary_sensor`, `button`, `switch`, `number`, `select` and `counter`
entities matching `gatelink`, `lora_bridge`, `welllink` or `lran`, including 55 `number`
entities, which matches §4's count. Each one is on one of the three dashboards. Two stay off
the LRAN dashboards: the four command buttons, which pulse the relay and are on the Gate
dashboard behind confirmations, and the rescaled V sensors, which only the Gate dashboard
reads. `binary_sensor.gatelink_synthetic_data` and `sensor.gatelink_boot_count` are live
but not in §4. Both are on the dashboards: synthetic data on the Network view, boot count
as the Events view's boots per day.

**Every statistics graph has data to read, but the cards were not seen rendered.** A
statistics query through ha-mcp (`ha_get_history(source="statistics")`, 30 d, daily)
returned rows for each graphed entity: SOC 8 days, enclosure temperature 19, yield total 9,
BMS BLE RSSI 8, boot count 3, and the new traversals sensor 1, from the restart onward. HA
parsed all three dashboards as written (`ha_config_get_dashboard`). Rendering was not
checked: the browser pane stops at HA's sign-in page, and ha-mcp's dashboard screenshot is
a beta feature that is off on the sandbox. The operator's own look at the dashboards is
the render check.

**The sandbox's yield total carries two bench jumps**, 123.45 kWh and 118.78 kWh, each in a
single day. A 100 Ah pack at 13 V holds about 1.3 kWh, so neither is a real day's yield. They
show as two tall bars on the Daily yield chart until they age out of the 30 d window.
Production's statistics will not have them.

## 2026-10-26 — H3's render check, and ha-mcp's dashboard screenshots

**The operator confirmed H3's render**, viewing the three dashboards themselves. That closes
the one part of H3's acceptance the entry above left unchecked.

**ha-mcp can now screenshot a dashboard on the sandbox.** The operator turned on ha-mcp's
two beta flags and installed Puppet, the renderer ha-mcp drives. The flags are in the app's
web UI under *Server Settings*, not on its *Configuration* tab; ha-mcp's `docs/beta.md` has
the steps. Puppet is app `0f1cc410_puppet`, 2.6.0, from
`https://github.com/balloob/home-assistant-addons`, and holds a long-lived token of its own.
`ha_config_get_dashboard(url_path="lran-detail", view_path="events",
include_screenshot=True)` returned a 1280×800 render of the Events view in the same session,
with no reconnect of the MCP server. The logbook card showed the GateLink entities' entries.

**A render is one viewport, not the whole page.** The 1280×800 capture cut off below the
first row of statistics graphs. A check of a lower card needs a taller viewport or a narrower
view.

**Puppet's port 10000 takes no authentication**, so anyone on the sandbox's network can pull
a signed-in render through it (ha-mcp `docs/beta.md`). It is acceptable on the sandbox's
LAN. Whether production gets Puppet is a decision for H10.

## 2026-10-26 — H4, the Tier 1 alerts

**`alerts.yaml` holds the six Tier 1 automations and two connectivity sensors**, installed
on the sandbox with a clean config check. The four event alerts, FIRE's critical push on
the phone and V-5 pass. **The two offline alerts were not exercised**: the operator could
not power the boards down this session.

**The events were injected with `mqtt.publish` through ha-mcp**, on the real
`lran/gatelink/event/<name>` topics, not retained, QoS 1, with `ctx_id` 4294967295 and
`event_id` 9001 to 9005, so the GateLink soak's log can tell them from real events. The
operator agreed to that before the first publish. Each payload had the fields
`publish.cpp`'s `on_event()` writes.

| Injected | Result |
|---|---|
| Held-open, first edge, `event_id` 9002 | Admitted. Time-sensitive push, tag `gatelink:4294967295:9002`. One email through the stand-in, with `lran_email_enabled` on for the run |
| Its follow-up, `direction` `entry` | Admitted on key `…:9002:f`. Push with the same tag, "A vehicle came in…". No email |
| The first edge again | Dropped, `reason: repeat` |
| FIRE, `event_id` 9003 | Critical push to the Mac and the phone (`critical: 1`, `channel: alarm_stream`). **The operator saw it on the phone as a critical alert** |
| Hard shutdown, 9004; BMS alarm, 9005 with `detail` 18 | High pushes. The BMS message reads `detail 0x0012` |

**V-5 holds across a restart.** HA restarted at 10:52:31. Afterwards neither
`automation.lran_1_vehicle_while_held_open` nor `automation.lran_1_fire_asserted` had a
`last_triggered` later than its injection, and `input_text.lran_recent_events` came back
with all six keys, so a retransmission arriving after a restart is still dropped.

**A more-indented line in a folded scalar keeps its newlines.** The first held-open
message had a line break in the middle of the text, because the continuation of a Jinja
expression was indented further than the lines around it. The fix moves the expression into
a variable. A continuation line inside `{{ }}` is harmless only when the text after it is on
the same line.

**The offline alerts read a new MQTT binary sensor each, not the entities' availability.**
`binary_sensor.lran_gatelink_online` reads `lran/gatelink/availability` and takes the
bridge's availability as its own, so a bridge outage leaves it unavailable rather than
off. That keeps one outage from alerting twice. `binary_sensor.lran_bridge_online` reads
the bridge's LWT topic.

**`lran_notify` gained `push_only`**, for the bridge-offline alert, which plan §9.1 says
must not email. Passing `follow_up` would have done it, but it would have said the wrong
thing about the alert.

**Spec §8.9 gives BMS_ALARM's `detail` no meaning**, so the alert shows the raw value in
hex. The bridge passes it through unnamed.

## 2026-10-26 — Alert levels and the message catalog

**The operator asked for two changes before H4b**: each alert switched on or off and set
critical or not from the dashboard, and the text of every push and email in one file. They
chose one selector per alert, `Off`, `Normal`, `High` or `Critical`; a YAML catalog; and
for `Off`, a logbook entry with no push (plan §7.5, v0.6).

**`ha/packages/lran/messages.yml` holds the text, and `alerts.yaml` now holds none.** Each
automation passes its alert's name and the event's raw fields to `lran_notify`, which reads
the catalog and the selector. The file is `.yml` because HA loads every `*.yaml` under
`packages/lran/` as a package, recursively; `!include` does not care about the extension.

**A bare `Off` in YAML is the boolean false.** The selector options and any `level: "Off"`
in the catalog need the quotes. `tools/checks/ha_messages.py` fails on an unquoted one,
and on an alert with no entry, an entry with a level and no selector, and a placeholder
its automation does not pass.

**The levels seed through a flag of their own**, `lran_alert_levels_seeded`, because the
sandbox's `lran_defaults_seeded` was already on and its seed would never run again.
After the restart at about 16:56, all six selectors read their catalog levels.

**Verified on the sandbox by calling `script.lran_notify` directly**, with no MQTT
publish, so the GateLink soak saw nothing:

| Call | Result |
|---|---|
| `vehicle_while_held` at `Off`, `direction` `undetermined` | No push. Logbook: "Vehicle at the gate while held open (alert off, not pushed): A vehicle passed while the gate was held open (hold source: manual)." |
| `bms_alarm` at `Normal`, `detail` `0x0012` | One ordinary push through `lran_site_push`; no email |
| An alert with no entry, `severity` `low` | Logged under its bare name; nothing withheld, nothing pushed |
| `fire` at `Off` | Finished with no push |
| *Restore default levels* | Every selector back at its catalog level |

The Alerts card's warning renders "Hard shutdown sends no push or email. The logbook still
records it." with that alert at `Off`, checked through the template engine. **The card was
not seen rendered**: ha-mcp's screenshot of `lran-maintenance` returned HA's home page,
which is what a non-admin user gets for an admin-only dashboard. Puppet's user is
presumably not an admin. **The automations were not driven end to end** since the change,
because that needs an MQTT publish the soak would see. `ha_messages.py` checks the fields
each one passes; H4b's offline run drives two of them for real.

**Optional script fields need `| default`.** HA logs a template warning for every
undefined variable, so `actions`, unset on every call, had been logging one per alert since
H1. `lran_notify` now defaults each optional field once, in its variables.

**Later the same day the operator opened the Alerts card and confirmed it renders.**

## 2026-10-27 — H5, the Tier 2 health alerts

**The seven §9.2 automations are installed in `alerts.yaml`, with nine selectors and their
`messages.yml` entries.** H4b was skipped, because the GateLink soak is still running.

**Two of the operator's choices shaped them** (plan v0.7). §9.2 puts the boot alert at
`low`, which no selector offered, so every selector gains `Low`: a logbook entry and
nothing sent. And an alert with two levels becomes two alerts, because a selector holds
one: `soc_low` and `soc_alarm`, `node_boot` and `node_boot_loop`. The operator also chose
a test-only package as the way to force a state. H5 did not need one, because moving a
threshold past the live reading fires each state alert, so none was written. A banner
check still needs it.

**`numeric_state` does not watch a threshold helper.** The first SOC test raised
`lran_soc_warn_pct` to 100 against a reading of 99, and nothing fired: the trigger listens
to its own entity, and SOC had not moved. The SOC and cell-spread alerts now use template
triggers, which watch every entity they read, as the enclosure alert's already did for the
°F conversion. A threshold changed in the UI now takes effect at once. Before the change
it would have waited for the sensor's next move.

**A queued warning could replace the alarm.** Both SOC triggers share the tag
`gatelink:soc`, and the automation runs `queued`. In the second test the warning ran
after the alarm and its push replaced the alarm's. A reading that comes back from
unavailable below both thresholds fires both triggers in no fixed order, so the warning
now stands down while the alarm holds. Retested: alarm at 100, then warning at 100; the
warning's run ended at that condition, and only the alarm was sent.

**`lran_notify` now logs a muted alert**, marked `(alerts muted, not pushed)`. Before,
`lran_alerts_muted` stopped the script before its logbook entry. S-14 asks for every
enclosure excursion to be recorded, and muting is for bench work on the phone, not for the
record. Plan §7.3 changes with it.

**The nine new selectors started at `Off`**, because `lran_alert_levels_seeded` was
already on. The six existing ones were at their defaults, so *Restore default levels* was
run, and all fifteen read their catalog levels, `Low` included. Plan §7.5 now states the
gap; the Alerts card's warning would have named all nine.

**Verified on the sandbox.** The state alerts were driven by thresholds alone. The event
alerts were driven by events published through ha-mcp's `mqtt.publish`, non-retained, with
`ctx_id` 4294967295 and the bridge's `boot` 4294967295, agreed with the operator
beforehand:

| Alert | Driven by | Result in the logbook |
|---|---|---|
| `soc_low` | `lran_soc_warn_pct` 40 → 100, SOC 99 | "GateLink battery low: The GateLink battery is at 99 %, below the 100 % warning." Pushed, no email |
| `soc_alarm` | `lran_soc_alarm_pct` 25 → 100 | "GateLink battery very low: … below the 100 % alarm. …" |
| `soc_low` muted | `lran_alerts_muted` on, warning 40 → 100 | "GateLink battery low (alerts muted, not pushed): …" |
| `charge_inhibited` | event 9001, `inhibit_path` `bms` | "… inhibited, decided by the BMS." |
| `mppt_error` | event 9002 | "… reports an error: no error name reported." The live sensor read `no_error`, which the alert treats as no name |
| `phy_reverted`, node | `lran/gatelink/event/phy_reverted`, event 9003 | "… reported by GateLink: the node went back to its last known-good settings. Node named: none." |
| `phy_reverted`, bridge | `lran/bridge/event/phy_reverted`, `reason` `not_heard`, `node` `gatelink` | "… reported by the bridge: a node was not heard on the new settings. Node named: gatelink." |
| `node_boot` ×2 | events 9005 and 9006, `power_on` then `watchdog` | Logged at `Low`, nothing pushed |
| `node_boot_loop` | event 9007, `brownout`, the third within the hour | "GateLink has restarted 3 times in the last hour. The last: a brownout." |
| `enclosure_hot` | `lran_enclosure_temp_warn_c` 40 → 20 at 13:51 UTC, reading about 27.7 °C | Fired at 14:06:03, 15 minutes on: "The GateLink enclosure has been at 82.04°F for 15 minutes, above the 20 °C warning." |
| `cell_spread` | `lran_cell_spread_warn_mv` 50 → 0 at 13:51 UTC, spread 5 mV | Fired at 14:21:03, 30 minutes on: "The GateLink battery's cells have differed by 5 mV for 30 minutes, above the 0 mV warning." |

Two wording faults showed in that run and were fixed in the catalog: the boot message
repeated its title, and the culprit read as the raw token `gatelink`. The three fake boot
times were cleared from `input_text.lran_gatelink_boot_times` afterwards, so the real
GateLink's next boot is not counted as a fourth.

Every threshold was set back to its default afterwards, `lran_alerts_muted` is off, and
the boot list is empty. **Not verified**: a push on the phone for any Tier 2 alert, since
the sandbox's `site.yaml` pushes to the Mac; real SMTP; and an alert from a real GateLink
event, since none of these events occurred during the session.

**A gap left open by the operator.** A template trigger fires only when its condition goes
from false to true. A state alert that is already past its threshold when HA starts or
reloads its automations therefore stays silent until the condition clears and recurs.
That covers the SOC, enclosure and cell-spread alerts. The handoff's *Open* carries it.

## 2026-10-28 — H4b, the offline alerts, from the soak's shutdown

**Both offline alerts fired on real outages, at their delays, and the operator saw both
pushes on the phone and the Mac.** Nothing was injected. The bench soak's shutdown on
2026-10-27 was the test, with `lran_offline_warn_min` left at 10, not set to 1. Times are
EDT, from the recorder, the logbook and the automation traces, read through ha-mcp on
2026-10-28.

| Time | `binary_sensor` | Alert |
|---|---|---|
| 13:11:41 | `lran_gatelink_online` off | — |
| 13:21:41 | | `gatelink_offline`, "…not heard GateLink for 10 minutes…" |
| 13:26:18 | `lran_gatelink_online` on | Its return push, "GateLink is back after 15 minutes offline." |
| 14:49:41 | `lran_gatelink_online` off | — |
| 14:59:41 | | `gatelink_offline` again |
| 15:24:54 | `lran_bridge_online` off; `lran_gatelink_online` unavailable | — |
| 15:29:54 | | `bridge_offline`, push only |
| 17:22, and 07:55 on 2026-10-28 | HA restarted; `lran_bridge_online` unavailable → unknown → off | — |
| 17:27:41, and 08:00:08 | | `bridge_offline` again, once after each restart |

**The two pushes the operator saw are two outages, not one alert sent twice.** GateLink
went down 35 minutes before the bridge did, so its alert had already gone out. When the
bridge dropped, `lran_gatelink_online` went unavailable, not off, and GateLink did not
alert a second time. That is the double-alert guard of 2026-10-26, H4, working.

**The bridge alert sent no email.** `lran_notify`'s email step read `push_only` true and was
skipped (trace `084a45b0…`). GateLink's alert skipped its email for a different reason:
`input_boolean.lran_email_enabled` is off on the sandbox, so its email path is still
unproven (*Open*, real SMTP).

**The return push names the outage.** The 13:26 push replaced the 13:21 push through the tag
`gatelink:offline`. The 13:11 outage looks like the GateLink reflash that preceded the
14:39 boot-loop alert (five restarts, reset pin or USB). Nothing here confirms it.

**A restart re-alerts the bridge outage, as `alerts.yaml` intends.** Each restart started the
sensor unknown, and the alert fired five minutes later. Neither restart is known to have
come from this work.

**The GateLink return after a bridge outage was not seen, and the code says it will not be
sent.** Both boards were still off at 09:20 on 2026-10-28. The retained
`lran/gatelink/availability` still reads `offline` from 14:49, so when the bridge comes back
the sensor goes unavailable → off. GateLink's next frame then takes it off → on, but
`gone_min` counts from that off, which lasted seconds, so the return push is skipped. The
14:59 push stays on the phone, unreplaced, and the 14:49 outage is never reported over.
Plan §9.1 promises "a second push when it returns, with how long it was gone". This
outage would get no second push. An HA restart breaks the count the same way.

**Fixed, by the operator's choice: the offline alert records when the outage began.** The
alert writes the sensor's `off` time to `input_text.lran_gatelink_offline_since`, unless a
start is already recorded, so a re-alert after a restart keeps the first. The return push
now fires on `on` from any state, only while a start is recorded, counts from it, and
clears it (plan v0.8, §9.1). Installed at 09:46 with a clean config check, and no LRAN
entry in the system log after the reload.

**The pending outage was seeded by hand**, with 1793126981, the 14:49:41 `off`, because
the alert that recorded it ran before the helper existed. HA rendered the return text as
"18 h 57 min" at 09:47. **The return push itself has not been seen.** It needs both boards
powered up. Expected: one `gatelink_online` push, replacing the 14:59 push, naming the
outage from 14:49, and the helper empty afterwards.

**One logbook gap, not chased.** The 08:00:08 run of `lran_notify` reached its
`logbook.log` step in the trace, but ha-mcp's logbook read does not return that entry. The
17:27 entry and the earlier ones are there.

## 2026-10-28 — H9, the entity check, and the one ID the sandbox lacks

**`tools/ha/check_entities.py` asks HA through ha-mcp, not the REST API.** The REST route
needs `~/.lran_ha_token`, which the Mac does not have. The dev flow already sends every
question to HA through ha-mcp (handoff, *The dev flow*). The script speaks MCP over HTTP to
the server named in `~/.claude.json` and renders one template, the list of every entity
ID. The server's address carries its secret path, so the script reads it and never prints
it. `--server ha-prod` points it at production when H10 registers that server.

**Telling an entity from a service is the hard part of reading the YAML.** `button.press`
and `input_number.set_value` match the same pattern as `button.gatelink_close`. The script
skips a match that also appears under `action:`, `service:` or `perform_action:` in the
same file. The first run flagged `button.press` because the Gate dashboard names it under
`perform_action:`, which the first draft did not read. A `script.*` stays in, because a
script's service is also its entity. `'input_select.lran_alert_' ~ alert` leaves a prefix
ending in `_`, and the check passes a prefix when at least one live entity starts with it.

**Against the sandbox at 13:55 UTC: 199 of the 200 entity IDs are present, and the prefix
matches.** The missing one is `number.gatelink_bms_retry_s`, on the Maintenance dashboard.
`ha/discovery/number-lran_gatelink_bms_retry_s.json` has it, and a1de65d added it on
2026-10-26. The sandbox has `bms_poll_s` and `bms_window_max_ms` but not this one. A
discovery config is retained, so a bridge that had ever run a1de65d or later would have
left the entity behind. **The sandbox's bridge has therefore not run firmware from
a1de65d on.** The YAML is right. The bridge has been off since 11:55 UTC today, and its
firmware version reads `unavailable`. The check passes once a bridge built from current
`main` connects.

**The offline half runs in CI.** `--offline` parses every package and dashboard and lists
the IDs. A YAML file that does not parse fails the checks job, before it reaches the
sandbox.

## 2026-10-28 — H6, the Tier 3 reminders and the Close gate handler: built, not verified

**`reminders.yaml` is installed on the sandbox, and V-8 has not run.** The config check
read valid, and the selectors, scripts and automations reloaded with no LRAN entry in the
system log. The three new selectors came up `Off`, as the handoff's trap says, and were set
to `Normal` after the reload. No reminder has been pushed and no Close gate tapped. The
bridge needs a flash from `main` at the bench first (H9's entry above), then relay dry-run
on, before the tap means anything.

**Three choices the plan did not state, now in §9.3 (v0.10).**

- "Not closed" is `moving`, `open_countdown` or `open_held`, the gate-state strings in
  `publish.cpp`. `fault` and `unknown` are left to the Gate dashboard's banners.
- The nobody-home reminder counts only tracked persons. The sandbox has `person.jeff` and
  `person.claude_user`, both `unknown`, so a rule of "no person at home" would have read
  the sandbox as empty all day. With the rule as built, the trigger renders false there.
- The handler skips a gate that already reads closed and logs the tap. Its action carries
  iOS's `authenticationRequired: true`, so the phone must be unlocked to run it. Android
  ignores the key. **Not yet seen on the phone.**

**The sandbox's sun looks wrong for this site.** At 14:05 UTC, `sun.sun` gave the next
sunset as 16:18 UTC, 12:18 in New York, the install's time zone. The home location is
probably not set to the property, so `lran_3_open_at_night` would fire at the wrong hour
there. Production's location needs the same check before H10.

**The V-8 run, when the bench is ready.** Confirm `binary_sensor.gatelink_relay_dry_run`
reads `on`. Send one reminder through `script.lran_notify` (`alert: held_open_long`, the
Close gate action), with no broker involved. Tap Close gate on the phone. Expected: a
`lran_3_close_action` trace that presses `button.gatelink_close`, and GateLink reporting
the close as `DRY_RUN` with no relay energized. Agree the time with the operator, because
the press reaches the sandbox broker and the soak.

## 2026-10-28 — H4b's return push ran, and H9's diagnosis holds

**The boards came up at 14:13 UTC, and the fixed GateLink return path ran.**
`binary_sensor.lran_bridge_online` went `on` at 14:13:30 and
`binary_sensor.lran_gatelink_online` at 14:14:03. The trace of
`automation.lran_1_gatelink_offline` for that run took the return branch. It cleared
`input_text.lran_gatelink_offline_since`, then called `lran_notify` with
`msg: gatelink_online`, `gone: 19 h 24 min`, the tag `gatelink:offline` and
`follow_up: true`. 19 h 24 min counts from the recorded start, 18:49:41 UTC on
2026-10-27, which is 14:49:41 in New York. **Whether the push replaced the 14:59 push on
the phone was not checked.** The operator was away from the bench.

**The bridge runs c9d3667, from 2026-10-15**, by `sensor.lora_bridge_firmware_commit`.
`git merge-base --is-ancestor a1de65d c9d3667` says a1de65d is not in it. That confirms
H9's reading: `number.gatelink_bms_retry_s` is missing because the firmware predates it.
Relay dry-run reads `off`, and the gate `closed`.

## 2026-10-28 — Bench checks: H9 passes, and the reminders lose their Close gate action

**The bridge was flashed over USB from `main` at 760e9a3**, built on the macOS bench
machine from a clean tree. `sensor.lora_bridge_firmware_commit` read `760e9a3` once it
came back. The bench soak had ended the day before, so nothing else was listening on the
broker.

**H9 passes.** `python3 tools/ha/check_entities.py` printed `ok: 203 entity IDs and 1
prefixes, all in ha-sandbox (264 entities)`. `number.gatelink_bms_retry_s` appeared, and
read 5, once GateLink was heard again.

**GateLink was not heard after the flash until the operator restarted it.** Its last
readings reached HA at 20:43:20–20:43:39 UTC, before the flash. The bridge rebooted
three times in the next few minutes: once after the upload, and twice more when a capture
opened its USB port. A 150 s console capture from one of those boots showed no frame from
GateLink, and HA received no GateLink reading until the operator restarted it, at about
20:52. A new enclosure temperature arrived at 20:52:40. The same silence followed a bridge
power-up on 2026-10-26, in the GateLink handoff. Whether GateLink was waiting on a context roll it never
heard is still not established.

**Relay dry-run went on through HA.** `mqtt.publish` to
`lran/gatelink/cmd/set_relay_dry_run/set` with payload `1`, not retained, at 20:53:50.
`binary_sensor.gatelink_relay_dry_run` read `on` at 20:54:50. GateLink's `cmd/ack` was not
read: the sandbox broker refused `mosquitto_sub` without credentials, and the session did
not take them from `secrets.h`.

**V-8, as H6 built it, found a gap.** Three reminders went out through
`script.lran_notify` (`alert: held_open_long`, the Close gate action) at 20:54:57, 21:02:48
and 21:07:01. The `lran_site_push` trace shows both phones' notify calls carrying
`actions: [{action: LRAN_CLOSE_GATE, title: Close gate, authenticationRequired: true}]`.

- **The first two taps never reached HA.** The operator tapped the push itself, which only
  opens or dismisses it on iOS. The action shows only on a long-press, after "loading
  actions". The press of `button.gatelink_close` at 20:56:59 came from the Gate
  dashboard's Close, which asks for confirmation, under the operator's user. It went out
  with dry-run on.
- **A synthetic `mobile_app_notification_action` event, `action: LRAN_CLOSE_GATE`, fired
  through the REST API at 21:05:17, triggered the handler.** It logged "Close gate tapped,
  but the gate already reads closed. No command sent."
- **The long-pressed action ran from a locked iPhone with no visible unlock prompt.** The
  handler triggered at 21:07:14 and took the same closed branch. The companion docs say an
  action with `authenticationRequired: true` needs the device unlocked. Face ID probably
  unlocked the phone silently: on the plain reminder below, a tap with the phone facing
  the operator unlocked it with no prompt, and a tap facing away did nothing. The action was
  not tried facing away, so whether `authenticationRequired` holds is not established.
- **The press branch could not run at all with dry-run on.** IN1 and IN2 are tied to their
  off positions, so `sensor.gatelink_gate` reads `closed` and the handler stops before
  `button.press`. The handoff's recipe expected a press.

**By operator choice, the notification actions are gone** (plan v0.11, §3, §9.3, §9.5). No
requirement asked for them: GateLink PRD S-8 forbids an automatic close, and v0.10 met it
with an action a person taps. A reminder is now a plain push whose tap opens the Gate
dashboard, `/lran-gate`, where Close asks for confirmation. `lran_notify` takes `url` in
place of `actions`, sent as iOS's `url` and Android's `clickAction`. `lran_3_close_action`
is removed. H8's approach prompt loses its Open gate action for the same reason. The
operator's reason is the long-press: an action that shows only after one is easy to miss,
and it adds little over opening the dashboard.

**The plain reminder works, behind the phone's lock.** Reminders went out at 21:20:32 and
21:23:47. The trace shows the iPhone's push carrying `tag`, `url: /lran-gate` and
`clickAction: /lran-gate`, and no actions. A tap opened the Gate dashboard. With the phone
locked and facing away, a tap neither opened it nor asked for an unlock. Facing the
operator, Face ID unlocked it silently, and the companion app asked whether to open the
notification's URL, with *Always*, *Open* and *Cancel*. *Always* stops it asking.

**What V-8 still owes for H6:** GateLink's `DRY_RUN` answer to a dashboard Close, read from
`cmd/ack` or the bridge console.

## 2026-10-28 — H6 is met: GateLink answers `DRY_RUN` to the dashboard's Close

**V-8 passes.** The operator pressed Close on the Gate dashboard and confirmed it, with
relay dry-run still on from the earlier run. `tools/bench/bench.py run --mqtt` logged `lran/#`
with the broker login it reads from `secrets.h`. No serial port was open, so the bridge
did not reboot.

```text
21:40:46 UTC  lran/gatelink/cmd/close/set  PRESS
21:40:48 UTC  lran/gatelink/cmd/ack        {"outcome":"acked","seq":3,"attempts":1,"result":16,"detail":0}
```

`result` 16 is 0x10, `DRY_RUN` (spec §8.2, `result` in `COMMAND_ACK`). The bridge sent the
command once, and `cmd_submitted`, `cmd_sent` and `cmd_acked` each went from 2 to 3. The
radio log shows the command out as `seq` 3 and GateLink's ACK back at −48 dBm about 0.6 s
later. `lran/gatelink/gate/state` did not change, and GateLink published no event.

**The first attempt timed out on a press that never happened.** The 5-minute wait ended
before the operator pressed Close, and `button.gatelink_close` still showed the 20:56:59
press. The press reached the broker 65 s after the wait ended, and the harness was still
logging it.

**H4b's return push replaced the offline push on the phone**, by the operator's report:
the *GateLink back online* push of 14:14 UTC stood in place of the 14:59 offline push.

Relay dry-run is still on. It stays on until the operator asks for it off.

## 2026-10-28 — H7, the Tier 4 reports: built and met on the sandbox

**Relay dry-run went off first.** `mqtt.publish` to
`lran/gatelink/cmd/set_relay_dry_run/set` with payload `0`, not retained, at about 21:42
UTC, by operator request. `binary_sensor.gatelink_relay_dry_run` read `off` after the HA
restart at 21:50.

**HA 2026.10 has `recorder.get_statistics`, and it removes most of the sensors H7 would
have needed.** It returns long-term statistics as a response variable: a measurement
sensor's `mean`, `min` and `max` per period, and a total's `change`. The morning report
reads hourly rows for 24 h, and the weekly report reads fourteen daily rows. The rows'
`start` is in UTC and `today_at()` is local, so the first draft's split of the week, which
compared the ISO strings, would have been wrong across the offset. It compares timestamps
instead. The MPPT charge state is text, has no statistics, and needs four `history_stats`
sensors.

**Two operator choices** (plan v0.12): yesterday's yield comes from an MQTT sensor in
`reports.yaml`, because discovery makes no entity for `yield_yesterday_kwh` (plan §5, F4).
"Any event in the last 24 h" is every alert since the last report, which `lran_notify`
now notes through a queued script.

**Both reports ran at 21:53 UTC**, triggered by hand, after the new selectors were set to
`Normal`. They start at `Off`, as *Traps* in the handoff says. The weekly report read:

```text
Battery 99 % to 100 %. Cycles added 12. Worst cell spread 163 mV. BMS link RSSI -72 dBm on
average, +3 dB on the week before. MPPT time in bulk 14.8 h, absorption 0.0 h, float 0.0 h,
off 56.4 h.
```

The "week before" is 2026-10-14 and 10-15, from an earlier soak, against 2026-10-24 to
10-27. The bench values are not a battery's: the 163 mV spread and the 12 cycles are bench
artifacts, not measurements of a pack in service.

**The morning report read SOC and yesterday's yield as `not reported`**, because GateLink's
`battery/state` and `solar/state` carried `available: false` on the bench. The operator
chose to record that rather than bring the BMS and MPPT up. Its other figures were real:
enclosure 67.3 °F to 82.6 °F, and 37 missed polls in a row, from today's bridge reflash.
The first wording read "Battery not reported, not reported in 24 h", so the change's
missing text became its own: "change over 24 h not known".

**The alert note works.** Three `lran_notify` calls at severity `low`, two titled "H7 test A"
and one "H7 test B", left the count at 3 and the list at "H7 test A; H7 test B". The next
morning report printed `3 (H7 test A; H7 test B)` and cleared both.

**The operator confirmed both pushes reached the phone, and a tap opened the Gate dashboard.**

**A bench traversal counted once.** At 21:59:28 UTC GateLink's `vehicle_detected` event
moved `counter.lran_gatelink_traversals_today` to 1. Its follow-up at 21:59:42, with
`follow_up: true` and direction `undetermined`, was admitted by the filter as
`gatelink:2702468059:2:f` and not counted.

**`ha_config_set_automation(run_actions=True)` is refused** while ha-mcp's strict
best-practices mode is on, until the skill's acknowledgment key is passed.
`automation.trigger` through `ha_call_service` runs an automation without it.

## 2026-10-30 — Fluvy chosen for production; H11's decisions, before any install

**The operator wants production HA to run Fluvy** (`acosta290/fluvy`), its theme and its
cards, and the LRAN dashboards moved to them. Plan v0.15 records the choices in §2 and
adds task H11.

**Fluvy is a HACS integration**, not a theme. One install brings the theme, 63 cards, a
settings panel and its own dashboard resource. It needs HA 2026.9 and HACS 2.0. The latest
release on 2026-10-30 was v1.11.3, and three releases came out on 2026-10-28.

**The sandbox needs no `configuration.yaml` edit for it.** `frontend: themes:
!include_dir_merge_named themes` is already there. Lovelace resources are in storage
mode, because `configuration.yaml` names dashboards only, so Fluvy writes its own resource.
Its resource URL carries the build, `/fluvy-frontend/<version>-<build>/`, so a YAML
`resources:` block would need a hand edit after every update. The sandbox runs HA
2026.10.4, and HACS lists Fluvy as `1390720234`.

**The operator's choices**, asked before any install:

- Fluvy's cards throughout, for one look, superseding plan §2's *core cards only* of
  2026-10-25.
- Pinned at v1.11.3.
- Applied everywhere, HA's own pages included.
- The dashboards stay YAML-mode files in the repository.
- No confirmation on gate commands. Fluvy's cover card cannot ask one, and the gate's own
  controls never have.

**Nothing is installed or changed on the sandbox yet.** The session stopped here at the
operator's request, to resume H11 from the handoff.

## 2026-10-30 — H11: Fluvy installed on the sandbox, and the three dashboards on its cards

**Fluvy v1.11.3 is installed through HACS**, with its integration added and HA restarted.
It registered its own resource, `/fluvy-frontend/1.11.3-2cfe827a/fluvy.js`, and wrote the
theme `Fluvy`, now the backend default for light and dark.

**Fluvy's *Scope* defaults to `dashboards`, not `everywhere`**, and in that scope it styles
only dashboards whose path starts `fluvy-`. Plan §2 asks for everywhere, so the house
setting was written through HA's `frontend/set_system_data`, key `fluvy`, value
`{"version": 3, "scope": "everywhere"}`. Fluvy parses that record field by field, so a
partial one is safe. On production the settings panel's *Scope* tab does the same.

**Every card rendered as an error placeholder until Puppet restarted.** The engine kept a
browser from before HA's restart, with the old frontend loaded. Restarting the
`0f1cc410_puppet` app fixed it, and a Fluvy update will need the same.

**What the renders showed**, each fixed in the YAML before the next render:

- A Fluvy heading takes one grid cell, where HA's takes the row. In the three-column
  sections it pushed the charts sideways, so each heading has `columns: full`.
- Fluvy's tile defaults to its large size. The tiles are `size: compact`.
- Fluvy's chart card reads at most 90 days: the 1-year graphs drew 90. Fluvy's source
  states the limit (`LONGEST` in `packages/cards/src/chart/data/span.ts`), so the two
  1-year min, mean and max graphs stay HA's `statistics-graph`.
- Fluvy's entities card shows a `number` as a value, not a control. Settings are helpers
  cards, which draw a stepper per number, chips per select and a switch per boolean.
- Fluvy capitalises a text state: the bridge commit `addfd6b` reads `Addfd6b`, and the OTA
  slot `app0` reads `App0`. Left as it is, and listed in plan §8.3.

**An alert matcher matches `unavailable` only when it names it.** Fluvy's `match.ts`
says so, and a render confirmed it. A test alert on `sensor.gatelink_battery`, which is
unavailable on the bench, showed as a row with its entity and age. That is how the Gate
dashboard's *GateLink unavailable* alert reads `sensor.gatelink_gate`, and why the binary
sensors' alerts stay quiet while GateLink is gone.

**`lran-maintenance` was rendered as a copy.** ha-mcp's Puppet user is not an admin, so
the config went to a temporary non-admin storage dashboard, `lran-h11-render`, which was
screenshotted and then deleted.

**The checks.** `check_entities.py` passes against the sandbox, 221 IDs and one prefix,
and each dashboard names the same entity IDs as before the port. Every view rendered.
The Gate view was also rendered at phone width.

**Not checked:** whether the chart card puts series in different units on one scale. The
24 h and 7 d power charts mix %, mV, mA, W and °F, and the bench has no BMS or MPPT
readings. Nor are HA's own pages checked under Fluvy's theme: Puppet renders dashboards
only.

## 2026-10-30 — The firmware rows go back to HA's own entities card

**The operator chose HA's `entities` card for the Network view's firmware rows**, after
H11 found that Fluvy capitalises a text state. Under Fluvy's theme the HA card shows the
commit as `addfd6b` and the OTA slot as `app0`, checked in a render. Plan §8.3 records the
choice.

## 2026-10-30 — Open and close as named tiles; a review of the Fluvy port

**Fluvy's cover card cannot label its buttons.** Its source fixes the open button's glyph
as an up arrow and the close button's as a down arrow (`cover-card.ts`). The words reach
only `aria-label` and `title` (`actions()` in `packages/ui/src/parts.ts`), and no card
option changes either. A tooltip never shows on a phone.

**A Fluvy scenes card takes any entity once a tile names its own `tap_action`.** Its
singular sibling, the scene card, throws on a domain other than scene, script, button,
input-button or automation. Three layouts were rendered on a temporary storage dashboard,
`lran-gate-buttons-test`, then deleted. The operator chose the scenes card: a Fluvy tile
for the cover's state, then *Open gate* and *Close gate* tiles calling
`cover.open_cover` and `cover.close_cover`. Plan v0.17 records it in §8.1 and §8.3.

**The Gate view rendered at the mobile preset, 390×844**, with both tiles labelled. At
that width each tile drops its subtitle and keeps its name.

**Not checked: a press.** GateLink's relay dry-run is off by operator request, so a press
would move the gate. H6's check, a command with dry-run on and `cmd/ack` reading `result`
16, has not been repeated on the new tiles. A scenes tile also does not grey out at the end
of travel, as the cover card's buttons did.

**The review** is
[`briefs/2026-10-30-fluvy-port-review.md`](./briefs/2026-10-30-fluvy-port-review.md). It
found Fluvy's `battery` gauge colours a warning below 40 % and danger below 20 %, where
LRAN's SOC alarm defaults to 25 %. Four of the Gate dashboard's six alerts can be snoozed
by the viewer. Neither is changed yet.

## 2026-11-01 — The dashboard rework, and crossings the counter never saw

**The operator asked for four dashboards in place of three**: Gate for operation only,
Vehicles with a log of the day's gate crossings, and LRAN Status and LRAN Configuration
both admin only. Plan v0.18 and H12 record it. The soak was running, and the operator
approved HA restarts, injected events and dry-run commands during it.

**A crossing while the gate was held was never counted.** `detect.cpp` sends
`VEHICLE_WHILE_HELD_OPEN` in place of `VEHICLE_DETECTED`, and `lran_4_count_traversal` read
only the second. The daily count, the 30-day chart and the morning report all ran low by
every such crossing. `lran_1_vehicle_while_held` now counts them, inside its own run,
because the event filter admits an event once.

**The first event's direction was thought to arrive late. The operator pointed out that
only the confirmation does.** The first event says `undetermined`, but its `input_bits`
show which sensor fired, and spec 8.6 ties that sensor to the direction. The log writes
*entering* or *leaving* at once. The follow-up confirms it, or flags a vehicle that tripped
one sensor and backed away, and only a confirmed crossing counts. GateLink's firmware did
not change.

**The log passed every case with injected events**, `ctx_id` 4294967295, not retained,
from 8:47 PM EST:

| Case | Entry |
|---|---|
| SAFETY first, then a follow-up `entry` | *Vehicle entered at 8:47 PM* |
| The same first event again | No change |
| EXIT first, then a follow-up `exit` | *Vehicle left at 8:47 PM* |
| One sensor, follow-up with no direction | *Vehicle started entering at 8:47 PM but did not cross*, and the same for leaving |
| Both sensors at once | *Vehicle crossed at 8:47 PM, direction unknown* |
| `vehicle_while_held_open`, then its follow-up | *Vehicle left at 8:47 PM, gate held open*, in red |
| `vehicle_detected` with `gate_state` `open_held` | *Vehicle entered at 8:47 PM, gate held open*, in red |
| `vehicle_detected`, then a held-open alert with a new `event_id`, then its follow-up | One entry, in red |
| `synthetic: true` | No entry |
| A follow-up with no first event | *Vehicle left at 8:48 PM* |
| A first event with no follow-up | *Vehicle leaving at 8:48 PM* |

Ten entries and a count of 7 came out, as expected, and both held across an HA restart.
The injected held-open events also raised the Tier 1 alert. The log and the counter were
then cleared.

**The three actions drew `DRY_RUN`.** `bench.py run --mqtt` logged them with relay dry-run
on:

```text
20:50:10  lran/gatelink/cmd/gate/set  OPEN
20:50:13  lran/gatelink/cmd/ack       {"outcome":"acked","seq":6,"attempts":1,"result":16,"detail":0}
20:50:19  lran/gatelink/cmd/hold/set  ON
20:50:21  lran/gatelink/cmd/ack       {"outcome":"acked","seq":7,"attempts":1,"result":16,"detail":0}
20:50:27  lran/gatelink/cmd/gate/set  CLOSE
20:50:29  lran/gatelink/cmd/ack       {"outcome":"acked","seq":8,"attempts":1,"result":16,"detail":0}
```

The gate state did not change, and dry-run went back off, read from
`binary_sensor.gatelink_relay_dry_run`. **The commands went out as service calls, not taps.** Nothing here
can tap a tile: the app's browser pane was refused the sandbox's address, and Puppet only
renders.

**H7's figures now read real values.** `automation.trigger` on the morning report gave
*Battery 99 %, -1 % in 24 h. Solar yesterday 0.03 kWh.*

**Every gate status read as written**, with `sensor.gatelink_gate` and the cover forced
through the REST API and restored. The status's words are in plan 7.4.

**Fluvy's card frames read as buttons**, so the gate and link status moved to a `text_only`
markdown card, whose `<font>` and `<ha-icon>` both rendered. `<font color="red">` rendered in
the log too. On a Fluvy template card, `color: amber` with `active` came out in the accent
colour, and `#FFB300` with `tint: always` came out amber. The render used a temporary
non-admin storage dashboard and a state forced on `binary_sensor.gatelink_load_output`, and
the dashboard is deleted.

**Not checked:**

- **F5 on a flashed bridge.** The sandbox's bridge has not published the new row, so
  `check_entities.py` will report `binary_sensor.gatelink_load_output` until it does.
- **A real crossing.** Whether a real vehicle's first edge carries the bits the log reads.
- **A tap on any tile**, and Restore defaults' dialog.

**All but one of the config numbers read `unknown`**: 28 of GateLink's 29, with only
`poll_interval_s` known. `charge_inhibit_temp_c`'s history reads `unknown` from at least
18:37 UTC, before this session's first restart. Pressing Request configuration did not fill
them in the following minutes. Not chased, and it is why Temperature's *Charge inhibit
below* reads Unknown.
