# LRAN Range Test Firmware — Pass 2 Tasks

**Status:** **Phase A and Phase B complete 2026-09-05.** Both targets build, 182 host tests
pass, both boards flashed and measured on the bench: Heltec pair **192/192** (reproducing
the pass-1 reference exactly) and XIAO→Heltec **192/192**. X1 confirmed over the air.
**Still open: the gate-bearing walk (B1b / D1) and the XIAO ambient survey.**
**Revision:** 0.1 (2026-09-05)
**Target:** Seeed XIAO ESP32S3 + Wio-SX1262 **Kit** (B2B), on a Seeeduino XIAO Expansion Board
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` v0.16 (`ver = 2`)
**Predecessor:** `LRAN-Range-Test-Firmware-Pass1-Tasks.md` — pass 1 complete, R2 gate passed
on hardware 2026-08-31
**Record:** `docs/rangetest/engineering-log.md`

---

## 1. What pass 2 is, and what it is not

Pass 1 §"Pass 2 — XIAO configuration" states the whole of the debt pass 1 owed pass 2:

> a second `BoardRadioConfig` populated from `gatelink-expansion-board.md`, and nothing
> else changing. If pass 2 turns out to need more than that, R2 was built wrong, and it
> is worth saying so in the log rather than absorbing it quietly.

**Pass 2 needs more than that, and R2 was not built wrong.** The excess is in three
places, none of them the radio pin map:

1. `board.rf_sw` is a field R2 defined and `radio_link.cpp` never reads (task X1). The
   Heltec carries `kPinNone`, so the gap was invisible. This is the divergence pass 1
   predicted by name — *"the first real test of the seam"* — and it is the one change
   that must be right for GateLink.
2. The **board is not the only thing that changes** — the *display and button* change
   with it. R2 scoped a radio seam, correctly; it did not scope a board seam, and
   `ui_oled.h` hardcodes Heltec's Vext/reset/I2C pins (task X4).
3. The **antenna changes**, which is a D33 clamp input and not a cosmetic difference
   (task X10).

Item 1 is a defect in R2's *use*, not its design. Items 2 and 3 are new surface that
pass 1 had no second board to reveal. Recorded here rather than absorbed.

---

## 2. Hardware identification — settled, and it changes what pass 2 can claim

### 2.1 There are two Wio-SX1262 products and they are not pin-compatible

| | **Kit** — "Wio-SX1262 with XIAO ESP32S3" (p-5982) | **Header board** — "Wio-SX1262 for XIAO" (p-6379) |
|---|---|---|
| Interconnect | B2B connector, XIAO underside pads | 2.54 mm headers, D-pads |
| NSS | **41** | 5 (D4) |
| RST | **42** | 3 (D2) |
| BUSY | **40** | 4 (D3) |
| DIO1 | **39** | 2 (D1) |
| RF_SW (RXEN) | **38** | 6 (D5) |
| SCK / MISO / MOSI | 7 / 8 / 9 | 7 / 8 / 9 |
| TCXO | 1.8 V on DIO3 | 1.8 V on DIO3 |
| DIO2 as RF switch | true | true |

**The board in hand is the Kit.** Sources: Meshtastic `variants/esp32s3/seeed_xiao_s3/`
(the Kit) and meshtastic/firmware issue #8409 (the header board, opened precisely because
the Kit variant does not drive it). The two agree that **both** a discrete RXEN line and
`DIO2_AS_RF_SWITCH` are set — see §2.3.

### 2.2 Bridge Impl Plan §2.3.1 finding 2 resolves NEGATIVE — say so plainly

`firmware/simnode/CLAUDE.md` states the risk exactly:

> If the module in the XIAO kit turns out to be a different variant from the one already
> in hand for the carrier, this profile stops validating GateLink's radio while still
> working perfectly.

**That is what happened.** The Kit's B2B pad assignment has nothing in common with the
carrier's D-pad wiring in `gatelink-expansion-board.md` §6 outside the three SPI nets.
What pass 2 does and does not buy, stated so no later reader over-reads it:

| Validated by the Kit | **Not** validated by the Kit |
|---|---|
| The SX1262 silicon and the Wio module's RF performance — sensitivity and radiated power, the B1b delta against the Heltec | The carrier's D-pad → StamPLC GPIO net list (§6) |
| RadioLib 7.7.1 against a non-Heltec SX1262 — D32 exercised on a second board | The carrier's RF_SW routing on Bus 15 (G40) |
| **The discrete-RF-switch code path**, which the Heltec cannot exercise (task X1) | The Grove-cable DIO1/RST run and §7.1.1 open-circuit behaviour |
| The injected-config seam under a genuinely different pin map — R-4.1b | Shared-SPI arbitration on the StamPLC bus (§7.2) |

The rule stands unchanged: **XIAO validates the module; only the carrier validates the
carrier.**

### 2.3 §2.3.1 finding 1 resolves POSITIVE, and confirms §7.3

Finding 1 asked whether the Wio-SX1262 needs a host-driven RF switch line, which could
not be settled from documentation because Seeed publishes no module schematic. Both
variants' board-support definitions set a discrete RXEN **and** `DIO2_AS_RF_SWITCH`.
This confirms `gatelink-expansion-board.md` §7.3 as written:

> Use `setRfSwitchPins(LORA_RF_SW, RADIOLIB_NC)` alongside `DIO2_AS_RF_SWITCH`.

RadioLib 7.7.1 `Module.h` confirms the parameter order is `(rxEn, txEn)`, so `LORA_RF_SW`
is the **RX enable** and TX enable is unconnected. Verified against the pinned version,
not assumed.

### 2.4 The carrier's own pad table gained independent corroboration

`gatelink-expansion-board.md` §6's Wio pad column and `firmware/simnode/CLAUDE.md`'s
`LRAN_PROFILE_XIAO_WIO` both described themselves as *derived* — Meshtastic's variant plus
XIAO D-pad numbering, one chain of reasoning. The header-board column in §2.1 above comes
from a different source (issue #8409) and matches the carrier's pad table **value for
value**: D9 MISO, D8 SCK, D10 MOSI, D3 BUSY, D5 RF_SW, D4 NSS, D1 DIO1, D2 RST.

Two independent derivations agreeing is not a ring-out, and the Kit cannot supply one.
**The carrier's continuity check remains open** — `gatelink-expansion-board.md` §11's
"ring out each D-pad" checkbox does not get ticked by pass 2.

### 2.5 The XIAO D-pad numbering is now transcribed, not derived

Meeting the standard `board_config.h` set for the Heltec entry, from the vendor board
definition at the version this project pins (`espressif32@6.13.0`):

```
~/.platformio/packages/framework-arduinoespressif32/variants/XIAO_ESP32S3/pins_arduino.h

  D0..D10 = 1, 2, 3, 4, 5, 6, 43, 44, 7, 8, 9
  SDA = 5   SCL = 6   SCK = 7   MISO = 8   MOSI = 9
```

This retires the "derived, not transcribed" caveat in `firmware/simnode/CLAUDE.md` for the
**D-pad numbering**. It does not retire it for the **module pad assignment**, which is
what §2.4 is about. Two different claims; only one is now settled.

### 2.6 The Kit is the only variant compatible with the expansion board

`SDA = 5` and `SCL = 6` are the XIAO's I2C pads, and they carry the Seeeduino XIAO
Expansion Board's SSD1306. **The header board would put NSS on GPIO 5 and RF_SW on
GPIO 6** — a head-on collision with the OLED bus. The Kit's B2B pads (38–42) are not
brought out to the D-pad header at all, so radio and display coexist.

This is luck, not design, and it is exactly the kind of thing that is obvious in
hindsight and expensive live. **Task X7 makes it a host test** so the next board added to
this file cannot reintroduce it silently.

Expansion board peripherals and their pads, for the same collision check:

| Peripheral | Pad | GPIO | Pass 2 use |
|---|---|---|---|
| SSD1306 OLED, 128×64 @ 0x3C | D4 / D5 | 5 / 6 | **Yes** — the R6 display |
| User button | D1 | 2 | **Yes** — the R1 role selector (task X5) |
| SD card CS | D2 | 3 | No — but shares the radio's SPI bus, see §6 |
| Buzzer | D3 (A3) | 4 | No |
| RTC PCF8563 | D4 / D5 | 5 / 6 | No — shares the OLED I2C bus, harmless |

---

## 3. Open items — resolved 2026-09-05

All four settled by inspection of the assembled stack. Recorded here because §2.2's
conclusion rests on the interconnect, and because two answers changed a task.

| # | Item | Resolution |
|---|---|---|
| **O1** | Mechanical stack | **All three stack.** Top to bottom: Wio → XIAO via **B2B**; XIAO → Expansion Board via the 2.54 mm headers on the XIAO underside, plus **4 spring pins** contacting the XIAO's bottom pads. Those pogo pins carry **SWD**, not B2B signals, so they do not contend with the radio — consistent with the stack booting. See §3.1 |
| **O2** | Antenna gain | **3.0 dBi assumed** for range-test work → `LRAN_ANTENNA_GAIN_DBI10=30`, the same figure pass 1 already carries. A stubby is likely on the bench; see §3.2 before fitting it |
| **O3** | Module part number | **Not obtainable** — no top-side silkscreen, and the stack is zip-tied with the underside inaccessible. Identified instead by **interconnect**, which is the stronger discriminator: the B2B connection is what separates p-5982 from p-6379 (§2.1). Bridge Impl Plan §2.3.1's request for a part number is answered by variant, not by number, and that is what it actually needed |
| **O4** | Button | **Three buttons reachable**: Expansion Board RESET, Expansion Board D1 (GPIO 2), and a **user button on the top of the Wio board**. Task X5 rewritten — the Wio button is GPIO 21 and is the right one. See §3.3 |

### 3.1 The stack already ran the radio pin map

**The stack boots stock Meshtastic and drives the display.** That is worth more than a
silkscreen: Meshtastic's `variants/esp32s3/seeed_xiao_s3/` is the source §2.1's Kit column
was taken from, so a stack that boots it is a stack whose **display** pin map is confirmed
on this hardware before pass 2 writes a line.

Whether it confirms the **radio** map depends on something not yet observed — see **O5**
in §3.4. A booting display proves I2C; it does not prove GPIO 38–42.

### 3.2 3.0 dBi is the safe assumption, and the direction matters

The D33 clamp takes gain as an input and lowers the conducted ceiling as gain rises, so
**over-stating gain is the conservative error**. At 3.0 dBi the clamp gives −4 dBm
conducted; fitting a 2 dBi stubby against that figure yields −2 dBm EIRP, under the
~−1 dBm §15.249 ceiling. One rule follows:

> **3.0 dBi is safe for any antenna of 3.0 dBi or less. It is not safe for a higher-gain
> antenna**, and the flag must be raised and the board reflashed before one is fitted.

This is why the figure lives in `platformio.ini` where an antenna swap is a visible
one-line diff, and it is why the pass-1 comment block is carried across verbatim (X10).
Record the stubby's actual gain when it is known; if it exceeds 3.0 dBi it is a reflash,
not a note.

### 3.3 The user button on the Wio is GPIO 21

Meshtastic's Kit variant defines:

```
#define BUTTON_PIN 21        // This is the Program Button
#define BUTTON_NEED_PULLUP
#define I2C_SDA 5
#define I2C_SCL 6
```

GPIO 21 is **not** one of the XIAO's D-pads (D0–D10 = 1, 2, 3, 4, 5, 6, 43, 44, 7, 8, 9),
so it reaches the button across the B2B connector — which places it on the Wio board, and
matches the top-side button observed in O4. Active-low with a pull-up: the **same
convention as the Heltec PRG**, so the R1 gesture logic is unchanged.

`I2C_SDA 5` / `I2C_SCL 6` independently confirm §2.6's OLED prediction.

### 3.4 One item remains open

| # | Item | Why it matters |
|---|---|---|
| **O5** | **Does the radio transmit under stock Meshtastic** — does the node key up, mesh, or show LoRa activity, as opposed to only lighting the display? | It is a **free empirical validation of §2.1's Kit radio pin map on this exact stack**, obtainable before pass 2 compiles anything. If Meshtastic drives the radio here, GPIO 38–42 and the RXEN line are confirmed by a working third-party implementation, and X8's job shrinks to measurement. If it does not, X8 is also a bring-up and should be planned as one |

**Status 2026-09-05: closed, not pursued.** The stock firmware has not been configured or
examined, and the operator has no Meshtastic experience — the learning curve costs more
than the evidence is worth at this stage. **If phase B hits trouble that points at the pin
map, back up and reflash Meshtastic** rather than debugging GPIO 38-42 from first
principles; that is the cheaper order even counting the reflash.

### 3.5 The XIAO's USB is the ESP32, and that is not just a port name

Found while writing X6, not predicted by the plan.

The Heltec's CP2102 is a **separate chip**: it stays enumerated when the ESP32 resets, so
the host's open port survives a reboot. **On the XIAO the USB device is the ESP32 itself.**
A reset tears the device down and the host must re-enumerate it, which takes on the order
of a second.

Two things that are reliable on the Heltec may not be on the XIAO, and both fail quietly:

- **The serial role selector** (`'i'`/`'r'`/`'v'`, role.h) fires inside a 3 s window that
  may have partly elapsed before the host reopens the port.
- **`capture.py`'s boot window** — the regression that destroyed a third of the
  2026-09-05 campaign, and the one the pass-1 tool tests were written for — has less
  margin here than on the board it was tuned against.

**The button selector is unaffected**, so the walking-unit path is safe either way.

**Deliberately not pre-solved.** Lengthening the window to a guessed number would be
inventing a constant to fix a problem nobody has measured. The right value is how long
*this host* takes to re-enumerate *this board*, which is a measurement — X8 takes it.

> #### MEASURED 2026-09-05 — the mechanism was real, the consequence was not
>
> | | reset → first byte |
> |---|---|
> | XIAO (USB-Serial-JTAG) | 104–106 ms |
> | Heltec (CP2102) | 106–109 ms |
>
> `n = 4` each, **port handle survived every trial**. A run-mode reset does not tear the
> host connection down: the ROM bootloader and the running application share the same
> USB-Serial-JTAG peripheral (`303A:1001`), so the USB device never disappears. **The role
> window and `capture.py` needed no change**, and refusing to guess a longer window was
> the right call for the wrong reason — the number turned out not to matter at all.
>
> **The real cost landed at flashing time, which this section did not predict.** Coming
> *from* a firmware with a different USB stack — stock Meshtastic enumerates as TinyUSB
> CDC `2886:0059` — the reset into download mode swaps the USB device, esptool loses its
> handle mid-connect, and the upload fails with `Could not configure port`. The board is
> in the bootloader; it is on a *new* port. Flash to that port and it works. Once this
> firmware is installed it does not recur.
>
> Recorded as a miss, not a hit: the section predicted a capture-time problem that does
> not exist and missed an upload-time one that does.

O5 gates nothing and blocks no code. It changes only how much X8 is expected to discover.

**But the observation is only free while Meshtastic is still on the board.** Phase A's
first flash overwrites it, and recovering the evidence afterwards costs a reflash out and
back. If it is going to be looked at, the cheap moment is before X6 produces a binary —
not because anything depends on it, but because the same fact costs minutes now and an
afternoon later.

---

## 4. Tasks

Ordered so that nothing that touches the radio is written after the board is powered.

### 4.0 Sequencing — two phases, and why the display seam is not deferred

An earlier draft offered to split the radio work (X1, X2, X6, X7) from the board seams
(X3, X4, X5) and leave the XIAO headless until the Heltec pair had been re-run. **That
split is rejected, on the operator's reasoning:**

> ultimately we will have to work with one of the heltec boards for testing, so we will be
> forced to cross the display seam bridge before actual multi-node bench work begins.

This is right, and it inverts the apparent cost. The Heltec is the **peer in every
two-node test** — B1b's delta, the bench PER, the R1 gesture at both ends. So
"the Heltec is unchanged" is not an extra verification the refactor imposes; it is the
**first thing bench work proves regardless**. Deferring X3/X4 would buy nothing and cost a
second pass over the same files, with the XIAO's display left as a known gap in between.

| Phase | Tasks | Where | Gate |
|---|---|---|---|
| **A — desk** | X1, X2, X3, X4, X5, X6, X7, X10 | Host + compile only | `pio test -e native` green, including X7's new assertions; both environments build |
| **B — bench** | X8, X9 | Hardware, two boards | §4.0.1 |

**Phase A result.** `heltec` 393,161 B flash / `xiao` 382,529 B; 181 host tests, 29 in
`test_phy_params`. One thing the host tests could not have caught: `[env:xiao]` needs
**`-Wno-error=cpp`**, the only relaxation of `-Werror` in this project — RadioLib emits an
unconditional `#warning` under `ARDUINO_USB_CDC_ON_BOOT`. It downgrades rather than
silences, so the warnings still print. Reasoning in `platformio.ini` and the log.

X9 (documents) is written *with* the code that makes it true, per the repo's docs-as-code
rule, not batched at the end of phase B.

#### 4.0.1 Phase B opens with a Heltec regression, and it is not ceremony

Before any XIAO measurement is trusted, the **Heltec pair runs the pass-1 flow unchanged**
— role selection by gesture, OLED legible, a sweep to a committed CSV comparable with the
pass-1 traces.

Two distinct things are being checked at once, and they fail differently:

- **X4 was a no-op on the Heltec.** A wrong Vext order or a skipped reset pulse gives a
  dark panel, which is loud and obvious.
- **X3 selects the right board config.** This one is quiet. A build that picked the wrong
  `kBoard` still runs, still displays, and puts the wrong pin map or the wrong antenna
  gain into a CSV that looks entirely normal — the D33 figure in particular is arithmetic
  nobody sees fail.

The R3 settings dump names the board, so **read the board name off the dump before
trusting the first trace**. That is the cheap check that catches the quiet failure.

### X1 — Teach `RadioLink` the discrete RF switch line

**The one change GateLink actually depends on.** `board.rf_sw` exists in
`BoardRadioConfig` and no code reads it (`radio_link.cpp` handles `dio2_as_rf_switch`
only). On the Heltec that is invisible; on the Wio it is a radio that reports a
successful transmit and puts nothing on the air — the same silent class as TCXO voltage
and DIO2, and the reason spec §12.2 lists all three together.

**Do:** in `begin()`, after `SX1262::begin()` and beside the existing
`setDio2AsRfSwitch()` call:

```cpp
// spec 12.2 / gatelink-expansion-board 7.3 - the Wio-SX1262 needs BOTH. Seeed does
// not tie DIO2 to the RF switch internally, so DIO2 alone leaves the PA disconnected.
// RadioLib 7.7.1 Module.h: setRfSwitchPins(rxEn, txEn) - RF_SW is the RX enable and
// TX enable is unconnected. Checked, not fired and forgotten (repo rule 4's reasoning).
if (board.rf_sw != kPinNone) {
  g_radio->setRfSwitchPins(board.rf_sw, RADIOLIB_NC);
}
```

**Constraints.** `setRfSwitchPins` returns `void` in 7.7.1, so it is the one radio call in
this file that cannot be checked — say so in the comment rather than leaving a reader to
wonder why the pattern broke. `kPinNone` is translated to `RADIOLIB_NC` here and nowhere
else, per `board_config.h`'s existing rule.

**This is the whole of the GateLink reuse requirement.** After X1, GateLink's firmware
reaches a working radio by adding one `BoardRadioConfig` instance and changing nothing in
`radio_link.{h,cpp}`. §6 records what that claim does *not* cover.

**Acceptance:** the Heltec path is byte-identical (`rf_sw == kPinNone`, branch not taken);
the Wio transmits and is heard.

### X2 — Populate the pass-2 board config

Replace the `TODO(R2-pass2)` slot in `board_config.h` with the Kit entry, carrying the
same provenance discipline the Heltec entry set — cite the source and its version, and
say which numbers are transcribed and which are derived.

```
name        "xiao_esp32s3_wio_kit"     short_name  "XIAO+Wio"
nss 41   rst 42   busy 40   dio1 39
sck  7   miso  8  mosi  9
rf_sw 38    tcxo_mv 1800   dio2_as_rf_switch true
```

**Also add a second, still-unpopulated slot** for the header board / GateLink carrier,
with §2.1's column recorded as *not rung out*. Pass 1 left a reserved slot rather than
copying unrung values, and that judgement was right; the same reasoning applies again.
The carrier's entry is populated by GateLink's own bring-up, after §2.4's continuity
check.

**Do not name the constant `kXiaoWio`.** Name it `kXiaoWioKit`. §2.1 is a two-product
trap, and a name that does not distinguish them is how the wrong map gets picked later.

### X3 — One board selection point

`main.cpp` names `kHeltecV3` at three sites (settings dump, role display, radio begin).
Introduce a single build-selected `kBoard` reference in `board_config.h`, chosen by a
`-D LRAN_BOARD_*` flag with the Heltec as the default, and route all three through it.

**No `#ifdef` above the driver.** Per `firmware/simnode/CLAUDE.md`, the profile is
compile-time and the struct is injected; the preprocessor picks *which instance*, and
never reaches into role or radio logic. If X3 needs more than one conditional in one
header, that is a finding for the log.

### X4 — Generalise the display seam

`ui_oled.h` hardcodes `kPinOledSda 17`, `kPinOledScl 18`, `kPinOledRst 21`, `kPinVext 36`
— all Heltec V3. The XIAO expansion board is the same SSD1306 at the same 0x3C, with
**no Vext and no reset line**, on I2C 5/6.

**Do:** add a `BoardUiConfig` struct beside `BoardRadioConfig` — `sda`, `scl`, `rst`,
`vext`, `addr` — and make `Ui::begin()` skip the Vext sequence and the reset pulse when
those are `kPinNone`. Keep it a **separate struct** from `BoardRadioConfig`; the radio
config is cited by spec §12.2 and injected into a driver, and widening it to carry display
pins would blur a seam four firmwares depend on.

Heltec keeps its exact current values, including the comment recording that they came from
wattcycle-reader and are verified on hardware. The Vext bring-up order is not touched.

**Acceptance:** the Heltec display is unchanged on hardware — this task must be provably
a no-op for pass 1.

**The XIAO side is already corroborated.** Meshtastic's Kit variant defines `I2C_SDA 5` /
`I2C_SCL 6` (§3.3), and the assembled stack drives its display on stock firmware (§3.1),
so these two pins are confirmed on this hardware rather than predicted.

**One thing to check before trusting the panel type.** Meshtastic's variant comments the
screen as *1.3 inch* while Seeed's Expansion Board page specifies *0.96 inch*; a 1.3"
module is frequently an **SH1106**, not an SSD1306. The two differ by a 2-column RAM
offset, and the symptom is a picture shifted a few pixels with a wrapped edge — legible
enough to dismiss, wrong enough to misread a link figure. The pinned driver
(`thingpulse@4.6.2`) provides both `SSD1306Wire` and `SH1106Wire`, so the fix is a class,
not a dependency. Confirm against the panel in hand and record which, per the same
provenance rule the radio pins follow.

### X5 — The role selector button

The R1 gesture (tap = RESPONDER, hold = SURVEY, no press = INITIATOR) is
`kPinPrgButton = 0` in `role.h`, confirmed behaviourally on the Heltec.

**Use the Wio board's user button, GPIO 21** (§3.3), not GPIO 0 and not the Expansion
Board's D1. Three reasons, in order of weight:

1. **GPIO 0 is the ESP32-S3 BOOT strapping pin.** The R1 gesture is a press held across a
   reset, which on the XIAO risks entering download mode instead of selecting a role.
   The Heltec gets away with GPIO 0; this board should not be asked to.
2. GPIO 21 is active-low with a pull-up — the **same convention as the PRG button**, so
   `role.cpp` needs no polarity concept and the gesture logic is untouched.
3. It is on the **top of the Wio board**, reachable with the stack zip-tied (O3), and it
   is the button a third-party firmware already treats as the program button.

Move the pin to board config alongside the UI pins. Leave D1 (GPIO 2) unclaimed — it is
the obvious spot for a second control later, and X7's collision test will keep it honest.

Confirm behaviourally, as pass 1 did, and log it the same way: what matters is that the
button reaches the GPIO.

### X6 — `[env:xiao]`

`board = seeed_xiao_esp32s3` (present in `espressif32@6.13.0`, checked). Identical
`lib_deps` with RadioLib pinned to 7.7.1 and the OLED driver to 4.6.2 — **D32 and repo
rule 9: neither floats in one of N environments.** Same `-Wall -Wextra -Werror`, same
`lib_extra_dirs`, no `upload_port`.

Carry the pass-1 antenna comment block across in full, with the XIAO's own figure per
**O2**. See task X10.

### X7 — Host tests

Extend `test_phy_params`, which already pins the Heltec entry:

1. The Kit entry, field by field, including `rf_sw == 38` and `dio2_as_rf_switch == true`.
2. **A pin-collision invariant over every board profile** — no radio pin may equal any
   other radio pin, the UI I2C pins, or the role button. This is the test that would have
   caught §2.6 before smoke, and it is the test that protects the *next* board.
3. The Heltec entry is unchanged — X4 and X3 are refactors and the existing assertions
   must pass untouched.

`board_config.h` is Arduino-free and host-testable by design (that is stated in its
header); X4's `BoardUiConfig` must stay that way, so it holds `kPinNone` and not
`RADIOLIB_NC` or `-1` literals.

### X8 — Hardware bring-up and the B1b delta

Gated on phase A green (§4.0). O1–O4 are resolved; O5 is informational.

0. **Heltec regression first, per §4.0.1** — the pass-1 flow, unchanged, board name read
   off the R3 settings dump. This also re-establishes the reference the XIAO is measured
   against, so it is step zero rather than a checkbox.
1. **Measure USB re-enumeration time on the XIAO** (§3.5) — reset to port-reopened, on
   the machine that will run the campaign. This decides whether the 3 s role window and
   `capture.py`'s boot window survive on this board. Cheap, and it is the number that
   would otherwise be guessed.
2. Radio init, settings dump, and a `PING` exchange with the Heltec at the pass-1
   parameters. **Antenna fitted before any transmit** — §7.3, +22 dBm into an open
   connector damages the PA, and the D33 clamp does not protect against a missing load.
3. Bench PER both directions at matched power. The Heltec↔Heltec figures from pass 1 are
   the reference; the Wio↔Heltec delta is **the module contribution to link margin** —
   the measurement B1b existed for.
4. R8 ambient survey from the XIAO at the gate location. Pass 1 recorded a −66 dBm
   neighbour at 914.0; whether the Wio sees the same picture is a property of the module
   and its antenna, not of the site.

**M21 now covers two modules.** The Wio's own FCC grant conditions are a separate
question from the Heltec's, and D33's "not a compliance determination" caveat applies to
both.

### X9 — Document reconciliation

Docs-as-code, in the same commit as the code that makes them true:

| Document | Change |
|---|---|
| `firmware/range-test/CLAUDE.md` | Board gotchas section gains the Wio: two RF-switch mechanisms, not one |
| `firmware/simnode/CLAUDE.md` | Pin map block splits Kit from header board; §2.5 retires the D-pad caveat; §2.2 records that `simnode-xiao-wio` on the Kit does **not** validate the carrier |
| `docs/gatelink/gatelink-expansion-board.md` | §7.3 confirmed against RadioLib 7.7.1 and two variant definitions; §2.4's corroboration noted; the §11 ring-out checkbox **stays open** |
| `docs/bridge/LRAN-Bridge_Node-Implementation-Plan` | §2.3.1 finding 1 closed positive, finding 2 closed negative; §10.8.1 split by variant |
| `docs/rangetest/engineering-log.md` | Dated entries per task, per repo workflow |
| `docs/shared/LRAN-Decision-Register` | M21 restated as covering two modules. **No new decision proposed** — see §5 |

Pass 1's own lesson applies and is worth repeating here, because this task is the one
most likely to be skipped: *a measurement is not finished when the trace is committed.*

### X10 — The antenna is a clamp input, not a label

`LRAN_ANTENNA_GAIN_DBI10=30` is set per-environment in `platformio.ini` and feeds the
**D33 power clamp**. The Wio kit ships a different antenna from the Heltec whip. Get this
wrong and two things are wrong together: the CSV records an antenna that was not fitted,
**and** the clamp permits an EIRP over the ceiling by exactly the error.

**Resolved (O2): 3.0 dBi**, giving `LRAN_ANTENNA_GAIN_DBI10=30` — numerically the same
value pass 1 carries, but it is set explicitly in the XIAO environment and **not
inherited**, because a shared default is how the two environments would silently diverge
the day one antenna changes.

§3.2 has the standing rule: 3.0 dBi covers any antenna of 3.0 dBi or less, so the bench
stubby is safe against this figure. **A higher-gain antenna is a flag change and a
reflash**, not a note in the log. The pass-1 comment block is carried across verbatim so
that decision stays visible in the build.

---

## 5. No new decision is proposed

D32 (RadioLib) and D33 (TX power) already bind this work, and R-4.1b already states the
injected-config requirement. Pass 2 produces **findings and measurements**, not a
decision — §2.2 and §2.3 close two open findings in the Bridge Impl Plan, and X8 feeds
D1's confirming pass and M21.

If X1 turns out not to be sufficient for GateLink — see §6 — *that* is worth a decision,
because it would mean the seam needs a bus-arbitration concept it does not have.

---

## 6. What "config-only change" does and does not promise

The reuse claim after X1 is precise, and worth stating narrowly so nobody leans on it
further than it holds. GateLink gets a working radio by adding one `BoardRadioConfig` and
touching no driver code, **provided**:

- It runs an ESP32-S3 (the StamPLC does), so `SPIClass(FSPI)` is still the right
  controller. `radio_link.cpp` hardcodes FSPI with a comment explaining why on this chip.
- **It owns its SPI bus.** `RadioLink` calls `g_spi.begin()` and assumes exclusive use.
  The carrier shares the StamPLC bus (§7.2), and the expansion board's SD slot shares it
  here (§2.6). Neither is exercised by pass 2 and neither is solved by X1.
- It needs no third RF-switch topology. `setRfSwitchPins(rxEn, txEn)` covers both boards;
  a module wanting separate TXEN/RXEN needs only the field, but anything beyond that
  needs `setRfSwitchTable()`.

Bus sharing is the one that will actually come due, and it belongs to GateLink's own
bring-up rather than being pre-solved here on a board that cannot test it.
