// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The serial console. Task BF-4; Impl Plan 10.4.
//
// ARDUINO-FREE: characters in, lines out through a Sink, so the parser is host-tested.
//
// NO SCENARIO REQUIRES A REFLASH (simnode rule 4). Every command's name, argument and token
// is exact - ROLE_RANGE, `pattern`, `frag` - because simctl scripts type them.
//
// Every command answers with a line starting `OK` or `ERR`, so a script can tell a refused
// command from a silent one. Commands whose roles do not exist yet answer
// `ERR not implemented` and name the task that brings them.

#pragma once

#include <cstddef>
#include <cstdint>

#include "identity.h"
#include "node.h"
#include "sink.h"

namespace simnode {

inline constexpr size_t kConsoleLineMax = 128;

// A command the board supplies, for what cannot be host code - `radio`, which reads the
// driver. Returns false when `argv[0]` is not its command.
using BoardCommand = bool (*)(char** argv, int argc, Sink* out);

class Console {
 public:
  Console(Node* node, IdentityTable* ids, Sink* out, BoardCommand board = nullptr);

  // One received character. A line runs to CR or LF; an empty line is ignored.
  void feed(char c, uint32_t now_ms);

  // One complete line. Modified in place while it is tokenized.
  void execute(char* line, uint32_t now_ms);

 private:
  void cmd_help();
  void cmd_id(char** argv, int argc);
  void cmd_enable(char** argv, int argc, bool enable);
  void cmd_ver(char** argv, int argc);
  void cmd_ctx(char** argv, int argc);
  void cmd_ping(char** argv, int argc, uint32_t now_ms);
  void cmd_stats(char** argv, int argc);
  void cmd_log(char** argv, int argc);
  void list_identity(const Identity& e);

  Node*          node_;
  IdentityTable* ids_;
  Sink*          out_;
  BoardCommand   board_;

  char   buf_[kConsoleLineMax] = {0};
  size_t len_                  = 0;
  bool   overflow_             = false;
};

// Parses `f0`, `F0` or `0xF0`. False on anything else, including a value above 0xFF.
bool parse_hex_byte(const char* token, uint8_t* out);

}  // namespace simnode
