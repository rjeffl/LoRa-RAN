#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# simctl's scenario table mirrors firmware/simnode/src/fault.cpp. This fails when the
# two disagree. Task BF-21; Impl Plan 7.2, 10.5.
#
# WHY A CHECK RATHER THAN A COMMENT. Two lists that must agree is exactly the shape the
# PA-table mirror has (tools/rangetest/check_pa_table.py), and it fails the same way:
# silently, in the direction that looks like everything is fine. A fault added to the
# firmware with no scenario is a row that never runs again, and Impl Plan 7.2's whole
# argument is that the rows most likely to go unrun are the ones that matter most.
#
# It also checks the counter names, because those come from spec 14.1 through
# lran::kCounterRegistry and a scenario naming one that does not exist would fail at the
# bench rather than here.

import os
import pathlib
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "simctl"))

from catalogue import ALL_ROWS, UNDRIVEN  # noqa: E402

FAULT_CPP = os.path.join(ROOT, "firmware", "simnode", "src", "fault.cpp")
COUNTERS_H = os.path.join(ROOT, "lib", "lran-protocol", "include", "lran", "counters.h")


def firmware_rows(text):
    """Every `{"name", FaultId::X, frames, counter,` row in kFaultCatalogue."""
    start = text.index("const FaultInfo kFaultCatalogue[]")
    end = text.index("const size_t kFaultCatalogueLen")
    flat = re.sub(r"\s+", " ", text[start:end])
    rows = {}
    for m in re.finditer(r'\{"([a-z_0-9]+)", FaultId::\w+, (\d+), (\w+),', flat):
        rows[m.group(1)] = int(m.group(2))
    return rows


def counter_alias_map(text):
    """`constexpr auto kRxRunt = &lran::Counters::rx_runt;` -> {kRxRunt: rx_runt}."""
    out = {"kNone": None}
    for m in re.finditer(r"constexpr auto (\w+)\s*=\s*&lran::Counters::(\w+);", text):
        out[m.group(1)] = m.group(2)
    return out


def registry_names(text):
    return set(re.findall(r'\{"(\w+)",\s*&Counters::', text)) or set(
        re.findall(r"&Counters::(\w+)", text))


def main():
    fault_text = pathlib.Path(FAULT_CPP).read_text()
    fw = firmware_rows(fault_text)
    aliases = counter_alias_map(fault_text)
    counters = registry_names(pathlib.Path(COUNTERS_H).read_text())

    scen = {s.name: s for s in ALL_ROWS}
    covered = set(scen) | set(UNDRIVEN)
    problems = []

    for name in sorted(set(fw) - covered):
        problems.append(f"{name}: in fault.cpp, no scenario and not listed in UNDRIVEN")
    for name in sorted(covered - set(fw)):
        problems.append(f"{name}: a scenario names a fault fault.cpp does not have")

    for name, s in sorted(scen.items()):
        if name not in fw:
            continue
        if s.frames != fw[name]:
            problems.append(
                f"{name}: scenario says {s.frames} frame(s), fault.cpp says {fw[name]}")
        if s.counter is not None and counters and s.counter not in counters:
            problems.append(f"{name}: counter '{s.counter}' is not in spec 14.1's registry")

    # A scenario naming a counter the firmware's own row does not name is a scenario
    # judging the wrong thing - and it would pass at the bench whenever the counter
    # happened to move for another reason.
    for name, s in sorted(scen.items()):
        if name not in fw:
            continue
        fw_counter = aliases.get(
            re.search(r'\{"%s", FaultId::\w+, \d+, (\w+),' % re.escape(name),
                      re.sub(r"\s+", " ", fault_text)).group(1))
        if s.counter != fw_counter:
            problems.append(
                f"{name}: scenario judges '{s.counter}', fault.cpp's row names '{fw_counter}'")

    if problems:
        print("FAIL - simctl's catalogue and fault.cpp disagree:")
        for p in problems:
            print(f"  {p}")
        return 1

    print(f"OK - {len(fw)} faults in fault.cpp, {len(scen)} scripted, "
          f"{len(UNDRIVEN)} explicitly not driven")
    return 0


if __name__ == "__main__":
    sys.exit(main())
