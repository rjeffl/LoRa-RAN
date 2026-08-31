# Range test — session handoff

**Written 2026-08-31, at the end of the `range/sweep` session.**

> **This file goes stale.** It records *session state and next actions*, nothing else.
> Where it disagrees with the documents below, they win — check the engineering log's
> last entry against the date above before trusting anything here.
>
> | Authority | Document |
> |---|---|
> | What to build, and the branch plan | [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) |
> | What happened and why | [`engineering-log.md`](./engineering-log.md) — read the last few entries first |
> | Firmware-specific rules and gotchas | [`/firmware/range-test/CLAUDE.md`](../../firmware/range-test/CLAUDE.md) |
> | Repo-wide invariants | [`/CLAUDE.md`](../../CLAUDE.md) — **authoritative, conflicts resolve here** |
> | Trace schema | [`data/README.md`](./data/README.md) |

## Where things stand

| | |
|---|---|
| Branch | `range/sweep` — **pushed, PR #13 open, NOT merged** |
| Merged so far | #12 (`range/skeleton`, R1–R3), #11 (gitignore) |
| Working tree | clean |
| Done | **R1–R7.** Both branch gates passed on hardware |
| Not started | **R8** (`range/survey`), **R9** (`range/w9`), R10 (fieldwork, not code) |

## First actions next session

1. **Merge PR #13** (needs a human — the permission classifier blocks `gh pr merge` here).
2. `git checkout main && git pull --ff-only`
3. Confirm `main` is green before branching from it:

```bash
pio test -d firmware/range-test -e native   # expect 99 passed
pio run  -d firmware/range-test -e heltec   # expect SUCCESS
pio test -d lib/lran-protocol -e native     # expect 107 passed
python3 tools/vectors/check.py              # expect 72 vectors OK
```

4. `git checkout -b range/survey` and start **R8**.

`pio` is at `~/.platformio/penv/bin/pio` and is not on `PATH`.

## Hardware state

Two Heltec V3 boards, both flashed with the R7 firmware and working.

```
/dev/cu.usbserial-0001    LOCATION=0-1
/dev/cu.usbserial-3       LOCATION=2-1
```

**Both CP2102 bridges report `SER=0001`.** The boards are *not* distinguishable by USB
serial — only by enumerated device node, which is not stable across replug. Do not write
a port name into anything durable. The OLED badge (`INIT` / `RESP`) is the reliable
identifier.

**Role selection:** reset, then within 3 s either press PRG (→ `RESPONDER`) or send a
serial character. Bench aids, not the field flow:

| Console | Key | Effect |
|---|---|---|
| either | `i` / `r` | select role during the boot window |
| responder | `p` | increment position (same as PRG) |
| responder | `d` / `x` | dump / clear the stored position log |
| initiator | `s` | force the next sweep while armed |
| initiator | `n` | advance the position locally |

A full sweep is 24 test points, ~6–7 minutes. Capture a trace with:

```bash
python3 tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --out docs/rangetest/data/YYYY-MM-DD-<place>.csv --sweeps 1 --note "..."
```

**Start `capture.py` *before* resetting the initiator** — it needs the CSV header, which
is printed once at boot, and it discards data rows seen before it.

## R8 — what it actually asks for

Ambient RSSI survey, **required by §12.1 before D1 may fix a frequency**. This is
**M20**, and it also gates D33's third standing condition.

- Scan **902–928 MHz in 200 kHz steps** — 130 bins. The step matches the LoRaWAN channel
  grid so a LoRaWAN-shaped occupancy pattern is recognisable on sight.
- Record **peak and mean RSSI per bin**, over repeated passes lasting **several
  minutes**. The site's known occupants are slow-cadence temperature sensors; a sweep
  spending a second per bin will see nothing and wrongly conclude the band is empty.
- Run at **both** the bridge location **and** the most distant node location, and keep
  both traces. §12.1 requires both because they will not look alike, and it is the
  node's noise floor that sets its margin.
- CSV out, same directory as R7 — the schema will need extending or a second one.

Add it as a **third mode on the initiator binary**, per the task text.

Known occupants worth recognising: four YoLink temperature sensors plus a switch, talking
to a hub with a Semtech SX1276. Whether they are LoRaWAN band-plan or proprietary is
**unconfirmed** — M20 settles it empirically.

## Things that will cost you time if rediscovered

All are in the engineering log with full context; this is the index.

- **Never ask RadioLib's `getPacketLength()` whether a packet arrived.** It holds the
  *last* length and is not cleared by reading, so a poll built on it re-reports one
  buffered frame forever. Gate on the DIO1 interrupt. **This applies to any firmware in
  this repo**, not just here.
- **The responder must follow the initiator's retune.** Two radios on different SFs
  cannot hear each other at all, and the failure reads as a clean 100% PER — which at
  500 ft is indistinguishable from a real result.
- **Both ends must agree on what counts.** Warmup probes and armed beacons are marked
  `BenchKind::WarmupProbe` on the wire precisely because a local exclusion is not enough
  when both ends tally.
- **The Heltec vendor variant calls GPIO 14 `DIO0`.** On the SX1262 that line is DIO1.
  The number is right; the label is legacy.
- **PRG is GPIO 0, the BOOT strapping pin.** It cannot be held through reset — that
  enters the ROM downloader. Role selection is a post-boot window for this reason.
- **TCXO 1.8 V and `setDio2AsRfSwitch(true)`** both fail *silently* on this board: the
  radio reports success and transmits nothing.
- **The OLED needs hand-shading in direct sunlight.** Panel limit, not a layout one.

## Open, and not closable from this firmware alone

- **D1** — needs M20 (R8) for the frequency and **M21** for the power figure.
- **M6** — range and RSSI at ~500 ft on both bearings. **Untouched.** Everything measured
  so far is a ~1 m bench link, including the one committed trace.
- **W7** — the §15.1 airtime table regenerates once D1 fixes SF/BW/CR. The instrument
  exists: `firmware/range-test/src/airtime.cpp` reproduces all 21 of the current table's
  figures exactly, asserted in `test/test_airtime/`.
- **W9** — R9's 222-byte and fragmented `PING` runs. **R9 is the first work here that
  links `/lib/lran-protocol/`**; R4–R8 use raw bench frames and touch no shared codec.
- **D31** — copyright holder. Every file carries the `<holder>` placeholder.
