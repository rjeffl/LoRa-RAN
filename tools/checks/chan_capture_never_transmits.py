#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""The listen-only capture image never transmits and never starts a radio of its own.

D1 frequency change brief 5 runs several firmware/chan-capture/ receivers a metre apart,
each on a candidate frequency 0.2 MHz from the next, and compares their captures bucket
for bucket. The comparison rests on one premise: nothing in the room is transmitting
except the property. A receiver that sent a frame would appear in its neighbours'
captures as an excursion, at an offset where the receiver's rejection has not been
measured, and nothing in the capture file would say where it came from.

Root CLAUDE.md: a load-bearing premise must name the check that would falsify it. This
is that check.

WHAT THIS CHECKS. With comments and string literals removed, no file under
firmware/chan-capture/src/ calls:

  transmit(  startTransmit(  transmitDirect(      the SX1262 sending
  scanChannel(  startChannelScan(                 a CAD - silent, but a deaf window
  setOutputPower(                                 a PA setting this image has no use for
  WiFi  esp_wifi  BLE  esp_bt  BluetoothSerial    a second radio on the same board

WHAT THIS DOES NOT CHECK. That a library call made from here does not transmit
internally, or that the ESP32's own emissions stay out of 915 MHz. It reads text. It is a
tripwire on the shape of the mistake, not a proof.

    python3 tools/checks/chan_capture_never_transmits.py
    python3 tools/checks/chan_capture_never_transmits.py --self-test
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "firmware" / "chan-capture" / "src"

FORBIDDEN = [
    (re.compile(r"\b(transmit|startTransmit|transmitDirect)\s*\("), "a transmission"),
    (re.compile(r"\b(scanChannel|startChannelScan)\s*\("), "a CAD"),
    (re.compile(r"\bsetOutputPower\s*\("), "a PA setting"),
    (re.compile(r"\bWiFi\b|\besp_wifi"), "WiFi"),
    (re.compile(r"\bBLE\w*|\besp_bt\w*|\bBluetoothSerial\b"), "Bluetooth"),
]


def strip_comments_and_strings(text):
    """C and C++ comments and string and character literals, replaced by spaces.

    Newlines are kept, so a finding's line number is the file's. A banner string that says
    "never transmits" must not trip the check, and a commented-out call must not hide one.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif c in "\"'":
            quote = c
            out.append(" ")
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out.append(" ")
                    i += 1
                out.append("\n" if i < n and text[i] == "\n" else " ")
                i += 1
            out.append(" ")
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def findings_in(name, text):
    code = strip_comments_and_strings(text)
    found = []
    for lineno, line in enumerate(code.splitlines(), 1):
        for pattern, what in FORBIDDEN:
            m = pattern.search(line)
            if m:
                found.append("%s:%d: %s - `%s`" % (name, lineno, what, m.group(0).strip()))
    return found


def check(src=SRC):
    files = sorted(p for p in src.rglob("*") if p.suffix in (".cpp", ".h", ".c", ".hpp"))
    if not files:
        return ["%s: no source files - the check has nothing to read" % src]
    found = []
    for p in files:
        found += findings_in(str(p.relative_to(ROOT)), p.read_text())
    return found


def self_test():
    cases = [
        ("a transmission", "radio->transmit(buf, n);", True),
        ("a started transmission", "g_radio->startTransmit(buf, n);", True),
        ("a CAD", "st = g_radio->startChannelScan();", True),
        ("a PA setting", "g_radio->setOutputPower(-4, true);", True),
        ("WiFi", "WiFi.begin(ssid, pass);", True),
        ("Bluetooth", "BLEDevice::init(\"x\");", True),
        ("the word in a comment", "// this image never transmits(ever)", False),
        ("the word in a block comment", "/* no startTransmit(\n here */ int x;", False),
        ("the word in a string", "Serial.println(\"never transmit( anything\");", False),
        ("a receive", "g_radio->startReceive();", False),
        ("a name that contains the word", "uint32_t transmitted_count = 0;", False),
    ]
    failed = 0
    for label, text, should_fail in cases:
        got = bool(findings_in("fixture", text))
        if got != should_fail:
            failed += 1
            print("SELF-TEST FAILED: %s - expected %s" % (label, "a finding" if should_fail else "none"))
    if failed:
        return 1
    print("OK - %d fixtures behave" % len(cases))
    return 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
    found = check()
    if found:
        print("chan-capture must not transmit or start a second radio:")
        for f in found:
            print("  " + f)
        return 1
    print("OK - firmware/chan-capture/src/ calls nothing that transmits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
