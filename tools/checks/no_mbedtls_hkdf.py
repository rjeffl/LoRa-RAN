#!/usr/bin/env python3
"""Spec 9.1 - key derivation MUST NOT depend on mbedtls_hkdf() or MBEDTLS_HKDF_C.

P7 paid for this once. platform/esp32/mbedtls_mac.cpp compiled cleanly against
<mbedtls/hkdf.h> and then failed at LINK on the Heltec V3: the header ships with
Arduino-ESP32, but MBEDTLS_HKDF_C is not enabled in the prebuilt library, so the
symbol does not exist. A compile-only check passes it; only a link catches it.

The dependency looks harmless. A future framework bump that quietly enabled the
module would make it look harmless AND work - until a different board, or a later
bump, turned it off again, on a fleet with no OTA. HKDF is built from
mbedtls_md_hmac instead (RFC 5869 section 2).

Comments are ignored: this looks for a real dependency, not for prose explaining
why there isn't one.

    python3 tools/checks/no_mbedtls_hkdf.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SCAN = ("lib",)
EXTS = {".c", ".cpp", ".h", ".hpp", ".cc"}

# An include of the header, a call to the function, or the feature macro.
PATTERNS = (
    re.compile(r"#\s*include\s*[<\"]mbedtls/hkdf\.h[>\"]"),
    re.compile(r"\bmbedtls_hkdf\s*\("),
    re.compile(r"\bMBEDTLS_HKDF_C\b"),
)


def strip_comments(line: str) -> str:
    """Drop // comments and lines inside a block comment's continuation."""
    stripped = line.lstrip()
    if stripped.startswith(("//", "*", "/*")):
        return ""
    return line.split("//", 1)[0]


def main() -> int:
    hits = []
    for top in SCAN:
        for path in (ROOT / top).rglob("*"):
            if path.suffix not in EXTS or ".pio" in path.parts:
                continue
            for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                code = strip_comments(line)
                if any(p.search(code) for p in PATTERNS):
                    hits.append(f"{path.relative_to(ROOT)}:{n}: {line.strip()}")

    if hits:
        print("FAIL: forbidden HKDF dependency (spec 9.1):")
        for h in hits:
            print("  " + h)
        print("\nBuild HKDF from mbedtls_md_hmac per RFC 5869 section 2 instead.")
        print("See the 2026-08-29 P7 entry in docs/protocol-lib/engineering-log.md.")
        return 1

    print("OK - no mbedtls_hkdf / MBEDTLS_HKDF_C dependency (spec 9.1)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
