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
