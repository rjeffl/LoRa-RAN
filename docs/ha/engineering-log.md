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
