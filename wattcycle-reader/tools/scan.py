#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
import asyncio
from bleak import BleakScanner

async def main():
    print("scanning 15s ...")
    found = await BleakScanner.discover(timeout=15.0, return_adv=True)
    if not found:
        print("\n*** ZERO devices seen -- that means Bluetooth permission,")
        print("    not the battery. Grant iTerm2 access in System Settings.")
        return
    rows = sorted(found.values(), key=lambda x: x[1].rssi, reverse=True)
    for dev, adv in rows:
        name = adv.local_name or dev.name or "(no name)"
        uuids = ", ".join(u[4:8].upper() for u in adv.service_uuids) or "-"
        print(f"{adv.rssi:>5} dBm  {name:<28} {dev.address}  svc:{uuids}")

asyncio.run(main())