// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// VE.Direct text blocks, and the HEX lines interleaved with them on one UART. GateLink Impl
// Plan 4.2.4 and task L3. Ported from osh-labs/VE.Direct_mppt_arduino's
// src/VeDirectTextParser.{h,cpp} (MIT), the VE.Direct reference of record, checked
// 2026-10-01 at fadcc4e. Its byte-level state machine is kept. Its struct of twelve decoded
// fields is not: L3 keeps every field of a block, and decode_mppt() reads the ones GateLink
// sends.
//
// ARDUINO-FREE and allocation-free (root rules 3 and 7). Time is not read here: the caller
// knows when a block arrived, and spec 7.2.6 bit 1's staleness is the caller's.
//
// THE BLOCK. Each record is "\r\n" LABEL '\t' VALUE. The last record is "\r\nChecksum\t"
// and one raw byte that makes every byte of the block sum to zero, mod 256. That byte can
// be any value, ':' and '\n' included, so it is read by position and never as text.
//
// THE MULTIPLEXER. A ':' anywhere but the checksum byte starts a HEX line, which runs to
// '\n'. Its bytes are left out of the text checksum, and the text state resumes where the
// ':' interrupted it. That is osh-labs' specification, section 6.4. Its code abandons the
// interrupted block instead; this port follows the specification, because the checksum
// still guards whatever is delivered, and counts every interrupted block that then fails
// (`interrupted`). A nonzero `interrupted` beside zero `bad_checksum` says the MPPT does
// not exclude HEX bytes from the text checksum, and that resuming is wrong.
//
// NEVER SILENT (root rule 4). Every block that completes and is not delivered names one
// TextDrop and increments one counter.

#pragma once

#include <cstddef>
#include <cstdint>

#include "vedirect/hex.h"

namespace vedirect {

// osh-labs' buffer sizes, 16 and 33 with the terminator. Victron's labels are a few
// characters; its longest values are product strings.
inline constexpr size_t kLabelChars = 15;
inline constexpr size_t kValueChars = 32;

// The MPPT 75/15 sends about twenty records a block. Victron splits a longer report into
// several blocks, each with its own checksum, so the margin covers other products too.
inline constexpr size_t kMaxFields = 24;

struct TextField {
  char label[kLabelChars + 1] = {0};
  char value[kValueChars + 1] = {0};
};

struct TextBlock {
  TextField fields[kMaxFields];
  size_t    count = 0;

  // The value of `label`, or nullptr. Labels are case-sensitive, as Victron sends them.
  const char* find(const char* label) const;
};

// What feed() just completed.
enum class TextEvent : uint8_t {
  None,     // mid-record, or a byte between blocks
  Block,    // a block passed its checksum; block() holds it
  Dropped,  // a block or HEX line completed and was refused; last_drop() says why
  HexLine,  // a HEX line ended; hex_line() holds it, for decode() in hex.h
};

enum class TextDrop : uint8_t {
  None,
  BadChecksum,  // a whole block failed its checksum
  Unsynced,     // the first block after reset() failed it: the parser joined mid-block
  Interrupted,  // a block a HEX line interrupted failed it; see THE MULTIPLEXER
  Overflow,     // the checksum passed, but a label, value or the field count did not fit
  HexTooLong,   // a HEX line longer than kMaxChars
};
const char* drop_name(TextDrop d);

struct TextCounters {
  uint32_t blocks       = 0;
  uint32_t bad_checksum = 0;
  uint32_t unsynced     = 0;
  uint32_t interrupted  = 0;
  uint32_t overflow     = 0;
  uint32_t hex_lines    = 0;
  uint32_t hex_too_long = 0;
};

class TextParser {
 public:
  TextParser() { reset(); }

  // Back to waiting for a block boundary. block() and counters() survive it.
  void reset();

  TextEvent feed(uint8_t b);

  const TextBlock&    block() const { return block_; }
  TextDrop            last_drop() const { return last_drop_; }
  const TextCounters& counters() const { return counters_; }

  // The HEX line feed() last reported, from ':' up to but not including '\r' or '\n' -
  // the form decode() and spec 7.6 take. It holds until the next ':' starts another.
  const char* hex_line() const { return hex_; }
  size_t      hex_len() const { return hex_len_; }

 private:
  enum class State : uint8_t { Idle, RecordBegin, Label, Value, Checksum, Hex };

  TextEvent end_block();
  TextEvent end_hex();
  void      end_record();

  State        state_       = State::Idle;
  State        resume_      = State::Idle;  // where a HEX line returns to
  uint8_t      checksum_    = 0;
  bool         synced_      = false;  // a block boundary has passed since reset()
  bool         started_     = false;  // a text byte of this block has been read
  bool         interrupted_ = false;
  bool         overflow_    = false;
  bool         field_long_  = false;  // this record's label or value did not fit
  TextField    field_;
  size_t       label_len_   = 0;
  size_t       value_len_   = 0;
  TextBlock    scratch_;
  TextBlock    block_;
  char         hex_[kMaxChars + 1] = {0};
  size_t       hex_len_     = 0;
  bool         hex_long_    = false;
  TextDrop     last_drop_   = TextDrop::None;
  TextCounters counters_;
};

// The 75/15's labels, decoded in the units Victron sends. A label the block lacks, or
// whose value does not parse, reads as the sentinel (root rule 6). Values are wider than
// spec 7.2.2's fields because this is the UART's view: GateLink scales into those fields.
// Fields osh-labs and spec 7.2.2 do not define, such as FW and SER#, stay strings, read
// through TextBlock::find().
inline constexpr uint32_t kU32NotAvailable = UINT32_MAX;
inline constexpr int32_t  kI32NotAvailable = INT32_MIN;
inline constexpr uint16_t kU16NotAvailable = UINT16_MAX;

enum class LoadState : uint8_t { Off, On, NotAvailable };

struct MpptText {
  uint16_t  pid          = kU16NotAvailable;  // PID, "0x" then hex
  uint32_t  batt_mv      = kU32NotAvailable;  // V, mV
  int32_t   batt_ma      = kI32NotAvailable;  // I, mA, negative = discharge
  uint32_t  pv_mv        = kU32NotAvailable;  // VPV, mV
  uint32_t  pv_w         = kU32NotAvailable;  // PPV, W
  int32_t   load_ma      = kI32NotAvailable;  // IL, mA
  LoadState load         = LoadState::NotAvailable;  // LOAD, ON or OFF
  uint16_t  charge_state = kU16NotAvailable;  // CS, passed through (spec 7.2.2)
  uint16_t  tracker      = kU16NotAvailable;  // MPPT
  uint16_t  err          = kU16NotAvailable;  // ERR
  uint32_t  yield_total  = kU32NotAvailable;  // H19, 0.01 kWh (spec 7.2.2)
  uint32_t  yield_today  = kU32NotAvailable;  // H20, 0.01 kWh
  uint32_t  pmax_today   = kU32NotAvailable;  // H21, W (spec 7.2.2)
  uint32_t  yield_yest   = kU32NotAvailable;  // H22, 0.01 kWh
  uint16_t  day_seq      = kU16NotAvailable;  // HSDS
};

// Fills `out` from `b` and returns how many known labels carried a value that did not
// parse. The caller counts those: they are fields lost from a block whose checksum passed.
size_t decode_mppt(const TextBlock& b, MpptText* out);

}  // namespace vedirect
