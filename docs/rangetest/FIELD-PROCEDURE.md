# Range test — field setup and procedure

**Written 2026-09-03.** Covers the two field jobs the `range-test` firmware exists to do:

| Job | Task | Answers | Roles used |
|---|---|---|---|
| **A. Position walk** | R10 fieldwork, the R4–R7 sweep | **M6** — range and RSSI at the target locations | INITIATOR + RESPONDER |
| **B. Ambient survey** | **R8** | **M20** — what else is on the band, at every site | SURVEY (one board) |

> Authority: [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md)
> for what to build, [`engineering-log.md`](./engineering-log.md) for why things are the
> way they are, [`data/README.md`](./data/README.md) for the trace schemas.
>
> **Do not close D1 from this fieldwork.** The frequency needs job B (**M20**); the power
> figure needs the modules' FCC grant conditions (**M21**), which is not a field task.

---

## Target locations

Seven, in the order a loop of the property visits them. The survey stores one run per
site under these names, so they are also the `site_name` column in the trace.

| # | Site | `site_name` | What is or will be there |
|---:|---|---|---|
| 0 | Bridge, in the house | `bridge-house` | The bridge. §12.1 requires a survey here |
| 1 | Gate controller | `gatelink-gate` | **GateLink** — the only node with a date on it |
| 2 | Front island | `weather-island` | Existing weather station |
| 3 | Well | `welllink-well` | **WellLink** |
| 4 | Irrigation pump | `irrigation-pump` | Future pump control and monitoring |
| 5 | Lower hop yard | `hopyard-lower` | Future remote weather station |
| 6 | Propane tank | `propane-tank` | Future propane level monitor |

The order is not load-bearing — walk them in whatever order the property makes easy. What
matters is that **the site shown on the OLED is the site you are standing at** when you
press PRG, because that is the label the measurement is filed under, and a run stored at
the well under `irrigation-pump` is right numbers in the wrong place.

---

## Before you leave the house

### Kit

- Both Heltec V3 boards, **both flashed from the same commit**. They are physically
  identical and their USB bridges both report `SER=0001`; **the OLED badge is the only
  reliable identifier** — `INIT`, `RESP` or `SURV`.
- **Matched antennas.** Same stock whips at both ends. D33's conditions turn on antenna
  gain, and a swapped antenna invalidates the power figure.
- Power bank for the walking board.
- The laptop, tethered to the initiator, which stays at the house or the gate.
- Something to note antenna height and bearing on. **Both go in `--note`**, and a range
  figure without them is not a result anyone can reuse.

### Flash both boards and confirm green

```bash
~/.platformio/penv/bin/pio run -d firmware/range-test -e heltec -t upload --upload-port /dev/cu.usbserial-0001
```

`pio` is at `~/.platformio/penv/bin/pio` and is **not on `PATH`**. On macOS always
`/dev/cu.*`, never `/dev/tty.*`.

### Erase the bench data first

Both boards carry NVS state from bench work: the responder's position log and up to seven
stored surveys. **Left in place they are dumped at boot and land in your field trace.**

- Responder: boot as `RESP`, press `x` on the console.
- Survey: boot as `SURV`, press `z` on the console.

### Role selection — a 3-second window after boot, not a hold through reset

PRG is GPIO 0, the BOOT strapping pin; held through reset it enters the ROM downloader
and the application never runs. So the roles are chosen **after** the board starts, while
the OLED shows a countdown:

| Board | Do this within 3 s of reset | Badge |
|---|---|---|
| Initiator | nothing — it is the no-press default | `INIT` |
| Responder | **press PRG** (or send `r`) | `RESP` |
| Survey | send **`v`** on the console | `SURV` |

Survey has no button selector on purpose: PRG already means RESPONDER, and that is the
meaning used untethered in the field. Select `SURV` at the house, where the laptop is.

---

## Job A — the position walk

One person. The initiator stays put and logs; you carry the responder.

### Start the capture BEFORE resetting the initiator

`capture.py` needs the CSV header, which is printed once at boot, and it discards data
rows seen before it.

```bash
python3 tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --out docs/rangetest/data/2026-09-03-walk-gatelink.csv \
    --note "bearing 120deg to gate, both ends 1.2m, stock whips 2.0dBi, dry, foliage full"
```

**One capture spans the whole walk.** It runs until Ctrl-C by default and appends rows as
they arrive, so a dropped cable costs the rest of the walk and not the part already done.
Do not restart it per position.

### The position cycle

This is the part to get right. **One press, one sweep, one position.**

**1. Initiation — you press PRG on the responder.**

The responder owns the position number; it is the walking end and the only one that knows
it has moved. A press increments it locally and the new number rides out in the next
echo. The initiator learns it from there and starts a sweep — there is no other channel.

On the press you get, immediately:

- the responder's OLED showing the **new position number**, and
- `# position -> N` on its console if you happen to be tethered.

The initiator then prints `# sweep start, position N` and begins.

**2. While the sweep runs — stand still.**

A sweep is **24 test points, measured at 424 s (~7 minutes)** on the default plan.
The responder's OLED shows live RSSI, SNR, position and echo count, and the numbers keep
changing. It will also go through quiet stretches: the initiator retunes for every test
point and the responder has to hunt for each new configuration, and at SF12 one probe
period is over 8 seconds.

> **Do not walk on because it looks idle.** Moving mid-sweep puts half a sweep at one
> position under the label of another, and nothing downstream can detect it. Wait for the
> signal below — that is the entire reason it exists.

If the responder shows the **stale** display with a counting age, it has heard nothing for
12 s. That is normal between configurations. An age that climbs past a minute means you
have walked out of range, which is itself a result — note where you were standing.

**3. Completion — the responder tells you, in large letters.**

When the sweep finishes, the initiator goes **ARMED** and beacons once a second. The
beacon is its own frame type precisely so the walking end can tell "sweep finished" from
"a configuration change is in progress", which look identical otherwise.

The responder then shows an **inverted bar reading `DONE`** with the position number, and
`PRG = next` underneath. That is your go signal. Nothing else means you may move.

**Expect it about 15–20 seconds after the sweep actually ends — measured at 17.2 s on
2026-09-03.** The responder may be sitting on the SF12 configuration when the sweep stops
and has to cycle back round to the beacon's configuration to hear it. It is not instant
and it is not broken.

Tethered, the same transition prints `# far end ARMED - sweep complete at position N -
press PRG to move on`.

**4. Walk to the next position and press PRG again.** The `DONE` display clears on the
press.

### Coming back

1. **Ctrl-C** the capture. It writes the file.
2. Check the file ends with a `# capture ended:` line. Without one it was truncated by
   something that never got to finish, and the last position in it is suspect.
3. Plug in the **responder** and reset it as `RESP`. It dumps its own per-position log at
   boot — `RESP,position,probes_heard,...`. **That is the reverse-direction data and the
   only copy of it**; the initiator's CSV cannot separate downlink loss from uplink loss
   without it.

### If the day allows, walk a second bearing

M6 asks for both. The second one is cheap once you are already outside with the boards
working, and two runs at **different antenna heights** are worth more than one careful run
at an unrecorded height.

---

## Job B — the ambient survey campaign

One board, seven sites, **one trip**. Nothing transmits in this mode.

The board stores each site's run to NVS, so you do **not** need the laptop at the gate or
the hop yard. Select the mode at the house, walk the loop, read all seven out on return.

### At the house

1. Reset the board, send **`v`** within 3 s. Badge reads `SURV`.
2. Press **`z`** once to erase any bench data.
3. Confirm the plan it prints: 130 bins, 902.0–927.8 MHz in 200 kHz steps, one pass every
   ~4 seconds.

The OLED shows the **site name** it will file under, the pass count as a large number, and
the loudest bin found so far.

### At each site

1. Stand still and **let it run for several minutes.** The pass count is the big number
   and it should climb steadily — that is also how you know the board has not hung.
2. **Press PRG once.** It stores this site and advances to the next one, clearing the
   accumulator for a fresh run. The OLED site name changes; that is your confirmation.
3. Walk to the next site. Repeat.

> **How long is "several minutes"?** Longer than feels necessary. The noise floor settles
> in seconds, but occupancy detection is **probabilistic**: one radio listening to one
> 125 kHz slice at a time sees each bin about 1/130 of the time, so a sensor that keys up
> for 100 ms every five minutes is missed by most short runs. A peak well above the floor
> is real evidence of an occupant. **A quiet bin is not proof of an empty one**, and no
> run length short of hours makes it so. Five minutes per site is a reasonable floor.

> **If the store fails** the console says so and the site does **not** advance — the OLED
> site name stays put. That is deliberate: advancing over an unstored run would lose it
> silently. Press PRG again, and if it still fails, that site has to be read out over
> serial before you move on.

### Back at the house

Reset the board into `SURV` again with the capture running. It dumps **every stored site**
as one table, one header, all seven sites in one file:

```bash
python3 tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --out docs/rangetest/data/2026-09-03-survey-campaign.csv --sweeps 1 \
    --note "seven-site ambient survey, ~5 min per site, stock whip 2.0dBi at 1.2m"
```

Then reset the board and send `v`. Expect **910 rows** — 130 bins × 7 sites — and a
`# survey campaign complete - 7 site(s)` line.

### Survey console keys

| Key | Effect |
|---|---|
| `d` | dump the run in progress |
| `a` | dump every stored site |
| `s` | store this site **without** advancing |
| `p` | store and advance — same as PRG |
| `x` | clear the run in memory (NVS untouched) |
| `z` | **erase every stored survey** |
| `[` `]` | dwell per bin, −/+ 10 ms |

---

## Things that will cost you time in the field

- **Shade the OLED with a hand in direct sunlight.** Confirmed outdoors 2026-08-31: the
  panel is outmatched by direct sun and comfortably readable with minimal shading. This is
  procedure, not a defect, and the measurement never depends on it — the CSV over serial
  and the NVS logs are the record.
- **Watch the neighbours.** A full-size frame at SF9 holds the channel for over a second.
  If a YoLink sensor stops reporting during a run, that is worth knowing before concluding
  the link is fine.
- **Do not write a port name into anything durable.** Both CP2102 bridges report
  `SER=0001`; the enumerated device node is not stable across replug.
- **Record height.** It matters more than you expect at 915 MHz over 150 m of ground, and
  it is the one thing that cannot be recovered afterwards.

## What to commit when you get back

The traces, into `docs/rangetest/data/`, and an entry in
[`engineering-log.md`](./engineering-log.md) — dated, with antenna gain and height,
bearing, weather, and anything that surprised you. A range figure with no record of the
conditions is not a result anyone can reuse, and D1 will be reopened by the first
unexplained reading if the evidence cannot be checked.
