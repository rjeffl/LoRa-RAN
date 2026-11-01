# `tools/bench/` — the bench harness

`bench.py` puts every serial console and the broker's `lran/#` traffic in one
timestamped log, and runs commands you append to a control file. Use it for any bench
check instead of writing a serial or MQTT script in a scratchpad.

Run it with PlatformIO's Python, which has pyserial:

```bash
PY=~/.platformio/penv/bin/python
```

## The loop

1. **Find the ports.** The adapter names change between boards and USB sockets.

   ```bash
   $PY tools/bench/bench.py ports
   ```

2. **Start the harness in the background**, with one `--port TAG=DEVICE` per board. Add
   `--mqtt` to log `lran/#` and allow `pub`. It reads the broker address and credentials
   from `secrets.h` and never prints them. The root `secrets.h` is production from
   2026-10-31. For the sandbox broker, add `--secrets secrets.sandbox.h` after `run`.

   ```bash
   $PY tools/bench/bench.py run --port BRG=/dev/cu.usbserial-0001 --port SIM=/dev/cu.usbmodem1101 --mqtt
   ```

3. **Send commands.** `send` prints a mark id, which tells `wait` where these commands
   start in the log.

   ```bash
   $PY tools/bench/bench.py send "SIM bms on 49A1" 'pub lran/bridge/config/set {"set":{"simnode_diag_enable":true}}'
   ```

4. **Wait for the result**, not for a fixed time. `wait` returns on the first match after
   the mark. On a timeout it exits 1 and prints the last lines of the log.

   ```bash
   $PY tools/bench/bench.py wait 'decode 0x8C' --since <mark> --tag SIM --timeout 30
   ```

5. **Stop it** with `send quit`. Copy the log you want to keep into the engineering log
   or a committed trace.

Each log line reads `<local time with UTC offset> <seconds since start> <tag> <text>`,
for example `2026-10-25T18:30:13-0400    112.04 MQTT lran/gatelink/node/state {...}`. An
MQTT line starting `<retained>` is one the broker replayed from its store, on a subscribe
or a reconnect, not one a node just sent.

`bench.log` and `ctl.txt` live in `--dir`. The default is `$LRAN_BENCH_DIR`, or
`/tmp/lran-bench` when that's unset. Give each session its own directory, or the next
session's `wait` finds this session's lines.

## A soak, or any run that outlives the session

Start `run` outside any Claude Code session, under `nohup`, `systemd-run --user` or a
terminal that stays open. A session's background task ends with the session, and the
first GateLink soak's log stopped 4.3 h short that way (GateLink engineering log,
2026-10-25).

`nohup`'s redirect needs the directory to exist first:

```bash
D=~/lran-soak/$(date +%F); mkdir -p "$D"
nohup $PY tools/bench/bench.py --dir "$D" run --mqtt > "$D/run.out" 2>&1 &
```

## Control-file commands

| Command | Effect |
|---|---|
| `<TAG> <text>` | Write `<text>` and a newline to that port |
| `pub [-r] <topic> <body>` | MQTT publish at QoS 0; `-r` retains |
| `reset <TAG>` | Pulse RTS, which is EN on the Heltec and XIAO adapters |
| `hold <TAG>` / `release <TAG>` | Hold the board in reset, then let it go |
| `mark <text>` | A marker line that `wait --since` finds |
| `quit` | Stop `run` |

Ports open with DTR and RTS low. Asserting either on open resets an ESP32 through the
adapter's auto-reset circuit, and the boot that follows is not the one under test.

## Reading a console without a reset

`run` opens a port through pyserial, and that resets the StamPLC, which clears its RAM
counters. `attach` opens it with no reset, sends its commands and prints to stdout. On
macOS the board stayed up across it; Linux is not tested (GateLink engineering log,
2026-11-01). Quit `run` first if it holds the port.

```bash
$PY tools/bench/bench.py attach /dev/cu.usbmodem101 bms --until 'bms:'
$PY tools/bench/bench.py attach /dev/cu.usbmodem101 'file cat /bms.log' --seconds 120 --until 'file: /bms.log' > bms.log
```

A `file cat` ends in a line with the bytes sent and their zlib CRC. Cut that line and the
newline before it from the saved file, then compare `zlib.crc32` of the rest.

## Tests

`test_bench.py` needs no board. It covers the `secrets.h` reader, the MQTT packets against
a fake broker, the control-file commands and `wait`. CI's checks job runs it.
