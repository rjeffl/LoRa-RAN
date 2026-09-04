# `capture.py(1)` — range-test trace capture

**Written 2026-09-04.** The single reference for `tools/rangetest/capture.py`. Everything
else about the fieldwork is in [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md); the trace
schemas are in [`data/README.md`](./data/README.md).

---

## NAME

`capture.py` — drive a range-test board and capture its trace into a committed CSV.

## SYNOPSIS

```
~/.platformio/penv/bin/python tools/rangetest/capture.py
        --port PORT --out FILE
        [--reset] [--role {initiator,responder,survey}]
        [--key KEYS] [--key-after SECONDS]
        [--sweeps N] [--idle-timeout SECONDS]
        [--echo] [--note TEXT] [--baud BAUD]
```

## INTERPRETER

**Run it with PlatformIO's python.** `pyserial` lives in PlatformIO's venv; a bare
`python3` is usually a different install that has never seen it, and `pip3 show pyserial`
may well report on a third interpreter again.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py ...
```

Installing `pyserial` into whichever `python3` is first on `PATH` appears to fix it and
then fails on the next machine. The script's error message names both the interpreter
known to have it and the one you actually ran.

## DESCRIPTION

Reads a board's serial console, keeps the CSV header and data rows, discards the rest, and
writes a trace whose top block records the configuration that produced it.

With `--reset` it also **drives** the board: reset, select the role, send console keys,
then capture. That is one command for the whole job and it is the supported way.

> **Never run a serial console alongside it.** macOS opens a serial device *without an
> exclusive lock*, so a console and `capture.py` both succeed and then split the incoming
> bytes between them at random. It looks like it works and puts holes in the trace.

Rows are **appended and flushed as they arrive**, not held until exit, so a killed
terminal, a sleeping laptop or a tugged cable costs the rest of the run and not the part
already done. **Ctrl-C ends a capture cleanly and writes the file.**

## OPTIONS

| Option | Default | Meaning |
|---|---|---|
| `--port PORT` | *required* | Serial device. On macOS always `/dev/cu.*`, never `/dev/tty.*` |
| `--out FILE` | *required* | Trace to write. Opened when the CSV header is seen |
| `--baud BAUD` | `115200` | Console speed. The firmware does not vary it |
| `--reset` | off | Pulse reset after opening the port, so the settings dump and CSV header land *after* the capture is listening. Retires the old "start this before resetting the board" trap |
| `--role ROLE` | *initiator* | Role to select in the boot window. Requires `--reset` |
| `--key KEYS` | none | Console keys to send once the board is up, one per second, in order |
| `--key-after S` | `0` | Wait this long before sending `--key` — lets a survey scan a while, then be told to dump |
| `--sweeps N` | `0` | Stop after N completion markers. **0 means run until Ctrl-C**, which is what a position walk wants |
| `--idle-timeout S` | `1800` | Give up after this long with **no serial data at all**. Idle, not wall-clock |
| `--run-for S` | `0` (off) | Stop after this much **wall-clock** time, whatever the board is saying. Required for any command run against a board that never goes quiet — see below |
| `--echo` | auto | Print the board's own `#` lines. **On automatically whenever `--key` is given** |
| `--note TEXT` | empty | Free text recorded in the file header — bearing, antenna height, elevation, weather |

### `--role` and why the key is sent repeatedly

The role key is written every 150 ms across the whole boot window, not once. A single
timed write races a boot whose length is not fixed — ROM bootloader, `Serial.begin()`,
then the OLED bring-up's own delays — and a byte landing before the UART is configured is
simply gone. The board then comes up `INITIATOR`, which on the walking end will not echo
and **in survey mode transmits**. Observed exactly that way on hardware.

`select_role()` drains everything available per pass and returns on the first match, so
the repeats after it has chosen are read by the mode's own key handler. None of `i`, `r`
or `v` is a key in any mode, so they are inert.

### `--run-for` is for boards that never go quiet

`--idle-timeout` ends a run on **silence**, which is the right rule for a walk. It cannot
end a run against a board that talks continuously — **a responder hunting for an initiator
prints a tuning line on every dwell**, so an erase command against one runs forever. That
is not hypothetical; the documented erase recipe did exactly that.

Use `--run-for` for any setup command: it is a wall-clock deadline that does not care what
the board is saying. `--run-for` must exceed `--key-after`, or the keys are never sent.

### `--idle-timeout` is idle, not wall-clock

A walk spends most of its time with the initiator silent — it is ARMED, waiting for a PRG
press several hundred feet away. The only correct reading of "nothing is happening" is
time since the last byte. A wall-clock deadline ends the capture mid-walk and you find out
on the way back.

At SF12 with every probe timing out, the gap between CSV rows can exceed 30 s. The 1800 s
default is what makes that safe; do not lower it below a couple of minutes for a real run.

## FIRMWARE BEHAVIOUR THIS TOOL DEPENDS ON

Current as of 2026-09-04. These are the board-side facts that decide what a command does.

**The initiator boots ARMED.** It runs nothing until the first PRG press on the responder,
so a `--reset` capture can be started at any time without racing a responder that is not
listening yet — and **positions run 1, 2, 3…, never 0.** A trace whose first position is 0
came from firmware older than this.

**The responder saves each position as its sweep completes**, on the armed beacon rather
than on the next PRG press. The last position of a walk is therefore already in NVS when
the board is powered down; you no longer have to press PRG one extra time to keep it.

**The survey's site cursor is persisted; the role is not.** A power cycle re-asks the role
(R1) but resumes the campaign — `# resuming campaign at site N <name>`. Use `n`/`b` to move
the cursor without storing; pressing PRG to "step past" finished sites would write the
empty run in progress over each one.

**`SURVEY` is reachable by holding PRG** (~1.5 s), not only by serial `v`. `--role survey`
sends `v`, which is the tethered equivalent.

**The port is opened with DTR deasserted, and that is not cosmetic.** On this carrier DTR
drives IO0, which is GPIO 0, which is the PRG button — so a naive `serial.Serial(port)`
holds PRG down just by opening the port. In survey mode a press is store-and-advance, so
every tethered session used to store a bogus run and step the campaign cursor on by one.
It presents as an erase that "does not stick": the erase works, and a phantom press
immediately re-stores site 0. Fixed on both sides — the tool never asserts DTR, and the
firmware now requires the line to hold low for 50 ms before it counts as a press.

**Completion markers are matched as substrings anywhere in a `#` line.** A new firmware
message containing one silently truncates a capture — this happened once, when the
responder's armed line read "sweep complete". Check `COMPLETION_MARKERS` before wording a
new `#` message.

## SCHEMAS RECOGNISED

Three, and a data row is validated against **that** header's field count — so adding a
column to one schema cannot silently start dropping rows of another.

| Trace | Header begins | Completion marker |
|---|---|---|
| Sweep (R7) | `position,tp_index,` | `# sweep complete` |
| Ambient survey (R8) | `site_index,site_name,` | `# survey dump complete` / `# survey campaign complete` |
| Responder position log (R6) | `RESP,position,` | `# responder log complete` |

## OUTPUT FILE

```
# LRAN range test trace, captured <timestamp>
# note: <--note text>
# schema: docs/rangetest/data/README.md
#
# board=...            <- the R3 settings dump, verbatim
# antenna_gain_dbi=3.0
# ...
#
<header row>
<data rows>
# capture ended: <reason> - N rows
```

**A file without the `# capture ended:` line was truncated** by something that never got
to finish. Treat its last unit of work as suspect.

**Check `antenna_gain_dbi` in the settings block** before trusting a run. It is an input
to the D33 power clamp, not just a column.

### `# board rebooted here`

The board reset mid-capture. Rows before it are kept deliberately — an hour of walking is
not thrown away because the far end browned out. But **`position` is owned by the
responder**, so a *responder* reset restarts numbering at 0 and later positions collide
with earlier ones. Renumber from your notes, or treat the halves as two traces.

A repeated header with no fresh settings dump before it is a benign section break, not a
reboot, and is not marked.

---

## RECIPES

Every one of these is a complete command. Substitute the port; the two device nodes can
swap between invocations, so confirm the board by its OLED badge.

### The position walk

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset \
    --out docs/rangetest/data/2026-09-04-walk-gatelink.csv \
    --note "bearing 120deg, both ends 1.2m AGL, gate ~70ft above bridge, 3.0dBi, dry"
```

One capture for the whole walk. Ctrl-C when you get back.

Nothing is recorded until your first PRG press — the initiator is ARMED and waiting, and
prints `# ARMED at boot - press PRG on the responder to start position 1`. **Expect the
first position to be numbered 1.**

A capture stopped before any sweep completes therefore exits **1** with "no data rows
captured". That is the tool being honest, not a failure — nothing was measured.

### Read the responder's position log after the walk

The board comes home with the log in it and dumps it at boot. This is the
**reverse-direction data and the only copy of it** — the sweep CSV cannot separate
downlink loss from uplink loss without it.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role responder --sweeps 1 \
    --out docs/rangetest/data/2026-09-04-walk-gatelink-resplog.csv \
    --idle-timeout 60 \
    --note "responder log, walk of 2026-09-04, bearing 120deg"
```

Stops on its own at `# responder log complete`. Expect one row per position visited.

### Read the whole survey campaign back

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role survey --sweeps 1 \
    --out docs/rangetest/data/2026-09-04-survey-campaign.csv \
    --idle-timeout 60 \
    --note "seven-site ambient survey, ~5 min per site, 3.0dBi at 1.2m"
```

The boot dump-all is what gets captured. Expect **130 rows per stored site**, one header,
and `# survey campaign complete - N site(s)`.

### Scan here for a while, then capture the site (tethered)

**`--key d` dumps without storing** — use it when you only want the trace in the file:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role survey \
    --key-after 300 --key d --sweeps 1 \
    --out docs/rangetest/data/2026-09-04-survey-bridge.csv \
    --idle-timeout 60 --note "bridge, 5 min, 3.0dBi at 1.2m"
```

**`--key p` stores the site to NVS and advances the cursor.** `p` **is** the PRG press:
the tool performs it for you, and the site name on the OLED changes with nobody touching
the board. That surprises people who are also being told to press PRG themselves — decide
which of the two is doing it, not both:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role survey \
    --key-after 300 --key p --sweeps 1 \
    --out docs/rangetest/data/2026-09-04-survey-bridge.csv \
    --idle-timeout 60 --note "bridge-house, 5 min, 3.0dBi at 1.2m"
```

`p` is the same action as a PRG press. The cursor it leaves behind survives the power
cycle, so unplugging and walking on resumes at the next site.

### Erase bench data before a field run

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role survey --key z \
    --out /tmp/erase.csv --run-for 20
```

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role responder --key x \
    --out /tmp/erase.csv --run-for 20
```

**`--run-for`, not `--idle-timeout`.** A responder hunting for an initiator never stops
talking, so an idle deadline never fires and the command runs forever.

Both end with "no data rows captured" — correct, there is nothing to capture. What you
want is the echoed confirmation:

```
  sent key: z
  # all stored surveys erased from NVS, site cursor reset
```

> **Trust the confirmation from the command that did the work**, not a separate check
> afterwards. Every `--reset` re-enumerates the USB bridge, and with two identical CP2102s
> both reporting `SER=0001` the device nodes can swap between invocations — an erase
> confirmed on `0001` and a check run against `0001` are not necessarily the same board.
> This cost twenty minutes once already.

---

## CONSOLE KEYS BY MODE

Sendable with `--key`, or by thumb on the board.

| Mode | Key | Effect |
|---|---|---|
| survey | `d` | dump the run in progress |
| survey | `a` | dump every stored site |
| survey | `s` | store this site, do **not** advance |
| survey | `p` | store and advance to the next site (same as PRG) |
| survey | `n` | next site **without storing** |
| survey | `b` | previous site **without storing** |
| survey | `x` | clear the run in memory, NVS untouched |
| survey | `z` | **erase every stored survey** |
| survey | `[` `]` | dwell per bin, −/+ 10 ms |
| responder | `p` | increment position (same as PRG) |
| responder | `d` | dump the stored position log |
| responder | `x` | clear the position log |
| initiator | `s` | force the next sweep while armed |
| initiator | `n` | advance the position locally |

## EXIT STATUS

`0` on a written trace, `1` when no header or no data rows were seen, `2` for `--role`
without `--reset` or `--run-for` not exceeding `--key-after`.

A setup command (`--key z`, `--key x`) exits **1** with "no data rows captured". That is
correct — there is nothing to capture, and the result you want is the echoed confirmation
line.

## SEE ALSO

[`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) · [`data/README.md`](./data/README.md) ·
[`engineering-log.md`](./engineering-log.md) ·
[`../../firmware/range-test/CLAUDE.md`](../../firmware/range-test/CLAUDE.md)
