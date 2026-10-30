# `ha` — session handoff

**Written 2026-10-25, at the end of the session that did H0 and H1 and started this file.
Revised 2026-10-26 for the dev flow through ha-mcp, and again on 2026-10-26 at the end of
H3, when ha-mcp's dashboard screenshots came into use, on 2026-10-26 after the alert
levels and the message catalog, on 2026-10-27 at the end of H5, on 2026-10-28 at the
end of H4b, on 2026-10-28 after the bench checks, on 2026-10-30 after the operator
chose Fluvy (task H11), on 2026-10-30 at the end of H11, and on 2026-10-30 after the
Fluvy port review.**

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## Start here

**A session does one task group, and reads only what that task needs.** Open it with one
of these lines, then read this section and the sections the table names:

```text
Continue from docs/ha/HANDOFF.md: task H8.
```

The plan is [`LRAN-HA-Integration-Implementation-Plan`](../shared/LRAN-HA-Integration-Implementation-Plan.md),
cited below as *plan*. H4, H4b, H5, H6, H7, H9 and H11 are done. **H8 is next**, and the
sandbox can build it but not verify its trigger, because it needs a production `person`
entity. H10, the port to production, waits on bridge task B6.

| Task | Read |
|---|---|
| **H8** Tier 5 approach prompt | Plan §9.5, §7.3; §11's H8 row |
| **H10** Port to production | Plan §12; §8.3's porting issues; §11's H10 row |
| The Fluvy review's open decisions | `briefs/2026-10-30-fluvy-port-review.md`, *Recommendations*; plan §8.1, §8.3 |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**H8, the approach prompt** (plan §9.5). It sends a plain push that opens the Gate
dashboard, as the reminders do. Its trigger needs a tracked `person`, which the sandbox
does not have, so the sandbox can check the push but not the trigger.

**The Gate dashboard's Open gate and Close gate tiles have not been pressed.** Before
production, turn GateLink's relay dry-run on and press each once, as H6 did: `cmd/ack`
should carry `result` 16 and the gate should not move. The review brief's other three
open decisions wait on the operator: the SOC gauge's bands, alert snooze on the Gate
dashboard, and the Restore defaults dialog.

**H7's SOC and yield figures are unchecked with real values.** Rerun
`automation.lran_4_morning_report` through `automation.trigger` once GateLink's BMS and
MPPT readings are available.

## What the last session established

- **The Gate dashboard's Open and Close are named tiles** (plan v0.17, §8.1, §8.3;
  engineering log, 2026-10-30, named tiles). Fluvy's cover card draws them as bare arrows.
  **Neither tile has been pressed**: dry-run is off, so repeat H6's dry-run check first.
  The review brief, `briefs/2026-10-30-fluvy-port-review.md`, lists four open decisions:
  the SOC gauge's bands, alert snooze, the Restore defaults dialog and mixed-unit charts.

- **H11 is met** (plan v0.16, §8.3; engineering log, 2026-10-30, H11). Fluvy v1.11.3
  runs on the sandbox, the theme is the default and its *Scope* is `everywhere`. The
  three dashboards use Fluvy's cards, with the entity IDs unchanged, and every view
  renders. Plan §8.3 maps each card and lists the porting issues.

- **H7 is met** (engineering log, 2026-10-28, H7). The reports read long-term statistics
  through `recorder.get_statistics`; plan §9.4 states the method and its three
  approximations. `lran_notify` notes every alert for the morning report.
- **GateLink's relay dry-run is off**, by operator request.
- **`automation.trigger` runs an automation by hand.** ha-mcp's `run_actions` asks for its
  best-practices key first.
- **H6 is met** (engineering log, 2026-10-28, H6 is met). The dashboard's Close went out
  with dry-run on, and GateLink's `cmd/ack` carried `result` 16, `DRY_RUN` (spec §8.2).
  The gate state did not change.
- **`bench.py run --mqtt` reads `cmd/ack` without the bridge console.** It takes the
  broker login from `secrets.h` and never prints it, with the operator's approval. No
  serial port opens, so the bridge does not reboot. `--dir` goes before `run`.
- **Reminders carry no action, by operator choice** (plan v0.11, §3, §9.3, §9.5;
  engineering log, 2026-10-28, bench checks). A reminder is a plain push whose tap opens
  `/lran-gate`, checked on the phone. `lran_3_close_action` is gone, and `lran_notify`
  takes `url` in place of `actions`. H8's approach prompt follows the same rule.
- **iOS does require an unlock, and Face ID gives it silently.** A tap facing the operator
  unlocked the phone with no prompt; facing away, nothing happened. The old Close gate
  action was never tried facing away.
- **H9 passes against the sandbox**, with the bridge on 760e9a3, flashed from `main`.
- **GateLink was silent after the bridge flash until the operator restarted it**, the
  second time after 2026-10-26 (GateLink handoff, *Open*).
- **The nobody-home reminder cannot fire on the sandbox**, which has no tracked person.
- **H4b's fixed return path ran** at 14:14:03 UTC on 2026-10-28 (engineering log, the
  same date). It counted 19 h 24 min from the recorded start and cleared the helper. By
  the operator's report, the return push replaced the offline push on the phone.
- **H9's check runs against the sandbox through ha-mcp** (plan v0.9, §11; engineering
  log, 2026-10-28, H9). `tools/ha/check_entities.py` reads the server's address from
  `~/.claude.json`; `--server ha-prod` will point it at production.
- **Both offline alerts fired on the soak's real shutdown** (plan v0.8, §9.1; engineering
  log, 2026-10-28, H4b). GateLink alerted at its 10-minute delay, the bridge at 5 minutes
  with no email, and a bridge outage did not re-alert GateLink. The operator saw both on
  the phone and the Mac. The GateLink return push was seen once, for a 15-minute outage.
- **The return push now counts from a start the offline alert records**, in
  `input_text.lran_gatelink_offline_since`, and fires on `on` from any state. Before, a
  bridge outage or an HA restart left a GateLink outage with no return push.

- **The seven Tier 2 health alerts are installed and verified** (plan v0.7, §9.2;
  engineering log, 2026-10-27, H5). The state alerts were driven by moving thresholds, and
  the event alerts by events injected with `ctx_id` 4294967295. Every selector now has
  `Low`, an alert with two levels is two alerts, and `lran_notify` logs a muted alert,
  marked. A Tier 2 push on the phone was not checked: the sandbox pushes to the Mac.

- **Every alert has a level selector and its text in `messages.yml`** (plan §7.5;
  engineering log, 2026-10-26, alert levels). The Maintenance dashboard's *Alerts* card
  sets `Off`, `Normal`, `High` or `Critical` per alert. `lran_notify` was verified by direct
  calls, and H4b's offline run drove two automations through it end to end. `tools/checks/ha_messages.py` is the first CI check
  that reads `ha/packages/`.

- **The four Tier 1 event alerts are installed and verified** with events injected through
  ha-mcp's `mqtt.publish`, `ctx_id` 4294967295 (engineering log, 2026-10-26, H4). The
  held-open follow-up replaces its push and sends no email; a retransmission is dropped.
  **FIRE's critical alert was seen on the phone.** V-5 holds across an HA restart. The
  offline alerts were exercised in H4b.

- **The simnodes are gone from the sandbox**, and clearing their retained discovery configs
  is what keeps them gone (engineering log, 2026-10-25).
- **`core.yaml` and `templates.yaml` are installed and pass H1's acceptance**, 22 checks.
  The run called the scripts directly. **It does not show** real SMTP, a push on the phone,
  or an event arriving over MQTT.
- **Plan v0.2 fixed two v0.1 defects**: the filter key now carries the follow-up bit, and
  net power reads the pack.
- **The Gate dashboard is installed and passes H2's acceptance** at a phone viewport, with
  every banner condition forced through HA's REST API and restored (engineering log,
  2026-10-25, H2). Plan v0.3 records the link chip as two tiles and the V sensors without a
  device class.
- **The LRAN dashboards are installed and meet H3's acceptance as far as the data goes**
  (engineering log, 2026-10-26, H3). `lran-detail` holds Power, Network and Events;
  Maintenance is `lran-maintenance`, admin only, because HA cannot gate a view. Every live
  LRAN entity is on one of the three dashboards, and every graphed entity has statistics.
  The operator confirmed the render.
- **A session can now see a dashboard rendered**, through ha-mcp's screenshot beta and the
  Puppet app (engineering log, 2026-10-26, render check). H4 and later tasks check banners
  and cards this way rather than asking the operator.

## Fluvy

Read 2026-10-30 from `acosta290/fluvy` on GitHub, its README and `docs/`. Fetch a doc with
`gh api "repos/acosta290/fluvy/contents/docs/<name>.md?ref=v1.11.3" --jq .content | base64 -d`.
Its source answers what the docs leave out, such as the alert matcher in
`packages/cards/src/alerts/match.ts`.

- **It is a HACS integration, not a HACS theme**: one install brings the theme, 63 cards,
  a settings panel and a dashboard resource. HACS lists it as `1390720234`. GPL-3.0, one
  maintainer, no network calls of its own.
- **The card mapping is plan §8.3.** Fluvy's card editors cannot save to a YAML
  dashboard, so a card is written from Fluvy's card guide, `cards.md` in its docs folder.
- **Fluvy's house settings live in HA's frontend system data**, key `fluvy`, read and
  written with `frontend/get_system_data` and `frontend/set_system_data` through
  `ha_call_service(ws_command=…)`.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | Plan §2, §3, §6, then the task's own sections | the operator's choices and the rules every file here meets |
| 3 | [`engineering-log.md`](./engineering-log.md), latest entry | what was verified, and what was not |
| 4 | [`ha/README.md`](../../ha/README.md) | discovery: how the entities this layer reads are made |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | H0, H1, H2, H3, H4, H4b, H5, H6, H7, H9, H11 |
| Queue | H8, then H10 at the port. Bridge tasks F1 to F4 are built (plan §5) |

```bash
python3 tools/checks/run_ci_local.py      # the checks job; ha_messages.py reads ha/packages/lran
```

### The dev flow: files over Samba, everything else through ha-mcp

**From 2026-10-26 the sandbox is worked through two channels, and production will be worked
the same way.** Files go over the Samba mount, because the repository is their source and
the port to production is a file copy (plan §12). Everything that asks HA something or tells
it to act goes through the ha-mcp server (app 8.7.0 on the sandbox), not the REST API with a
token. The REST flow before then needed `~/.lran_ha_token`, which only one machine had.

```bash
cd ha/packages/lran && cp core.yaml templates.yaml alerts.yaml reminders.yaml reports.yaml messages.yml /Volumes/config/packages/lran/ && cd -
cp ha/dashboards/gate.yaml /Volumes/config/dashboards/lran/      # a YAML dashboard reloads with the page
```

Then, through ha-mcp:

| Step | Tool |
|---|---|
| Check the configuration before any reload or restart | `ha_get_system_health(include="config_check")`; read `config_check.is_valid` and `errors` |
| Reload a package's templates, automations or scripts | `ha_reload_core(target=…)`, then `ha_get_state` on its sensors: none may be `restored: true` (*Traps*) |
| Restart, when a reload does not cover the change | `ha_restart(confirm=True)`, which runs the config check itself first |
| Read states, history, the error log, automation traces | `ha_get_state`, `ha_get_history`, `ha_get_logs`, `ha_get_automation_traces` |
| Read a dashboard as HA resolved it | `ha_config_get_dashboard(url_path=…)` |
| See a dashboard rendered | `ha_config_get_dashboard(url_path=…, view_path=…, include_screenshot=True)`. One 1280×800 viewport, so a low card can fall outside it |

**Do not write the LRAN dashboards with `ha_config_set_dashboard`.** It writes storage-mode
dashboards. Ours are YAML files in the repository, so a dashboard edited through ha-mcp
would not be in the repository or reach production.

**Each machine registers ha-mcp itself, at user scope, never in the repository.** The
server's address carries its secret path. On the Mac it is `ha-sandbox` in
`~/.claude.json`'s `mcpServers`, `{"type": "http", "url": "http://192.168.2.52:9584/private_…"}`.
Production gets its own entry, `ha-prod`, when H10 reaches it. Check which one a session
has loaded before it acts on anything.

## Git state — ask git, do not read it here

```bash
git fetch origin -p                            # prune deleted remote branches first
git log --oneline -1 origin/main                # where main actually is
gh pr list --state open                         # what is open, if anything
git log --branches --not --remotes --oneline    # local-only work; empty is good
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository.

## Hardware state

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Home Assistant OS VM at 192.168.2.52, with Mosquitto | the sandbox | its address, and `/config/packages/lran/site.yaml`, which is on no other install | HA 2026.10.4 | `/config` holds the installed packages and the sandbox's own `site.yaml`, the only copy of it | running; the bench soak ended on 2026-10-27. GateLink's relay dry-run is off |
| The sandbox's `/config`, shared over Samba | the mount | mounted at `/Volumes/config` as `ha-samba-user` | — | — | mounted on the macOS machine |
| The ha-mcp app on the sandbox | ha-mcp | port 9584 and a secret path, registered as `ha-sandbox` | ha-mcp 8.7.0 | beta flags on: beta features and dashboard screenshots, set in its web UI's *Server Settings* | running; its config check read `valid` on 2026-10-26 |
| The Puppet app on the sandbox | Puppet | app slug `0f1cc410_puppet`, port 10000 | Puppet 2.6.0 | a long-lived token in its `access_token` option | running; renders for ha-mcp's screenshots. **Port 10000 takes no authentication** |

**The sandbox `site.yaml` exists only on the sandbox.** It pushes to the Mac only and sends
email to a persistent notification. Rebuilding it is a copy of `site.yaml.example` with
those two targets changed, which plan §7.3 names.

## Behaviour that changed, and will make older artifacts read differently

- **From 2026-10-30 the sandbox runs Fluvy**, theme and cards. A screenshot from before
  then shows HA's default theme and core cards, and the Gate dashboard's gate controls
  asked for confirmation. Until a later commit the same day, the Gate dashboard's open and
  close were the cover card's arrows; from then they are tiles named *Open gate* and
  *Close gate*.

- **From 2026-10-27 a muted alert leaves a logbook entry**, marked `(alerts muted, not
  pushed)`. Before then, muting left no trace of a `normal` or `low` alert.
- **From 2026-10-25 the sandbox registry has no `simnode_*` devices.** A screenshot or entity
  count from before then includes their 48 entities.

## Traps that cost real time here

- **After a Fluvy install or update, restart the Puppet app** (`0f1cc410_puppet`). Its
  browser keeps the old frontend, and every Fluvy card renders as an error placeholder.
- **A Fluvy heading needs `grid_options: {columns: full}`**, or it takes one grid cell and
  pushes the cards beside it (plan §8.3).

- **A node's `availability` is retained, so it outlives the bridge.** When the bridge
  comes back, `binary_sensor.lran_gatelink_online` reads the stale value first, then the
  bridge's own. Anything timed from that sensor's `last_changed` restarts at the bridge's
  return. Time from a recorded start instead, as the GateLink return push does (plan §9.1).
- **A test event on the sandbox broker reaches every subscriber**, not only HA. Agree each
  injection with the operator first. H4 published through ha-mcp's `mqtt.publish`, not
  retained, with `ctx_id` 4294967295 so a subscriber's log can tell the fakes apart. **Never
  publish a retained `availability`**: it would stand over the bridge's own state.
- **A `numeric_state` trigger does not watch its threshold helper**, only its entity. An
  alert on a sensor's state uses a template trigger, or a threshold moved in the UI does
  nothing until the sensor next changes (plan §9.2). A template trigger that is already
  true when the automations reload does not fire, so set a test threshold after the
  reload, not before.
- **A new alert selector starts at `Off`** on an install already seeded (plan §7.5). Set
  it after the reload. The Alerts card's warning names any left at `Off`.
- **A registry delete does not remove an MQTT-discovered device for good.** Its retained
  config recreates it at the next restart. Clear the config on the broker instead.
- **`initial:` on a helper resets it at every start.** The packages seed defaults once
  through `lran_0_seed_defaults` (plan §7.1).
- **HA shows temperatures in the install's units**, °F on the sandbox. The thresholds are
  in °C (plan §7.1).
- **`check_config` passes template keys the template reload rejects.** It passed
  `suggested_display_precision`, and the reload then dropped every template sensor in the
  package to `unavailable`. After a reload, check that the sensors are not `restored: true`.
- **A sensor with a device class gets HA's suggested display precision**, 0 for V and W,
  and the template schema has no key to override it (plan §7.4).
- **To force a dashboard condition without the broker**, `POST /api/states/<entity>` with
  the entity's current attributes. The next MQTT update overwrites it, so it never reaches
  the broker. Restore what you forced. **ha-mcp has no tool for this**: it is a REST
  endpoint, not a service or a WebSocket command. It still needs a long-lived token in
  `~/.lran_ha_token`, which the Mac does not have (*Open*).

## Open, and not closable from here

- **A Fluvy update can change a card under a YAML dashboard nobody re-checks**, so each
  update needs a render of every view on the sandbox before production takes it.
- **Two things are unverified under Fluvy.** How its chart card scales series in
  different units needs BMS and MPPT readings. HA's own pages need a browser, because
  Puppet renders dashboards only.
- **The 08:00:08 2026-10-28 `lran_notify` run has no logbook entry in ha-mcp's read**,
  though its trace reached `logbook.log`. Not chased.
- **Real SMTP.** Needs an account in the sandbox's `site.yaml` and `secrets.yaml`, or waits
  for production (plan §12, step 7).
- **Forcing a state has no ha-mcp route.** An event alert needs none: `mqtt.publish`
  injects the event (H4). An alert on a sensor's state needs none either: H5 moved its
  threshold instead. A banner check still needs one. The operator chose a test-only
  package of `input_boolean` overrides on 2026-10-27, and none is written yet.
- **A state alert already past its threshold when HA starts or reloads its automations
  does not fire** until the condition clears and recurs. This covers `lran_2_soc_low`,
  `lran_2_enclosure_hot` and `lran_2_cell_spread`. A re-check on start and on reload
  would close it. The operator left it open on 2026-10-27.
- **The BMS's four temperatures have no HA entity.** The bridge publishes them on
  `lran/gatelink/battery/state` as `cell1_temp_c` to `cell4_temp_c` (spec §7.2.3,
  `cell_temp_c[0..3]`), but `discovery.cpp` has no row for them, which makes this a bridge
  task. **The operator set the rule on 2026-10-27: the reference for low-temperature
  charging is the lowest of the four.** GateLink's inhibit already uses that rule
  (`charge_inhibit.cpp`, R-6.4a, `charge_inhibit_temp_c`). HA should show the four and a
  derived minimum beside `charge_inhibit_temp_c`. The four may not all be cell sensors,
  though: `aiobmsble` labels them ambient, MOSFET, cell, cell, and nothing has confirmed
  that on this pack (bms-protocol §10).
- **`lran_2_mppt_error` can name the error from `detail`.** Spec v0.19 §8.9 puts the
  VE.Direct `ERR` code in `MPPT_ERROR`'s `detail`, and `bms_alarms` in `BMS_ALARM`'s
  (**D86**). The alert still reads `sensor.gatelink_mppt_error`, which can lag the event.
  GateLink sends neither event yet, and `bms_alarms`' bits wait on `0x8D`'s decode.
- **The sandbox's home location looks wrong for the site.** `sun.sun` put the next sunset
  at 16:18 UTC on 2026-10-28, 12:18 in New York, so `lran_3_open_at_night` would fire at
  the wrong hour there. Check production's location before H10.
- **CI cannot run H9's live half.** The checks job runs `check_entities.py --offline`,
  which parses every package and dashboard. Whether the IDs exist needs ha-mcp, so that
  half runs by hand, on each install.
- **ha-mcp cannot render `lran-maintenance`.** Its screenshot returns HA's home page,
  which is what a non-admin user gets for an admin-only dashboard. H11 rendered a copy as
  a temporary non-admin storage dashboard instead. Making Puppet's user an admin would
  fix it; whether to is the operator's call.
- **Puppet on production** is a decision for H10: its port takes no authentication, and
  production's network may be less trusted than the sandbox's.

- **An iOS notification action's unlock is untested** with the phone facing away. It
  matters only if actions come back (plan §9.3).

### Closed, and not to be reopened by habit

- **H0, CLOSED 2026-10-25.** The simnodes stay gone unless `simnode_diag_enable` is turned
  on with a simnode running; spec §16.6 makes that a choice, not a defect.
