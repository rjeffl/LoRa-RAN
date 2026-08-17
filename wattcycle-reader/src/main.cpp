// main.cpp — WattCycle BLE BMS reader, milestones M0b + M1 + M2 + M3 + M4 +
// M5 + M6b.
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
//
// All of M2-M6b run as one block, once per boot, on the first sweep the
// target is seen: stop scanning, connect, discover, handshake, subscribe,
// request, decode whatever comes back, push it to the display, disconnect,
// resume scanning. This is a capability check, not the persistent-connection
// poll loop — that's M7. The reassembler and decoder in
// lib/bms_ble/TdtProtocol.* were already complete and host-tested
// (`pio test -e native`) before any of this ran on hardware, so this block is
// wiring, not new decode work.
//
// Measured RSSI: -77 to -88 dBm at desk range, -60 to -65 dBm at the
// approximate mounting position. A weak desk number is normal (§5.8) — the
// battery's antenna appears shielded by the BMS heat sink.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "BmsData.h"
#include "BmsDisplay.h"
#include "BmsTransport.h"
#include "NimBleTransport.h"
#include "TdtProtocol.h"

// --- Target (§3) -----------------------------------------------------------
static const char* kTargetName = "XDZN_001_49A1";   // underscores, not dashes
static const char* kTargetAddr = "c0:d6:3c:58:49:a1";

// 3 s sweeps with no pause: the display is the aiming instrument now, and a
// 7 s refresh is too slow to position a board by. Nothing here is power-tuned;
// that is an M7 question.
static const uint32_t kScanSeconds = 3;

static uint32_t g_sweep = 0;

static BmsDisplay g_display;
static bool g_have_display = false;

static NimBleTransport g_transport;
static bool g_probe_done = false;   // run the connect/discover/handshake check once

// Feeds notifications through the reassembler (M4) and decodes complete
// 0x8C frames (M5). onNotify() runs on NimBLE's host task (§7: "keep it
// short — feed the reassembler, no more"), so decode/print happens there
// too rather than posting to a queue — acceptable for this one-shot probe,
// revisit if M7's poll loop needs onNotify() to stay lighter.
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

    // Call from the main loop while waiting on a response, so a partial
    // frame (fragmentation, MTU refusal) still times out per §5.5 rule 5
    // instead of wedging the reassembler for the next probe.
    void tick(uint32_t now_ms) { reassembler_.tick(now_ms); }

    // M6b: lets main.cpp push a successfully decoded frame to the OLED
    // without BmsNotifyHandler knowing BmsDisplay exists (§9's rule: decode
    // produces a struct, presentation layers consume it).
    bool hasData() const { return has_data_; }
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
        has_data_ = true;

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
    bool has_data_ = false;
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
// scan's result list, so M2's connect() can use it afterwards.
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

void setup() {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) {
        // USB CDC needs a moment; don't wait forever on a headless boot.
    }

    Serial.println(F("\n\nWattCycle BLE BMS reader — M0b + M1 + M2 + M3 + M4 + M5 + M6b"));
    Serial.printf("heap at boot: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf("looking for: %s / %s\n", kTargetName, kTargetAddr);

    // M0b: prove the panel before it has to show real data. A failure here is
    // almost always the Vext step (§9) — the OLED is powered through Vext, so
    // without it the panel is dark and does not ACK on I2C.
    g_have_display = g_display.begin("WattCycle BMS");
    Serial.printf("OLED at 0x%02x: %s\n", kOledAddr,
                  g_have_display ? "OK" : "NOT FOUND (check Vext / wiring)");
    if (g_have_display) {
        g_display.setDeviceName(kTargetName);
        g_display.setLink(LinkState::Scanning, 0, false);
    }

    NimBLEDevice::init("");
    // Request the largest MTU the stack allows (§5.5), before any connect.
    // Default 23 fragments every response; this is what removes that class
    // of bug. Still-implemented reassembly (M4) means a refusal degrades
    // gracefully instead of corrupting data.
    NimBLEDevice::setMTU(bms::kDesiredMtu);
    Serial.printf("heap after NimBLE init: %lu bytes\n", (unsigned long)ESP.getFreeHeap());

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);   // request scan responses, so names appear
    scan->setInterval(100);
    scan->setWindow(99);
}

void loop() {
    NimBLEScan* scan = NimBLEDevice::getScan();

    NimBLEAdvertisedDevice target_dev;
    NimBLEScanResults results = scan->start(kScanSeconds, false);
    const int rssi = printResults(results, &target_dev);
    scan->clearResults();   // free the result list before the next sweep

    // M2-M6b (§11): connect, discover FFF0, handshake, subscribe, request
    // 0x8C, decode it, push it to the display. Runs once — this is a
    // capability check, not the persistent-connection poll loop (that's
    // M7) — so a fresh boot is how to re-run it.
    if (!g_probe_done && rssi != 0) {
        g_probe_done = true;
        Serial.println(F("\n--- M2: connect + discover ---"));
        if (g_transport.connect(&target_dev)) {
            g_transport.logDiscovery();

            // M6b: show the filled link indicator for real once there is an
            // actual connection to report, rather than waiting for the next
            // scan-driven render at the bottom of loop().
            if (g_have_display) {
                g_display.setLink(LinkState::Connected, g_transport.rssi(), true);
                g_display.render(millis());
            }

            Serial.println(F("--- M3: handshake ---"));
            // §5.1 steps 2-3: HiLink to FFFA, with response, then read FFFA
            // back and gate on 0x01. Everything past this point is silently
            // ignored by the BMS otherwise, and the link drops ~4 s in.
            bool hs_ok = g_transport.write(
                bms::GattChar::Handshake,
                (const uint8_t*)bms::tdt::kHandshakeMagic,
                bms::tdt::kHandshakeMagicLen, true);
            uint8_t hs_reply[8] = {0};
            int hs_len = hs_ok ? g_transport.read(bms::GattChar::Handshake,
                                                   hs_reply, sizeof(hs_reply))
                                : -1;
            hs_ok = hs_ok && hs_len >= 1 &&
                    hs_reply[0] == bms::tdt::kHandshakeAck;
            Serial.printf("  HiLink -> FFFA: %s, read-back: 0x%02x (%s)\n",
                          hs_ok ? "written" : "write FAILED",
                          hs_len > 0 ? hs_reply[0] : 0,
                          hs_ok ? "ACK" : "NOT ACK");

            if (hs_ok) {
                const bool sub_ok = g_transport.subscribe(&g_notify_handler);
                Serial.printf("  subscribe FFF1: %s\n", sub_ok ? "OK" : "FAILED");

                if (sub_ok) {
                    uint8_t req[bms::tdt::kRequestLen];
                    const size_t req_len = bms::tdt::buildRequest(
                        bms::tdt::CMD_CELLS_PACK, req, sizeof(req));
                    const bool sent = g_transport.write(bms::GattChar::Tx, req,
                                                         req_len, true);
                    Serial.printf("  0x8C request sent: %s\n", sent ? "OK" : "FAILED");
                    Serial.println(F("--- M4/M5: reassembly + decode ---"));

                    // Notifications land asynchronously via NimBLE's host
                    // task, not from anything we pump here — give it a
                    // window to arrive, ticking the reassembler so a
                    // partial frame still times out per §5.5 rule 5.
                    const uint32_t wait_until = millis() + 1500;
                    while ((int32_t)(wait_until - millis()) > 0) {
                        delay(20);
                        g_notify_handler.tick(millis());
                    }

                    // M6b (§11): "SOC / voltage / current / temp on the OLED
                    // ... with stale-data handling." Pushing the decoded
                    // frame here, then leaving it cached in BmsDisplay, is
                    // enough to exercise the staleness rule (§9,
                    // kStaleAfterMs) over the next few scan-only sweeps even
                    // though the persistent poll loop is still M7.
                    if (g_have_display && g_notify_handler.hasData()) {
                        g_display.setData(g_notify_handler.lastData(), millis());
                        g_display.render(millis());
                    }
                }
            }

            g_transport.disconnect();
            Serial.println(F("--- M2-M6b block complete, disconnected ---"));
        } else {
            Serial.println(F("--- M2: connect FAILED ---"));
        }
    }

    if (g_have_display) {
        // Back to Scanning — the indicator only fills for real during the
        // M2-M6b block above, while a connection actually exists. Any BmsData
        // pushed there stays cached in BmsDisplay and keeps rendering (fresh,
        // then dashed out after kStaleAfterMs) across the plain scan sweeps
        // that follow, since there's no persistent connection yet (M7).
        g_display.setLink(LinkState::Scanning, rssi, rssi != 0);
        g_display.render(millis());
    }
}
