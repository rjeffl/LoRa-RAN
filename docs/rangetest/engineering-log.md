# `firmware/range-test/` — engineering log

Dated entries. Measurements, surprises, and things that cost an hour.

Binding specification: [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md)
v0.7 (`ver = 2`). Tasks: [`LRAN-Range-Test-Firmware-Pass1-Tasks`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md).

## What this firmware is answering

| Item | Where it is tracked |
|---|---|
| **D1** — SF / BW / CR / TX power | Decision Register §2.1. **Bounded, not free:** TX power is capped by D33 and the frequency waits on M20 |
| **M6** — range and RSSI at ~500 ft **on both bearings** | Decision Register §5.1 |
| **M20** — ambient RSSI sweep of 902–928 MHz at **both** the bridge and the far node | Decision Register §5.1. Blocks D1's frequency and D33's third standing condition |
| **W9** — full-size (222 B) and fragmented `PING` over the air | Protocol Spec §18, §6.6.1, §6.6.2 |
| **§14 stage 1** — PHY CRC failures | The one discard path that cannot be produced at a desk; observe it at the far edge of the walk |

## Things to record every time, or the number is not reusable

**Conducted TX power and antenna gain separately** — D33's ceiling is EIRP, and a single
combined figure cannot be audited later. Also: antenna height at both ends, bearing,
weather, foliage state, and the date. A dry-February path and a wet-July path are not the
same path.

---

<!-- Entries below, newest last. -->

## 2026-08-31 — R1–R3, `range/skeleton`: project stands up and builds

`firmware/range-test/` created as a standalone PlatformIO project. Target builds clean
under `-Wall -Wextra -Werror`; 17 host tests pass in `native`. **No hardware yet** — every
acceptance criterion that needs two boards is untested. See "what is not verified" below.

### The Heltec V3 pin map is now *confirmed*, not *derived*

R2 requires the pin numbers come from the board schematic or the vendor board definition,
not from memory or a forum post. Transcribed from

```
~/.platformio/packages/framework-arduinoespressif32/
    variants/heltec_wifi_lora_32_V3/pins_arduino.h
```

at framework version `3.20017.241212+sha.dcc1105b`, which is what `espressif32@6.13.0`
resolves:

| Function | GPIO | Vendor variant symbol |
|---|---|---|
| NSS | 8 | `SS` |
| SCK | 9 | `SCK` |
| MISO | 11 | `MISO` |
| MOSI | 10 | `MOSI` |
| RST | 12 | `RST_LoRa` |
| BUSY | 13 | `BUSY_LoRa` |
| DIO1 | 14 | **`DIO0`** — see below |

**These agree, value for value, with Bridge Impl Plan §10.8.1's `LRAN_PROFILE_HELTEC`
entry.** That section describes both its maps as "derived, not transcribed from a vendor
pin table", and the Heltec column specifically as "the community-standard V3 assignment".
The Heltec column can now be described as **confirmed against the vendor variant**; the
XIAO column still carries §10.8.1's own instruction to ring it out against the module on
arrival.

**A naming trap worth an hour of somebody's time.** The vendor variant calls GPIO 14
`DIO0`. That is the SX127x name; on the SX1262 the line is DIO1, and DIO1 is what RadioLib
wants as its IRQ pin. The *number* is right and the *label* is legacy. Anyone who trusts
the symbol over the number will go looking for a different GPIO and not find one.

### R1's "hold PRG at boot" cannot work on this board

R1 specifies role selection by holding the PRG button at boot. PRG on the V3 is GPIO 0 —
the ESP32-S3 BOOT strapping pin. **GPIO 0 held low through reset puts the chip into the
ROM serial downloader**, so the application never runs and never reads the button. As
written, the instruction selects "download mode", not "responder".

Implemented instead as a **3-second selection window immediately after the application
starts**: OLED countdown, press PRG for `RESPONDER`, no press for `INITIATOR`. Every
property R1 actually asked for survives — one binary, two roles, not persisted, role shown
on the OLED, no laptop needed at the walking end.

Arguably better in the field: the walking unit is chosen by a deliberate press with the
display confirming it, rather than by a hold whose effect is invisible until the radio
does or does not start. `INITIATOR` is the no-press default because it is the tethered
end, so a wrong default is visible on the laptop that is already there.

**The GPIO 0 assignment itself is not from the vendor variant** — `pins_arduino.h` stops
at the LoRa and OLED pins and declares no button. It is the ESP32-S3 BOOT strapping pin
and the button wired to it on this board. `TODO(R2)` in `src/role.h`: confirm against the
V3 schematic at first bring-up and record it here.

### The D33 clamp is integer arithmetic, and host-tested

Task guardrail 3 puts the power cap in code rather than in operator discipline. Three
decisions worth recording:

- **Tenths of a dB as `int16_t`, no floats.** The clamp is compared and printed; a value
  that displays as `-3.0` and compares unequal to `-3.0f` is a debugging session nobody
  needs.
- **Rounds toward −∞, not toward zero.** C++ integer division truncates: `-35 / 10` is
  `-3` where the floor is `-4`. With a 2.5 dBi antenna the conducted ceiling is −3.5 dBm,
  and truncation would authorise −3 dBm — **0.5 dB above the D33 ceiling**. There is a
  host test named for exactly this.
- **A ceiling below the SX1262's −9 dBm minimum is a refusal, not a floor.** Past about
  8 dBi of antenna gain the EIRP ceiling drops under what the radio can emit. The firmware
  reports `BelowRadioFloor` and does not transmit, rather than quietly using −9 dBm and
  producing a log that cannot be audited.

Conducted power and antenna gain travel together in one `PowerPoint` struct so they cannot
drift apart between the clamp and the CSV (D33 standing condition 1).

Spec §18.1's worked example — 2 dBi antenna, roughly −3 dBm conducted — is a host test
rather than a comment.

### Provisional, and labelled as such in the code

`915.0 MHz / SF7 / CR 4:5` are **placeholders for getting two boards to talk**, not a
choice. D1 is open and bounded; §12.1 forbids fixing a frequency before M20's ambient
survey has run at both ends, which is R8 and has not happened.

### What is *not* verified

Everything requiring hardware. Specifically **R2's acceptance criterion is unmet**: no
packet has crossed the bench, and no RSSI has been observed at 1 m. R3's settings dump is
host-tested for content but has never been printed by a real boot. The radio bring-up path
— TCXO, `setDio2AsRfSwitch`, `begin()` return codes — is written and compiled but has
never run on an SX1262.

`range/skeleton`'s gate is "both boards flash, radio inits, link established at one test
point". **That gate is not passed.**
