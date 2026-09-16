// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-9; see oled_page.h.

#include "oled_page.h"

#include <cstdio>
#include <cstring>

namespace simnode {
namespace {

// Writes `head` then `name` into `out`, cutting `name` so the whole string is at most
// `max_len` characters. A cut name ends in '~': a console token shown incomplete must look
// incomplete, or an operator types the fragment and gets "unknown fault".
void join_cut(char* out, size_t cap, const char* head, const char* name, size_t max_len) {
  const int n = std::snprintf(out, cap, "%s%s", head, name);
  const size_t limit = max_len < cap - 1 ? max_len : cap - 1;
  if (n > 0 && static_cast<size_t>(n) > limit) {
    out[limit - 1] = '~';
    out[limit]     = '\0';
  }
}

void last_frame_row(const PageSnapshot& s, PageRow* row) {
  if (!s.radio_up) {
    // Outranks everything: every other row is meaningless without a radio.
    std::snprintf(row->left, sizeof(row->left), "RADIO DOWN");
    row->invert = true;
    return;
  }

  const LastRx& r = s.last;
  if (r.kind == RxKind::None) {
    std::snprintf(row->left, sizeof(row->left), "rx: nothing yet");
    return;
  }
  format_age(s.now_ms - r.at_ms, row->right, sizeof(row->right));

  char rssi[8];
  if (r.rssi_dbm == lran::kI16NotAvailable) {
    std::snprintf(rssi, sizeof(rssi), "--");  // root rule 6
  } else if (r.rssi_dbm < -199 || r.rssi_dbm > 99) {
    // No SX1262 reading lands out here. Found by test_the_top_row_fits_at_its_widest: an
    // unchecked -32767 pushed the row past the panel, so a value no radio produces says so
    // in one character rather than in six that cut off the age.
    std::snprintf(rssi, sizeof(rssi), "?");
  } else {
    std::snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(r.rssi_dbm));
  }

  switch (r.kind) {
    case RxKind::Frame:
      std::snprintf(row->left, sizeof(row->left), "%02x>%02x %s %s", r.src, r.dst,
                    short_type_name(r.type), rssi);
      return;
    case RxKind::HeaderDiscard:
      // Usually a frame for a node on another board. The console's `stats` says which stage.
      std::snprintf(row->left, sizeof(row->left), "%uB %s drop",
                    static_cast<unsigned>(r.len > 255 ? 255 : r.len), rssi);
      return;
    case RxKind::PhyCrc:
      std::snprintf(row->left, sizeof(row->left), "phy crc error");
      return;
    case RxKind::None:
      return;
  }
}

void identity_row(const IdentityView& v, PageRow* row) {
  if (!v.used) return;
  char head[8];
  std::snprintf(head, sizeof(head), "%02x ", v.id);

  if (v.fault != nullptr) {
    std::snprintf(row->right, sizeof(row->right), "%u", static_cast<unsigned>(v.fault_left));
    join_cut(row->left, sizeof(row->left), head, v.fault, kRowBudget - 1 - std::strlen(row->right));
    row->invert = true;
    return;
  }

  if (!v.enabled) std::snprintf(row->right, sizeof(row->right), "off");
  join_cut(row->left, sizeof(row->left), head, role_name(v.role),
           kRowBudget - (row->right[0] != '\0' ? 1 + std::strlen(row->right) : 0));
}

}  // namespace

const char* short_type_name(lran::MsgType t) {
  switch (t) {
    case lran::MsgType::Command:    return "CMD";
    case lran::MsgType::CommandAck: return "ACK";
    case lran::MsgType::Poll:       return "POLL";
    case lran::MsgType::Status:     return "STAT";
    case lran::MsgType::Event:      return "EVT";
    case lran::MsgType::Error:      return "ERR";
    case lran::MsgType::Ping:       return "PING";
    case lran::MsgType::HexReq:     return "HREQ";
    case lran::MsgType::HexRsp:     return "HRSP";
    case lran::MsgType::Config:     return "CFG";
    case lran::MsgType::ConfigAck:  return "CACK";
  }
  return "?";  // unknown_type frames never get past decode_header, so this is unreachable
}

void format_age(uint32_t ms, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  const uint32_t s = ms / 1000U;
  if (s < 60U) {
    std::snprintf(out, cap, "%lus", static_cast<unsigned long>(s));
  } else if (s < 3600U) {
    std::snprintf(out, cap, "%lum", static_cast<unsigned long>(s / 60U));
  } else if (s < 100U * 3600U) {
    std::snprintf(out, cap, "%luh", static_cast<unsigned long>(s / 3600U));
  } else {
    std::snprintf(out, cap, ">99h");
  }
}

PageSnapshot take_snapshot(const IdentityTable& ids, const FaultInjector& faults, const Node& node,
                           bool radio_up, uint32_t now_ms) {
  PageSnapshot s;
  s.radio_up = radio_up;
  s.now_ms   = now_ms;
  s.last     = node.last_rx();

  // Used slots packed to the top, in slot order - the order `id list` prints.
  size_t row = 0;
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    const Identity& e = ids.slot(i);
    if (!e.used) continue;
    IdentityView& v = s.ids[row++];
    v.used          = true;
    v.id            = e.id;
    v.role          = e.role;
    v.enabled       = e.enabled;

    const ArmedFault* a = faults.armed(e.id);
    if (a != nullptr && a->info != nullptr) {
      v.fault      = a->info->name;
      v.fault_left = a->left;
    } else if (e.silent_left > 0) {
      v.fault      = "silent";
      v.fault_left = e.silent_left;
    } else if (e.gl.ack_suppress_left > 0) {
      v.fault      = "ack_suppress";
      v.fault_left = e.gl.ack_suppress_left;
    } else if (e.gl.ack_dup_left > 0) {
      v.fault      = "ack_dup";
      v.fault_left = e.gl.ack_dup_left;
    }
  }
  return s;
}

PageLines build_page(const PageSnapshot& s) {
  PageLines p;
  last_frame_row(s, &p.rows[0]);
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    identity_row(s.ids[i], &p.rows[1 + i]);
  }
  if (!s.ids[0].used) {
    std::snprintf(p.rows[1].left, sizeof(p.rows[1].left), "no identities");
  }
  return p;
}

}  // namespace simnode
