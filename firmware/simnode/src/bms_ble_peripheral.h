// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Puts bms_emu's BMS on air as a NimBLE peripheral. GateLink task L7.
//
// THE ONLY FILE IN THIS FIRMWARE THAT INCLUDES NimBLE, as radio.cpp is for RadioLib.
// main.cpp passes `bms` here; bms_emu.cpp parses it.

#pragma once

#include <cstdint>

#include "bms_emu.h"

namespace simnode {

// The board's BmsControl, and the emulator it serves.
BmsControl* bms_peripheral();
BmsEmu*     bms_emulator();

// The `bms` console command, under the emulator's lock. False when argv[0] is not `bms`.
bool bms_console(char** argv, int argc, Sink* out);

// Runs the emulator's tick under its lock. Call it from loop(); it returns at once while
// the emulator is off.
void bms_service(uint32_t now_ms);

}  // namespace simnode
