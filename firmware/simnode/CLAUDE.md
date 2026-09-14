# `lran-simnode` — nodes `0xF0`–`0xF3`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to the simnode.

**Primary document:** `docs/bridge/LRAN-Bridge_Node-Implementation-Plan` v0.26 §10.
**Tasks:** `docs/bridge/LRAN-Bridge-Firmware-Tasks` v0.14 §4 (BF-2 to BF-9).
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.11** (`ver = 2`).
**Driver:** RadioLib, version pinned in `platformio.ini` (**D32**).
**Prose:** root `## Writing` — use the `nbj-write-clearly` skill. The target-specific
trap: **console commands, fault names, role names and schema IDs are exact tokens.**
`ROLE_GATELINK`, `DEBUG_SYNTHETIC`, `0xFE` and every `simctl` verb are typed by an
operator or a script and must match the firmware character for character. Never
paraphrase one for readability, in documents or in `--help` text.

## What this is

The fleet's test instrument. It is the only thing that exercises the bridge's receive
path, registry, scheduler, command retry and error handling **before GateLink exists** —
and the only thing that will exercise them **after GateLink is installed and unreachable
without a walk to the gate**. Treat it as permanent infrastructure, not scaffolding.

It is a protocol-level peer. It has no gate state machine, no relays, no I/O, and its
telemetry values are plausible rather than physical.

## What exists here today

**BF-2, BF-3, BF-5 and the core of BF-4, built 2026-09-14 and proven on air between two
Heltecs** (bridge engineering log, same date). `platformio.ini` (`simnode-heltec`,
`simnode-xiao-wio`, `native`), `profiles.h`, `identity.{h,cpp}`, `node.{h,cpp}`,
`console.{h,cpp}`, `radio.{h,cpp}` (**the only file that includes RadioLib**), `main.cpp`
(**the only file that includes `secrets.h`**, for `LRAN_MASTER_KEY` alone) and `sink.h`.

**`/lib/lran-sim/` (BF-7)** is built: `FramePatch`, Impl Plan §10.5.2. **The fault catalogue
and `fault` command (BF-8)** are built in `fault.{h,cpp}`, host-tested against the codec's
receive ladder. **Not yet:** `ROLE_GATELINK` and `push`/`event`/`ack`/`field` (**BF-6**), and
the command-path faults that need them; **self-disarm's OLED display (BF-9)** — the count-and-
disarm logic is in the injector, but nothing draws the armed state.
Those commands answer `ERR not implemented` and name their task. **The XIAO profile builds
and has not been flashed.**

```bash
pio test -d firmware/simnode -e native              # host, no secrets
pio run  -d firmware/simnode -e simnode-heltec      # NEEDS secrets.h
pio run  -d firmware/simnode -e simnode-xiao-wio
```

**Spec §12.3 media access and the PHY constants are `lib/lran-link/`'s**, shared with the
bridge. Change them there, and run both firmwares' tests.

### Traps found building it

- **Two Heltecs boot with the same identities**, `f0 ROLE_RANGE` and `f2 ROLE_HEALTH`.
  Nothing persists, so both answer to `f0` until one is reconfigured, and every reset
  restores the defaults. Opening the serial port resets the board.
- **`stats` counts frames an identity queued; `radio` counts `TX_DONE`.** Only `radio` shows
  a frame reached the air.
- **`ping` takes `to <hex>`**, an addition to Impl Plan §10.4 that lets two simnodes echo
  each other. The destination defaults to `00`, and **the bridge does not answer PING yet**
  (spec §17.3 gap, no task assigned), so a ping to `00` reports no echo.
- **Every identity decodes every frame.** A PING to `f1` raises `rx_not_addressed`, and so
  `rx_dropped`, on every other identity on the board. That is what four boards would count.

## Hardware profiles — build environments, not roles

| Env | Board | Typical use |
|---|---|---|
| `simnode-heltec` | Heltec V3 | `ROLE_FAULT`, `ROLE_HEALTH` at `0xF0`/`0xF2` |
| `simnode-xiao-wio` | XIAO ESP32S3 + Wio-SX1262 **Kit** (B2B) | `ROLE_GATELINK` at `0xF1`. **The same SX1262 module family as GateLink — but NOT the carrier's pad wiring.** See the pin maps below |

A profile supplies exactly one thing: a `RadioPins` struct (NSS, RST, BUSY, DIO1, SPI pins,
TCXO voltage, DIO2-as-RF-switch, optional RF_SW). Everything above the driver is identical.
**If the pin map turns out not to be cleanly injectable, that is a finding about R-4.1b —
report it rather than working around it with `#ifdef`s in the role code.**

Roles are runtime, assigned per identity. Profiles are compile-time. Do not mix them.

### Pin maps — Impl Plan §10.8.1 is authoritative

**TWO XIAO MAPS, AND ONLY ONE OF THEM IS THE HARDWARE IN HAND.** Seeed sells two
Wio-SX1262 products that are not pin-compatible outside the three SPI nets. Confirmed
2026-09-05; see `docs/rangetest/LRAN-Range-Test-Firmware-Pass2-Tasks.md` §2.1.

```c
// LRAN_PROFILE_HELTEC   — Heltec WiFi LoRa 32 V3
  nss=8  rst=12 busy=13 dio1=14  sck=9 miso=11 mosi=10
  rf_sw=RADIOLIB_NC  tcxo_mv=1800  dio2_as_rf_switch=true

// LRAN_PROFILE_XIAO_WIO_KIT  — "Wio-SX1262 with XIAO ESP32S3" (p-5982), B2B connector
//   THE BOARD IN HAND. Control lines cross the B2B connector, which is why they are
//   GPIO 38-42 and not D-pad numbers. Transcribed from meshtastic/firmware
//   variants/esp32s3/seeed_xiao_s3/variant.h.
  nss=41 rst=42 busy=40 dio1=39  sck=7 miso=8  mosi=9
  rf_sw=38           tcxo_mv=1800  dio2_as_rf_switch=true

// TCXO in millivolts, not a float: the range test's board_config.h, the bridge and
//   lib/lran-link's RadioPins all settled on uint16_t tcxo_mv.

// The OTHER Wio product — "Wio-SX1262 for XIAO" (p-6379), 2.54 mm headers — is
//   GateLink's module, NOT the board in hand, and its map is deliberately not
//   duplicated here. It lives in gatelink-expansion-board.md 6.1, which owns it.
//   Different GPIO entirely; do not reach for it by memory.
```

**`rf_sw` is a real pin on both XIAO profiles, not `RADIOLIB_NC`.** Seeed does not tie
DIO2 to the RF switch internally, so the Wio needs **both** `setRfSwitchPins(rf_sw,
RADIOLIB_NC)` **and** `dio2_as_rf_switch`. This closes Impl Plan §2.3.1 finding 1
positively and confirms `gatelink-expansion-board.md` §7.3 as written.

Three rules around these:

- **`kRadio` is passed to the radio wrapper's constructor.** It never reaches the driver
  through the preprocessor. GateLink later supplies a third instance with no driver change
  — that is the whole of R-4.1b, and a `#define`d pin map does not test it.
- **The struct shape is identical across profiles.** Heltec carries
  `rf_sw = RADIOLIB_NC` rather than omitting the field.
- **TCXO is 1.8 V on both boards but is still per-profile.** It is not a shared constant
  in `/lib/lran-protocol/`. Wrong TCXO voltage presents as a radio that will not
  calibrate, never as a clear error.

**The D-pad numbering is now transcribed**, from the vendor board definition at the
framework version this repo pins (`espressif32@6.13.0`):
`variants/XIAO_ESP32S3/pins_arduino.h`, D0–D10 = GPIO 1, 2, 3, 4, 5, 6, 43, 44, 7, 8, 9.
That caveat is retired.

**§2.3.1 finding 2 is closed, negatively, and it matters here more than anywhere.** The
kit that arrived is the **B2B variant**, not the carrier's module. This file used to warn
that a different variant would make this profile "stop validating GateLink's radio while
still working perfectly" — **that is now the situation**, so read it as a statement rather
than a risk:

> `simnode-xiao-wio` on the Kit validates the SX1262, the module's RF performance,
> RadioLib on a second board, and the injected-config seam. **It does not validate the
> carrier's net list.** XIAO validates the module; only the carrier validates the carrier.

The carrier's pad assignment did gain an *independent corroboration* — but two agreeing
derivations are not a continuity check, and **the Kit cannot supply one**, because it does
not use those pads. That map, its corroboration and its outstanding ring-out all live in
**`gatelink-expansion-board.md` §6.1 and §10**, which own them. Nothing in this directory
should carry a second copy.

## Multi-identity

Up to four logical identities per board, each with its own `node_id`, HKDF-derived key,
`ctx_id`, sequence spaces, announced `ver` and enabled flag. The radio is shared and
serialized internally.

**From the bridge's side this must be indistinguishable from four physical nodes.** If it
isn't, something in the bridge is keyed on the radio rather than on `node_id` — that is a
bridge defect and worth reporting, not compensating for here.

The simnode holds `master_key` for bench convenience. **A real node never does** — it is
flashed with only its own derived key.

## Rules specific to this target

1. **Every emitted payload is marked synthetic.** `status_reason = DEBUG_SYNTHETIC`, and
   schema `0xFE` rather than `0x10` for status. **Schema `0xF0` has no `status_reason`**, so
   every `0xF0` sets `health_flags` bit 0 instead (spec §7.5, found 2026-09-14). Synthetic
   data reaching HA history unmarked is a bug in two nodes at once, and it fails silently —
   it looks like real history until someone tries to explain a reading.
2. **`/lib/lran-sim/` builds malformed frames by post-processing a correct frame from
   `/lib/lran-protocol/`.** Never write a second serializer. A separate one drifts, and
   then a fault test passes while testing a frame the system would never produce.
3. **Every fault arms for a bounded count and self-disarms.** A simnode left in a fault
   mode looks exactly like a broken bridge, and the session where that costs an hour is the
   one where you were debugging something else. Show armed faults on the OLED.
4. **No scenario requires a reflash.** Everything is driven from the serial console and
   scriptable from `/tools/simctl/`. If a test needs a rebuild, the console is missing a
   command — add it.

## Roles

`ROLE_RANGE` (PING echo + `0xF0`) · `ROLE_HEALTH` (`0xF0` only) · `ROLE_GATELINK`
(`0xFE` status, `0x11` events, ACKs, `0x12` config) · `ROLE_FAULT` (§10.5 catalogue).

`ROLE_RANGE` is deliberately impoverished: during a range test, every line of protocol
logic in the way can produce a symptom indistinguishable from poor link margin.

## Fault catalogue

Impl Plan §10.5's entries (27, below, plus §10.5.1's two) each map to one Protocol Spec §14
stage or §9.4/§10 rule. This is the only mechanism that produces these frames — without it
every discard counter in the bridge ships unverified.

**`bad_phy_crc` cannot be injected.** The SX1262 computes the PHY CRC in hardware, so no
transmitter can emit a frame that fails it. §14 stage 1 is verified only at the far edge of
a real link during the range walk.

The entries most likely to be skipped by hand are the ones whose correct result is that
**nothing happens** — `hdr_rsv` accepted, `seq_wrap` accepted, `wrong_dst` discarded with
no `ERROR`. Those are the forward-compatibility rules, and they break quietly. Commit them
as `simctl` scripts so a regression run is one command.

## The fault catalogue is 27 entries, and 6 of them are new

Impl Plan §10.5 was written against spec v0.3 and has been brought up to the current
receive path. The six added entries are the ones nothing has ever produced:
`oversize` (stage 2a), `frag_zero` (5b), `frag_command` (8a), `set_displaced` (§11.3),
and the two whose correct result is that **`rx_dropped` does not move** — `frag_dup` and
`frag_late`.

**`single_frame_interleave` is the one to build first.** It is the only test of spec
v0.6's sole behavioural change (§11.2: a single frame never begins, joins, displaces or
expires a set), the simnode is the only thing that can produce the sequence, and the
defect it catches — a node's periodic `STATUS` destroying that node's in-progress
fragmented `CONFIG_ACK` on the bridge — is **silent by construction**. Expected result is
a set that completes and a counter that stays still.

**§10.5's counter column comes from Protocol Spec §14.1**, not from the table. A row whose
counter is not in `kCounterRegistry` is a defect in the table — report it rather than
adding a name.

**The catalogue is armed through `fault <hex> <name> [count] [gap <ms>] [to <hex>] [ctx
<hex32>]` (BF-8).** Each malformed frame is a real `0xF0` health status put through
`FramePatch`. `silent` withholds answers rather than sending. `fault list` prints the table
with each row's counter; `fault <hex> off` disarms. A fault fires its first injection on arm
and the rest as the outbox drains, then self-disarms — **the count-and-disarm half of BF-9 is
here; only the OLED is left.** `test/test_fault` asserts each fault moves the §14 counter its
row names, feeding the frames through the codec's own ladder.

**§10.5.2: `/lib/lran-sim/`'s patch-after-encode primitive exists (BF-7).** `oversize`,
`frag_zero` and `frag_command` all need a frame `encode()` refuses to emit. Build every
malformed frame with `lran::sim::FramePatch`: encode, patch, `seal()`. §10.5.2 maps each
fault to its operation. Two things to know when BF-8 uses it:

- **`frame()` is `nullptr` after any patch until `seal()` runs.** Check it before queuing.
- **`Seal::MacAndCrc` re-signs, and so repairs a `flip_mac()`.** `bad_mac` seals with
  `Seal::Crc`. A patch to an authenticated frame that must still verify seals with
  `Seal::MacAndCrc`.

If a fault needs a byte that `FramePatch` cannot reach, add an operation there, with a test
against `decode_header()` or a W4 vector. Do not build the frame by hand.

## Two faults arrive with P8, and they point the other way

**D34** puts `CommandGate` (spec §9.4 steps 4–5) in `/lib/lran-protocol/`, and **P8 gates
B0** alongside P6. Per §9.2 every authenticated type is bridge → node, so `cmd_replay`
and `cmd_stale_seq` (Impl Plan §10.5.1) test **this node's own gate**, driven from
`simctl`. `cmd_replay` is the one that matters: `ack_suppress` already exercises dedup
from the bridge's side, but `cmd_replay` asserts it **at the end that pulses a relay**,
which is what root rule 2 and **BS-3** are about. A second pulse at a driveway gate is the
failure the whole mechanism exists to prevent, and it has never been tested where it
happens.

## Milestone

**B0** — flashes and runs; four identities with independent keys, contexts and sequence
spaces; console accepts every command; `ROLE_RANGE` echoes `PING`; faults arm, fire the
specified count, self-disarm, and show armed state on the OLED.

B0 depends on protocol library **P6** (W4 vectors committed). This ordering is
deliberate: a simnode validated only against the bridge is a mirror, not an instrument.
