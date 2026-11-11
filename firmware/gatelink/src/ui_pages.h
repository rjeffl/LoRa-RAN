// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Page text for the panel, kept apart from M5GFX so the native build can test it, as the
// simnode's oled_page.h is. Task L6 adds the boot page; GL1 adds the rest.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gatelink {

// The panel is landscape 240x135. At text size 2 the built-in font is 12x16 pixels, so
// the panel holds 8 lines. board_show() insets each line 6 pixels to clear the case's
// bezel, which leaves room for 19 characters.
inline constexpr size_t kPageCols  = 19;
inline constexpr size_t kPageLines = 8;

struct PageText {
  char   line[kPageLines][kPageCols + 1];
  size_t count;
};

// The node key's state, worded for the serial banner and the panel. GateLink Impl Plan
// 6.8: a node that cannot authenticate must never be mistaken for a provisioned one, so
// lran::key_is_placeholder() decides, and both outputs say the same thing.
bool        node_key_unprovisioned(const uint8_t* key, size_t len);
const char* node_key_status(const uint8_t* key, size_t len);

struct BootInfo {
  const char*    version;  // custom_gatelink_version, stamped by scripts/version.py
  const char*    git;      // the commit built; "-dirty" means it matches no commit
  const char*    reset;    // the reset cause's name
  const uint8_t* node_key;
  size_t         node_key_len;
};

// The boot page. Lines longer than kPageCols are cut, never wrapped.
void render_boot_page(const BootInfo& info, PageText* out);

// D92 - the status lines below the boot page, so a look at the panel says whether each link
// works without a console. Rows 4 to 6; row 7 stays the INA226 and LM75 (M12).
inline constexpr size_t kRowVed  = 4;
inline constexpr size_t kRowBms  = 5;
inline constexpr size_t kRowLora = 6;

// An age in at most three digits and a unit: "45s", "12m", "3h", "9d".
void format_age(uint32_t age_s, char* out, size_t cap);

// "VE ok 2s 13.21V" or "VE STALE 45s", by vedirect_stale_s, from the last complete text
// block; "VE none" before the first. batt_mv is left off when unavailable.
void format_ved_line(bool have_block, uint32_t age_ms, uint32_t stale_s, uint16_t batt_mv,
                     char* out, size_t cap);

// The last few BLE windows, newest in bit 0, 1 for a read. A window a reply aborted is not
// recorded: that is the LoRa side taking priority, not the BLE link failing.
class BmsHistory {
 public:
  static constexpr uint8_t kDepth = 6;  // 30 min at bms_poll_s's default
  void    record(bool read_ok);
  uint8_t windows() const { return count_; }
  uint8_t reads() const;

 private:
  uint8_t bits_  = 0;
  uint8_t count_ = 0;
};

// "BMS 5/6 ok 4m": reads out of the recorded windows, and the age of the last read.
// "BMS 0/6 none" with no read since boot, "BMS off" while SET_BMS_POLLING holds it off.
void format_bms_line(const BmsHistory& h, bool polling, bool have_read, uint32_t read_age_s,
                     char* out, size_t cap);

// "RX 12s -87 7.5dB": the age of the last frame addressed here that decoded, with its RSSI
// in dBm and SNR. "RX none" before the first.
void format_lora_line(bool heard, uint32_t age_s, int16_t rssi_dbm, int16_t snr_db10, char* out,
                      size_t cap);

// PRD R-4.7, with D92's display_mode. Mode 0 keeps the backlight off and 1 keeps it on.
// Mode 2 is R-4.7b: a press toggles it, and display_timeout_s without one blanks it. A debug
// mode holds it on in mode 2 (R-4.7c), and the timer starts again when the last one ends.
// The backlight starts lit, so a reset shows the boot page for one timeout.
class Backlight {
 public:
  static constexpr uint8_t kOff = 0, kOn = 1, kButton = 2;
  explicit Backlight(uint32_t now_ms) : since_ms_(now_ms) {}
  bool update(uint8_t mode, bool pressed, bool debug, uint32_t timeout_s, uint32_t now_ms);

 private:
  bool     lit_       = true;
  uint8_t  mode_      = kButton;
  uint32_t since_ms_  = 0;
};

}  // namespace gatelink
