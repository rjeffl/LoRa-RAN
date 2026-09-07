#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 <holder>          # D31 open - see LRAN-Decision-Register
"""Check the firmware's paOptTable mirror against the pinned RadioLib source.

    python3 tools/rangetest/check_pa_table.py

WHY THIS EXISTS. `firmware/range-test/src/pa_config.cpp` carries a copy of
RadioLib's `paOptTable` so the PA configuration actually applied can be logged at
boot (handoff 6 requirement 7, M21 findings 7.5). The table cannot be read from
RadioLib at runtime: it is file-static in SX1262.cpp, and the SX1262's PA config is
written by the SetPaConfig COMMAND rather than to a register that reads back.

So the record rests on a premise - "the mirror is RadioLib 7.7.1's table" - and root
CLAUDE.md says a load-bearing premise must name the check that would falsify it and
point at where that check is tracked. This is that check. Prose claiming the two
agree reads like diligence and behaves like nothing.

WHAT IT COMPARES. Both tables are parsed from source, independently, and diffed
entry by entry. It also checks that the RadioLib version pinned in every
platformio.ini matches the version the mirror names, because a mirror that is
correct for a driver nobody builds any more is not correct.

RUN IT AFTER ANY RadioLib VERSION CHANGE. A version bump that changes the table and
nothing else is silent in every other check in this repository: the firmware builds,
the host tests pass, and the traces record a PA configuration that is no longer the
one applied.
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

MIRROR = REPO / "firmware/range-test/src/pa_config.cpp"
MIRROR_HEADER = REPO / "firmware/range-test/src/pa_config.h"
PLATFORMIO_INIS = sorted(REPO.glob("firmware/*/platformio.ini")) + sorted(
    REPO.glob("lib/*/platformio.ini")
)

# .pio/ is gitignored, so the pinned source is present only after a build or a
# `pio pkg install`. Every environment resolves the same pinned version, so any of
# them will do; they are all tried so the check works from a partial build tree.
RADIOLIB_CANDIDATES = [
    REPO / "firmware/range-test/.pio/libdeps" / env / "RadioLib/src/modules/SX126x/SX1262.cpp"
    for env in ("heltec", "xiao")
]

ENTRY_RE = re.compile(
    r"\{\s*\.paDutyCycle\s*=\s*(-?\d+)\s*,"
    r"\s*\.hpMax\s*=\s*(-?\d+)\s*,"
    r"\s*\.paVal\s*=\s*(-?\d+)\s*\}"
)
MIRROR_ENTRY_RE = re.compile(r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}")


def die(msg, hint=None):
    print(f"FAIL: {msg}", file=sys.stderr)
    if hint:
        print(f"      {hint}", file=sys.stderr)
    sys.exit(1)


def parse_radiolib(path):
    """The 32 entries of RadioLib's `paOptTable`, in order."""
    text = path.read_text()
    m = re.search(r"paOptTable\s*\[\s*32\s*\]\s*=\s*\{(.*?)\n\}", text, re.S)
    if not m:
        die(f"could not find paOptTable in {path}",
            "RadioLib changed the table's declaration - read it and update the "
            "mirror and this parser together.")
    entries = [tuple(int(g) for g in e) for e in ENTRY_RE.findall(m.group(1))]
    if len(entries) != 32:
        die(f"parsed {len(entries)} entries from RadioLib, expected 32",
            "a parse this wrong is a changed source layout, not a changed table.")
    return entries


def parse_mirror(path):
    """The 32 entries of kPaOptTable in pa_config.cpp, in order."""
    text = path.read_text()
    m = re.search(r"kPaOptTable\s*\[\s*32\s*\]\s*=\s*\{(.*?)\n\}", text, re.S)
    if not m:
        die(f"could not find kPaOptTable in {path}")
    entries = [tuple(int(g) for g in e) for e in MIRROR_ENTRY_RE.findall(m.group(1))]
    if len(entries) != 32:
        die(f"parsed {len(entries)} entries from the mirror, expected 32")
    return entries


def mirror_version(path):
    m = re.search(r'kPaTableRadioLibVersion\[\]\s*=\s*"([^"]+)"', path.read_text())
    if not m:
        die(f"could not find kPaTableRadioLibVersion in {path}")
    return m.group(1)


def pinned_versions():
    """Every RadioLib pin in the repo, as {version: [files]}."""
    pins = {}
    for ini in PLATFORMIO_INIS:
        for v in re.findall(r"jgromes/RadioLib@([0-9][^\s]*)", ini.read_text()):
            # One file, not one per environment: range-test pins the same version in
            # two [env:] blocks and listing it twice reads like two pins.
            name = ini.relative_to(REPO).as_posix()
            if name not in pins.setdefault(v, []):
                pins[v].append(name)
    return pins


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--radiolib", type=Path,
                    help="path to RadioLib's SX1262.cpp (default: the pinned copy "
                         "under firmware/range-test/.pio/libdeps)")
    args = ap.parse_args()

    src = args.radiolib
    if src is None:
        for c in RADIOLIB_CANDIDATES:
            if c.is_file():
                src = c
                break
    if src is None or not src.is_file():
        # NOT a silent skip. A check that passes when it could not run is worse than
        # no check: it reports success for a comparison that never happened.
        die("the pinned RadioLib source is not present, so the mirror was NOT checked",
            "run `~/.platformio/penv/bin/pio pkg install -d firmware/range-test` "
            "(or any build) first, or pass --radiolib.")

    declared = mirror_version(MIRROR_HEADER)
    pins = pinned_versions()
    if not pins:
        die("no jgromes/RadioLib pin found in any platformio.ini",
            "repo rule 9 requires the version to be pinned in every one.")
    if set(pins) != {declared}:
        detail = "; ".join(f"{v} in {', '.join(f)}" for v, f in sorted(pins.items()))
        die(f"the mirror names RadioLib {declared}, but the repo pins {detail}",
            "re-run this check against the new version and update "
            "kPaTableRadioLibVersion before trusting a boot record.")

    upstream = parse_radiolib(src)
    mirror = parse_mirror(MIRROR)

    bad = [(i, u, m) for i, (u, m) in enumerate(zip(upstream, mirror)) if u != m]
    if bad:
        print(f"FAIL: {len(bad)} of 32 entries differ "
              f"(index = conducted dBm + 9)", file=sys.stderr)
        for i, u, m in bad:
            print(f"  entry {i:2d} ({i - 9:+3d} dBm): "
                  f"RadioLib paDutyCycle={u[0]} hpMax={u[1]} paVal={u[2]}  !=  "
                  f"mirror paDutyCycle={m[0]} hpMax={m[1]} paVal={m[2]}",
                  file=sys.stderr)
        print("\n      RadioLib is right. Update the mirror in pa_config.cpp, and "
              "check\n      whether any committed trace's pa_* lines were captured "
              "under the old\n      table - a trace is a dated record and is "
              "corrected by a new note,\n      never by editing it.", file=sys.stderr)
        sys.exit(1)

    print(f"OK - 32/32 paOptTable entries match RadioLib {declared}")
    print(f"     upstream: {src.relative_to(REPO).as_posix()}")
    print(f"     mirror:   {MIRROR.relative_to(REPO).as_posix()}")
    print(f"     pinned in: {', '.join(sorted(f for fs in pins.values() for f in fs))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
