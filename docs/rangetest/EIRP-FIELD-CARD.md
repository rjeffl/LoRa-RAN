# §7.6 EIRP check — field card

**One page to carry. The reasoning is in
[`EIRP-SANITY-CHECK.md`](./EIRP-SANITY-CHECK.md); this is what to do.**
Written 2026-09-06 for a run on the **Kubuntu field laptop**, planned for the next clear
day. Read [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) once before the first trip.

---

## Before leaving the house

- [ ] **Field laptop has the repo at `main`, current.** `git pull --ff-only`. `capture.py`
      must be the version that writes `pa_*` into the trace header.
- [ ] **Confirm the serial port and permissions on the field laptop** — see *Kubuntu deltas*
      below. Do this indoors, not in a field.
- [ ] **XIAO stays powered down.** It is not in this measurement.
- [ ] **Erase the responder's position log** — `FIELD-PROCEDURE.md`, *"Erase the bench data
      first"*. `capture.py --reset --role responder --key x --run-for 20`, and **confirm
      `# position log cleared` comes back**. This is not housekeeping: the log is persisted
      to NVS and reloaded at boot, `g_position_id` restarts at 1 every run, and the log
      merges by position id — so a previous run's position 1 is **folded into** this run's,
      doubling `probes_heard`. Step 7's cross-check then reports an instrumentation fault
      that is not there.
- [ ] Two Heltecs, **two 19 cm 3.0 dBi sticks**, power bank, USB cable, **long USB
      extension** (see below), tape measure, both stands, something to shade the OLED.
- [ ] Antennas **screwed on before power**, every time.

---

## The stands — it is a distance rule, not a materials rule

**"Non-metallic" overstates it.** What matters is metal **near the antenna**. At 915 MHz
one wavelength is **32.8 cm**, so keep metal **at least ~35 cm, ideally ~65 cm, from the
antenna** and it stops mattering much. A metal tripod whose legs are a metre below the
antenna is far better than a laptop sitting 20 cm to one side of it.

That turns an impossible shopping list into three workable options:

- **A wooden or plastic extension on whatever you have.** A 1 m length of PVC or a wooden
  stake cable-tied to a metal ladder, tripod or fence post, antenna on top. The metal
  fasteners in a wooden ladder are irrelevant at 65 cm.
- **Plastic buckets, stacked**, or a plastic sawhorse, or a garden stake in a bucket of
  sand.
- **A camera tripod**, accepting its mass is below the antenna.

**Get the laptop out of the geometry entirely: put it on a long USB extension** and set it
several metres to the side of the initiator board. It is the largest, closest metal object
in the whole setup and it is the one you have complete control over. If it must sit near
the board, **put it in exactly the same place at every distance** — a constant error
cancels in check 1 and only shifts the intercept in check 4.

**Both ends at the same height**, 1.5 m or better. **Level ground**, grass not pavement or
gravel.

---

## If the stands do not happen, run it anyway

**Checks 1 and 2 do not depend on geometry at all**, and check 2 is the one that matters —
the D33 clamp reaching the PA over the air, which nothing in this repository has ever
verified.

So a run at **one distance, on poor stands, on a level lawn** still closes the important
half. What it cannot do is validate the absolute EIRP back-out (check 3) or the slope
(check 4). **That is a good trip, not a failed one** — say so in the note and in the log,
and the geometry-dependent half stays owed.

**Do not** substitute a desk run. The bench reference moved **−24 → −42 dBm on placement
alone**; an uncontrolled indoor run produces a confident wrong number for checks 3 and 4
while adding nothing to 1 and 2 that outdoors would not.

---

## Kubuntu deltas — the field laptop is not the Mac

| | macOS (M5) | **Kubuntu field laptop** |
|---|---|---|
| CP2102 port | `/dev/cu.usbserial-0001` | **`/dev/ttyUSB0`, `/dev/ttyUSB1`** |
| Which is which | by USB location | **by plug order** — plug one at a time and note it |
| Permissions | none needed | **user must be in `dialout`** |

**Check all of these indoors before the trip:**

```bash
ls -l /dev/ttyUSB*                     # both boards present?
id -nG | tr ' ' '\n' | grep -x dialout # in the group? if not: sudo usermod -aG dialout $USER, then LOG OUT
~/.platformio/penv/bin/python -c "import serial; print(serial.__version__)"
```

- **`~/.platformio/penv/bin/python` is the interpreter on both machines** — PlatformIO
  installs to `~/.platformio` on Linux too. A bare `python3` has never had `pyserial` on
  either box. Verify it rather than assume it; if PlatformIO was never installed on this
  laptop, `pyserial` in any interpreter is enough for `capture.py`.
- **`ModemManager` and `brltty` both grab USB serial adapters on Ubuntu.** If a port
  appears and then vanishes, or opens and immediately misbehaves, that is the usual cause.
  Check before blaming the board.
- **Both CP2102s still report `SER=0001`**, so the port name is not an identity on this
  machine either. **Read `board=` and the role off the settings dump** to know which board
  you are talking to.

---

## The run

Tethered board is the **initiator**; the other walks as **responder**. Prefer the Heltec
holding the stored survey campaign as the tethered one — it keeps that board away from PRG
presses.

**1. Responder at 3 m.** On its stand, on the power bank, antenna vertical. Boot it and
**tap PRG** inside the 3 s window to select RESPONDER. Confirm on its OLED.

**2. Start the capture on the initiator.** One capture spans all three distances.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/ttyUSB0 --reset --role initiator \
    --out docs/rangetest/data/YYYY-MM-DD-eirp-sanity.csv \
    --note "findings 7.6 EIRP sanity check. Heltec pair, 3.0dBi sticks vertical, both ends <H>m AGL on <stands>, level grass, clear LOS, laptop <N>m off-path on USB extension, operator 2m off-path at the initiator end every position. P1=3.0m P2=6.0m P3=12.0m tape measured. XIAO POWERED DOWN."
```

**Read `board=` off the settings dump before going further.**

**3. Press PRG on the responder.** The initiator boots ARMED and sweeps nothing until this
press, so **this is P1, not P0.**

**4. Wait.** A sweep is **424 s**, and the responder's inverted `DONE` bar arrives **~17 s
after** the sweep actually ends — it is cycling from SF12 back to the beacon's config. That
lag is expected. **Do not move early.**

**5. Move to 6 m, press PRG. Then 12 m, press PRG.** Re-measure with the tape each time;
do not pace it. Add 24 m if the field allows.

**6. Ctrl-C.** Confirm the file ends with `# capture ended:`.

**7. Capture the responder's own log.** A **second capture**, its own port and its own
`--out` — the responder was untethered, so nothing has read it yet. **This is the only copy
of the reverse-direction data.** Can wait until you are indoors, but do it before the boards
are used for anything else.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/ttyUSB0 --reset --role responder --sweeps 1 \
    --out docs/rangetest/data/YYYY-MM-DD-eirp-sanity-resplog.csv \
    --idle-timeout 60 --note "responder log, 7.6 EIRP sanity check of YYYY-MM-DD"
```

**No `d` needed** — the responder dumps at boot when it has a stored log, so `--reset`
triggers it and `--sweeps 1` stops at `# responder log complete`. Its per-position counts
should close against the sweep trace; disagreement is instrumentation, not a link result —
**provided you cleared the log before the run**.

**Budget ~30 minutes of standing** for three positions, plus setup.

---

## Reading it (can wait until you are back indoors)

```bash
python3 tools/rangetest/eirp_check.py \
    docs/rangetest/data/YYYY-MM-DD-eirp-sanity.csv \
    --distance 1=3.0 --distance 2=6.0 --distance 3=12.0
```

Distances live on the command line because the firmware cannot know them. **Record the
exact invocation in the engineering log next to the result.**

**Expected, at −4 dBm conducted with 3.0 dBi:** ≈ **−39 dBm at 3 m, −45 at 6 m, −51 at
12 m**.

**The power step should read 5 dB, not 6.** The 2026-08-31 bench trace showed 6.0 dB
because it was captured at 2.0 dBi, where the ceiling was −3 dBm. At the fitted 3.0 dBi the
ceiling is −4, so the step up from the −9 dBm floor is 5 dB. The tool derives this from the
trace's own logged powers, so it will expect the right number.

**Individual readings several dB off the line are normal** — these distances sit below the
two-ray breakpoint (~27 m). An rms residual of 2–3 dB is a good outdoor short-range result;
**above ~5 dB, discard the absolute figure and carry only checks 1 and 2.**

### The one outcome that stops everything

**Check 2 failing** — a logged conducted power above the ceiling, or the high point logging
+22 dBm. That is the clamp not running: **a compliance fault, not a data-quality question.
Stop transmitting.**

---

## Small things that cost time

- **The OLED needs hand-shading in direct sunlight.** Contrast is already maxed. Procedure,
  not a defect.
- **`capture.py` will not overwrite an existing `--out`** without `--force`.
- **Never run a serial console alongside `capture.py`.** One command, one process, one port.
- **Never transmit without an antenna connected.** It is also one of the failures this check
  exists to detect — find it in the trace, not by damaging a PA.
- **A flashed or reset board boots ARMED and beacons once a second.** Nothing needs
  flashing on this trip, but if you do reset something, remember it is transmitting.
