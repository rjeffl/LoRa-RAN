# Range test — traps

**Each entry here cost real time on the range-test bench or in the field.** Most present as
a different fault from the one they are. The engineering log has the full account of each.
These sections were in the 2026-09-09 handoff, and moved here unchanged on 2026-09-23 so
the handoff stays short. "Section above" in an entry points to a section in this file.

**This file is kept current, unlike the log.** Add a trap when one costs an hour.

## A third board on the air

> ### Power down every board you are not measuring with
>
> An idle ARMED initiator **beacons once a second** on the single fixed channel. A third
> powered board cost up to **60 % PER** on 2026-09-05 and looked exactly like poor link
> margin. Unplug spares, or park one in `SURVEY` (`--role survey`) — it listens and never
> transmits. **ARMED is not idle.** Full account in `FIELD-PROCEDURE.md`. **B1b honoured
> this**: the Wio was off for the A/B sweeps and Heltec #1 was off for the walk, so no sweep
> in that capture has a third board live in it.

## The NVS collision — and it fakes a fault

**Clear the responder's position log before every run.** This is not housekeeping.

- `PositionLog` **persists to NVS** (key `poslog`) on every PRG advance and **reloads at
  boot**. A firmware upload does not touch it.
- **`g_position_id` is not persisted** (`main.cpp:115`) — every run restarts at 0 and the
  first press makes it 1.
- `PositionLog::slot_for()` (`resp_log.cpp:61`) **matches on `position_id`** and returns the
  existing entry. Only if none matches does it open a new slot.

So last run's position 1 is **added to** this run's position 1: `probes_heard` and
`echoes_sent` accumulate, and the RSSI/SNR series mix two sessions. The cross-check against
the sweep trace then disagrees, and a disagreement there is **an instrumentation fault, not a
link result**. **The failure mode is a confident diagnosis of the wrong thing.**

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbmodem1101 \
    --reset --role responder --key x --out /tmp/erase.csv --run-for 20
```

**Confirm `# position log cleared` comes back.** No line, no erase. Full procedure in
`FIELD-PROCEDURE.md`, *"Erase the bench data first"*; `--run-for`, never `--idle-timeout`.

**Do not clear an initiator that holds the survey campaign.** The campaign lives under
`surv*`/`survsite` in the same `lran-rt` namespace; nothing in the initiator path reads it
during a walk, and `z` would destroy it for nothing.

**All four of B1b's responder rows closed at exactly 192/192**, so nothing accumulated in
that run.

## The rest

- **A responder swapped in mid-capture sweeps with no press**, at position 0, wherever it is
  being carried. Reset the initiator. **New 2026-09-09**, and it cost a sweep.
- **Two uncontrolled numbers agreeing is not evidence.** B1b's A/B was a same-hour
  substitution at one mount, and it was talked out of its own result by a five-day-old reading
  whose indoor end was specified only as "the office on the NW side" — where multipath moves
  more than the 8.6 dB in question. **Weight the tighter experiment, and check what the looser
  one actually pinned down before using it as a control.** **New 2026-09-09**; it cost a wrong
  conclusion that stood for one commit.
- **Write down the mount, not just the position.** "0.8 m AGL on the back of a concrete
  column" and "on top of the gate controller enclosure" describe one spot and read as two. The
  A/B was nearly discarded over the wording. **New 2026-09-09.**
- **A stale responder position log merges into the new run** and fakes an instrumentation
  fault. Clear it, and confirm the line. Section above.
- **A `--note` template placeholder is a silent defect**, and so is a note clause the run then
  falsifies. Twice now: §7.6's `<H>m AGL on <stands>`, and B1b's position descriptions plus
  "Third Heltec POWERED DOWN" on a run that carried it to the gate. `capture.py` cannot tell a
  placeholder from a value and nothing downstream checks.
- **A third powered board corrupts a two-board measurement**, silently, by up to 60 % PER, and
  the symptom is indistinguishable from poor link margin. It was mistaken for a code
  regression across two full sweeps on 2026-09-05. **A ten-minute bisect against the previous
  firmware settles this class of question — reach for it before asserting a regression.**
- **A reflash session is a period during which every board transmits.** A flashed board boots
  ARMED, and esptool's hard reset *is* a boot. Park or unplug each board as it finishes.
- **Bench geometry moves RSSI further than anything you are trying to measure.** The Heltec
  reference moved **−24 → −42 dBm between two runs on placement alone**, and 2026-09-07's desk
  throwaway wandered 24 dB between two sweeps at *identical* placement. That is why §7.6 wants
  a tape measure, three distances and a slope rather than one number.
- **Opening a serial port presses PRG — on the Heltec.** GPIO 0 is also IO0, driven by the
  CP2102's DTR. Host tools must set `dtr = False` **before** opening. The XIAO has no bridge
  chip and its button is GPIO 21, so this trap is Heltec-only.
- **Flashing the XIAO from a firmware with a different USB stack fails once.** Bootloader entry
  swaps the USB device, esptool loses its handle, `Could not configure port`. The board *is* in
  the bootloader — on a **new** device node. Flash to that.
- **A tool that does not read the port while it waits loses everything the board says.** Cost
  19184 bytes of a survey dump, deterministically, and read as a firmware bug.
- **`capture.py` will not overwrite an existing `--out`** (needs `--force`).
- **Completion markers match as substrings anywhere in a `#` line.** Careless wording of a new
  firmware message truncates captures.
- **A boot line that is not `key=value` never reaches the trace.** `capture.py` collects
  `^[a-z][a-z0-9_]*=\S*$` into the header and counts everything else as `unparsed`. One space
  in one value and that line is silently absent.
- **`--idle-timeout` cannot end a run against a board that never goes quiet.** Use `--run-for`
  for setup commands.
- **Never ask RadioLib's `getPacketLength()` whether a packet arrived**, and never use
  `getRSSI()` without `false` for an ambient reading. Both hold stale values.
- **The responder must follow the initiator's retune**, or a clean 100 % PER looks real.
- **TCXO 1.8 V, `setDio2AsRfSwitch(true)`, and — on the Wio — a real `rf_sw` pin** all fail
  *silently*. The radio initialises, reports a successful transmit, and puts nothing on the
  air. `begin()` returning success proves none of them; only frames crossing does.
- **Both CP2102 bridges report `SER=0001`.** Trust the confirmation from the command that did
  the work, not a separate check afterwards.
- **A git hang is not a network failure.** Private repo plus Plasma's
  `SSH_ASKPASS_REQUIRE=prefer` sends the credential prompt to a GUI dialog and blocks forever;
  `GIT_TERMINAL_PROMPT=0` does not help, because it disables the *terminal* prompt and not
  askpass. `curl` against the same host separates transport from authentication.
- **Never transmit without an antenna connected.** It is also one of the failures the §7.6
  check exists to detect — detect it in the trace, not by damaging a PA.
- **The OLED needs hand-shading in direct sunlight.** Procedure, not a defect.
- **SF12 reads ~7 dB lower SNR than SF7 at the same RSSI**, on every trace back to
  2026-08-31. It is a property of the estimator, not of the link, and it means SF12 margin
  figures are understated. Do not chase it as a fault.
