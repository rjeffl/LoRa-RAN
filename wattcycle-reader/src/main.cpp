// main.cpp — WattCycle BLE BMS reader, milestone M1.
//
// M1 (§11): "Serial lists nearby BLE devices; XDZN_001_49A1 /
// C0:D6:3C:58:49:A1 appears with RSSI."
//
// That is all this does. It does not connect, handshake, or poll — M2 and M3
// add those on top of the transport interface in lib/bms_ble/BmsTransport.h.
// The protocol decoder in lib/bms_ble/TdtProtocol.* is already complete and
// tested against the captured frames (`pio test -e native`), so the remaining
// work is radio plumbing, not decode.
//
// Expected result: the battery shows up at roughly -77 to -88 dBm even at desk
// range — its antenna appears shielded by the BMS heat sink (§5.8). A weak
// number here is normal and is not a fault.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "BmsData.h"
#include "BmsTransport.h"
#include "TdtProtocol.h"

// --- Target (§3) -----------------------------------------------------------
static const char* kTargetName = "XDZN_001_49A1";   // underscores, not dashes
static const char* kTargetAddr = "c0:d6:3c:58:49:a1";

static const uint32_t kScanSeconds = 5;
static const uint32_t kPauseMs = 2000;

static uint32_t g_sweep = 0;

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

static void printResults(NimBLEScanResults& results) {
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
}

void setup() {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) {
        // USB CDC needs a moment; don't wait forever on a headless boot.
    }

    Serial.println(F("\n\nWattCycle BLE BMS reader — M1 (scan only)"));
    Serial.printf("heap at boot: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf("looking for: %s / %s\n", kTargetName, kTargetAddr);

    NimBLEDevice::init("");
    Serial.printf("heap after NimBLE init: %lu bytes\n", (unsigned long)ESP.getFreeHeap());

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);   // request scan responses, so names appear
    scan->setInterval(100);
    scan->setWindow(99);
}

void loop() {
    NimBLEScan* scan = NimBLEDevice::getScan();

    NimBLEScanResults results = scan->start(kScanSeconds, false);
    printResults(results);
    scan->clearResults();   // free the result list before the next sweep

    delay(kPauseMs);
}
