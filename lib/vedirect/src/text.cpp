// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
// Derived from osh-labs/VE.Direct_mppt_arduino, Copyright (c) 2026 Christopher E. Lee /
// United Consulting, MIT. Its notice is in lib/vedirect/LICENSE-osh-labs, and where this
// port departs from it is in lib/vedirect/osh-labs-deviations.md.

#include "vedirect/text.h"

#include <cstring>

namespace vedirect {

namespace {

bool parse_u32(const char* s, uint32_t* out) {
  if (*s == '\0') return false;
  uint64_t v = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    v = v * 10 + static_cast<uint64_t>(*s - '0');
    if (v >= kU32NotAvailable) return false;
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

bool parse_i32(const char* s, int32_t* out) {
  const bool neg = (*s == '-');
  uint32_t mag = 0;
  if (!parse_u32(neg ? s + 1 : s, &mag)) return false;
  // INT32_MIN is the sentinel, so the most negative value accepted is one above it.
  if (mag > static_cast<uint32_t>(INT32_MAX)) return false;
  *out = neg ? -static_cast<int32_t>(mag) : static_cast<int32_t>(mag);
  return true;
}

bool parse_u16(const char* s, uint16_t* out) {
  uint32_t v = 0;
  if (!parse_u32(s, &v) || v >= kU16NotAvailable) return false;
  *out = static_cast<uint16_t>(v);
  return true;
}

// PID arrives as "0xA053" in osh-labs' sample block. Either case is accepted: this reads
// the MPPT's output and sends nothing back, unlike hex.h's request checking.
bool parse_hex16(const char* s, uint16_t* out) {
  if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X') || s[2] == '\0') return false;
  uint32_t v = 0;
  size_t n = 0;
  for (s += 2; *s; ++s, ++n) {
    int d = -1;
    if (*s >= '0' && *s <= '9') d = *s - '0';
    if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
    if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
    if (d < 0 || n == 4) return false;
    v = (v << 4) | static_cast<uint32_t>(d);
  }
  if (v >= kU16NotAvailable) return false;
  *out = static_cast<uint16_t>(v);
  return true;
}

}  // namespace

const char* drop_name(TextDrop d) {
  switch (d) {
    case TextDrop::None:        return "none";
    case TextDrop::BadChecksum: return "bad_checksum";
    case TextDrop::Unsynced:    return "unsynced";
    case TextDrop::Interrupted: return "interrupted";
    case TextDrop::Overflow:    return "overflow";
    case TextDrop::HexTooLong:  return "hex_too_long";
  }
  return "?";
}

const char* TextBlock::find(const char* label) const {
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(fields[i].label, label) == 0) return fields[i].value;
  }
  return nullptr;
}

void TextParser::reset() {
  state_       = State::Idle;
  resume_      = State::Idle;
  checksum_    = 0;
  synced_      = false;
  started_     = false;
  interrupted_ = false;
  overflow_    = false;
  field_long_  = false;
  scratch_.count = 0;
  hex_len_     = 0;
  hex_long_    = false;
  last_drop_   = TextDrop::None;
}

TextEvent TextParser::feed(uint8_t b) {
  // The checksum byte may be ':' itself, so only a ':' outside it starts a HEX line.
  if (b == ':' && state_ != State::Checksum && state_ != State::Hex) {
    resume_   = state_;
    interrupted_ = interrupted_ || started_;
    state_    = State::Hex;
    hex_len_  = 0;
    hex_long_ = false;
  }

  if (state_ == State::Hex) {
    if (b == '\n') {
      state_ = resume_;
      return end_hex();
    }
    if (b == '\r') return TextEvent::None;
    if (hex_len_ < kMaxChars) {
      hex_[hex_len_++] = static_cast<char>(b);
    } else {
      hex_long_ = true;
    }
    return TextEvent::None;
  }

  checksum_ = static_cast<uint8_t>(checksum_ + b);
  started_  = true;

  switch (state_) {
    case State::Idle:
      // A block's first record opens with "\r\n"; wait for the '\n'.
      if (b == '\n') state_ = State::RecordBegin;
      break;

    case State::RecordBegin:
      field_      = TextField{};
      label_len_  = 0;
      value_len_  = 0;
      field_long_ = false;
      state_      = State::Label;
      [[fallthrough]];

    case State::Label:
      if (b == '\t') {
        if (!field_long_ && std::strcmp(field_.label, "Checksum") == 0) {
          state_ = State::Checksum;
        } else {
          state_ = State::Value;
        }
      } else if (label_len_ < kLabelChars) {
        field_.label[label_len_++] = static_cast<char>(b);
      } else {
        field_long_ = true;
      }
      break;

    case State::Value:
      if (b == '\n') {
        end_record();
        state_ = State::RecordBegin;
      } else if (b == '\r') {
        // The '\r' opens the next record; it is in the checksum and nowhere else.
      } else if (value_len_ < kValueChars) {
        field_.value[value_len_++] = static_cast<char>(b);
      } else {
        field_long_ = true;
      }
      break;

    case State::Checksum:
      return end_block();

    case State::Hex:
      break;  // handled above
  }
  return TextEvent::None;
}

void TextParser::end_record() {
  if (field_long_ || scratch_.count == kMaxFields) {
    overflow_ = true;
    return;
  }
  scratch_.fields[scratch_.count++] = field_;
}

TextEvent TextParser::end_block() {
  TextDrop drop = TextDrop::None;
  if (checksum_ == 0) {
    if (overflow_) drop = TextDrop::Overflow;
  } else if (!synced_) {
    drop = TextDrop::Unsynced;
  } else if (interrupted_) {
    drop = TextDrop::Interrupted;
  } else {
    drop = TextDrop::BadChecksum;
  }

  switch (drop) {
    case TextDrop::None:        block_ = scratch_; ++counters_.blocks; break;
    case TextDrop::Overflow:    ++counters_.overflow; break;
    case TextDrop::Unsynced:    ++counters_.unsynced; break;
    case TextDrop::Interrupted: ++counters_.interrupted; break;
    case TextDrop::BadChecksum: ++counters_.bad_checksum; break;
    case TextDrop::HexTooLong:  break;  // not a block outcome
  }
  last_drop_ = drop;

  scratch_.count = 0;
  checksum_      = 0;
  synced_        = true;
  started_       = false;
  interrupted_   = false;
  overflow_      = false;
  state_         = State::Idle;
  return drop == TextDrop::None ? TextEvent::Block : TextEvent::Dropped;
}

TextEvent TextParser::end_hex() {
  if (hex_long_) {
    ++counters_.hex_too_long;
    last_drop_ = TextDrop::HexTooLong;
    hex_len_   = 0;
    hex_[0]    = '\0';
    return TextEvent::Dropped;
  }
  hex_[hex_len_] = '\0';
  ++counters_.hex_lines;
  return TextEvent::HexLine;
}

size_t decode_mppt(const TextBlock& b, MpptText* out) {
  *out = MpptText{};
  size_t bad = 0;
  auto u32 = [&](const char* label, uint32_t* f) {
    const char* v = b.find(label);
    if (v && !parse_u32(v, f)) ++bad;
  };
  auto i32 = [&](const char* label, int32_t* f) {
    const char* v = b.find(label);
    if (v && !parse_i32(v, f)) ++bad;
  };
  auto u16 = [&](const char* label, uint16_t* f) {
    const char* v = b.find(label);
    if (v && !parse_u16(v, f)) ++bad;
  };

  if (const char* v = b.find("PID")) {
    if (!parse_hex16(v, &out->pid)) ++bad;
  }
  u32("V", &out->batt_mv);
  i32("I", &out->batt_ma);
  u32("VPV", &out->pv_mv);
  u32("PPV", &out->pv_w);
  i32("IL", &out->load_ma);
  if (const char* v = b.find("LOAD")) {
    if (std::strcmp(v, "ON") == 0) {
      out->load = LoadState::On;
    } else if (std::strcmp(v, "OFF") == 0) {
      out->load = LoadState::Off;
    } else {
      ++bad;
    }
  }
  u16("CS", &out->charge_state);
  u16("MPPT", &out->tracker);
  u16("ERR", &out->err);
  u32("H19", &out->yield_total);
  u32("H20", &out->yield_today);
  u32("H21", &out->pmax_today);
  u32("H22", &out->yield_yest);
  // osh-labs accepts both spellings of the day sequence label.
  u16(b.find("HSDS") ? "HSDS" : "Hsds", &out->day_seq);
  return bad;
}

}  // namespace vedirect
