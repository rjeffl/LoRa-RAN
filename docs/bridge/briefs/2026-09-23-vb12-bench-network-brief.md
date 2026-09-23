# V-B12 saturated arm — bench network brief

**Document:** `LRAN-Bridge-VB12-Bench-Network-Brief`
**Version:** 0.1
**Status:** **Reference for setting up the bench**, written 2026-09-23 when V-B12's
saturated arm was deferred. [`HANDOFF.md`](../HANDOFF.md) says when the task resumes.
Where this brief disagrees with Impl Plan §8.1 or the Bridge PRD, those documents win.
**Last updated:** 2026-09-23

The saturated arm needs two things that don't exist yet: a real WiFi transmit load on the
bridge, and a network the operator can load without degrading the house. This brief records
why the documented lever falls short, the network options discussed, and the operator's
current preference. It also lists what to settle before the test session starts.

## Why `diag_interval_s` cannot saturate WiFi

Impl Plan §8.1 names `diag_interval_s` as the lever for the saturated arm. Its floor is
10 s (`lib/lran-config/include/lran/config/table.h`, row `0x0002`). At that floor,
`sched_diag()` sends three small diagnostic documents every 10 s. It adds one per watched
node, but spec §16.6's bench gate holds that at zero for a simnode. M22 and Bridge PRD
§4.4 ask for "a sustained MQTT or iperf flood". Running
`per_measure.py --arm saturated` on this lever would close V-B12 without testing R-4.4's
claim. That is the same failure §8.1 already names for an inbound MQTT flood.

The load has to come from something new. A bench-only UDP transmitter on the bridge (a
"blaster") was the option chosen for further planning. It sends UDP to a discard port at a
capped rate and counts the packets and bytes it actually sent. Its on/off control and its
build form haven't been decided (see [Decisions to make first](#decisions-to-make-first)).

## What the network changes, and what it does not

**The network does not change what the test measures.** R-4.4 asks whether the ESP32-S3's
own 2.4 GHz transmitter degrades the SX1262's 917.4 MHz receive on the same board. That
coupling depends on the bridge radio's transmit duty cycle. The access point it talks to
does not enter into it.

The network affects three other things:

- **Airtime for other users.** A UDP flood takes most of its channel's airtime. An
  interleaved run holds roughly 8 to 10 minutes of load across about 20 minutes. Every other
  device on that channel slows during the saturated bursts.
- **Load stability.** Other traffic contends for the same airtime, so the load the
  blaster achieves varies. Each burst therefore records what the blaster actually sent.
- **The instrument's own path.** `sweep_interleave.py` counts frames from BF-27's frame
  log, and the bridge publishes that log over the same WiFi. A flood can back up
  `log_task` until the ring overwrites. The tool then reports `RING OVERWROTE` and the burst
  is spoiled. This happens on any network, so the blaster needs a rate cap below full
  saturation. The alternative is to count the saturated arm with `rx_frames` instead of
  the log.

## Network options

| Option | Broker | Household impact | Cost |
|---|---|---|---|
| House network | Sandbox broker, 192.168.2.52, unchanged | Every 2.4 GHz device on that channel slows during the bursts | None beyond the blaster. Run it at a quiet hour |
| IoT network, with a pinhole to the sandbox broker | 192.168.2.52 on TCP 1883, bridge IP only | None on the main network | A firewall rule, and a bridge reflash with the IoT credentials |
| **IoT network, with everything on it** (preferred) | A new Mosquitto on the IoT network | None on the main network | A broker to stand up, a bridge reflash, and the Mac's internet access to solve |

**The operator prefers the third option.** The bridge, the Mac and the broker all move to
the IoT network. In this environment the 2.4 GHz radio carries only IoT traffic, so a flood
there does not share airtime with the main network. The IoT network's traffic is not
bridged to the main network, and that is why the sandbox broker is out of reach from it.

## What the preferred option needs

- **The bridge.** Only the bridge uses WiFi. Set `WIFI_SSID`, `WIFI_PASSWORD` and
  `MQTT_HOST` in `secrets.h` to the IoT network and its broker, and reflash over USB. The
  bridge keeps its configuration in NVS, so a reflash does not clear its levers. Restore the
  original `secrets.h` values after the run, and reflash again.
- **The simnodes.** They have no WiFi. They stay on the Mac's USB ports, and nothing about
  them changes.
- **The broker.** Mosquitto can run on the Mac itself or on another host on the IoT network.
  Either way it needs a fixed address, such as a DHCP reservation, because `MQTT_HOST` is
  compiled into the bridge. A new broker holds no retained topics. The bridge republishes
  `lran/bridge/version`, retained, on every broker connect, so `sweep_interleave.py` still
  reads the image.
- **The tools.** Point `LRAN_MQTT_HOST`, `LRAN_MQTT_USER` and `LRAN_MQTT_PASSWORD` at the
  new broker. [`traps.md`](../traps.md#bench-credentials) says how to set them without
  printing them.
- **The Mac's internet access.** The Claude app needs internet for the whole session.
  There are two ways to keep it:
  1. **The IoT network allows outbound internet.** Check this first. If it does, the Mac
     joins the IoT network alone.
  2. **The Mac has two links.** Wired Ethernet to the main network carries internet, and
     IoT WiFi carries the broker and the bridge. Set the service order in macOS so that the
     default route is the wired link.
- **The Mac's own airtime.** If the Mac reaches the IoT network over 2.4 GHz WiFi, the frame
  log it receives shares the channel under test. That traffic is present in both arms, so
  the comparison holds, but it adds to the load the idle arm is meant to lack. A wired link
  to the IoT network avoids it, if the IoT network has a wired port.

## Decisions to make first

1. **The Mac's internet path.** Outbound internet on the IoT network, or a dual-homed Mac.
2. **The broker host.** The Mac or another IoT host, and its fixed address.
3. **The blaster's control.** The bridge has no console. The options discussed:
   - **A serial command in a bench build.** The spec is untouched. The sweep tool holds the
     bridge's port open for the whole run, and it has to, because opening that port reboots
     the bridge and zeroes its counters.
   - **An MQTT topic or a config row.** Every topic and config row belongs to the protocol
     specification, so this needs a spec revision and a decision first.
4. **The blaster's rate cap**, and whether the saturated arm counts from the frame log or
   from `rx_frames`.
5. **The Impl Plan edit.** §8.1 names `diag_interval_s` as the lever. It needs a correction
   that names the blaster, and it should go in the same commit as the blaster.

## Changelog

| Version | Date | Change |
|---|---|---|
| 0.1 | 2026-09-23 | First version. The saturated arm is deferred, and the operator prefers the IoT network option |
