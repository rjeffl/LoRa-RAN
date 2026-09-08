# Third-party notices

LRAN is MIT licensed (`LICENSE`, `Copyright (c) 2026 Robert J. Lee`). It builds against
third-party components that carry their own licenses and their own attribution
requirements. This file records them.

**This repository vendors none of them.** Every component listed here is fetched at build
time by PlatformIO from its own distribution, at the version pinned in the relevant
`platformio.ini`. Nothing in `lib/`, `firmware/`, `tools/` or `wattcycle-reader/src/` is
copied from any of them.

**When the obligations attach.** MIT and BSD-2-Clause require the copyright notice and
permission notice to travel with source or binary redistribution; Apache-2.0 additionally
requires a copy of the license and retention of any `NOTICE` file. Publishing this
repository's *source* does not distribute those components. **Distributing a compiled
binary does**, because the linked library code goes with it. If a `.bin` or a packaged
firmware image is ever released, ship this file with it.

The system-level inventory is System PRD §11.1, which covers the design as planned. This
file covers what is **actually in a build today**, and §3 records where the two differ.

---

## 1. In the build today

Versions are the ones pinned in each project's `platformio.ini`. Where a caret range is
pinned, the resolved version installed at the time of writing is given in brackets.

### RadioLib — MIT

Copyright (c) 2018 Jan Gromeš

SX1262 driver for every firmware in this repository (**D32**). Pinned exactly at
**7.7.1** in `firmware/range-test/platformio.ini`, both environments.
`firmware/range-test/src/pa_config.cpp` mirrors a file-static table from this library
rather than copying its code; `tools/rangetest/check_pa_table.py` checks the mirror
against the pinned version.

### ESP8266 and ESP32 OLED driver for SSD1306 displays (ThingPulse) — MIT

Copyright (c) 2016 by Daniel Eichhorn
Copyright (c) 2016 by Fabrice Weinberg

OLED display driver. Pinned at **4.6.2** in `firmware/range-test/platformio.ini`; pinned
at **^4.4.0** [4.6.2] in `wattcycle-reader/platformio.ini`.

### Unity — MIT

Copyright (c) 2007-25 Mike Karlesky, Mark VanderVoord, & Greg Williams

Host and target test framework. Supplied by PlatformIO's `unity` test framework rather
than by a `lib_deps` entry, in every `native` environment and in
`lib/lran-protocol`'s `esp32s3` test environment. **Test-only — it is not linked into any
firmware image**, so it carries no obligation on a released binary.

### NimBLE-Arduino — Apache-2.0

Copyright the NimBLE-Arduino authors (h2zero) and the Apache Mynewt NimBLE project.

BLE stack for the BMS client. Pinned at **^1.4.2** [1.4.3] in
`wattcycle-reader/platformio.ini`.

**Its `LICENSE` is Apache-2.0 plus an appended notice section, and that section carries
obligations of its own.** There is no separate `NOTICE` file, so the appended text is the
notice. It declares bundled or derived code under other terms:

- **queue.h 8.5** and **tinycrypt** — BSD-3-Clause.
- **FreeBSD**-derived code — BSD.
- **Gary S. Brown's CRC32** — Copyright (C) 1986 Gary S. Brown, usable without
  restriction.
- **esp32-snippets** — Copyright 2017 Neil Kolban.

A binary linking NimBLE must carry that whole section, not just the Apache-2.0 text.
Reproduce `LICENSE` from the pinned package verbatim rather than summarizing it from this
list.

### M5StamPLC — MIT

Copyright (c) 2024 M5Stack Technology CO LTD

StamPLC board HAL. Pinned at **^1.2.0** [1.2.0] in `wattcycle-reader/platformio.ini`.
It pulls in M5Unified and M5GFX transitively.

### M5Unified — MIT

Copyright (c) 2021 M5Stack

Resolved transitively through M5StamPLC. Installed at **0.2.27**.

### M5GFX — MIT, containing BSD-2-Clause code

Copyright (c) 2021 M5Stack

Resolved transitively through M5StamPLC. Installed at **0.2.20**. **Its own license is
MIT, but it is a fork of LovyanGFX and carries that code's licensing inside it:**

- **LovyanGFX** — BSD-2-Clause ("FreeBSD"), Copyright (c) lovyan03 (Ryo Suzuki). Most of
  `src/lgfx/`.
- **Adafruit GFX fonts** — BSD-2-Clause, Copyright (c) Adafruit Industries and the
  original font authors. `src/lgfx/Fonts/GFXFF/` and `src/lgfx/Fonts/glcdfont.h`.

Attributing M5GFX as MIT alone would be wrong. Both notices above have to travel with a
distributed binary that links it.

### Arduino-ESP32 core — LGPL-2.1-or-later, with Apache-2.0 components

Copyright (c) Espressif Systems (Shanghai) Co., Ltd. and the Arduino-ESP32 contributors.

The `arduino` framework for every ESP32 target here. Supplied by the `espressif32`
platform: **6.13.0** pinned in `firmware/range-test/platformio.ini`, **^6.9.0** in
`wattcycle-reader/platformio.ini` and `lib/lran-protocol/platformio.ini`'s `esp32s3`
environment (unpinned there). The installed core package is
`framework-arduinoespressif32 3.20017`.

The core bundles ESP-IDF components under **Apache-2.0**, including **mbedTLS**
(Copyright The Mbed TLS Contributors; Apache-2.0), which `lib/lran-protocol/` uses for
HMAC and HKDF on target. NVS/Preferences and the filesystem layers come from the same
place.

**LGPL-2.1 in an embedded build.** The Arduino core is statically linked. LGPL-2.1 §6
is satisfied in the usual embedded way — by making the object files or the full
corresponding source available so a recipient can relink. This repository is MIT source,
so the practical obligation is on a *distributed binary*: state the core's version and
license, and point at Espressif's source. It does not make LRAN's own code copyleft.

---

## 2. Not vendored, and deliberately so

### TDT BMS protocol client — LRAN's own code

`wattcycle-reader/lib/bms_ble/` is an original implementation written against
`aiobmsble` as a **behavioural reference**, from captured frames. No code was taken from
`aiobmsble` or `BMS_BLE-HA`. **If any ever is, verify its license first and add it here** —
System PRD §11.1 carries the same standing condition.

### Nice BusT4 protocol logic — GPL-3.0, not used

The `pruwait` / `xdanik` / `makstech` community lineage is **GPL-3.0** and is **not used
in v1**. It appears in the research archive as reference material only, and nothing in
this repository derives from it. A future port would make *that binary* GPL-3.0
regardless of this repository's license; System PRD §11.2 requires such a port to live in
its own clearly-marked subtree.

---

## 3. Where this differs from System PRD §11.1

§11.1 is the planned inventory and predates most of the build. Three differences are
worth recording rather than silently reconciling:

1. **§11.1 omits two components that are in the build today.** Unity and the ThingPulse
   SSD1306 driver appear in no row of it. Unity is test-only; the SSD1306 driver is
   linked into the range-test firmware.
2. **The LCD row names the wrong package.** §11.1 says "LovyanGFX (via M5Unified),
   BSD-2-Clause". The actual path is M5StamPLC → M5Unified + **M5GFX**, and M5GFX's own
   license is **MIT** with LovyanGFX's BSD-2-Clause code inside it. The license names in
   §11.1 are individually right and the attribution derived from them would be
   incomplete.
3. **Several §11.1 rows have no build yet.** PubSubClient, ArduinoJson, U8g2 and the
   VE.Direct parser are all listed there and none is installed, because the bridge and
   GateLink firmwares do not exist. **U8g2 in particular should be re-checked when a
   display target is next chosen** — the firmware that exists uses the ThingPulse driver
   instead, so §11.1's row may describe a decision that was quietly changed rather than a
   component still to come.

**This file is updated in the same commit as any change to a `lib_deps` line, a platform
pin or a framework version.** A notices file that lags the build is worse than none,
because it reads as a check that was performed.

---

## 4. License texts

Full texts ship inside each installed package under `.pio/libdeps/`, which is not
committed. The two short licenses are reproduced here so this file stands alone;
Apache-2.0 is linked rather than inlined for length.

### MIT

Applies to: RadioLib, the ThingPulse SSD1306 driver, Unity, M5StamPLC, M5Unified, M5GFX,
and LRAN itself. Each component's own copyright line is in §1; the permission notice is
identical to the one in this repository's `LICENSE`.

### BSD-2-Clause

Applies to: LovyanGFX and the Adafruit GFX fonts inside M5GFX.

```
Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of
   conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list
   of conditions and the following disclaimer in the documentation and/or other
   materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Apache-2.0

Applies to: NimBLE-Arduino, and the ESP-IDF components bundled with the Arduino-ESP32
core, including mbedTLS. Full text: <https://www.apache.org/licenses/LICENSE-2.0>

### LGPL-2.1-or-later

Applies to: the Arduino-ESP32 core. Full text:
<https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>
