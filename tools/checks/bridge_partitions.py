#!/usr/bin/env python3
"""The bridge's partition table supports A/B OTA with rollback, and the image fits.

Impl Plan 6.5 and R-5.3c: dual-partition OTA with rollback, configured in a
partition table that "cannot be retrofitted to a deployed bridge without a USB
flash." Two things can quietly break that promise, and this checks both.

THE TABLE (always). firmware/bridge/partitions.csv must hold an `otadata`
partition and exactly two OTA app slots, ota_0 and ota_1, of EQUAL size - an
unequal pair is an A/B scheme that works in one direction only. App slots must
be 64 KB aligned, nothing may overlap, and everything must fit the Heltec V3's
8 MB of flash.

THE IMAGE (with --firmware). A built image larger than a slot cannot be OTA'd at
all, and one that is nearly as large is one release from that. The check fails
past 90 % of a slot, which is the point to act while acting is still an OTA.

THE OVERRIDE (with --elf). Rollback on this node depends on ota.cpp's
`extern "C" bool verifyRollbackLater()` replacing Arduino-ESP32's weak default,
which otherwise lets the core mark every image valid before setup() runs. A C++
definition gets a mangled name, overrides nothing and links cleanly - so the only
place the failure is visible without flashing a board is the symbol table. The
linked ELF must carry verifyRollbackLater as a STRONG (T) symbol; W means the
core's default won and V-B9 would fail on the bench.

    python3 tools/checks/bridge_partitions.py
    python3 tools/checks/bridge_partitions.py --firmware firmware/bridge/.pio/build/heltec/firmware.bin
    python3 tools/checks/bridge_partitions.py --elf firmware/bridge/.pio/build/heltec/firmware.elf
    python3 tools/checks/bridge_partitions.py --self-test
"""

import glob
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
TABLE = ROOT / "firmware" / "bridge" / "partitions.csv"

FLASH_BYTES = 8 * 1024 * 1024   # Heltec WiFi LoRa 32 V3, board definition
APP_ALIGN = 0x10000              # ESP-IDF: app partitions on 64 KB boundaries
FILL_LIMIT = 0.90


def parse(text):
    """[(name, type, subtype, offset, size)] from partition CSV text."""
    rows = []
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            raise ValueError(f"short row: {raw!r}")
        name, ptype, sub, off, size = cols[:5]
        rows.append((name, ptype, sub, int(off, 0), int(size, 0)))
    return rows


def check_table(rows):
    problems = []
    apps = {sub: (name, off, size) for name, ptype, sub, off, size in rows if ptype == "app"}

    if not any(ptype == "data" and sub == "ota" for _, ptype, sub, _, _ in rows):
        problems.append("no otadata partition - the bootloader cannot track which slot to boot")

    ota_slots = sorted(s for s in apps if s.startswith("ota_"))
    if ota_slots != ["ota_0", "ota_1"]:
        problems.append(f"OTA app slots are {ota_slots}, expected exactly ota_0 and ota_1")
    elif apps["ota_0"][2] != apps["ota_1"][2]:
        problems.append(
            f"ota_0 is {apps['ota_0'][2]:#x} and ota_1 is {apps['ota_1'][2]:#x} - "
            "unequal slots are an A/B scheme that works in one direction only")

    if "factory" in apps:
        problems.append("a factory partition is present - a USB flash would boot it, "
                        "not the OTA slots, and rollback would target the wrong image")

    for name, ptype, _, off, _ in rows:
        if ptype == "app" and off % APP_ALIGN:
            problems.append(f"{name} at {off:#x} is not 64 KB aligned")

    spans = sorted((off, off + size, name) for name, _, _, off, size in rows)
    for (a0, a1, an), (b0, _, bn) in zip(spans, spans[1:]):
        if b0 < a1:
            problems.append(f"{an} and {bn} overlap")
    if spans and spans[-1][1] > FLASH_BYTES:
        problems.append(f"{spans[-1][2]} ends at {spans[-1][1]:#x}, past 8 MB of flash")

    return problems, (apps["ota_0"][2] if "ota_0" in apps else 0)


def check_image(image_bytes, slot_bytes):
    if slot_bytes == 0:
        return ["no ota_0 slot to measure the image against"]
    fill = image_bytes / slot_bytes
    print(f"image {image_bytes} bytes, slot {slot_bytes} bytes, {fill:.1%} full")
    if image_bytes > slot_bytes:
        return ["the image does not fit an OTA slot - it can only be flashed over USB"]
    if fill > FILL_LIMIT:
        return [f"the image fills {fill:.0%} of a slot, past {FILL_LIMIT:.0%} - one "
                "release from not fitting, on a table that cannot grow without USB"]
    return []


def override_problems(nm_output):
    """Problems with verifyRollbackLater in `nm` output; [] when it is strong."""
    kinds = [parts[-2] for parts in (line.split() for line in nm_output.splitlines())
             if len(parts) >= 2 and parts[-1] == "verifyRollbackLater"]
    if not kinds:
        return ["verifyRollbackLater is absent from the image - rollback is not wired"]
    if "T" not in kinds:
        return [f"verifyRollbackLater is {'/'.join(kinds)}, not T - the core's weak default "
                "won, so every image is marked valid before setup() runs. Is the "
                "definition in ota.cpp still extern \"C\"?"]
    return []


def find_nm():
    pattern = os.path.expanduser(
        "~/.platformio/packages/toolchain-xtensa-esp32s3/bin/*-elf-nm")
    found = sorted(glob.glob(pattern))
    return found[0] if found else None


def check_override(elf):
    nm = find_nm()
    if nm is None:
        return ["xtensa-esp32s3 nm not found - build the target first, which installs it"]
    out = subprocess.check_output([nm, str(elf)]).decode(errors="replace")
    return override_problems(out)


def self_test():
    good = """
    nvs,data,nvs,0x9000,0x5000,
    otadata,data,ota,0xe000,0x2000,
    app0,app,ota_0,0x10000,0x330000,
    app1,app,ota_1,0x340000,0x330000,
    """
    cases = [
        ("good table", good, True),
        ("no otadata", good.replace("otadata,data,ota,0xe000,0x2000,", ""), False),
        ("unequal slots", good.replace("0x340000,0x330000", "0x340000,0x300000"), False),
        ("one slot", good.replace("app1,app,ota_1,0x340000,0x330000,", ""), False),
        ("misaligned", good.replace("app1,app,ota_1,0x340000", "app1,app,ota_1,0x341000"), False),
        ("overlap", good.replace("app1,app,ota_1,0x340000", "app1,app,ota_1,0x300000"), False),
        ("factory", good + "factory,app,factory,0x670000,0x100000,\n", False),
    ]
    ok = True
    for label, text, want_ok in cases:
        problems, _ = check_table(parse(text))
        if (not problems) != want_ok:
            print(f"SELF-TEST FAIL: {label}: {problems}")
            ok = False

    slot = 0x330000
    if check_image(slot // 2, slot) or not check_image(slot - 1, slot) \
            or not check_image(slot + 1, slot):
        print("SELF-TEST FAIL: image fill thresholds")
        ok = False

    # The override: strong passes; weak (the core's default won) and absent fail.
    if override_problems("42080a64 T verifyRollbackLater\n42080e94 W verifyOta\n"):
        print("SELF-TEST FAIL: a strong verifyRollbackLater was flagged")
        ok = False
    if not override_problems("42080a64 W verifyRollbackLater\n"):
        print("SELF-TEST FAIL: a weak verifyRollbackLater passed")
        ok = False
    if not override_problems("42080e94 W verifyOta\n"):
        print("SELF-TEST FAIL: a missing verifyRollbackLater passed")
        ok = False
    # A C++ definition shows up mangled and must not count as the override.
    if not override_problems("42080a64 T _Z19verifyRollbackLaterv\n"):
        print("SELF-TEST FAIL: a mangled C++ definition passed")
        ok = False

    print("SELF-TEST OK" if ok else "SELF-TEST FAILED")
    return 0 if ok else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()

    problems, slot = check_table(parse(TABLE.read_text()))

    if "--firmware" in argv:
        i = argv.index("--firmware")
        image = pathlib.Path(argv[i + 1])
        problems += check_image(image.stat().st_size, slot)

    if "--elf" in argv:
        i = argv.index("--elf")
        problems += check_override(pathlib.Path(argv[i + 1]))
        if not problems:
            print("verifyRollbackLater is strong in the linked image - ota.cpp's verdict owns rollback")

    if problems:
        print(f"FAIL: {TABLE.relative_to(ROOT)}")
        for p in problems:
            print(f"  {p}")
        print("\nImpl Plan 6.5: this table cannot be changed on a deployed bridge "
              "without a USB flash.")
        return 1

    print(f"OK - {TABLE.relative_to(ROOT)}: otadata, two equal OTA slots of {slot:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
