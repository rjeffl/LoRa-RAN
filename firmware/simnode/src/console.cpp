// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-4; see console.h.

#include "console.h"

#include <cstdlib>
#include <cstring>

namespace simnode {
namespace {

constexpr int kMaxArgs = 12;

bool parse_uint(const char* token, unsigned long max, unsigned long* out) {
  if (token == nullptr || *token == '\0') return false;
  char*               end = nullptr;
  const unsigned long v   = std::strtoul(token, &end, 10);
  if (*end != '\0' || v > max) return false;
  *out = v;
  return true;
}

const char* add_result_text(AddResult r) {
  switch (r) {
    case AddResult::Ok:       return "ok";
    case AddResult::BadId:    return "id must be f0-f3";
    case AddResult::Exists:   return "identity already exists";
    case AddResult::Full:     return "table full - four identities";
    case AddResult::NotReady: return "table not initialised";
  }
  return "?";
}

}  // namespace

bool parse_hex_byte(const char* token, uint8_t* out) {
  if (token == nullptr || *token == '\0') return false;
  char*               end = nullptr;
  const unsigned long v   = std::strtoul(token, &end, 16);
  if (*end != '\0' || v > 0xFF) return false;
  *out = static_cast<uint8_t>(v);
  return true;
}

Console::Console(Node* node, IdentityTable* ids, Sink* out, BoardCommand board)
    : node_(node), ids_(ids), out_(out), board_(board) {}

void Console::feed(char c, uint32_t now_ms) {
  if (c == '\r' || c == '\n') {
    if (overflow_) {
      sink_printf(out_, "ERR line longer than %u characters", static_cast<unsigned>(kConsoleLineMax - 1));
    } else if (len_ > 0) {
      buf_[len_] = '\0';
      execute(buf_, now_ms);
    }
    len_      = 0;
    overflow_ = false;
    return;
  }
  if (len_ + 1 >= sizeof(buf_)) {
    overflow_ = true;
    return;
  }
  buf_[len_++] = c;
}

void Console::execute(char* line, uint32_t now_ms) {
  char* argv[kMaxArgs];
  int   argc = 0;
  for (char* tok = std::strtok(line, " \t"); tok != nullptr && argc < kMaxArgs;
       tok       = std::strtok(nullptr, " \t")) {
    argv[argc++] = tok;
  }
  if (argc == 0) return;

  const char* cmd = argv[0];
  if (std::strcmp(cmd, "help") == 0) {
    cmd_help();
  } else if (std::strcmp(cmd, "id") == 0) {
    cmd_id(argv, argc);
  } else if (std::strcmp(cmd, "enable") == 0) {
    cmd_enable(argv, argc, true);
  } else if (std::strcmp(cmd, "disable") == 0) {
    cmd_enable(argv, argc, false);
  } else if (std::strcmp(cmd, "ver") == 0) {
    cmd_ver(argv, argc);
  } else if (std::strcmp(cmd, "ctx") == 0) {
    cmd_ctx(argv, argc);
  } else if (std::strcmp(cmd, "ping") == 0) {
    cmd_ping(argv, argc, now_ms);
  } else if (std::strcmp(cmd, "stats") == 0) {
    cmd_stats(argv, argc);
  } else if (std::strcmp(cmd, "log") == 0) {
    cmd_log(argv, argc);
  } else if (std::strcmp(cmd, "push") == 0 || std::strcmp(cmd, "event") == 0 ||
             std::strcmp(cmd, "ack") == 0 || std::strcmp(cmd, "field") == 0) {
    sink_printf(out_, "ERR not implemented: %s arrives with ROLE_GATELINK (BF-6)", cmd);
  } else if (std::strcmp(cmd, "fault") == 0) {
    sink_printf(out_, "ERR not implemented: fault arrives with the catalogue (BF-7, BF-8)");
  } else if (board_ != nullptr && board_(argv, argc, out_)) {
    return;
  } else {
    sink_printf(out_, "ERR unknown command '%s' - try help", cmd);
  }
}

void Console::cmd_help() {
  static const char* const kLines[] = {
      "OK commands:",
      "  id add <hex> <ROLE_RANGE|ROLE_HEALTH|ROLE_GATELINK|ROLE_FAULT>",
      "  id del <hex>   |  id list",
      "  enable <hex>   |  disable <hex>",
      "  ver <hex> <n>",
      "  ctx <hex> [new]",
      "  ping <hex> <n> [pattern] [frag [<chunk>]] [to <hex>]   (dst defaults to 00)",
      "  stats <hex>",
      "  radio          (the driver's counters: frames actually on air)",
      "  log <quiet|info|debug>",
      "  push, event, ack, field (BF-6) and fault (BF-8) are not implemented yet",
  };
  for (const char* l : kLines) out_->line(l);
}

void Console::list_identity(const Identity& e) {
  sink_printf(out_, "id %02x %s %s ver %u ctx 0x%08lx tx_seq %u cmd_hw %u rx %lu tx %lu dropped %lu",
              e.id, role_name(e.role), e.enabled ? "enabled" : "disabled",
              static_cast<unsigned>(e.proto_ver), static_cast<unsigned long>(e.ctx_id),
              static_cast<unsigned>(e.tx_seq), static_cast<unsigned>(e.gate.high_water()),
              static_cast<unsigned long>(e.counters.rx_frames),
              static_cast<unsigned long>(e.counters.tx_frames),
              static_cast<unsigned long>(e.counters.total_dropped()));
}

void Console::cmd_id(char** argv, int argc) {
  if (argc >= 2 && std::strcmp(argv[1], "list") == 0) {
    sink_printf(out_, "OK %u identit%s", static_cast<unsigned>(ids_->count()),
                ids_->count() == 1 ? "y" : "ies");
    for (size_t i = 0; i < kMaxIdentities; ++i) {
      if (ids_->slot(i).used) list_identity(ids_->slot(i));
    }
    return;
  }

  uint8_t id = 0;
  if (argc == 4 && std::strcmp(argv[1], "add") == 0) {
    Role role;
    if (!parse_hex_byte(argv[2], &id)) {
      sink_printf(out_, "ERR bad id '%s'", argv[2]);
      return;
    }
    if (!parse_role(argv[3], &role)) {
      sink_printf(out_, "ERR bad role '%s' - ROLE_RANGE, ROLE_HEALTH, ROLE_GATELINK or ROLE_FAULT",
                  argv[3]);
      return;
    }
    const AddResult r = ids_->add(id, role);
    if (r != AddResult::Ok) {
      sink_printf(out_, "ERR id add %02x: %s", id, add_result_text(r));
      return;
    }
    sink_printf(out_, "OK id %02x added as %s", id, role_name(role));
    return;
  }

  if (argc == 3 && std::strcmp(argv[1], "del") == 0) {
    if (!parse_hex_byte(argv[2], &id) || !ids_->remove(id)) {
      sink_printf(out_, "ERR no identity '%s'", argv[2]);
      return;
    }
    sink_printf(out_, "OK id %02x removed", id);
    return;
  }

  sink_printf(out_, "ERR usage: id add <hex> <role> | id del <hex> | id list");
}

void Console::cmd_enable(char** argv, int argc, bool enable) {
  uint8_t   id = 0;
  Identity* e  = nullptr;
  if (argc != 2 || !parse_hex_byte(argv[1], &id) || (e = ids_->find(id)) == nullptr) {
    sink_printf(out_, "ERR usage: %s <hex> of an existing identity", argv[0]);
    return;
  }
  e->enabled = enable;
  sink_printf(out_, "OK id %02x %s", id, enable ? "enabled" : "disabled");
}

void Console::cmd_ver(char** argv, int argc) {
  uint8_t       id = 0;
  unsigned long v  = 0;
  Identity*     e  = nullptr;
  if (argc != 3 || !parse_hex_byte(argv[1], &id) || (e = ids_->find(id)) == nullptr ||
      !parse_uint(argv[2], 255, &v)) {
    sink_printf(out_, "ERR usage: ver <hex> <n>");
    return;
  }
  e->proto_ver = static_cast<uint8_t>(v);
  sink_printf(out_, "OK id %02x announces ver %lu", id, v);
}

void Console::cmd_ctx(char** argv, int argc) {
  uint8_t   id = 0;
  Identity* e  = nullptr;
  if (argc < 2 || argc > 3 || !parse_hex_byte(argv[1], &id) ||
      (e = ids_->find(id)) == nullptr || (argc == 3 && std::strcmp(argv[2], "new") != 0)) {
    sink_printf(out_, "ERR usage: ctx <hex> [new]");
    return;
  }
  if (argc == 3) ids_->new_context(id);
  sink_printf(out_, "OK id %02x ctx 0x%08lx", id, static_cast<unsigned long>(e->ctx_id));
}

// ping <hex> <n> [pattern] [frag [<chunk>]] [to <hex>]
//
// `to` is an addition to Impl Plan 10.4's form, which has no destination. Without it a PING
// can only go to the bridge; with it, two simnode boards echo each other, which is how B0's
// "ROLE_RANGE echoes PING" is shown before the bridge answers PINGs itself.
void Console::cmd_ping(char** argv, int argc, uint32_t now_ms) {
  uint8_t       id      = 0;
  unsigned long n       = 0;
  bool          pattern = false;
  unsigned long chunk   = 0;
  uint8_t       dst     = lran::kNodeBridge;

  if (argc < 3 || !parse_hex_byte(argv[1], &id) || !parse_uint(argv[2], 255, &n)) {
    sink_printf(out_, "ERR usage: ping <hex> <n> [pattern] [frag [<chunk>]] [to <hex>]");
    return;
  }
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "pattern") == 0) {
      pattern = true;
    } else if (std::strcmp(argv[i], "frag") == 0) {
      // spec 6.6.2's example chunk, when none is given.
      chunk = 14;
      if (i + 1 < argc && parse_uint(argv[i + 1], 255, &chunk)) ++i;
      if (chunk == 0) {
        sink_printf(out_, "ERR frag chunk must be at least 1");
        return;
      }
    } else if (std::strcmp(argv[i], "to") == 0 && i + 1 < argc &&
               parse_hex_byte(argv[i + 1], &dst)) {
      ++i;
    } else {
      sink_printf(out_, "ERR unexpected '%s'", argv[i]);
      return;
    }
  }

  const PingResult r =
      node_->ping(id, static_cast<uint8_t>(n), pattern, static_cast<uint8_t>(chunk), dst, now_ms);
  if (r != PingResult::Ok) {
    sink_printf(out_, "ERR ping %02x: %s", id, ping_result_name(r));
    return;
  }
  const Identity* e = ids_->find(id);
  sink_printf(out_, "OK ping %02x -> %02x seq %u, n %lu, %u frame(s)", id, dst,
              static_cast<unsigned>(e->ping.seq), n, static_cast<unsigned>(e->ping.fragments));
}

void Console::cmd_stats(char** argv, int argc) {
  uint8_t         id = 0;
  const Identity* e  = nullptr;
  if (argc != 2 || !parse_hex_byte(argv[1], &id) || (e = ids_->find(id)) == nullptr) {
    sink_printf(out_, "ERR usage: stats <hex> of an existing identity");
    return;
  }
  const lran::Counters& c = e->counters;
  sink_printf(out_, "OK stats %02x", id);
  sink_printf(out_, "  rx_frames %lu tx_frames %lu rx_dropped %lu unhandled %lu",
              static_cast<unsigned long>(c.rx_frames), static_cast<unsigned long>(c.tx_frames),
              static_cast<unsigned long>(c.total_dropped()),
              static_cast<unsigned long>(e->unhandled));
  sink_printf(out_, "  rx_crc_err %lu rx_runt %lu rx_oversize %lu rx_bad_crc %lu rx_bad_ver %lu",
              static_cast<unsigned long>(c.rx_crc_err), static_cast<unsigned long>(c.rx_runt),
              static_cast<unsigned long>(c.rx_oversize), static_cast<unsigned long>(c.rx_bad_crc),
              static_cast<unsigned long>(c.rx_bad_ver));
  sink_printf(out_, "  rx_not_addressed %lu rx_rejected_ctx %lu rx_rejected_mac %lu",
              static_cast<unsigned long>(c.rx_not_addressed),
              static_cast<unsigned long>(c.rx_rejected_ctx),
              static_cast<unsigned long>(c.rx_rejected_mac));
  sink_printf(out_, "  rx_reassembly_timeout %lu rx_frag_duplicate %lu rx_frag_late %lu",
              static_cast<unsigned long>(c.rx_reassembly_timeout),
              static_cast<unsigned long>(c.rx_frag_duplicate),
              static_cast<unsigned long>(c.rx_frag_late));
  sink_printf(out_, "  cad_backoffs %lu (radio) answers_dropped %lu (node)",
              static_cast<unsigned long>(node_->radio_counters()->cad_backoffs),
              static_cast<unsigned long>(node_->answers_dropped()));
}

void Console::cmd_log(char** argv, int argc) {
  LogLevel l;
  if (argc != 2 || !parse_log_level(argv[1], &l)) {
    sink_printf(out_, "ERR usage: log <quiet|info|debug>");
    return;
  }
  node_->set_log_level(l);
  sink_printf(out_, "OK log %s", log_level_name(l));
}

}  // namespace simnode
