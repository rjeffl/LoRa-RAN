// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R2 - see radio_link.h.

#include "radio_link.h"

#include <RadioLib.h>
#include <SPI.h>

namespace rangetest {
namespace {

// The SX1262 sits on its own SPI bus on this board, so the wrapper owns one. FSPI is
// the general-purpose controller on the ESP32-S3; HSPI on this chip is the one the
// flash and PSRAM use.
SPIClass    g_spi(FSPI);
Module*     g_module = nullptr;
SX1262*     g_radio  = nullptr;

// Storage for the Module and SX1262 objects. Repo rule 3: no dynamic allocation in
// node firmware. Placement-new into static storage keeps RadioLib's object model
// (which wants a constructed Module) without reaching the heap.
alignas(Module) uint8_t g_module_storage[sizeof(Module)];
alignas(SX1262) uint8_t g_radio_storage[sizeof(SX1262)];

// spec 12.1 gives the sync word as "Private (0x12 / SX126x 0x1424)". Those are the
// SAME value at two layers, not two choices: RadioLib takes the one-byte form and
// expands it across the SX126x's two sync-word registers as
//   reg0 = (sw & 0xF0) | (ctrl >> 4), reg1 = ((sw & 0x0F) << 4) | (ctrl & 0x0F)
// which for sw = 0x12 and RadioLib's default ctrl = 0x44 produces 0x14, 0x24.
// Passing 0x1424 here would be wrong - it does not fit a uint8_t and the register
// pair would end up somewhere else entirely.
constexpr uint8_t kRadioLibSyncWord = 0x12;

static_assert(kSyncWord == 0x1424, "spec 12.1 sync word changed - recheck the map");

float bandwidth_khz() { return static_cast<float>(kBandwidthKhz10) / 10.0f; }

// Set from the DIO1 interrupt when a packet lands.
//
// THIS IS NOT DECORATION. The first version of poll() asked getPacketLength()
// whether a frame had arrived. That register holds the length of the LAST packet
// received and is not cleared by reading it, so with no new traffic poll() kept
// returning the same buffered frame - on the bench, a probe sent every 2 s was
// reported ~15 times a second, every report carrying identical RSSI and SNR, and the
// responder echoed each one. Caught on the R2 bench run; see the engineering log.
//
// It would have been quietly fatal to R4: round-trip PER is echoes-received over
// probes-sent, and both counters were being inflated by re-reads of one packet.
volatile bool g_rx_flag = false;

void IRAM_ATTR on_dio1_rx() { g_rx_flag = true; }

}  // namespace

int16_t RadioLink::begin(const BoardRadioConfig& board, const TestPoint& tp) {
  ready_ = false;

  g_spi.begin(board.sck, board.miso, board.mosi, board.nss);

  g_module = new (g_module_storage) Module(board.nss, board.dio1, board.rst,
                                           board.busy, g_spi);
  g_radio  = new (g_radio_storage) SX1262(g_module);

  // spec 12.2's two silent failures are both settled in this one call and the next.
  //
  // tcxoVoltage: the V3 has a TCXO, not a crystal. Pass 0 (RadioLib's "no TCXO") and
  // the oscillator never starts; the symptom is a radio that will not calibrate.
  const int16_t st = g_radio->begin(
      /* freq            */ static_cast<float>(tp.freq_hz) / 1000000.0f,
      /* bw              */ bandwidth_khz(),
      /* sf              */ tp.sf,
      /* cr              */ tp.cr_denom,
      /* syncWord        */ kRadioLibSyncWord,
      /* power           */ tp.power.conducted_dbm,
      /* preambleLength  */ kPreambleSymbols,
      /* tcxoVoltage     */ static_cast<float>(board.tcxo_mv) / 1000.0f,
      /* useRegulatorLDO */ false);
  if (st != RADIOLIB_ERR_NONE) return st;

  // The second silent failure. The V3 routes its RF path from DIO2; without this the
  // PA is never connected to the antenna and every transmit "succeeds" into a dead
  // end. Checked, not fired and forgotten.
  if (board.dio2_as_rf_switch) {
    const int16_t sw = g_radio->setDio2AsRfSwitch(true);
    if (sw != RADIOLIB_ERR_NONE) return sw;
  }

  // spec 2.1 requires both. RadioLib defaults to explicit header and CRC on for
  // LoRa, but "the default happens to be right" is not the same as requiring it, and
  // a later RadioLib version is free to change a default.
  const int16_t crc = g_radio->setCRC(kCrcEnabled ? 2 : 0);
  if (crc != RADIOLIB_ERR_NONE) return crc;

  static_assert(kExplicitHeader, "spec 2.1 requires explicit header; implicit mode "
                                 "would need setHeaderType() here");

  g_radio->setPacketReceivedAction(on_dio1_rx);

  ready_ = true;
  return RADIOLIB_ERR_NONE;
}

int16_t RadioLink::apply(const TestPoint& tp) {
  if (!ready_ || g_radio == nullptr) return RADIOLIB_ERR_WRONG_MODEM;

  int16_t st = g_radio->setFrequency(static_cast<float>(tp.freq_hz) / 1000000.0f);
  if (st != RADIOLIB_ERR_NONE) return st;

  st = g_radio->setSpreadingFactor(tp.sf);
  if (st != RADIOLIB_ERR_NONE) return st;

  st = g_radio->setCodingRate(tp.cr_denom);
  if (st != RADIOLIB_ERR_NONE) return st;

  // The clamp has already run in phy_params; this is the value it produced. The
  // driver is not the place the D33 ceiling is enforced - task guardrail 3 puts it
  // in code that can be host tested, which this cannot.
  st = g_radio->setOutputPower(tp.power.conducted_dbm);
  if (st != RADIOLIB_ERR_NONE) return st;

  return RADIOLIB_ERR_NONE;
}

int16_t RadioLink::set_frequency(uint32_t freq_hz) {
  if (!ready_ || g_radio == nullptr) return RADIOLIB_ERR_WRONG_MODEM;
  // Calibration NOT skipped. RadioLib recalibrates the image rejection when the
  // frequency crosses a band boundary, and a survey pass deliberately walks 26 MHz -
  // skipping it would trade a few milliseconds per bin for a systematic gain error
  // across part of the band, which is exactly the shape of a wrong answer that still
  // looks like a plausible spectrum.
  return g_radio->setFrequency(static_cast<float>(freq_hz) / 1000000.0f);
}

float RadioLink::instant_rssi_dbm() {
  if (!ready_ || g_radio == nullptr) return 0.0f;
  return g_radio->getRSSI(/* packet */ false);
}

int16_t RadioLink::set_power(int8_t conducted_dbm) {
  if (!ready_ || g_radio == nullptr) return RADIOLIB_ERR_WRONG_MODEM;
  return g_radio->setOutputPower(conducted_dbm);
}

int16_t RadioLink::transmit(const uint8_t* data, size_t len) {
  if (!ready_ || g_radio == nullptr) return RADIOLIB_ERR_WRONG_MODEM;
  return g_radio->transmit(const_cast<uint8_t*>(data), len);
}

int16_t RadioLink::start_receive() {
  if (!ready_ || g_radio == nullptr) return RADIOLIB_ERR_WRONG_MODEM;

  // Cleared HERE, and this is load bearing. DIO1 is shared: RadioLib's blocking
  // transmit() leaves the TxDone interrupt to fire through the same line and the same
  // action, which would set the flag with no packet to read. Every transmit path in
  // main() calls start_receive() afterwards, so clearing on the way into receive
  // discards that spurious set at the one point it can be identified as spurious.
  g_rx_flag = false;
  return g_radio->startReceive();
}

bool RadioLink::poll(uint8_t* buf, size_t cap, size_t* out_len, bool* out_crc_error) {
  if (out_crc_error != nullptr) *out_crc_error = false;
  if (!ready_ || g_radio == nullptr || buf == nullptr) return false;

  // The interrupt is the arrival signal. getPacketLength() below is only asked HOW
  // BIG the frame is, never WHETHER one came.
  if (!g_rx_flag) return false;
  g_rx_flag = false;

  const size_t avail = g_radio->getPacketLength();
  if (avail == 0) return false;

  const size_t take = (avail > cap) ? cap : avail;
  const int16_t st  = g_radio->readData(buf, take);

  // RSSI and SNR are valid for the frame just read, CRC failure included - which is
  // the point of surfacing the failure rather than dropping it. A frame arriving at
  // the edge of the link with a bad PHY CRC still tells you what the margin was.
  last_rssi_ = g_radio->getRSSI();
  last_snr_  = g_radio->getSNR();

  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    // spec 14 stage 1. Counted by the caller, never silently discarded.
    if (out_crc_error != nullptr) *out_crc_error = true;
    if (out_len != nullptr) *out_len = 0;
    return true;
  }
  if (st != RADIOLIB_ERR_NONE) return false;

  if (out_len != nullptr) *out_len = take;
  return true;
}

int16_t RadioLink::scan_channel() {
  if (!ready_ || g_radio == nullptr) return RADIOLIB_ERR_WRONG_MODEM;
  return g_radio->scanChannel();
}

}  // namespace rangetest
