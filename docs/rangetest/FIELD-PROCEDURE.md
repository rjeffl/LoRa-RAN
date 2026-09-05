# Range test — field setup and procedure

**Written 2026-09-03.** Covers the two field jobs the `range-test` firmware exists to do:

| Job | Task | Answers | Roles used |
|---|---|---|---|
| **A. Position walk** | R10 fieldwork, the R4–R7 sweep | **M6** — range and RSSI at the target locations | INITIATOR + RESPONDER |
| **B. Ambient survey** | **R8** | **M20** — what else is on the band, at every site | SURVEY (one board) |

> `capture.py` option reference and recipes: [`CAPTURE-PY.md`](./CAPTURE-PY.md).
>
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
- **The 3.0 dBi production antennas, one on each board.** See below — this is a firmware
  setting as well as a piece of hardware.
- Power bank for the walking board.
- The laptop, tethered to the initiator, which stays at the house or the gate.
- Something to note bearing, antenna height and approximate elevation on. **These go in
  `--note`**, and a range figure without them is not a result anyone can reuse.

### The antenna is a build setting, not just hardware

**Changing antennas requires a reflash.** The gain is an *input to the D33 power clamp*,
not just a column in the CSV: the Part 15.249 ceiling is on **EIRP**, which is conducted
power plus antenna gain. Fit a 3.0 dBi antenna against a firmware that still believes
2.0 dBi and two things are wrong together — the trace records an antenna that was not
fitted, **and the clamp permits an EIRP 1 dB over the ceiling.**

It is set in one visible place, `firmware/range-test/platformio.ini`:

```ini
-DLRAN_ANTENNA_GAIN_DBI10=30      ; 3.0 dBi production antenna
```

| Antenna | Flag | Conducted ceiling | EIRP |
|---|---|---|---|
| Stock whip | `20` | −3 dBm | −1.0 dBm |
| **Production (in use)** | **`30`** | **−4 dBm** | −1.0 dBm |

There is no default in the code — the build fails with a message if the flag is missing,
because a default is exactly what silently survives an antenna change.

**Use the same antenna at both ends.** Matched antennas are what make the two directions
comparable; the firmware records one gain figure and applies it to both.

> **Nothing else about the procedure changes**, and no test needs re-running. The only
> visible effect is that the sweep's upper power point is 1 dB lower, so the whole
> campaign sits 1 dB further from the ceiling. The bench traces taken on stock whips are
> format proofs, not range data, so nothing is invalidated.

### Reflash both boards — once, for the antenna

Yes, you need to reflash before the test: the boards currently in hand were built for the
stock 2.0 dBi whips.

```bash
~/.platformio/penv/bin/pio run -d firmware/range-test -e heltec -t upload --upload-port /dev/cu.usbserial-0001
```

`pio` is at `~/.platformio/penv/bin/pio` and is **not on `PATH`**. On macOS always
`/dev/cu.*`, never `/dev/tty.*`. Run it once per board — and note that the two port names
can swap between plug-ins, so flash one, then the other, and confirm each says `SUCCESS`.

**Confirm the boards agree with the build.** Every trace carries the settings dump at the
top; check it reads `antenna_gain_dbi=3.0` before you rely on a run.

### There is no separate serial console — `capture.py` drives the board

Short answer to "which console app": **none, and do not open one.** `capture.py` resets
the board, selects the role, sends console keys and captures, all in one command.

This is not a convenience. Two processes on one serial port open without an exclusive
lock on macOS and then **split the incoming bytes between them at random** — it looks
like it works and produces a trace with holes in it. One command, one process, one port.

**Run it with PlatformIO's python**, which is the interpreter that has `pyserial`:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbserial-0001 --reset --out out.csv
```

A bare `python3` on this machine is one of several framework installs and has never seen
`pyserial` — `pip3 show pyserial` reporting it "already installed" is a different Python
than the `python3` first on your `PATH`. Installing it again would work here and break on
the field laptop. The script's error message now names both options if you hit it.

**On power:** yes, the initiator needs USB for power anyway, so plug it in and let
`capture.py --reset` do the reset. You do not press the board's reset button, and you no
longer have to start the capture before resetting — that trap is gone.

If you ever do want a plain console — for poking at a board when you are *not*
capturing — use `~/.platformio/penv/bin/pio device monitor -p /dev/cu.usbserial-0001 -b 115200`,
and close it before running `capture.py`.

### Erase the bench data first

Both boards carry NVS state from bench work: the responder's position log and up to seven
stored surveys. **Left in place they are dumped at boot and land in your field trace.**

One command each, no console needed:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --reset --role survey --key z --out /tmp/erase.csv --run-for 20
```

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --reset --role responder --key x --out /tmp/erase.csv --run-for 20
```

**`--run-for`, not `--idle-timeout`:** a responder hunting for an initiator never stops
talking, so an idle deadline never fires and the command would run forever.

Both end with "no data rows captured" — correct, there is nothing to capture. What you
are looking for is the board's own confirmation, which `capture.py` echoes whenever
`--key` is used:

```
  selected role: survey
  sent key: z
  # all stored surveys erased from NVS, site cursor reset
```

and `# position log cleared` for the responder. **If you do not see that line, the erase
did not happen** — check the role was selected. Repeat for the second board.

> **The port name can change between commands.** Every `capture.py --reset` re-enumerates
> the USB bridge, and with two identical CP2102s both reporting `SER=0001` the two device
> nodes can swap. Erasing "0001" and then checking "0001" is not necessarily the same
> board — that is how this looked like a bug for twenty minutes. **Trust the confirmation
> line from the command that did the work**, not a separate check afterwards, and finish
> both erases on one board before moving to the other.

### Role selection — a 3-second window after boot, not a hold through reset

PRG is GPIO 0, the BOOT strapping pin; held through reset it enters the ROM downloader
and the application never runs. So the roles are chosen **after** the board starts, while
the OLED shows a countdown:

| Board | Do this within 3 s of reset | Badge |
|---|---|---|
| Initiator | nothing — it is the no-press default | `INIT` |
| Responder | **tap PRG** (or send `r`) | `RESP` |
| Survey | **hold PRG ~1.5 s** (or send `v`) | `SURV` |

**Tap for RESPONDER, hold for SURVEY.** While the button is down the OLED shows the role a
release would choose right now, and the word flips from `RESPONDER` to `SURVEY` as you
cross the threshold — watch for the flip rather than counting. Both are reachable with a
thumb, on a power bank, with no laptop.

> This board has **no battery fitted**, so moving from the laptop to a power bank is a
> **power cycle**, and the role is deliberately not persisted (R1 — a power cycle re-asks).
> The survey used to be serial-only, which meant a board unplugged at the house came back
> up as `INITIATOR` — the mode that **transmits**. The hold is what makes the untethered
> survey possible at all.

---

## Job A — the position walk

One person. The initiator stays put and logs; you carry the responder.

### Start the capture — one command, it resets the board itself

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset \
    --out docs/rangetest/data/2026-09-03-walk-gatelink.csv \
    --note "bearing 120deg to gate, both ends 1.2m AGL, gate ~70ft above bridge, 3.0dBi both ends, dry, foliage full"
```

No `--role` needed: INITIATOR is the no-press default. The `--reset` makes the settings
dump and CSV header land *after* the capture is listening, so there is nothing to get the
order wrong about.

Then reset the **responder** separately and tap PRG within 3 s so its badge reads `RESP`.
It is untethered and has no capture of its own.

**The order no longer matters.** The initiator boots ARMED and runs nothing until your
first press, so it cannot start sweeping against a responder that is not listening yet.
Boot them in whichever order is convenient.

**One capture spans the whole walk.** It runs until Ctrl-C by default and appends rows as
they arrive, so a dropped cable costs the rest of the walk and not the part already done.
Do not restart it per position.

### The position cycle

This is the part to get right. **One press, one sweep, one position.**

**0. The initiator boots ARMED and runs nothing until you ask.**

It prints `# ARMED at boot - press PRG on the responder to start position 1` and waits,
beaconing once a second so the responder's hunt can find it. **No sweep runs until your
first press**, so there is no way to lose the start of a sweep to a responder that has not
been booted yet. Positions therefore run **1, 2, 3…**, not from 0.

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

Tethered, the same transition prints
`# far end ARMED - position N measured and saved, press PRG to move on`.

(It deliberately does **not** say "sweep complete" — that phrase is what `capture.py` stops
a capture on, and the responder does not run sweeps.)

**4. Walk to the next position and press PRG again.** The `DONE` display clears on the
press.

**The responder saves each position as the sweep for it completes** — on the same armed
beacon that raises `DONE`, not on your next press. So the last position of the walk is
already stored when you power the board down, and a brown-out costs at most the position
in progress.

> **So do NOT press PRG after the last position.** It is the natural instinct — one more
> press to be sure it was written — and it is the wrong move: the press advances the
> cursor and starts a sweep for a position you are not standing at. The initiator dutifully
> records ~24 test points of 100% PER against a responder you are about to switch off, and
> the trace gains a position that looks like the link failing at the far end of the walk.
> That is exactly what happened on 2026-09-04; see the engineering log and the header of
> `data/2026-09-04-walk-gatelink.csv`. **Walk away and power down.**

### Coming back

1. **Ctrl-C** the capture. It writes the file.
2. Check the file ends with a `# capture ended:` line. Without one it was truncated by
   something that never got to finish, and the last position in it is suspect.
3. Plug in the **responder** and capture its per-position log. **This is the
   reverse-direction data and the only copy of it** — the initiator's CSV cannot separate
   downlink loss from uplink loss without it:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role responder --sweeps 1 \
    --out docs/rangetest/data/2026-09-04-walk-gatelink-resplog.csv \
    --idle-timeout 60 \
    --note "responder log, walk of 2026-09-04, bearing 120deg"
```

   It stops on its own at `# responder log complete`. Expect one `RESP,` row per position
   visited. Commit it next to the sweep trace — the two are only useful together.

### Height, elevation, and what to actually write down

The original advice — "record antenna height, it matters at 915 MHz" — is the standard
guidance for a **flat** path, where height above local ground buys Fresnel clearance over
the ground itself. **On this property it is not the quantity that matters.** With the gate
sitting 60–80 ft above the bridge, the path is dominated by terrain relief, and 1.2 m
versus 2.0 m of mast is noise against 70 ft of hillside.

So record all three, and do not agonise over precision:

| What | Why | How |
|---|---|---|
| **Antenna height above local ground**, each end | Still governs near-field clearance and ground reflection right at the antenna. Cheap to note | Tape measure, ±10 cm is plenty |
| **Approximate true elevation**, each end | This is what sets the path profile, and it is the number that explains an unexpectedly good or bad link | Phone GPS altitude, or a topo/contour source. **±10 ft is fine** — you are explaining a 70 ft difference, not surveying |
| **Line of sight — yes, no, or partial** | The single most predictive thing you can write down, and it costs nothing | Look. Note what is in the way: trees, the barn, a rise |

Phone GPS altitude is noisy in the vertical (±10–20 ft is typical) but that is well inside
what you need here. If you want better later, the fixed installations can be read off a
contour map once and recorded permanently — **the node heights for the bridge and GateLink
are already set**, so those two are a one-time note, not a per-run measurement.

> **Two runs at different heights are still worth more than one** — but only where you can
> actually change the height, and only after the fixed installations are ruled out as the
> problem. This is not a reason to delay the walk.

### If the day allows, walk a second bearing

M6 asks for both. The second one is cheap once you are already outside with the boards
working.

---

## Job B — the ambient survey campaign

One board, seven sites, **one trip**. Nothing transmits in this mode.

The board stores each site's run to NVS, so you do **not** need the laptop at the gate or
the hop yard. Select the mode at the house, walk the loop, read all seven out on return.

### Site 0, at the house

Erase any bench data first (above). Measure `bridge-house` tethered, and keep the trace —
this one is free, because the laptop is right there:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role survey --sweeps 1 \
    --out docs/rangetest/data/2026-09-04-survey-bridge.csv \
    --idle-timeout 60 --note "bridge-house, 3.0dBi at 1.2m"
```

Confirm from the output: badge `SURV`, 130 bins, one pass every ~4 s, and
`antenna_gain_dbi=3.0`. Let it scan for five minutes, then **press PRG on the board** —
same gesture as every other site. That stores `bridge-house` and advances the cursor to
`gatelink-gate`; the capture stops on the dump and writes the file.

> **If you would rather the tool pressed it for you**, add `--key-after 300 --key p`.
> `p` *is* the PRG press — the tool will store and advance on its own, five minutes in,
> and the site name on the OLED will change without you touching anything. That is
> correct behaviour and it surprises people, so it is opt-in rather than the default.

### Then unplug and walk — the campaign resumes by itself

**Unplugging is a power cycle.** The board forgets its *role* (R1 — a power cycle
re-asks), but it keeps everything about the campaign: the stored sites **and which site it
is up to**.

At the power bank, all you do is:

1. Plug in.
2. **Hold PRG** until the OLED reads `SURVEY`, then release.

That is the whole procedure. The board prints
`# resuming campaign at site 2 weather-island` and the OLED shows that name. **Nothing is
stepped past and nothing is overwritten.** Verified on hardware: two sites stored, power
cycled, resumed at site 2 with both intact.

Power-cycle as often as you like — swapping the power bank between sites is free.

> The site cursor is persisted precisely so you never have to press PRG "past" the sites
> already done. Doing that would store the empty run in progress over each one on the way,
> which is a campaign destroying itself to get back to where it was.

**If you need to move the cursor without storing** — a site skipped, or a mis-set cursor —
`n` advances and `b` steps back, neither of which writes anything. Both need the laptop.

The OLED shows the **site name** it will file under, the pass count as a large number, and
the loudest bin found so far.

### At each site

1. Stand still and **let it run for several minutes.** The pass count is the big number
   and it should climb steadily — that is also how you know the board has not hung.
2. **Press PRG once.** It stores this site and advances to the next one, clearing the
   accumulator for a fresh run. The OLED site name changes; that is your confirmation.
3. Walk to the next site. Repeat.

> **Known limitation: the walk between sites is measured.** The scan never stops, so
> whatever the radio hears in transit is folded into the next site's run — and because
> `peak_dbm10` is a peak hold, one burst heard while walking past an emitter is
> attributed permanently to the site you were heading for.
>
> **There is no press pattern that avoids this.** Pressing on arrival instead of on
> departure only moves the contamination from the destination site to the one you just
> left; the accumulator is running either way. Every site except the first carries its
> inbound transit, and that is inherent to the current firmware.
>
> What this costs: the **occupant inventory** is not reliably site-attributable. The
> **floor and the mean are unaffected** — the floor is stationary and min-held, and a few
> minutes of walking against a five-minute dwell barely moves a mean that sits on the
> floor anyway — so anything that depends on floor or mean, which is what §12.1 actually
> asks for, is sound.
>
> The 2026-09-05 campaign was run this way and its peak column is caveated in the trace
> header. **The fix is a survey hold state in the firmware**, filed as the next task; if
> you are tethered you can approximate it by sending `x` on arrival to clear the transit
> before the dwell begins.

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
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role survey --sweeps 1 \
    --out docs/rangetest/data/2026-09-03-survey-campaign.csv \
    --note "seven-site ambient survey, ~5 min per site, 3.0dBi at 1.2m"
```

One command — it resets the board into `SURV`, and the boot dump-all is what it captures.
Expect **910 rows** (130 bins × 7 sites), one header, and a
`# survey campaign complete - 7 site(s)` line.

### Survey console keys

| Key | Effect |
|---|---|
| `d` | dump the run in progress |
| `a` | dump every stored site |
| `s` | store this site **without** advancing |
| `p` | store and advance — same as PRG |
| `n` | next site **without storing** |
| `b` | previous site **without storing** |
| `x` | clear the run in memory (NVS untouched) |
| `z` | **erase every stored survey** |
| `[` `]` | dwell per bin, −/+ 10 ms |

---

## Power, and the optional LiFePO4 module

Both field jobs run the walking board off a **USB power bank**. That works today and is
what the procedure above assumes.

### Fitting a battery is a hardware question, not a firmware one

**This firmware touches nothing battery-related.** No ADC read, no `VBAT` pin, no charge
control — the vendor board variant does not even declare a battery pin. Charging and the
USB/battery power path on the Heltec V3 are done by the onboard charger and power-path
circuitry and are **independent of what firmware is running**. Meshtastic does not enable
charging either; it only *reads* battery voltage for display.

So fitting the module should behave the same under this firmware as under any other.

**What you give up** is the readout: there is no battery voltage on the OLED and no
low-battery warning, because nothing here reads it. On a five-minute-per-site survey that
is not much of a loss, but a board that browns out mid-site loses only the run in
progress — the stored sites and the site cursor are in NVS and survive.

**What you gain** is worth more than it sounds: with a battery fitted, **moving between
the laptop and the power bank is no longer a power cycle**, so the role survives the swap
and the hold-PRG step disappears.

> **Verify it on the bench before relying on it in the field.** Fit the module, plug USB
> in, confirm it charges; unplug USB, confirm the board keeps running; replug, confirm it
> does not reset. That is a ten-minute check and it is the only way to know for this board
> revision — I have not tested it, and nothing in the firmware would tell you.

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
- **Record what is actually recoverable about geometry.** See below — height above local
  ground is not the useful quantity on this property, and it is the one thing that cannot
  be reconstructed afterwards.

## What to commit when you get back

The traces, into `docs/rangetest/data/`, and an entry in
[`engineering-log.md`](./engineering-log.md) — dated, with antenna gain and height,
bearing, weather, and anything that surprised you. A range figure with no record of the
conditions is not a result anyone can reuse, and D1 will be reopened by the first
unexplained reading if the evidence cannot be checked.
