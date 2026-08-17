// main.cpp — WattCycle BLE BMS reader, milestones M0b + M1 + M2.
//
// M1 (§11): "Serial lists nearby BLE devices; XDZN_001_49A1 /
// C0:D6:3C:58:49:A1 appears with RSSI."   — confirmed on hardware.
// M0b (§11): OLED alive, showing the layout with live RSSI.
// M2 (§11): "Connects, enumerates FFF0, confirms FFF1/FFF2 handles." Runs
// once, on the first sweep the target is seen: stop scanning, connect,
// resolve FFF1/FFF2/FFFA via NimBleTransport, log handles, disconnect, then
// resume scanning. No handshake and no persistent connection yet — those are
// M3. The protocol decoder in lib/bms_ble/TdtProtocol.* is already complete
// and tested against the captured frames (`pio test -e native`), so the
// remaining work is radio plumbing, not decode.
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
static bool g_m2_done = false;   // run the connect+discover check once

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

    Serial.println(F("\n\nWattCycle BLE BMS reader — M0b + M1 + M2"));
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

    // M2 (§11): connect, enumerate FFF0, confirm FFF1/FFF2/FFFA handles.
    // Runs once — this is a capability check, not the persistent-connection
    // poll loop (that's M7) — so a fresh boot is how to re-run it.
    if (!g_m2_done && rssi != 0) {
        g_m2_done = true;
        Serial.println(F("\n--- M2: connect + discover ---"));
        if (g_transport.connect(&target_dev)) {
            g_transport.logDiscovery();
            g_transport.disconnect();
            Serial.println(F("--- M2: OK, disconnected ---"));
        } else {
            Serial.println(F("--- M2: FAILED ---"));
        }
    }

    if (g_have_display) {
        // Link stays Scanning — the indicator only fills once there is a real
        // connection to report, which is M3. Until then RSSI is the whole
        // point of the panel: it makes the board an aiming instrument for
        // finding a mounting position without a laptop attached.
        g_display.setLink(LinkState::Scanning, rssi, rssi != 0);
        g_display.render(millis());
    }
}
