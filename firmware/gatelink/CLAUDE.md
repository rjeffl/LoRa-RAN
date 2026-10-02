# `gatelink` — node `0x01`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to GateLink's firmware.

**Primary document:** `docs/gatelink/LRAN-GateLink_Node-Implementation-Plan` v0.22, §5
for the architecture and §8 for the milestones.
**Requirements:** `docs/gatelink/LRAN-GateLink_Node-PRD` v0.14.
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.17** (`ver = 2`).
**Session state:** `docs/gatelink/HANDOFF.md`. Start there.

## What exists here today

**The L6 skeleton.** It boots on a bare StamPLC, prints its banner, draws the boot page
and starts the seven tasks of plan §5.2 with stub bodies. Each stub counts its passes and
waits out its period. `log_task` prints the counts every 30 s as an `alive:` line.

| File | What it holds | Native? |
|---|---|---|
| `main.cpp` | Boot, banner, task start. **The only file that includes `secrets.h`**, for `LRAN_GATELINK_NODE_KEY` alone | no |
| `tasks.{h,cpp}` | The task table and its invariants | yes |
| `task_runtime.{h,cpp}` | Static task creation and the stub bodies | no |
| `ui_pages.{h,cpp}` | Panel text, and the node key's status | yes |
| `board_stamplc.{h,cpp}` | The board layer over M5StamPLC; the panel only, until GL1 | no |

Every other file in plan §5.3's module map arrives with the milestone that fills it.
**`board_profile.h` waits for GL0**: which Wio-SX1262 the carrier uses is not recorded,
and the Kit's pins on the header board give a carrier that never answers.

## Build and test

```bash
pio test -d firmware/gatelink -e native        # task table and boot page
pio run  -d firmware/gatelink -e gatelink      # target; needs LRAN_GATELINK_NODE_KEY
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
