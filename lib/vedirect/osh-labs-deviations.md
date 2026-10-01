# `lib/vedirect` — where it departs from osh-labs

`lib/vedirect/` is a port of
[`osh-labs/VE.Direct_mppt_arduino`](https://github.com/osh-labs/VE.Direct_mppt_arduino)
(MIT; notice in [`LICENSE-osh-labs`](./LICENSE-osh-labs)), the VE.Direct reference of
record (GateLink Implementation Plan §4.2.4). It is not a fork and holds no copied file:
the code was rewritten to this repository's rules. This list records every place the port
behaves differently from upstream, and every upstream defect found while porting.

**Compared against upstream commit `fadcc4e` (2026-07-08), checked 2026-10-01.** A later
upstream commit may change any row, so check that commit before reporting a row upstream.

**Keep this list current.** A change to `lib/vedirect/` that departs from upstream adds a
row in the same commit. A row reported upstream gets the issue link in its last column.

## Behaviour that differs from upstream's code

| # | Area | Upstream | This port | Why | Reported |
|---|---|---|---|---|---|
| 1 | HEX line inside a text block | The code abandons the block (`VeDirectTextParser.cpp`, `RECORD_HEX`) | The block resumes where the `:` interrupted it | Upstream's own specification, §6.4, says to preserve the block. The checksum still guards what is delivered. The `interrupted` counter is the check that would show resuming is wrong | — |
| 2 | Block that fails its checksum before the first block boundary | One invalid frame, as for any other failure | Counted as `unsynced`, apart from `bad_checksum` | Root rule 4. A reboot mid-block must not look like line noise | Local rule |
| 3 | Label, value or field count that does not fit | The frame is invalid, with no reason given | Dropped as `overflow`, counted, even when the checksum passes | Root rule 4: every discard names its reason | Local rule |
| 4 | What a valid block yields | A struct of 12 decoded fields; other labels are ignored | Every field, as label and value strings. `decode_mppt()` reads the 75/15's labels, adding `PID`, `H19` and `H21` | L3 asks for every field. `H19` and `H21` units come from Protocol Spec §7.2.2, not upstream | — |
| 5 | A value that is not a number | `strtol` reads it as 0 or as its leading digits | The field stays at its sentinel, and `decode_mppt()` counts it as malformed | Root rule 6. Zero is a real reading | — |
| 6 | A field the block lacks | Zero | A sentinel: `UINT32_MAX`, `INT32_MIN` or `UINT16_MAX` | Root rule 6 | Local rule |
| 7 | Handing HEX bytes on | A callback per byte (`HexByteSink`) | The whole line, returned as a `TextEvent::HexLine` and buffered to `kMaxChars`. A longer line is dropped as `hex_too_long` | No function pointers into the parser, and a line the HEX codec cannot hold is counted rather than half-delivered | Local design |
| 8 | Lowercase HEX digits (`hex.h`) | Upstream's receive parser accepts them | Refused | This code also checks requests typed into Home Assistant, and Victron requires uppercase. `hex.h`'s header gives the reasoning | Local rule |

"Local rule" and "Local design" rows follow from this repository's rules, not from a
VE.Direct question, so they are not for reporting upstream.

## Defects in upstream's documentation

| # | Where | What it says | What is true | Reported |
|---|---|---|---|---|
| D1 | `VeDirect_Arduino_Spec.md` §6.4 against `VeDirectTextParser.cpp` | §6.4: "The current Text frame accumulation state is preserved across the interruption" | The code discards the partial frame. One of the two is wrong; row 1 follows §6.4 | — |
| D2 | `VeDirect_Arduino_Spec.md` §3.1 | Shows records as `:Label\t<value>\r\n` lines, ending `Checksum\t<byte>\r\n` | The code, and this port, read records opened by `\r\n` and a checksum byte with nothing after it | — |
| D3 | `VeDirect_Arduino_Spec.md` §2 | The 75/15 is the "Verified Controller", with firmware "TBD at bench test" | Not yet a claim of field proof. Nothing to report until a 75/15 confirms it; GL4's capture is a candidate | — |

## Not ported

- `VeDirectRegisters.h`, the register map. `hex.h` reads any register by number, and
  GateLink inspects only the command nibble (spec §7.6).
- `VeDirectArduino.{h,cpp}`, the Arduino `HardwareSerial` wrapper. GateLink owns its UART.
