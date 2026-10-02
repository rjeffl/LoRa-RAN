// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L7 - the emulated BMS on air, as a NimBLE peripheral (bms-protocol 2).
//
// TWO TASKS TOUCH THE EMULATOR. NimBLE calls the server callbacks from its host task, and
// loop() calls bms_service() and the console. A FreeRTOS mutex guards every call. The
// handshake is applied inside the FFFA write callback, so a client that reads FFFA right
// after its write reads 0x01 and never meets a loop() that has not run yet.
//
// THE ENTROPY SOURCE. main.cpp turns bootloader_random on and leaves it on, because the
// board otherwise runs no RF of its own. ESP-IDF says the SAR ADC noise source must not run
// alongside the Bluetooth radio, so start() turns it off before the controller starts and
// stop() turns it back on once the controller is down (Impl Plan 8.1, L7). esp_random() is
// true random while the Bluetooth radio runs, so a ctx_id drawn meanwhile keeps spec 10.1.

#include "bms_ble_peripheral.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <bootloader_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstring>

namespace simnode {

namespace {

// bms-protocol 7 - the pack negotiated 512; the client asks for it.
constexpr uint16_t kBmsMtu = 512;

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};

class Peripheral final : public BmsControl,
                         public BmsLink,
                         public NimBLEServerCallbacks,
                         public NimBLECharacteristicCallbacks {
 public:
  Peripheral() : emu_(this) {}

  BmsEmu* emu() { return &emu_; }

  void service(uint32_t now_ms) {
    if (!running_) return;
    Lock l(mutex());
    emu_.tick(now_ms);
  }

  // --- BmsControl -----------------------------------------------------------------------

  bool start(const char* adv_name) override {
    Lock l(mutex());
    bootloader_random_disable();  // before the controller starts; see the file comment
    NimBLEDevice::init(adv_name);
    NimBLEDevice::setMTU(kBmsMtu);
    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(this, false);

    NimBLEService* svc = server_->createService(NimBLEUUID(static_cast<uint16_t>(0xFFF0)));
    rx_ = svc->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0xFFF1)),
                                    NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    tx_ = svc->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0xFFF2)),
                                    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
                                        NIMBLE_PROPERTY::READ);
    hs_ = svc->createCharacteristic(NimBLEUUID(static_cast<uint16_t>(0xFFFA)),
                                    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
                                        NIMBLE_PROPERTY::READ);
    rx_->setCallbacks(this);
    tx_->setCallbacks(this);
    hs_->setCallbacks(this);
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0xFFF0)));
    adv->setScanResponse(true);  // the name goes in the scan response if it does not fit
    running_ = adv->start();
    if (!running_) {
      NimBLEDevice::deinit(true);
      bootloader_random_enable();
    }
    return running_;
  }

  // Not under the lock: stopping the host ends the connection, and the disconnect callback
  // that follows takes the lock, so holding it across deinit() would deadlock.
  void stop() override {
    {
      Lock l(mutex());
      if (!running_) return;
      running_ = false;
    }
    // Otherwise the disconnect that deinit() causes starts advertising again on a host that
    // is stopping, and NimBLE logs rc=30.
    server_->advertiseOnDisconnect(false);
    NimBLEDevice::stopAdvertising();
    if (conn_ != kNoConn) server_->disconnect(conn_);
    conn_ = kNoConn;
    NimBLEDevice::deinit(true);
    server_ = nullptr;
    rx_ = tx_ = hs_ = nullptr;
    bootloader_random_enable();  // the controller is down; spec 10.1's source comes back
  }

  // `bms status` and `bms fault` read and arm the emulator, so they take the lock. `on` and
  // `off` run the stack, and take it themselves.
  bool console(char** argv, int argc, Sink* out) {
    const bool stack = argc >= 2 && (std::strcmp(argv[1], "on") == 0 || std::strcmp(argv[1], "off") == 0);
    if (stack) return bms_command(argv, argc, &emu_, this, out);
    Lock l(mutex());
    return bms_command(argv, argc, &emu_, this, out);
  }

  bool running() const override { return running_; }

  // bms-protocol 2 - the pack's suffix is the tail of its MAC, so ours is the tail of ours.
  const char* default_suffix() const override {
    static char suffix[5];
    uint8_t     mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    std::snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    return suffix;
  }

  // --- BmsLink: called with the lock held -------------------------------------------------

  // NimBLE 1.4.3's NimBLECharacteristic::notify() discards the host's return code, so a
  // refused notification would vanish. This calls the host directly and reports it.
  bool notify(const uint8_t* data, size_t len) override {
    if (rx_ == nullptr || conn_ == kNoConn) return false;
    os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (om == nullptr) return false;
    const int rc = ble_gattc_notify_custom(conn_, rx_->getHandle(), om);  // consumes om
    if (rc != 0) Serial.printf("bms: notify rc=%d\n", rc);
    return rc == 0;
  }

  void drop() override {
    if (server_ != nullptr && conn_ != kNoConn) server_->disconnect(conn_);
  }

  // --- NimBLE callbacks: the host task ----------------------------------------------------

  void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
    Lock l(mutex());
    conn_ = desc->conn_handle;
    emu_.on_connect(millis());
  }

  void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) override {
    Lock l(mutex());
    conn_ = kNoConn;
    emu_.on_disconnect();
    // NimBLE 1.4.3 advertises again on its own after a disconnect.
  }

  void onMTUChange(uint16_t mtu, ble_gap_conn_desc*) override {
    Lock l(mutex());
    emu_.on_mtu(mtu);
  }

  void onWrite(NimBLECharacteristic* c, ble_gap_conn_desc*) override {
    Lock l(mutex());
    const NimBLEAttValue v = c->getValue();
    emu_.on_write(c == hs_ ? BmsChar::Handshake : c == tx_ ? BmsChar::Tx : BmsChar::Rx, v.data(),
                  v.size(), millis());
  }

  // A write leaves its bytes in the characteristic's value, so FFFA would read back "HiLink".
  // The pack reads back its acknowledgement instead (bms-protocol 3).
  void onRead(NimBLECharacteristic* c, ble_gap_conn_desc*) override {
    if (c != hs_) return;
    Lock l(mutex());
    const uint8_t v = emu_.handshake_value();
    c->setValue(&v, 1);
  }

  void onSubscribe(NimBLECharacteristic* c, ble_gap_conn_desc*, uint16_t sub_value) override {
    if (c != rx_) return;
    Lock l(mutex());
    emu_.on_subscribe((sub_value & 0x0001) != 0);  // bit 0 is notifications
  }

 private:
  static constexpr uint16_t kNoConn = 0xFFFF;

  // Created on first use, which is from loop() or the console, before NimBLE starts.
  static SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
  }

  BmsEmu                emu_;
  NimBLEServer*         server_  = nullptr;
  NimBLECharacteristic* rx_      = nullptr;
  NimBLECharacteristic* tx_      = nullptr;
  NimBLECharacteristic* hs_      = nullptr;
  uint16_t              conn_    = kNoConn;
  bool                  running_ = false;
};

Peripheral g_peripheral;

}  // namespace

BmsControl* bms_peripheral() { return &g_peripheral; }
BmsEmu*     bms_emulator() { return g_peripheral.emu(); }
bool        bms_console(char** argv, int argc, Sink* out) { return g_peripheral.console(argv, argc, out); }
void        bms_service(uint32_t now_ms) { g_peripheral.service(now_ms); }

}  // namespace simnode
