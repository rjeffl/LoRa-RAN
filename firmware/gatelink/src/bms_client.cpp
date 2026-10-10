// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL5; see bms_client.h. The access sequence is wattcycle-reader's (Impl Plan 4.3), with the
// connection dropped and the controller de-initialized after every read (PRD R-3.4b).

#include "bms_client.h"

#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "bms_ble/nimble_transport.h"
#include "bms_ble/tdt_protocol.h"

namespace gatelink {
namespace {

// How often a wait checks for its end. It bounds the abort latency of every step that is
// not a single NimBLE call.
constexpr uint32_t kWaitStepMs = 10;

uint32_t now_ms() { return static_cast<uint32_t>(xTaskGetTickCount()); }

// Feeds notifications through the reassembler and decodes 0x8C. on_notify() runs on
// NimBLE's host task and bms_task reads the result, so one lock guards both; the decode
// runs inside it because Frame::payload points into the reassembler's buffer
// (wattcycle-reader's BmsNotifyHandler, Impl Plan 5.2). Nothing here logs: the host task
// must not reach Serial, and a count says as much.
class NotifyHandler final : public bms::BmsTransport::NotifyHandler {
 public:
  void on_notify(const uint8_t* data, size_t len) override {
    size_t off = 0;
    portENTER_CRITICAL(&mux_);
    while (off < len) {
      bms::tdt::FrameReassembler::Status st;
      off += rx_.feed(data + off, len - off, st, now_ms());
      switch (st) {
        case bms::tdt::FrameReassembler::kComplete: {
          bms::BmsData d;
          d.clear();
          if (rx_.frame().cmd == bms::tdt::kCmdCellsPack &&
              bms::tdt::decode_cells_and_pack(rx_.frame(), d)) {
            last_ = d;
            ++counters_.frames;
          } else {
            ++counters_.undecoded;
          }
          break;
        }
        case bms::tdt::FrameReassembler::kCrcError:
          ++counters_.crc_errors;
          break;
        case bms::tdt::FrameReassembler::kBadTerminator:
          ++counters_.bad_term;
          break;
        case bms::tdt::FrameReassembler::kIncomplete:
          break;
      }
    }
    counters_.discarded = static_cast<uint32_t>(rx_.discarded_bytes());
    portEXIT_CRITICAL(&mux_);
  }

  // The clock is read after the caller's blocking write, which the reassembler's timeout
  // needs (handoff, Open).
  void tick() {
    portENTER_CRITICAL(&mux_);
    rx_.tick(now_ms());
    portEXIT_CRITICAL(&mux_);
  }

  // A partial frame from an earlier window must not prefix this one's answer.
  void reset() {
    portENTER_CRITICAL(&mux_);
    rx_.reset();
    portEXIT_CRITICAL(&mux_);
  }

  BmsRxCounters counters() const {
    portENTER_CRITICAL(&mux_);
    const BmsRxCounters k = counters_;
    portEXIT_CRITICAL(&mux_);
    return k;
  }

  bms::BmsData last() const {
    portENTER_CRITICAL(&mux_);
    const bms::BmsData d = last_;
    portEXIT_CRITICAL(&mux_);
    return d;
  }

 private:
  mutable portMUX_TYPE       mux_ = portMUX_INITIALIZER_UNLOCKED;
  bms::tdt::FrameReassembler rx_;
  bms::BmsData               last_ = {};
  BmsRxCounters              counters_;
};

// Watches a scan for the pack's name. Only the address and RSSI are kept: copying the
// advertised device would copy its payload onto the heap.
class Finder final : public NimBLEAdvertisedDeviceCallbacks {
 public:
  void arm(const char* name) {
    portENTER_CRITICAL(&mux_);
    name_  = name;
    found_ = false;
    portEXIT_CRITICAL(&mux_);
  }
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (found_ || name_ == nullptr || !dev->haveName()) return;
    if (std::strcmp(dev->getName().c_str(), name_) != 0) return;
    portENTER_CRITICAL(&mux_);
    addr_  = dev->getAddress();
    rssi_  = static_cast<int16_t>(dev->getRSSI());
    found_ = true;
    portEXIT_CRITICAL(&mux_);
  }
  bool found(NimBLEAddress* addr, int16_t* rssi) const {
    portENTER_CRITICAL(&mux_);
    const bool f = found_;
    if (f) {
      *addr = addr_;
      *rssi = rssi_;
    }
    portEXIT_CRITICAL(&mux_);
    return f;
  }

 private:
  mutable portMUX_TYPE mux_  = portMUX_INITIALIZER_UNLOCKED;
  const char*          name_ = nullptr;
  volatile bool        found_ = false;
  NimBLEAddress        addr_;
  int16_t              rssi_ = 0;
};

NimBleTransport g_transport;
NotifyHandler   g_notify;
Finder          g_finder;
BmsHeap         g_heap;

// The window's own end: the caller's stop, then the cap.
bool ended(const BmsWindowArgs& a, BmsEnd* why) {
  if (a.stop != nullptr && a.stop()) {
    *why = BmsEnd::Aborted;
    return true;
  }
  if (now_ms() - a.start_ms >= a.cap_ms) {
    *why = BmsEnd::Cap;
    return true;
  }
  return false;
}

uint32_t remaining_ms(const BmsWindowArgs& a) {
  const uint32_t used = now_ms() - a.start_ms;
  return used >= a.cap_ms ? 0 : a.cap_ms - used;
}

// Whole seconds, at least 1, for the NimBLE calls that take seconds.
uint32_t seconds_up(uint32_t ms) { return ms < 1000 ? 1 : (ms + 999) / 1000; }

BmsEnd scan(const BmsWindowArgs& a, NimBLEAddress* addr, int16_t* rssi) {
  NimBLEScan* s = NimBLEDevice::getScan();
  g_finder.arm(a.name);
  s->setAdvertisedDeviceCallbacks(&g_finder, false);
  s->setActiveScan(true);  // the name is in the scan response
  s->setInterval(100);
  s->setWindow(99);
  s->setMaxResults(0);  // the callback is enough; keep no result list
  if (!s->start(seconds_up(remaining_ms(a)), nullptr, false)) return BmsEnd::NotFound;
  BmsEnd why = BmsEnd::NotFound;
  for (;;) {
    if (g_finder.found(addr, rssi)) {
      why = BmsEnd::Read;  // found; the caller goes on
      break;
    }
    if (ended(a, &why)) break;
    if (!s->isScanning()) {
      why = BmsEnd::NotFound;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(kWaitStepMs));
  }
  if (s->isScanning()) s->stop();
  return why;
}

BmsEnd handshake() {
  bool ok = g_transport.write(bms::GattChar::Handshake,
                              reinterpret_cast<const uint8_t*>(bms::tdt::kHandshakeMagic),
                              bms::tdt::kHandshakeMagicLen, true);
  uint8_t   reply[8] = {0};
  const int n        = ok ? g_transport.read(bms::GattChar::Handshake, reply, sizeof(reply)) : -1;
  // bms-protocol 3 - without the 0x01 the pack ignores every request and drops the link.
  if (!ok || n < 1 || reply[0] != bms::tdt::kHandshakeAck) return BmsEnd::Handshake;
  if (!g_transport.subscribe(&g_notify)) return BmsEnd::Subscribe;
  return BmsEnd::Read;
}

BmsEnd request_and_wait(const BmsWindowArgs& a) {
  const uint32_t before = g_notify.counters().frames;
  uint8_t        req[bms::tdt::kRequestLen];
  const size_t   n = bms::tdt::build_request(bms::tdt::kCmdCellsPack, req, sizeof(req));
  if (n == 0 || !g_transport.write(bms::GattChar::Tx, req, n, true)) return BmsEnd::Request;
  BmsEnd why = BmsEnd::NoAnswer;
  for (;;) {
    g_notify.tick();
    if (g_notify.counters().frames != before) return BmsEnd::Read;
    if (!g_transport.is_connected()) return BmsEnd::NoAnswer;
    // The cap is the answer's timeout: a pack that never answers ends as NoAnswer, not Cap.
    if (ended(a, &why)) return why == BmsEnd::Cap ? BmsEnd::NoAnswer : why;
    vTaskDelay(pdMS_TO_TICKS(kWaitStepMs));
  }
}

// Times one step into `field`.
struct Step {
  explicit Step(uint32_t* field) : field_(field), t0_(now_ms()) {}
  ~Step() { *field_ = now_ms() - t0_; }
  uint32_t* field_;
  uint32_t  t0_;
};

BmsEnd run(const BmsWindowArgs& a, BmsSnapshot* out, BmsTiming* t) {
  {
    Step s(&t->init_ms);
    NimBLEDevice::init("");
    if (!NimBLEDevice::getInitialized()) return BmsEnd::InitFailed;
    // bms-protocol 7 - the largest MTU, so a 0x8C answer arrives whole. The reassembler
    // still runs, because a peer may refuse it.
    NimBLEDevice::setMTU(bms::kDesiredMtu);
  }
  BmsEnd why = BmsEnd::Read;
  if (ended(a, &why)) return why;

  NimBLEAddress addr;
  int16_t       scan_rssi = 0;
  {
    Step s(&t->scan_ms);
    why = scan(a, &addr, &scan_rssi);
  }
  if (why != BmsEnd::Read) return why;
  if (ended(a, &why)) return why;

  {
    Step s(&t->connect_ms);
    const uint32_t left = remaining_ms(a);
    const uint8_t  secs = static_cast<uint8_t>(seconds_up(left) > 255 ? 255 : seconds_up(left));
    if (!g_transport.connect(addr, secs)) return BmsEnd::ConnectFailed;
  }
  if (ended(a, &why)) return why;

  g_notify.reset();
  {
    Step s(&t->handshake_ms);
    why = handshake();
  }
  if (why != BmsEnd::Read) return why;
  if (ended(a, &why)) return why;

  {
    Step s(&t->answer_ms);
    why = request_and_wait(a);
  }
  if (why != BmsEnd::Read) return why;

  // R-3.4f - the connection's RSSI, read while it is up. The scan's figure is the fallback.
  const int link_rssi = g_transport.rssi();
  out->have     = true;
  out->data     = g_notify.last();
  out->rssi_dbm = static_cast<int16_t>(link_rssi != 0 ? link_rssi : scan_rssi);
  out->read_ms  = now_ms();
  return BmsEnd::Read;
}

}  // namespace

BmsEnd bms_window(const BmsWindowArgs& args, BmsSnapshot* out, BmsTiming* t) {
  *t = BmsTiming{};
  g_transport.set_log(args.log);
  g_heap.before = esp_get_free_heap_size();
  const BmsEnd end = run(args, out, t);
  {
    Step s(&t->teardown_ms);
    g_transport.disconnect();
    // PRD R-3.4b - the controller goes down between polls. clearAll frees the scan and
    // client objects too, so the heap returns to where it started.
    if (NimBLEDevice::getInitialized()) NimBLEDevice::deinit(true);
  }
  g_heap.after = esp_get_free_heap_size();
  g_heap.low   = esp_get_minimum_free_heap_size();
  return end;
}

BmsRxCounters bms_rx_counters() { return g_notify.counters(); }

BmsHeap bms_heap() { return g_heap; }

}  // namespace gatelink
