# `ha` — session handoff

**Written 2026-10-25, at the end of the session that did H0 and H1 and started this file.
Revised 2026-10-26 for the dev flow through ha-mcp, and again on 2026-10-26 at the end of
H3, when ha-mcp's dashboard screenshots came into use, on 2026-10-26 after the alert
levels and the message catalog, on 2026-10-27 at the end of H5, and on 2026-10-28 at the
end of H4b.**

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## Start here

**A session does one task group, and reads only what that task needs.** Open it with one
of these lines, then read this section and the sections the table names:

```text
Continue from docs/ha/HANDOFF.md: task H6.
```

The plan is [`LRAN-HA-Integration-Implementation-Plan`](../shared/LRAN-HA-Integration-Implementation-Plan.md),
cited below as *plan*. H4, H4b and H5 are done. H4b and H9 each leave one check for the
boards' power-up (*The next job*). H6 needs H4, and H7 depends only on H1.

| Task | Read |
|---|---|
| **H6** Tier 3 reminders and the action handler | Plan §9.3, §7.3 (`actions`), §7.5, §11's H6 row (V-8); *Traps*, below |
| **H7** Tier 4 reports | Plan §9.4, §7.5 |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**First, when the bridge and GateLink are powered up: check the GateLink return push**
(engineering log, 2026-10-28, H4b). Expected: one *GateLink back online* push, replacing the
2026-10-27 14:59 offline push, naming the outage from 14:49:41, and
`input_text.lran_gatelink_offline_since` empty afterwards. Read it with
`ha_get_automation_traces("automation.lran_1_gatelink_offline")` and `ha_get_state`. If no
push came, the trace says which condition stopped it. Log the result as a new dated entry.

**Before that power-up, flash the bridge from current `main`, then run
`python3 tools/ha/check_entities.py`** (engineering log, 2026-10-28, H9). It fails on one
ID, `number.gatelink_bms_retry_s`, because the sandbox's bridge has not run firmware from
a1de65d on. A bridge built from `main` publishes it, and the check should then pass, which
meets H9's acceptance (plan §11). Record the bridge commit HA reports in the same entry.

**Then H6**, Tier 3 reminders and the action handler.

## What the last session established

- **H9's check exists and runs against the sandbox through ha-mcp** (plan v0.9, §11;
  engineering log, 2026-10-28, H9). `tools/ha/check_entities.py` reads the server's address
  from `~/.claude.json`; `--server ha-prod` will point it at production. 199 of 200 IDs are
  present. The missing one is a bridge firmware lag, not a YAML error (*The next job*).
- **The H4b bullets below still stand**: the fixed return path has not run.
- **Both offline alerts fired on the soak's real shutdown** (plan v0.8, §9.1; engineering
  log, 2026-10-28, H4b). GateLink alerted at its 10-minute delay, the bridge at 5 minutes
  with no email, and a bridge outage did not re-alert GateLink. The operator saw both on
  the phone and the Mac. The GateLink return push was seen once, for a 15-minute outage.
- **The return push now counts from a start the offline alert records**, in
  `input_text.lran_gatelink_offline_since`, and fires on `on` from any state. Before, a
  bridge outage or an HA restart left a GateLink outage with no return push. **The fixed
  path has not run yet**: it waits for the boards' power-up.

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
| Done | H0, H1, H2, H3, H4, H4b, H5. H4b's fixed return push waits on the power-up check |
| Queue | H6 to H9, then H10 at the port. F1 to F3 are bridge tasks (plan §5) |

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
cd ha/packages/lran && cp core.yaml templates.yaml alerts.yaml messages.yml /Volumes/config/packages/lran/ && cd -
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
| Home Assistant OS VM at 192.168.2.52, with Mosquitto | the sandbox | its address, and `/config/packages/lran/site.yaml`, which is on no other install | HA 2026.10.4 | `/config` holds the installed packages and the sandbox's own `site.yaml`, the only copy of it | running; carries GateLink's bench soak 2 |
| The sandbox's `/config`, shared over Samba | the mount | mounted at `/Volumes/config` as `ha-samba-user` | — | — | mounted on the macOS machine |
| The ha-mcp app on the sandbox | ha-mcp | port 9584 and a secret path, registered as `ha-sandbox` | ha-mcp 8.7.0 | beta flags on: beta features and dashboard screenshots, set in its web UI's *Server Settings* | running; its config check read `valid` on 2026-10-26 |
| The Puppet app on the sandbox | Puppet | app slug `0f1cc410_puppet`, port 10000 | Puppet 2.6.0 | a long-lived token in its `access_token` option | running; renders for ha-mcp's screenshots. **Port 10000 takes no authentication** |

**The sandbox `site.yaml` exists only on the sandbox.** It pushes to the Mac only and sends
email to a persistent notification. Rebuilding it is a copy of `site.yaml.example` with
those two targets changed, which plan §7.3 names.

## Behaviour that changed, and will make older artifacts read differently

- **From 2026-10-27 a muted alert leaves a logbook entry**, marked `(alerts muted, not
  pushed)`. Before then, muting left no trace of a `normal` or `low` alert.
- **From 2026-10-25 the sandbox registry has no `simnode_*` devices.** A screenshot or entity
  count from before then includes their 48 entities.

## Traps that cost real time here

- **A node's `availability` is retained, so it outlives the bridge.** When the bridge
  comes back, `binary_sensor.lran_gatelink_online` reads the stale value first, then the
  bridge's own. Anything timed from that sensor's `last_changed` restarts at the bridge's
  return. Time from a recorded start instead, as the GateLink return push does (plan §9.1).
- **The sandbox broker carries GateLink's live bench soak.** A test event published to
  `lran/gatelink/event/*` reaches the soak's subscribers as well as HA. Agree each
  injection with the operator first. H4 published through ha-mcp's `mqtt.publish`, not
  retained, with `ctx_id` 4294967295 so the soak's log can tell the fakes apart. **Never
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
  the soak. Restore what you forced. **ha-mcp has no tool for this**: it is a REST
  endpoint, not a service or a WebSocket command. It still needs a long-lived token in
  `~/.lran_ha_token`, which the Mac does not have (*Open*).

## Open, and not closable from here

- **The 08:00:08 2026-10-28 `lran_notify` run has no logbook entry in ha-mcp's read**,
  though its trace reached `logbook.log`. Not chased.
- **Real SMTP.** Needs an account in the sandbox's `site.yaml` and `secrets.yaml`, or waits
  for production (plan §12, step 6).
- **Spec §8.9 gives BMS_ALARM's `detail` no meaning.** The alert shows it raw, in hex. A
  spec revision should define it, or say it is reserved.
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
- **Spec §7.3 gives `MPPT_ERROR`'s `detail` no meaning**, as it gives BMS_ALARM's none.
  `lran_2_mppt_error` takes the name from `sensor.gatelink_mppt_error` instead, which can
  lag the event. GateLink does not send the event yet.
- **CI cannot run H9's live half.** The checks job runs `check_entities.py --offline`,
  which parses every package and dashboard. Whether the IDs exist needs ha-mcp, so that
  half runs by hand, on each install.
- **ha-mcp cannot render `lran-maintenance`.** Its screenshot returns HA's home page,
  which is what a non-admin user gets for an admin-only dashboard. Making Puppet's user an
  admin would fix it; whether to is the operator's call.
- **The `write-clearly` skill is not installed on the Mac**, though CLAUDE.md requires it
  for every prose change. H3's prose was written without it.
- **Puppet on production** is a decision for H10: its port takes no authentication, and
  production's network may be less trusted than the sandbox's.

### Closed, and not to be reopened by habit

- **H0, CLOSED 2026-10-25.** The simnodes stay gone unless `simnode_diag_enable` is turned
  on with a simnode running; spec §16.6 makes that a choice, not a defect.
