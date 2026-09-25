# `ha/` — what Home Assistant sees

**`ha/discovery/` is generated from the firmware, not written by hand.** It
holds every MQTT discovery config `firmware/bridge` publishes, one file per topic, and
`tools/checks/ha_examples.py` fails the build when they stop matching. Impl Plan §4.4
asks for them "for reference and for bench testing without a running HA"; an example
nobody checks drifts from the code the first time a table row changes, silently, in the
one artifact somebody reaches for when Home Assistant is not cooperating.

```bash
python3 tools/checks/ha_examples.py            # committed files vs. the firmware
python3 tools/checks/ha_examples.py --write    # regenerate after a discovery change
```

**A failure is usually not a bug.** It means a table in `discovery.cpp` changed and these
were not regenerated. Regenerate, read the diff, and commit it with the change that
caused it — that diff is exactly what Home Assistant will see differently.

## Reading a file

The filename is the topic with `homeassistant/` and `/config` removed and the slashes
turned into dashes, because a filename cannot hold the slashes:

```
ha/discovery/sensor-lran_gatelink_rssi.json
  -> homeassistant/sensor/lran_gatelink_rssi/config
```

Every config is published **retained**, on boot and on every broker reconnect
(**R-3.3b**).

**The keys are Home Assistant's abbreviations** — `stat_t`, `uniq_id`, `dev_cla`. They are
as stable as the long forms and they are used because `kMaxPayloadLen` is 768 bytes and
the long forms put a node's config within a hundred bytes of it; growing that buffer costs
RAM in every publish queue slot. `~` is the device's base topic, so `~/diag/state` reads
as `lran/gatelink/diag/state`.

## What is here, and what is not

| Present | Why |
|---|---|
| `lran_bridge_*` | The bridge's own device: firmware version, commit, OTA slot, and the two §14.1 totals plus `tx_frames` |
| `lran_gatelink_*`, `lran_welllink_*` | One device per **registered** node (R-3.3b), with its link diagnostics and the buttons its node type implements |

| Absent | Why |
|---|---|
| `lran_simnode*` | Spec §16.6 gates a bench node's publication and **BF-26** has not built the toggle. HA's registry remembers a `unique_id` forever, so four devices that could never update is a cost paid once and kept. Regenerate with `--bench` to see what they would be |
| Every §14.1 counter as its own entity | 22 rows per node in HA's registry, paid forever, for numbers read during a bench session. They stay readable on `lran/bridge/diag/state`. **R-3.5e** reasons the same way about the MPPT's registers |
| A `reboot` button | Spec §8.1 guards `REBOOT` with `0xA5` in `arg` precisely so it cannot be issued by accident, and a dashboard button is that accident. It stays reachable by publishing to `lran/<node>/cmd/reboot/set` |
| `set_debug_mode` and its neighbours | They carry a bitmask in `arg2` and a button cannot express one. They belong with the configuration work |

## Two things to know before pointing this at a real Home Assistant

- **HA's entity registry remembers every `unique_id` it has ever seen, and a retained
  discovery config survives a reflash.** Develop against the dev HA VM and the dev broker
  until **B6** (Impl Plan §11.3). A `unique_id` published once is effectively frozen.
- **A node entity's availability is that node's own topic, never the bridge's LWT**
  (**R-3.3d**). A node that has gone offline reads unavailable while the bridge is still
  connected and publishing, which is the case the bridge's LWT cannot describe.

## `ha/automations/` — written by hand

These are the examples an LRAN automation starts from. Nothing generates them, and
`ha_examples.py` does not read them.

**Every automation on an event topic ignores `synthetic: true`.** The bridge's dummy
publish (**BF-27**) reaches a node's real event topics whenever that node has not been
heard since the bridge booted (Impl Plan §6.6.2). Its events carry `synthetic: true`,
and a radio frame's carry `false`. An automation without the condition sends a real
email or SMS about a bench test.

[`lran_event_notify.yaml`](automations/lran_event_notify.yaml) has the condition. Paste it
into Home Assistant's automation editor in YAML mode. It raises a persistent notification;
replace the action with the notifier you use. It is written for Home Assistant 2026.9,
whose automations take `triggers`, `conditions` and `actions`.
