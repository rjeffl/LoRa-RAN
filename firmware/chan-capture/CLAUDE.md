# lran-chan-capture — context

**The root `CLAUDE.md` is authoritative.** This file carries only what is specific to the
listen-only receiver.

## What it is

**A receiver that samples one channel's RSSI about 100 times a second and never transmits.**
It runs `lib/lran-link`'s `ChanMonitor`, the bridge's M25 sampler, and writes the same
`CHAN`, `CHANSUM` and `CHAN-BOOT` lines. `tools/simctl/rssi_capture.py` and `rssi_report.py`
read its files unchanged. It exists for
[`LRAN-D1-Frequency-Change-Brief`](../../docs/shared/LRAN-D1-Frequency-Change-Brief.md) §5, which
runs several receivers side by side, one per candidate frequency, over the same night.

**It needs no `secrets.h`.** It holds no key, joins no network and sends no frame.

## Traps

- **It must never transmit.** Several of these sit a metre apart on frequencies 0.2 MHz
  apart, and a frame from one would show in its neighbours' captures as an excursion that
  nothing traces. `tools/checks/chan_capture_never_transmits.py` fails CI if `src/` calls a
  transmit, a CAD, `setOutputPower`, WiFi or Bluetooth. `receiver.cpp` is the only file that
  includes RadioLib.
- **The frequency is stored in NVS, not built in.** `freq <hz>` on the serial console stores
  it and reboots. A board keeps its last frequency across a reflash of the same image, so
  **read the file's `CHAN-BOOT` line before believing which channel it measured.** With
  nothing stored, the board samples `kPhy.freq_hz`.
- **Its files differ from the bridge's in four ways.** It writes no `FRAME` lines, so
  `rssi_report.py` has no transmissions of ours to correlate against. It samples on a fixed
  10 ms period, where the bridge samples once per `lora_task` wake. `own_rx` in its `CHAN`
  lines counts LRAN-PHY frames it heard, and it keeps sampling through them. And it restarts
  receive every 100 ms, so it rarely decodes a frame longer than that. `main.cpp` says why.
- **Never remove the 100 ms restart.** Without it, a strong burst 0.2 MHz away, such as the
  bridge's poll a metre off, left the receiver reading about −74 dBm on a quiet channel until
  receive was restarted. The IRQ status read 0x0000 during it, so nothing the radio reports
  shows the state; `receiver.cpp` has the evidence. A capture from a build without the
  restart has plateaus that look like a continuous carrier.
- **`radio` and `restart` on the console probe the receiver live.** `radio` prints the IRQ
  status and one RSSI reading; `restart` prints both sides of a forced restart. Both run on
  the sampler task, which is the only task that touches the SPI bus.
- **Two receivers do not read the same.** The Heltec and the XIAO + Wio Kit differ in front
  end, RF switch and antenna, and two boards of one kind can differ too. Brief §5 puts every
  receiver on one frequency first to measure the offset between them.
- **`rssi_report.py` calls a periodic source "not periodic" once a capture runs long enough,
  and the verdict is wrong rather than the source.** Day 1's 24-hour capture reported the
  property's Davis station as "not periodic (509 events)" in the −70 to −60 dBm band, where
  the same events fit a 130.6882 s clock to a **median residual of 0.54 s** over 660
  occurrences. Two defects in `periodicity()` in `tools/simctl/rssi_analyze.py` combine, and
  both grow with capture length: a gap shorter than half the guessed period is charged a whole
  period, so a foreign event in the band invents an occurrence; and the verdict gates on the
  **maximum** residual, so one foreign event flips a clean clock. Neither shows over one hour.
  **Read a long capture's periodicity by seeding a fit with a known period and reading the
  residuals** rather than trusting the printed verdict.
  [`LRAN-D1-Parallel-Capture-Analysis`](../../docs/shared/LRAN-D1-Parallel-Capture-Analysis.md)
  has the figures and the mechanism. **Unfixed on 2026-09-20** by operator direction, since no
  capture is planned; fix it before the next run, because the verdict is what this repository
  cites when it attributes an occupant.
- **The XIAO has not run a long capture yet.** Its native USB drops output written before the
  host opens the port, so `setup()` waits up to 5 s for a host before printing the header.
  **`rssi_capture.py --reset-on-open` was built for the Heltec's CP2102 and is untested on
  the XIAO**, whose port also disappears and returns across a reset. Check its first
  capture file for `#RESET` and `CHAN-BOOT` before leaving it unattended.

## Build

```bash
pio test -d firmware/chan-capture -e native
pio run  -d firmware/chan-capture -e heltec   -t upload --upload-port <port>
pio run  -d firmware/chan-capture -e xiao-wio -t upload --upload-port <port>
python3 tools/checks/chan_capture_never_transmits.py
```

**Tell the two Heltecs apart by enclosure**, as the bridge's `CLAUDE.md` says: both CP2102
bridges report `SER=0001`, and port names move on replug.
