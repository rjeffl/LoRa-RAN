// main.cpp — WattCycle BLE BMS reader, milestones M0b + M1 + M2 + M3 + M4 +
// M5 + M6b + M7 + M7a.
//
// M1 (§11): "Serial lists nearby BLE devices; XDZN_001_49A1 /
// C0:D6:3C:58:49:A1 appears with RSSI."   — confirmed on hardware.
// M0b (§11): OLED alive, showing the layout with live RSSI.
// M2 (§11): "Connects, enumerates FFF0, confirms FFF1/FFF2 handles."
// M3 (§11): HiLink -> FFFA, read-back 0x01, subscribe FFF1, send 0x8C.
// M4 (§11): notification bytes fed through TdtProtocol::FrameReassembler.
// M5 (§11): a complete 0x8C frame decoded and printed field by field.
// M6b (§11): the decoded frame and a filled link indicator pushed to the
// OLED, riding on BmsDisplay's existing staleness handling from M0b.
// M7 (§11): "Polls on interval; survives battery going out of range and
// coming back; logs RSSI, consecutive-failure count, and free heap." Replaces
// M2-M6b's one-shot probe with a persistent connect-and-poll state machine
// (§5.6: "a persistent connection is viable" once handshaked).
//
// State machine: Scanning <-> Polling.
//   Scanning: normal M1 active-scan sweeps (this is still the aiming
//     instrument). When the target is seen, attempt connect + discover +
//     handshake + subscribe. Success moves to Polling; failure stays in
//     Scanning and counts against g_consecutive_failures.
//   Polling: connection is held open. Every kPollIntervalMs, send a 0x8C
//     request; the reassembler/decoder run from BmsNotifyHandler::onNotify()
//     same as M4/M5, and any newly decoded frame is pushed to the display.
//     If the connection drops (out of range, BMS-side timeout, etc.), fall
//     back to Scanning — this is the "survives going out of range and coming
//     back" requirement. RSSI while polling comes from the connection itself
//     (NimBLEClient::getRssi()), not from scanning, since the two aren't run
//     concurrently here.
//
// The reassembler and decoder in lib/bms_ble/TdtProtocol.* were already
// complete and host-tested (`pio test -e native`) before any of M2-M6b ran on
// hardware, and needed no changes to keep passing here — this file is BLE
// state-machine wiring, not decode work.
//
// M7a: GateLink's real node hardware turned out to be the M5Stack StamPLC,
// not the Heltec V3 — same ESP32-S3 family, so none of the above needed to
// change, but the two boards' displays are different hardware entirely
// (I2C SSD1306 OLED vs SPI ST7789v2 TFT). BOARD_STAMPLC (set only in
// platformio.ini's m5stack_stamplc env) picks TftDisplay over BmsDisplay
// below; both implement the same public API (begin/setDeviceName/setLink/
// setData/render), so nothing past this block needs to know which one is
// active.
//
// Measured RSSI: -77 to -88 dBm at desk range, -60 to -65 dBm at the
// approximate mounting position. A weak desk number is normal (§5.8) — the
// battery's antenna appears shielded by the BMS heat sink.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "BmsData.h"
#include "BmsTransport.h"
#include "LinkState.h"
#include "NimBleTransport.h"
#include "TdtProtocol.h"

#if defined(BOARD_STAMPLC)
#include "TftDisplay.h"
using ActiveDisplay = TftDisplay;
#else
#include "BmsDisplay.h"
using ActiveDisplay = BmsDisplay;
#endif

// --- Target (§3) -----------------------------------------------------------
static const char* kTargetName = "XDZN_001_49A1";   // underscores, not dashes
static const char* kTargetAddr = "c0:d6:3c:58:49:a1";

// 3 s sweeps with no pause: the display is the aiming instrument while
// Scanning, and a 7 s refresh is too slow to position a board by. Nothing
// here is power-tuned (§11 M7 note) — that's a later question.
static const uint32_t kScanSeconds = 3;

// Poll cadence once connected. Not power-tuned either — picked to be well
// inside the observed stable-idle window (§5.6: held 3 s idle with no drop)
// without hammering the link.
static const uint32_t kPollIntervalMs = 5000;

static uint32_t g_sweep = 0;

static ActiveDisplay g_display;
static bool g_have_display = false;

static NimBleTransport g_transport;

enum class ConnState { Scanning, Polling };
static ConnState g_state = ConnState::Scanning;
static uint32_t g_consecutive_failures = 0;
static uint32_t g_last_poll_ms = 0;
static uint32_t g_last_frame_seen = 0;   // last BmsNotifyHandler::frameCount() consumed

// Feeds notifications through the reassembler (M4) and decodes complete
// 0x8C frames (M5). onNotify() runs on NimBLE's host task (§7: "keep it
// short — feed the reassembler, no more"), so decode/print happens there
// too rather than posting to a queue — acceptable at this poll cadence;
// revisit if a tighter interval ever makes onNotify() a bottleneck.
class BmsNotifyHandler : public bms::BmsTransport::NotifyHandler {
  public:
    void onNotify(const uint8_t* data, size_t len) override {
        size_t off = 0;
        while (off < len) {
            bms::tdt::FrameReassembler::Status status;
            off += reassembler_.feed(data + off, len - off, status, millis());
            switch (status) {
                case bms::tdt::FrameReassembler::STATUS_COMPLETE:
                    handleFrame(reassembler_.frame());
                    break;
                case bms::tdt::FrameReassembler::STATUS_CRC_ERROR:
                    Serial.println(F("  reassembler: CRC ERROR"));
                    break;
                case bms::tdt::FrameReassembler::STATUS_BAD_TERMINATOR:
                    Serial.println(F("  reassembler: BAD TERMINATOR"));
                    break;
                case bms::tdt::FrameReassembler::STATUS_INCOMPLETE:
                    break;
            }
        }
    }

    // Call regularly while a request may be outstanding, so a partial frame
    // (fragmentation, MTU refusal) still times out per §5.5 rule 5 instead
    // of wedging the reassembler for the next poll.
    void tick(uint32_t now_ms) { reassembler_.tick(now_ms); }

    // M6b/M7: lets main.cpp push a newly decoded frame to the OLED without
    // BmsNotifyHandler knowing BmsDisplay exists (§9's rule: decode produces
    // a struct, presentation layers consume it). frameCount() is a
    // monotonic counter rather than a bool so the caller can tell a *new*
    // frame from the same one already pushed — needed now that Polling
    // checks in every loop() iteration instead of once per probe.
    uint32_t frameCount() const { return frame_count_; }
    const bms::BmsData& lastData() const { return last_data_; }

  private:
    void handleFrame(const bms::tdt::Frame& frame) {
        if (frame.cmd != bms::tdt::CMD_CELLS_PACK) {
            Serial.printf("  frame: cmd 0x%02x, %u bytes (not 0x8C, not decoded)\n",
                          frame.cmd, frame.payload_len);
            return;
        }

        bms::BmsData data;
        if (!bms::tdt::decodeCellsAndPack(frame, data)) {
            Serial.println(F("  decode 0x8C: FAILED"));
            return;
        }
        last_data_ = data;
        ++frame_count_;

        Serial.printf("  decode 0x8C: OK — %u cells, %u temps\n",
                      data.cell_count, data.temp_count);
        Serial.print(F("    cells (mV):"));
        for (uint8_t i = 0; i < data.cell_count; ++i) {
            Serial.printf(" %u", data.cell_mV[i]);
        }
        Serial.printf("  (delta %u mV)\n", data.deltaCell_mV());
        Serial.print(F("    temps (0.1C):"));
        for (uint8_t i = 0; i < data.temp_count; ++i) {
            Serial.printf(" %d", data.temp_dC[i]);
        }
        Serial.println();
        Serial.printf("    pack: %u mV   current: %ld mA (%s)   SOC: %u%%\n",
                      (unsigned)data.pack_mV, (long)data.current_mA,
                      data.discharging ? "discharge flag" : "charge flag",
                      data.soc_pct);
        Serial.printf("    remaining/nominal: %u/%u (0.1 Ah)   cycles: %u   SOH: %u (0.1%%)\n",
                      data.remaining_dAh, data.nominal_dAh, data.cycles, data.soh_dpct);
    }

    bms::tdt::FrameReassembler reassembler_;
    bms::BmsData last_data_;
    uint32_t frame_count_ = 0;
};
static BmsNotifyHandler g_notify_handler;

// Non-const ref: NimBLE 1.4's accessors are not const-qualified.
static bool isTarget(NimBLEAdvertisedDevice& dev) {
    if (strcasecmp(dev.getAddress().toString().c_str(), kTargetAddr) == 0) return true;
    return dev.haveName() && dev.getName() == kTargetName;
}

static void printHex(const std::string& s) {
    for (size_t i = 0; i < s.length(); ++i) {
        Serial.printf("%02x", (uint8_t)s[i]);
    }
}

// Returns the target's RSSI, or 0 if it wasn't seen this sweep. When found,
// also copies the advertised device into *out_target if it is non-null — the
// copy stays valid after scan->clearResults(), unlike a reference into the
// scan's result list, so connect() can use it afterwards.
static int printResults(NimBLEScanResults& results, NimBLEAdvertisedDevice* out_target) {
    const int count = results.getCount();

    Serial.printf("\n=== sweep %lu — %d device%s ===\n", (unsigned long)++g_sweep,
                  count, count == 1 ? "" : "s");
    Serial.println(F("  RSSI  ADDRESS            NAME"));
    Serial.println(F("  ----  -----------------  --------------------------"));

    int target_rssi = 0;
    bool found = false;

    for (int i = 0; i < count; ++i) {
        NimBLEAdvertisedDevice dev = results.getDevice(i);
        const bool hit = isTarget(dev);

        Serial.printf("%s %4d  %-17s  %s\n", hit ? ">>" : "  ", dev.getRSSI(),
                      dev.getAddress().toString().c_str(),
                      dev.haveName() ? dev.getName().c_str() : "(no name)");

        if (hit) {
            found = true;
            target_rssi = dev.getRSSI();
            if (out_target != nullptr) *out_target = dev;
            // Advertised manufacturer data contains the MAC bytes but no
            // telemetry (§4) — which is why a connection is required and
            // passive advertisement monitoring is not an option.
            if (dev.haveManufacturerData()) {
                Serial.print(F("     mfr data: "));
                printHex(dev.getManufacturerData());
                Serial.println();
            }
        }
    }

    if (found) {
        Serial.printf("\n  TARGET FOUND: %s @ %d dBm\n", kTargetName, target_rssi);
    } else {
        Serial.printf("\n  target %s NOT seen this sweep\n", kTargetName);
    }
    Serial.printf("  free heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());

    return found ? target_rssi : 0;
}

// Connect, discover FFF0, handshake, subscribe (M2+M3). Leaves the
// connection open on success — M7 holds it per §5.6, rather than the
// M2-M6b probe's connect/round-trip/disconnect. Returns false, having
// already cleaned up via g_transport.disconnect(), on any step's failure.
static bool connectAndHandshake(NimBLEAdvertisedDevice& target_dev) {
    Serial.println(F("\n--- connect + discover ---"));
    if (!g_transport.connect(&target_dev)) {
        Serial.println(F("  connect FAILED"));
        return false;
    }
    g_transport.logDiscovery();

    // §5.1 steps 2-3: HiLink to FFFA, with response, then read FFFA back and
    // gate on 0x01. Everything past this point is silently ignored by the
    // BMS otherwise, and the link drops ~4 s after connecting.
    bool hs_ok = g_transport.write(bms::GattChar::Handshake,
                                    (const uint8_t*)bms::tdt::kHandshakeMagic,
                                    bms::tdt::kHandshakeMagicLen, true);
    uint8_t hs_reply[8] = {0};
    const int hs_len = hs_ok ? g_transport.read(bms::GattChar::Handshake,
                                                 hs_reply, sizeof(hs_reply))
                              : -1;
    hs_ok = hs_ok && hs_len >= 1 && hs_reply[0] == bms::tdt::kHandshakeAck;
    Serial.printf("  HiLink -> FFFA: read-back 0x%02x (%s)\n",
                  hs_len > 0 ? hs_reply[0] : 0, hs_ok ? "ACK" : "NOT ACK");
    if (!hs_ok) {
        g_transport.disconnect();
        return false;
    }

    if (!g_transport.subscribe(&g_notify_handler)) {
        Serial.println(F("  subscribe FFF1 FAILED"));
        g_transport.disconnect();
        return false;
    }
    Serial.println(F("  subscribe FFF1: OK — polling"));
    return true;
}

void setup() {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) {
        // USB CDC needs a moment; don't wait forever on a headless boot.
    }

#if defined(BOARD_STAMPLC)
    Serial.println(F("\n\nWattCycle BLE BMS reader — M0b + M1 + M2 + M3 + M4 + M5 + M6b + M7 + M7a (StamPLC)"));
#else
    Serial.println(F("\n\nWattCycle BLE BMS reader — M0b + M1 + M2 + M3 + M4 + M5 + M6b + M7"));
#endif
    Serial.printf("heap at boot: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf("looking for: %s / %s\n", kTargetName, kTargetAddr);

    // M0b/M7a: prove the panel before it has to show real data. On the
    // Heltec V3, a failure here is almost always the Vext step (§9) — the
    // OLED is powered through Vext, so without it the panel is dark and
    // does not ACK on I2C. StamPLC's TFT is integrated on the same board
    // and always reports OK (TftDisplay::begin() has no "not found" case).
    g_have_display = g_display.begin("WattCycle BMS");
    Serial.printf("display: %s\n", g_have_display ? "OK" : "NOT FOUND (check Vext / wiring)");
    if (g_have_display) {
        g_display.setDeviceName(kTargetName);
        g_display.setLink(LinkState::Scanning, 0, false);
    }

    NimBLEDevice::init("");
    // Request the largest MTU the stack allows (§5.5), before any connect.
    // Default 23 fragments every response; this is what removes that class
    // of bug. Reassembly (M4) means a refusal still degrades gracefully
    // instead of corrupting data.
    NimBLEDevice::setMTU(bms::kDesiredMtu);
    Serial.printf("heap after NimBLE init: %lu bytes\n", (unsigned long)ESP.getFreeHeap());

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);   // request scan responses, so names appear
    scan->setInterval(100);
    scan->setWindow(99);
}

static void loopScanning() {
    NimBLEScan* scan = NimBLEDevice::getScan();

    NimBLEAdvertisedDevice target_dev;
    NimBLEScanResults results = scan->start(kScanSeconds, false);
    const int rssi = printResults(results, &target_dev);
    scan->clearResults();   // free the result list before the next sweep

    if (g_have_display) {
        g_display.setLink(LinkState::Scanning, rssi, rssi != 0);
        g_display.render(millis());
    }

    if (rssi == 0) return;   // not seen this sweep — stay Scanning, try again next sweep

    if (connectAndHandshake(target_dev)) {
        g_state = ConnState::Polling;
        g_consecutive_failures = 0;
        g_last_poll_ms = 0;   // poll immediately on entering Polling
        if (g_have_display) {
            g_display.setLink(LinkState::Connected, g_transport.rssi(), true);
            g_display.render(millis());
        }
    } else {
        ++g_consecutive_failures;
        Serial.printf("  consecutive failures: %lu\n", (unsigned long)g_consecutive_failures);
    }
}

static void loopPolling() {
    const uint32_t now = millis();

    // The "survives battery going out of range and coming back" half of M7:
    // detect the drop and fall back to Scanning, which will reconnect once
    // the target is seen again.
    if (!g_transport.isConnected()) {
        Serial.println(F("\n--- link dropped, resuming scan ---"));
        ++g_consecutive_failures;
        g_transport.disconnect();   // releases the NimBLEClient cleanly
        g_state = ConnState::Scanning;
        if (g_have_display) g_display.setLink(LinkState::Scanning, 0, false);
        return;
    }

    if (now - g_last_poll_ms >= kPollIntervalMs) {
        g_last_poll_ms = now;
        uint8_t req[bms::tdt::kRequestLen];
        const size_t req_len = bms::tdt::buildRequest(bms::tdt::CMD_CELLS_PACK, req, sizeof(req));
        const bool sent = g_transport.write(bms::GattChar::Tx, req, req_len, true);
        if (!sent) ++g_consecutive_failures;
        Serial.printf("\n--- poll: 0x8C sent=%s  rssi=%d dBm  heap=%lu  failures=%lu ---\n",
                      sent ? "OK" : "FAILED", g_transport.rssi(),
                      (unsigned long)ESP.getFreeHeap(), (unsigned long)g_consecutive_failures);
    }

    g_notify_handler.tick(now);

    // Push only genuinely new frames (M4/M5 decode inside onNotify() already
    // ran by the time we get here) — pushing the same frame repeatedly would
    // keep re-stamping BmsDisplay's freshness timer and defeat the M6b
    // staleness check.
    if (g_notify_handler.frameCount() != g_last_frame_seen) {
        g_last_frame_seen = g_notify_handler.frameCount();
        if (g_have_display) g_display.setData(g_notify_handler.lastData(), now);
    }

    if (g_have_display) {
        g_display.setLink(LinkState::Connected, g_transport.rssi(), true);
        g_display.render(now);
    }

    delay(200);   // keep the loop responsive without hammering the CPU
}

void loop() {
    switch (g_state) {
        case ConnState::Scanning:
            loopScanning();
            break;
        case ConnState::Polling:
            loopPolling();
            break;
    }
}
