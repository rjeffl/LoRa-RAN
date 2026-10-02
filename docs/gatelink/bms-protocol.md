# TDT smart BMS protocol over BLE

**Document:** `bms-protocol`
**Version:** 0.1
**Node:** `GateLink`, node ID `0x01`
**Status:** Confirmed on hardware for one pack at rest. §10 lists what is not verified.
**Implementation:** [`lib/bms-ble/`](../../lib/bms-ble/)
**Source:** [`wattcycle-reader-poc_3`](../../wattcycle-reader/docs/wattcycle-reader-poc_3.md)
§4–§5 and §10.3, lifted out by GateLink task L2
**Last updated:** 2026-10-02

> **This document covers the BLE link between GateLink and the battery's BMS.** It is not
> part of the LRAN protocol. How GateLink carries BMS values over LoRa is the Protocol
> Specification's: the status schema's BMS block, spec §7.2.3. Neither document redefines
> the other.

## 1. What this is

The gate battery is a WattCycle 12 V 100 Ah Mini LiFePO4, a 4S pack. Its BMS is a **TDT
smart BMS**, and GateLink reads it over BLE as a central (GateLink PRD §3.4, R-3.4a–R-3.4d).
The vendor publishes no protocol. Everything here was captured from the gate battery, on
the bench, by the `wattcycle-reader` proof of concept. `aiobmsble` 0.27.0, a maintained
Python library, decodes the same pack and is the reference implementation (§12).

`lib/bms-ble/` implements §4 to §7 and §9. Its 21 host tests run against the captured frames
in §9:

```bash
pio test -d lib/bms-ble -e native
```

## 2. Advertising and GATT layout

| Item | Value |
|---|---|
| Advertised name | `XDZN_001_49A1`, with underscores. GateLink Impl Plan §4.3 writes the pattern as `XDZN_001_xxxx` |
| BMS MAC | `C0:D6:3C:58:49:A1` |
| Advertisement | Manufacturer data (`b'<XI\xa1\xffXD'`) holds the MAC bytes and no telemetry. Reading the pack needs a connection |

**Service `0xFFF0`** carries the data path:

| Characteristic | Properties | Role |
|---|---|---|
| `0xFFF1` | notify, read | Responses arrive here |
| `0xFFF2` | write, write-no-response, read | Requests go here |
| `0xFFFA` | write, write-no-response, read | **Handshake.** `HiLink` is written here, and read back for the `0x01` acknowledgement |

**Service `02F00000-…-FE00`**, with characteristics `FF00`–`FF05`, carries no battery
data. The same UUID appears on unrelated hardware, such as Govee thermometers and WLT8016
modules, so it is a chipset service from the Telink BLE SDK. Ignore it, and do not write
to it.

Two earlier assumptions were wrong, and each cost time:

- **`FFF2` has no CCCD.** An iOS sniff appeared to show one. macOS reports
  `CBATTErrorDomain Code=6` when subscribing there, and the property list has no `notify`.
- **The `FFF0`/`FFF1`/`FFF2` triple does not identify a JBD BMS.** TDT uses the same layout
  with an unrelated protocol, and so does Daly. Only a successful exchange identifies the
  protocol.

## 3. Access sequence

Order and target both matter. The sequence was found by instrumenting `aiobmsble`'s
transport layer against the gate battery (§11).

1. Connect, and request the largest MTU the stack allows (§7).
2. **Write ASCII `HiLink` (`48 69 4C 69 6E 6B`) to `FFFA`, with response.**
3. **Read `FFFA` back.** It returns `0x01`, the handshake acknowledgement. If it returns
   anything else, do not go on.
4. Subscribe to `FFF1` by writing its CCCD.
5. Send requests to `FFF2`. Responses arrive as notifications on `FFF1`.

> **The handshake goes to `FFFA`, not `FFF2`.** Before a successful handshake, the BMS
> acknowledges every write to `FFF2` at the ATT layer and ignores it. It then drops the
> link about 4 s after the connection. That failure looks exactly like a wrong protocol,
> and finding the cause took about twenty blind probe combinations. `FFFA` looked unused
> because the JBD layout has no counterpart to it.

## 4. Frame format and CRC

```text
request:   1E 00 01 03 00 <cmd> 00 00 <crc_hi> <crc_lo> 0D                (11 bytes)
response:  7E 00 01 03 00 <cmd> 00 <len> <payload...> <crc_hi> <crc_lo> 0D
```

- **The request head is `0x1E`, and the response head is `0x7E`.** `aiobmsble` tries `0x7E`
  first, three writes with response and three without, before it tries `0x1E`. That costs
  about 10 s per session, because this unit never answers `0x7E`. Send `0x1E`. Keep `0x7E`
  in mind only for a second TDT unit, which might differ.
- **Every request is written with response.** Every successful exchange in the captures
  was.
- `<len>` is the payload byte count. It excludes the CRC and the terminator.
- The terminator is `0x0D`.

The checksum is CRC-16/MODBUS: polynomial 0x8005, initial value 0xFFFF, reflected in and
out, no final XOR. It covers every byte from the head up to the CRC. It was verified
against five distinct captured frames.

**The CRC is transmitted big-endian, high byte first.** That is the opposite of the Modbus convention, and the test suite guards it.

## 5. Commands

| Command | Returns | Request frame |
|---|---|---|
| `0x8C` | Cells, temperatures, pack voltage and current, SOC, capacity, cycles | `1E 00 01 03 00 8C 00 00 B1 44 0D` |
| `0x8D` | Alarm and protection bitmaps, MOSFET status. **Not decoded** (§10) | `1E 00 01 03 00 8D 00 00 71 15 0D` |
| `0x92` | Software version, manufacturer, serial number | `1E 00 01 03 00 92 00 00 B7 24 0D` |

## 6. Payload of command `0x8C`

The payload length varies with two inline counts. Every multi-byte field is big-endian.

| Offset | Field | Scaling |
|---|---|---|
| 0 | Cell count `N` | — |
| 1 | `N` × u16 cell voltage | mV |
| 1 + 2N | Temperature sensor count `M` | — |
| 2 + 2N | `M` × u16 temperature | 0.1 K. °C = (raw − 2731) / 10 |
| then | u16 current | **Bit `0x4000` is read as the discharge flag** (§10). Magnitude = (raw & 0x3FFF) × 10 mA |
| +2 | u16 pack voltage | × 10 mV |
| +4 | u16 remaining capacity | × 0.1 Ah |
| +6 | u16 nominal capacity | × 0.1 Ah |
| +8 | u16 cycle count | — |
| +10 | u16 state of health | × 0.1 % |
| +12 | u16 SOC | % |

The current field needs the most care. Read as a plain signed int16, a pack at rest reads
16384 instead of zero.

The worked example is the §9 capture, with payload length `0x20`, `N` = 4 and `M` = 4:

```text
04 0D89 0DA1 0D9C 0D9B  04 0B82 0B9D 0B7F 0B7E
4000 0570 03E7 03E8 0001 03E8 0064
```

It decodes to cells of 3.465, 3.489, 3.484 and 3.483 V, and temperatures of 21.5, 24.2,
21.2 and 21.1 °C. Current is 0.0 A and the pack is at 13.92 V. Capacity is 99.9 of
100.0 Ah, with 1 cycle and an SOH of 100.0 %. SOC is 100 %.

## 7. MTU, fragmentation and framing

**Negotiate a large MTU.** macOS negotiated an MTU of 512, and a 71-byte response arrived
as one notification. NimBLE-Arduino 1.4.3 on the Heltec V3 and on the StamPLC also
negotiated 512 when the client asked for it. NimBLE's default is 23, which leaves 20 bytes
of payload per notification, so the client must ask.

**Implement reassembly anyway.** MTU negotiation can fail or be refused, and a client must
not depend on it silently. One frame per notification is the usual case, not a guarantee.

**Frame on the length byte, never on the terminator.** The total frame length is
`8 + payload_len + 3`: the header, the payload, the CRC and `0x0D`. The `0x8C` response in
§9 has four literal `0x0D` bytes before its real terminator, because cell voltages near
3.4 V encode as `0x0D89`, `0x0DA1` and so on. It also has a literal `0x7E` inside the
payload, because cell 4 reads `0x0B7E`. Reading until `0x0D` corrupts the frame silently,
and so does scanning for a head inside a frame.

The reassembly rules:

1. Resync by scanning for `0x7E`, and discard anything before it.
2. Wait for 8 bytes, then read `payload_len` from offset 7.
3. Wait for `8 + payload_len + 3` bytes in total.
4. Check that the last byte is `0x0D`, then validate CRC-16/MODBUS over every byte before
   the CRC.
5. Drop a partial frame after about 1 s.

The reassembler passes the §9 frames at chunk sizes of 20, 7 and 1 bytes.

## 8. Connection lifetime

**Before a successful handshake**, the BMS drops the connection about 4 s after it is made,
whatever is written to `FFF2`. Writes that the BMS does not accept do not reset that timer.
The PoC also records the window as 3.9 s.

**After a successful handshake**, the connection holds. It stayed up across a 3 s idle gap
with no reconnect and kept answering polls. `wattcycle-reader` held one connection open and
polled every 5 s.

The protocol therefore allows a held connection. GateLink does not hold one: PRD R-3.4a and
R-3.4b require it to connect, read, disconnect and de-initialize the BLE controller on every
`bms_poll_s`, for power reasons. GateLink Impl Plan §4.3 and §5.2 describe that client.

## 9. Reference capture

`aiobmsble` 0.27.0 took these frames from the gate battery at rest and fully charged. The
raw log is `wattcycle-reader/tools/results_from_aiobmsble.txt`. **They are the fixtures for
`lib/bms-ble/`'s host tests.** Do not edit one to match a decoder.

```text
req  0x8C: 1e 00 01 03 00 8c 00 00 b1 44 0d
rsp  0x8C: 7e 00 01 03 00 8c 00 20
           04 0d89 0da1 0d9c 0d9b 04 0b82 0b9d 0b7f 0b7e
           4000 0570 03e7 03e8 0001 03e8 0064 55 a3 0d

req  0x8D: 1e 00 01 03 00 8d 00 00 71 15 0d
rsp  0x8D: 7e 00 01 03 00 8d 00 18
           04 00000000 04 0000000000000000 0000 0629 00000000 0000 bd 3f 0d

req  0x92: 1e 00 01 03 00 92 00 00 b7 24 0d
rsp  0x92: 7e 00 01 03 00 92 00 3c
           57 54 33 30 5f 31 30 30 30 34 53 57 31 34 5f 4c 5f 30 31 00   "WT30_10004SW14_L_01"
           31 31 31 31 32 32 32 32 33 33 33 33 34 34 34 34 35 35 35 35   "11112222333344445555"
           49 4b 4b 4b 4b 30 30 30 30 41 49 49 30 30 30 30 30 30 30 30   "IKKKK0000AII00000000"
           2e b7 0d
```

The `0x92` payload is three fixed 20-byte ASCII fields. The PoC document elides it; the
bytes above come from the raw log, and their CRC verifies.

`aiobmsble` decoded the capture to these values:

| Field | Value |
|---|---|
| Cell count / temperature sensors | 4 / 4 |
| Cell voltages | 3.465, 3.489, 3.484, 3.483 V (delta 24 mV) |
| Temperatures | 21.5, 24.2, 21.2, 21.1 °C. `aiobmsble` labels them ambient, MOSFET, cell, cell (§10) |
| Pack voltage | 13.92 V |
| Current | 0.0 A |
| SOC | 100 % |
| Remaining / nominal | 99.9 / 100.0 Ah |
| Cycles | 1 |
| Charge and discharge MOSFETs | Both on, from `0x8D` |
| Problem code | 0, from `0x8D` |

| `0x92` field | Value |
|---|---|
| Software version | `WT30_10004SW14_L_01` |
| Manufacturer | `11112222333344445555` |
| Serial number | `IKKKK0000AII00000000` |

**The manufacturer and serial number are unprogrammed placeholder patterns.** Do not key
anything off them. In particular, a serial number cannot tell two packs apart.

## 10. Not verified

Each item below is open. The Decision Register holds the status of the measurements it
names.

- **The sign of the pack current: measurement M7, register item W6.** The pack has only
  been seen at rest, where the current field reads `0x4000`. That value is the discharge
  flag with zero magnitude, so a resting pack cannot tell charge from discharge.
  `lib/bms-ble/` records the raw flag in `BmsData::discharging` and applies the assumed
  convention in `current_ma`. The status schema's `pack_ma` carries `TODO(W6)` for the
  same reason. Capture `0x8C` once under charge and once under load at GL5.
- **`0x8D` is only partly understood.** The leading `04 … 04 …` mirrors the cell and
  temperature counts. `0629` sits where the MOSFET and status bits appear to live, and in
  this capture both MOSFETs were on and the problem code was 0. Mapping the bitmaps needs a
  capture taken during a real protection event. `lib/bms-ble/` frames and validates `0x8D`
  but does not decode it, because a guessed bit map would raise confident and wrong
  alarms. The PoC proposed treating any nonzero byte in the alarm region as "raise it to
  HA". The status schema's `bms_alarms` passes the field through unmodified (spec §7.2.3).
- **Which temperature sensor is which.** The ambient, MOSFET and cell labels in §9 are
  `aiobmsble`'s. Nothing on this pack has confirmed them, so `BmsData::max_temp_dc()`
  reports the hottest sensor rather than a named one.
- **BLE RSSI at the mounting position: measurement M23**, which supersedes M5 (D28). The
  PoC's desk readings were −77 to −88 dBm, and −80 to −84 dBm at close range. It read −60
  to −65 dBm at the approximate mounting position. A Heltec V3
  inside the gate's steel enclosure read −50 to −60 dBm. GateLink Impl Plan §4.3 records
  why the figures differ and how M23 samples the cavity.
- **A second TDT unit.** Everything here comes from one pack. The `0x7E` request head, the
  MTU behaviour and the handshake timing might differ on another unit or firmware version.
  The software version above identifies this one.

## 11. How this was found

Black-box probing found nothing across about thirty combinations of characteristic,
protocol, write mode and timing, because the handshake target was never one of the
variables. **Instrumenting the known-good implementation found it.** The PoC patched
`BleakClient.write_gatt_char`, `read_gatt_char` and `start_notify` to log every call with
its arguments and timing, then ran `aiobmsble` through them. The answer was in the first
eight lines of output.

The PoC recorded four lessons for the other nodes:

- **Service topology is a weak signal.** JBD, Daly and TDT all use `FFF0`/`FFF1`/`FFF2`,
  with protocols that cannot read each other. Never infer a protocol from a UUID.
- **A successful ATT write proves nothing.** Twenty writes were acknowledged at the
  transport layer and ignored by the application.
- **Look for a community implementation before reverse engineering.** One search found a
  maintained library covering this battery, and doing it first would have saved the whole
  probing exercise.
- **A phone app is a poor exploration tool.** The iOS nRF workflow cannot act inside the
  3.9 s window, and its descriptor listing was wrong.

## 12. Reference implementation and tools

**`aiobmsble` is the oracle.** When the C++ decode and the pack disagree, run `aiobmsble`
against the same pack minutes apart and compare field by field. A firmware defect then shows
up as a disagreement, not as a plausible wrong number. It works from a laptop beside the
enclosure in the field too.

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install aiobmsble aiooui     # aiooui is needed by the CLI entry point
aiobmsble -v                     # find and dump every reachable BMS
```

`wattcycle-reader/tools/` keeps the PoC's probes. `bms_probe_v1_0.py` is an independent
Python implementation of §3 to §7: `--raw` dumps each frame's bytes, and `--selftest`
decodes the §9 frames with no hardware. It is the tool for the M7 capture, because
`aiobmsble` decodes the current but does not show its raw bytes. `instrument.py` logs
every BLE call `aiobmsble` makes, with its arguments and timing, and is how §3 was found.
`scan.py` lists every advertiser in range with its RSSI and service UUIDs.

## Changelog

- **v0.1** (2026-10-02) — Lifted out of `wattcycle-reader-poc_3` §4–§5 and §10.3 by
  GateLink task L2, with every unverified item kept. Adds the full `0x92` capture from the
  raw log, the MTU that NimBLE negotiated on both boards, and the open question of which
  temperature sensor is which.
