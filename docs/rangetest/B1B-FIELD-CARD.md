# B1b — field card

**One page to carry.** Written 2026-09-09. Read [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md)
once before the first trip; this card is what to do on the day.

**What this run answers:** does the gate link close at the gate, on the pairing that will
actually be deployed, and with what margin. The bridge is a Heltec V3 (**R-4.1a**);
GateLink carries a Wio-SX1262. So the deployed link is **Heltec at the house, Wio at the
gate**, and that is the configuration this card walks.

**What it does not answer**, and no version of it can:

- **The carrier's net list.** The Kit (p-5982) is not GateLink's header board (p-6379).
  `gatelink-expansion-board.md` §10's ring-out is the only thing that closes that.
- **§2.3.1 sub-question (b)** — the sleep-current cost of holding `RF_SW` high. That is a
  meter on the carrier, not a walk.
- **The Wio's TX/RX split.** Reciprocal RSSI gives only `(TX − RX)` per node, and the
  2026-09-07 runs already established the asymmetry at 3.37 dB. Do not run permutations
  hoping to separate it.
- **The well bearing.** M6 asks for both; this card covers the gate bearing only.

---

## The three boards, and which is which

**The enclosures tell them apart, and that is the identification to use.** Both CP2102
bridges report `SER=0001`, port names are not identities, and the settings dump tells you
`heltec` or `xiao` but not *which* Heltec.

| Board | This card calls it | How to tell it apart | Role in B1b |
|---|---|---|---|
| Heltec V3 in the **Heltec dev board handheld case**. Powered down; **holds the seven stored survey sites in NVS** | **Heltec #1** | **The handheld case.** Confirmable in software until you erase it: boot it as `--role survey` and it dumps seven stored sites | **Off, at the house.** Only leaves the house for the optional A/B in §4 |
| Heltec V3 in the **Meshtastic flat case**. Both ends of the 2026-09-07 EIRP runs, position log cleared that day. **This is the target unit for the bridge node** | **Heltec #2** | **The flat case.** Boots as survey with **no** stored sites | **Fixed INITIATOR**, tethered to the laptop |
| XIAO ESP32S3 + Wio-SX1262 **Kit**, enumerates as a native USB device | **XIAO+Wio** | `board=` in the settings dump reads the XIAO entry; no CP2102, so no `usbserial` node | **Walking RESPONDER.** This is the half B1b exists to measure |

**Heltec #2 being the bridge's target unit is why it sits at the house end.** The initiator
in this run is the board that will become the bridge, in the room the bridge will live in,
so the house end of the trace is the deployed hardware and not a stand-in.

**The software discriminator expires; the cases do not.** Erasing Heltec #1's surveys for
the optional A/B makes the two boards identical over serial, which is the reason to read
the enclosure rather than the boot dump.

**All three were reflashed 2026-09-06 from `3a9843d` and nothing under `firmware/`, `lib/`
or `tools/` has changed since.** No reflash is owed. A reflash for any other reason changes
the boot banner to `v0.9` in that session's captures, which is a build stamp and not a
measurement difference.

---

## 1. Before leaving the house

- [ ] `git pull --ff-only`. Confirm `git status --porcelain` is empty.
- [ ] **Antennas screwed on before power, on every board, every time.** 3.0 dBi at both
      ends, and confirm `antenna_gain_dbi=3.0` in the settings dump before you rely on a
      run. The gain is an input to the **D33** power clamp, not a column in the CSV.
- [ ] **Clear the XIAO+Wio's position log.** It is the responder, its log persists to NVS
      and reloads at boot, and `g_position_id` restarts at 1 every run — so last run's
      position 1 is **folded into** this run's. **Confirm `# position log cleared` comes
      back.** No line, no erase.
- [ ] **Heltec #1 stays powered down and stays at the house**, unless you are doing the
      optional A/B in §4. An idle ARMED board beacons once a second on the single fixed
      channel and cost up to 60 % PER on 2026-09-05.
- [ ] Power bank for the XIAO+Wio, USB cable, laptop and cable for Heltec #2, tape measure
      or a phone GPS, something to note conditions on, something to shade the OLED.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbmodem1101 \
    --reset --role responder --key x --out /tmp/erase.csv --run-for 20
```

**Ports.** Pick the machine before the trip and use one set of names.

| | macOS (M5) | Kubuntu field laptop |
|---|---|---|
| Heltec #2 (CP2102) | `/dev/cu.usbserial-0001` | `/dev/ttyUSB0` or `/dev/ttyUSB1`, **by plug order** |
| XIAO+Wio (native USB) | `/dev/cu.usbmodem1101` | `/dev/ttyACM0` — **not** a `ttyUSB` node |

The Kubuntu laptop is the one verified green on all nine checks with both firmware targets
building, so prefer it if a field reflash is even faintly possible.

---

## 2. The positions — two sweeps, not nine

The 2026-09-04 walk already characterised P1–P6 out to 106 m with a Heltec responder. **Do
not re-walk those spots with the XIAO+Wio.** They are waypoints, not node sites, GateLink
lives at one place, and arcsecond GPS cannot support an RSSI-vs-distance curve, so the
re-walk would produce per-position numbers with no consumer.

| New label | Where | Why it is in the run |
|---|---|---|
| **G1** | Roughly midway through the unwalked leg past 2026-09-04's P6, ~125 m | So that a failure has a location. If the link does not close at the gate, G1 says whether it died in the last 40 m or before it |
| **G2** | **The gate.** GateLink's actual mounting location, or as close as you can stand to it | The measurement. This is the one that matters |

**Trace positions renumber from 1.** G1 is trace position 1 and G2 is trace position 2 —
neither maps to the 2026-09-04 numbering. **Put that mapping in `--note` at capture time**,
not afterwards.

---

## 3. The run

**1. Tether Heltec #2 at the house**, in the office on the NW side — the bridge's target
location and the same spot as 2026-09-04, so the two traces share the initiator end. It is
the INITIATOR, which is the no-press default.

**2. Start the capture.** One capture spans both positions.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset \
    --out docs/rangetest/data/YYYY-MM-DD-b1b-walk-gate.csv \
    --note "B1b gate-bearing walk. INITIATOR Heltec V3 in the Meshtastic flat case (the bridge's target unit), INDOORS at the bridge target location (office, NW side, 1 exterior wall). RESPONDER XIAO+Wio-SX1262 Kit, outdoors, hand-carried. Bearing 120deg to gate. 3.0dBi both ends. Trace position 1 = G1 <describe spot, GPS, AGL both ends, LOS/obstructions>; position 2 = G2 the gate <same>. Third Heltec (handheld case) POWERED DOWN <at the house | in the pack, off>. Weather <..>, foliage <..>."
```

**Fill the placeholders in before you press Return.** The §7.6 run went out with
`<H>m AGL on <stands>` unedited and the two geometry-dependent checks were exactly the ones
that needed it. `capture.py` cannot tell a placeholder from a value and nothing downstream
checks.

**3. Read `board=` and `antenna_gain_dbi=` off the settings dump** before walking away.

**4. Boot the XIAO+Wio and tap its role button within 3 s.** On this board the button is
the **user button on top of the Wio, GPIO 21** — not GPIO 0, and not the expansion board's
D1. Badge reads `RESP`.

**5. Walk to G1. Press the button. Stand still.** A sweep is 24 test points, about
**424 s**. The inverted `DONE` bar arrives **~17 s after** the sweep actually ends, because
the responder is cycling from SF12 back to the beacon's configuration. **`DONE` is the only
go signal.** Quiet stretches mid-sweep are normal; an age that climbs past a minute means
you have walked out of range, which is itself a result — note where you were standing.

**6. Walk to G2, the gate. Press the button. Stand still.**

**7. Do not press the button after G2.** The responder saved G2 on the armed beacon
already. A press advances the cursor and starts a sweep for a position you are not standing
at, which is how 2026-09-04 gained a position 7 that is not a location. **Walk away and
power the board down.**

**8. Back at the house, Ctrl-C the capture.** Confirm the file ends with `# capture ended:`.

**9. Capture the XIAO+Wio's own position log.** It walked untethered, so nothing has read
it yet, and **this is the only copy of the reverse-direction data** — the initiator's CSV
cannot separate downlink loss from uplink loss without it.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbmodem1101 --reset --role responder --sweeps 1 \
    --out docs/rangetest/data/YYYY-MM-DD-b1b-walk-gate-resplog.csv \
    --idle-timeout 60 --note "responder log, B1b gate walk of YYYY-MM-DD, XIAO+Wio"
```

No `d` is needed — the responder dumps at boot from NVS, so `--reset` triggers it and
`--sweeps 1` stops at `# responder log complete`. Expect one row per position visited, and
its counts should close against the sweep trace.

**Budget ~15 minutes of standing**, plus the walk out and back.

---

## 4. Optional — the Heltec A/B at the gate

**Only worth doing at G2**, and only if the day allows. It answers what the module costs on
the deployed path by measuring both boards over the same path within the same hour, which
is the only condition under which a Wio-versus-Heltec number has ever meant anything here:
bench geometry moved the Heltec reference −24 → −42 dBm on placement alone.

**It needs its own capture file.** Heltec #1 boots with `g_position_id` at 0, so its first
press produces trace position 1 again — a duplicate position id inside the walk capture.
Ctrl-C at step 8, then start a second capture before carrying Heltec #1 out. That costs a
return trip to the gate and buys an unambiguous file.

Before it leaves the house, Heltec #1 needs **both** erases — its stored surveys dump at
boot and would land in the trace:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --reset --role survey --key z --out /tmp/erase.csv --run-for 20
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --reset --role responder --key x --out /tmp/erase.csv --run-for 20
```

**Erasing the surveys costs nothing.** All seven sites are committed at 130 bins each in
`data/2026-09-05-survey-campaign-r11.csv` and `data/2026-09-05-survey-campaign.csv`. NVS on
a bench board is not a backup.

**The XIAO+Wio must be powered down for the A/B sweep**, and Heltec #1 powered down for
everything else. Two boards make a measurement; a third that is powered is in the
experiment whether or not it is in the plan.

Capture Heltec #1's position log afterwards, the same way as step 9, to its own `--out`.

---

## 5. What to look at

The trace carries `phy_crc_err` per test point, which is B1b's §10.5 criterion. **A nonzero
count at the gate is a finding, not a fault** — it is the counter behaviour at a marginal
link, observed somewhere other than 1 m for the first time.

Read, per position: PER, RSSI and SNR against the sweep's configurations, and where the
link starts losing points. The 2026-09-04 trace and the engineering log's 2026-09-05
excess-loss table are the comparison for the initiator end, with the caveat that the day,
the foliage and the per-position AGL all differ.

**A link that does not close at the gate is a result**, not a failed trip. Record which
configurations closed and which did not, and where you were standing.

---

## 6. What to commit

The two traces into `docs/rangetest/data/`, plus a dated entry in
[`engineering-log.md`](./engineering-log.md) carrying the geometry, the weather, the board
at each end, and anything that surprised you. Update `HANDOFF.md` — B1b is the only
measurement this directory still owes, and closing it changes what the next session opens.

**A measurement is not finished when the trace is committed.**

---

## Traps that have already cost time here

- **A stale responder position log merges into the new run** and fakes an instrumentation
  fault. Clear it, and confirm the line.
- **A third powered board corrupts a two-board measurement** by up to 60 % PER, silently,
  and the symptom is indistinguishable from poor link margin.
- **The XIAO has no CP2102**, so it never appears as a `usbserial` node, and the
  serial-port-presses-PRG trap is Heltec-only.
- **`capture.py` will not overwrite an existing `--out`** without `--force`.
- **Never run a serial console alongside `capture.py`.** One command, one process, one port.
- **A boot line that is not `key=value` never reaches the trace header.**
- **The OLED needs hand-shading in direct sunlight.** Procedure, not a defect.
- **Never transmit without an antenna connected.**
