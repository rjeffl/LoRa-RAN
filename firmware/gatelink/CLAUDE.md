# `gatelink` — node `0x01`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to GateLink's firmware.

**Primary document:** `docs/gatelink/LRAN-GateLink_Node-Implementation-Plan` v0.28, §5
for the architecture and §8 for the milestones.
**Requirements:** `docs/gatelink/LRAN-GateLink_Node-PRD` v0.16.
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.17** (`ver = 2`).
**Session state:** `docs/gatelink/HANDOFF.md`. Start there.

## What exists here today

**The L6 skeleton, with GL1's board layer under `io_task`.** It boots, clears the relay
latch, prints its banner, draws the boot page and starts the seven tasks of plan §5.2.
`io_task` times relay pulses, debounces the inputs and reads the buttons and sensors.
`log_task` runs a bench console (`relay <1-4> [ms]`, `in`, `sense`, `sd`, `beep`) and
prints the pass counts every 30 s as an `alive:` line. The other bodies are stubs.

| File | What it holds | Native? |
|---|---|---|
| `main.cpp` | Boot, banner, task start. **The only file that includes `secrets.h`**, for `LRAN_GATELINK_NODE_KEY` alone | no |
| `tasks.{h,cpp}` | The task table and its invariants | yes |
| `task_runtime.{h,cpp}` | Static task creation, `io_task`, the bench console in `log_task`, and the stub bodies | no |
| `ui_pages.{h,cpp}` | Panel text, and the node key's status | yes |
| `board_stamplc.{h,cpp}` | The board layer over M5StamPLC: relays, inputs, buttons, buzzer, sensors, RTC, panel, microSD | no |
| `spi_bus.{h,cpp}` | The one SPI lock (plan §5.2) | no |
| `gate_io.{h,cpp}` | Relay pulse timing and input debounce, with time passed in | yes |
| `board_profile.h` | The carrier's radio as a `RadioPins` value, for the header board (p-6379) | yes |
| `bringup.cpp` | The GL0 bring-up console, built only by `gatelink-bringup` in place of `main.cpp` | no |

Every other file in plan §5.3's module map arrives with the milestone that fills it.
**The carrier carries the header board, not the Kit** (operator, 2026-10-05). The Kit's
pins on the header board give a carrier that never answers.

## Build and test

```bash
pio test -d firmware/gatelink -e native        # task table, boot page, pulse and debounce
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
  buttons, INA226, LM75 and RTC. Whether M5Unified's I²C class locks is not established.
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
- **`SPI.begin(7, 9, 8)` before `M5StamPLC.begin()`.** Otherwise `SD.begin()` starts the
  bus on the board definition's defaults, G11–G13.
