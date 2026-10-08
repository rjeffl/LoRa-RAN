# `gatelink` — node `0x01`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to GateLink's firmware.

**Primary document:** `docs/gatelink/LRAN-GateLink_Node-Implementation-Plan` v0.29, §5
for the architecture and §8 for the milestones.
**Requirements:** `docs/gatelink/LRAN-GateLink_Node-PRD` v0.16.
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.17** (`ver = 2`).
**Session state:** `docs/gatelink/HANDOFF.md`. Start there.

## What exists here today

**The L6 skeleton, with GL1's board layer under `io_task` and GL3's protocol node in
`lora_task`.** It boots, clears the relay latch, prints its banner, draws the boot page and
starts the seven tasks of plan §5.2. `io_task` times relay pulses, debounces the inputs,
reads the buttons and sensors, and carries out each gate command's relay sequence.
`lora_task` runs lran-node's engine on the carrier radio: it announces `BOOT`, answers
`POLL`, `PING`, `ROLL_CONTEXT` and `COMMAND`, and sends each `COMMAND_ACK` after io_task
reports the last trailing edge (plan §5.2). It does not answer `CONFIG` yet. `ui_task`
shows the INA226 and LM75 on the panel's last line. `log_task` runs a bench console
(`relay <1-4> [ms]`, `in`, `sense`, `sd`, `beep`, `radio`, `lran`, `bus <s>`, `restart`,
`hang`), prints the engine's log lines, and prints the pass counts every 30 s as an
`alive:` line. `radio` prints the driver's counters and its last RadioLib error; `lran`
prints the context, frame counts, refusals and commands. `bus <s>` is GL1's SPI test. The
other bodies are stubs.

| File | What it holds | Native? |
|---|---|---|
| `main.cpp` | Boot, banner, task start. **The only file that includes `secrets.h`**, for `LRAN_GATELINK_NODE_KEY` alone | no |
| `tasks.{h,cpp}` | The task table and its invariants | yes |
| `task_runtime.{h,cpp}` | Static task creation, `io_task`, `lora_task`'s node, the bench console in `log_task`, and the stub bodies | no |
| `gatelink_app.{h,cpp}` | GateLink's lran-node application: spec §8.1 commands to K1–K4, status and event bodies, PING echo | yes |
| `ui_pages.{h,cpp}` | Panel text, and the node key's status | yes |
| `board_stamplc.{h,cpp}` | The board layer over M5StamPLC: relays, inputs, buttons, buzzer, sensors, RTC, panel, microSD | no |
| `spi_bus.{h,cpp}` | The one SPI lock (plan §5.2) | no |
| `gate_io.{h,cpp}` | Relay pulse timing, the command sequencer and input debounce, with time passed in | yes |
| `radio.{h,cpp}` | The SX1262 driver: receive, spec §12.3 media access, transmit, and GL1's sync-word probe | no |
| `board_profile.h` | The carrier's radio as a `RadioPins` value, for the header board (p-6379), and the VE.Direct UART pins | yes |
| `bringup.cpp` | The GL0 bring-up console, built only by `gatelink-bringup` in place of `main.cpp`. `cad [rx]` times one channel scan. `ved` reads VE.Direct text and runs read-only HEX; `ved edges` finds a crossed harness | no |

Every other file in plan §5.3's module map arrives with the milestone that fills it.
**The carrier carries the header board, not the Kit** (operator, 2026-10-05). The Kit's
pins on the header board give a carrier that never answers.

## Build and test

```bash
pio test -d firmware/gatelink -e native        # task table, boot page, pulse, sequencer, GateLinkApp
pio run  -d firmware/gatelink -e gatelink      # target; needs LRAN_GATELINK_NODE_KEY
pio run  -d firmware/gatelink -e gatelink-bringup  # GL0 console; no secrets.h
python3 tools/checks/io_task_never_blocks.py   # R-5.2a, plan §5.2
python3 tools/checks/node_holds_no_master.py   # plan §6.8
```

**The target build needs `LRAN_GATELINK_NODE_KEY` in the root `secrets.h`.** A copy made
before L5 lacks it, and `main.cpp` stops the build with a message naming the fix:

```bash
python3 tools/provision/node_key.py
```

It prints one `#define` to paste into `secrets.h`. Never paste it anywhere else.

## Rules specific to this firmware

- **`main.cpp` never names the master.** GateLink holds its own derived key (plan §6.8).
  `node_holds_no_master.py` fails CI if any code or build flag here names it.
- **`io_task` waits on nothing but `vTaskDelayUntil`.** No lock, no queue timeout, no
  `Serial`. `io_task_never_blocks.py` reads its body.
- **Only `io_task` touches the internal I²C bus** once tasks run: relays, inputs,
  buttons, INA226, LM75 and RTC. M5GFX locks each transaction with `portMAX_DELAY`, so a
  second user could stall `io_task`.
- **`board_relays_off_early()` is the first call in `setup()`** (PRD R-3.5j). The relay
  expander does not reset with the ESP32.
- **Only `log_task` writes to `Serial`.** A full USB CDC buffer blocks the writer.
- **The native `build_src_filter` is the seam.** A file that moves across it is a visible
  edit to `platformio.ini`.
- **A period an operator may need to change is a parameter.** `tasks.cpp` names the
  `lran-config` row (`input_poll_ms`, `bms_poll_s`) rather than a number.

## Traps

- **`Serial` is silent without `-DARDUINO_USB_CDC_ON_BOOT=1`**, while boot ROM lines still
  appear. The flag is set; do not remove it.
- **The StamPLC and the XIAO both enumerate as `/dev/cu.usbmodem*`.** Read the banner, and
  pass `--upload-port`.
- **The panel is landscape 240×135, turned 180° because GateLink mounts the StamPLC upside
  down.** The case's bezel covers the left edge, so each line starts 6 pixels in and holds
  19 characters at text size 2.
- **Read the installed M5StamPLC headers under `.pio/libdeps/`**, not GitHub.
- **With the 12 V supply on, the StamPLC's USB does not enumerate on a USB-C host port.**
  The board backfeeds 5 V onto VBUS. Go through a USB 2.0 hub with USB-A ports
  (engineering log, 2026-10-05).
- **GPIO 3, the LCD's reset, also holds both IO expanders in reset.** After a chip reset
  nothing answers at 0x43 or 0x59 until it is driven high (engineering log, 2026-10-06).
- **USB VBUS keeps the StamPLC running with the 12 V off.** Unplug USB to power-cycle it.
- **A radio that resets on TX or CAD looks like a firmware hang.** `begin` succeeds, then the
  first `tx` or `cad` gets no DIO1 edge and every later command fails with -707 or
  `WRONG_MODEM`. It was the expansion board's power (engineering log, 2026-10-07). Run the
  bring-up image's `tx` and `cad` before debugging the driver.
- **`SPI.begin(7, 9, 8)` before `M5StamPLC.begin()`.** Otherwise `SD.begin()` starts the
  bus on the board definition's defaults, G11–G13.
