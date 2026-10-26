# `ha` — session handoff

**Written 2026-10-25, at the end of the session that did H0 and H1 and started this file.**
It replaces the previous file wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## Start here

**A session does one task group, and reads only what that task needs.** Open it with one
of these lines, then read this section and the sections the table names:

```text
Continue from docs/ha/HANDOFF.md: task H2.
```

The plan is [`LRAN-HA-Integration-Implementation-Plan`](../shared/LRAN-HA-Integration-Implementation-Plan.md),
cited below as *plan*. H2, H3 and H7 depend only on H1 and can run in any order.

| Task | Read |
|---|---|
| **H2** Gate dashboard | Plan §8 and §8.1, §7.4's sensor table, §6's `lovelace` block; *Traps*, below |
| **H3** LRAN dashboard | Plan §8.2, §4's entity table, §10 |
| **H4** Tier 1 alerts | Plan §9.1, §7.2, §7.3, §3; *Traps*, below, before injecting any event |
| **H7** Tier 4 reports | Plan §9.4 |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**H2, the Gate dashboard**, as `ha/dashboards/gate.yaml`, installed in the sandbox under
`/config/dashboards/lran/` with plan §6's `lovelace` block. It reads the derived sensors
H1 made (plan §7.4), and the four command buttons stand in for the cover until F1.

## What the last session established

- **The simnodes are gone from the sandbox**, and clearing their retained discovery configs
  is what keeps them gone (engineering log, 2026-10-25).
- **`core.yaml` and `templates.yaml` are installed and pass H1's acceptance**, 22 checks.
  The run called the scripts directly. **It does not show** real SMTP, a push on the phone,
  or an event arriving over MQTT.
- **Plan v0.2 fixed two v0.1 defects**: the filter key now carries the follow-up bit, and
  net power reads the pack.

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
| Done | H0, H1 |
| Queue | H2 to H9, then H10 at the port. F1 to F3 are bridge tasks (plan §5) |

```bash
python3 tools/checks/run_ci_local.py      # the checks job; nothing here reads ha/packages yet
```

**To check a package change on the sandbox**, copy it over the Samba mount, then ask HA
to check its configuration before restarting:

```bash
cp ha/packages/lran/core.yaml ha/packages/lran/templates.yaml /Volumes/config/packages/lran/
curl -s -X POST -H "Authorization: Bearer $(cat ~/.lran_ha_token)" http://192.168.2.52:8123/api/config/core/check_config
```

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

**The sandbox `site.yaml` exists only on the sandbox.** It pushes to the Mac only and sends
email to a persistent notification. Rebuilding it is a copy of `site.yaml.example` with
those two targets changed, which plan §7.3 names.

## Behaviour that changed, and will make older artifacts read differently

- **From 2026-10-25 the sandbox registry has no `simnode_*` devices.** A screenshot or entity
  count from before then includes their 48 entities.

## Traps that cost real time here

- **The sandbox broker carries GateLink's live bench soak.** A test event published to
  `lran/gatelink/event/*` reaches the soak's subscribers as well as HA. Before H4 injects
  one, agree it with the operator, or use a node token nothing else watches.
- **A registry delete does not remove an MQTT-discovered device for good.** Its retained
  config recreates it at the next restart. Clear the config on the broker instead.
- **`initial:` on a helper resets it at every start.** The packages seed defaults once
  through `lran_0_seed_defaults` (plan §7.1).
- **HA shows temperatures in the install's units**, °F on the sandbox. The thresholds are
  in °C (plan §7.1).

## Open, and not closable from here

- **Real SMTP.** Needs an account in the sandbox's `site.yaml` and `secrets.yaml`, or waits
  for production (plan §12, step 6).
- **The phone's critical and time-sensitive pushes**, checked at H4. `site.yaml` adds
  `notify.mobile_app_jliphone16` then.
- **No CI check reads `ha/packages/`.** H9's `check_entities.py` needs a live HA. A YAML
  syntax check in the checks job would catch a broken file before the sandbox does.

### Closed, and not to be reopened by habit

- **H0, CLOSED 2026-10-25.** The simnodes stay gone unless `simnode_diag_enable` is turned
  on with a simnode running; spec §16.6 makes that a choice, not a defect.
