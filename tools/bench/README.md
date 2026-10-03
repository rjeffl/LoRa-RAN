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
   from `secrets.h` and never prints them.

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

`bench.log` and `ctl.txt` live in `--dir`. The default is `$LRAN_BENCH_DIR`, or
`/tmp/lran-bench` when that's unset. Give each session its own directory, or the next
session's `wait` finds this session's lines.

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

## Tests

`test_bench.py` needs no board. It covers the `secrets.h` reader, the MQTT packets against
a fake broker, the control-file commands and `wait`. CI's checks job runs it.
