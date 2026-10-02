#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""
Watch exactly what aiobmsble does at the BLE transport layer.

Monkey-patches BleakClient so every connect / subscribe / write / read is
printed with its arguments and timing, then runs aiobmsble's TDT driver against
the battery. Whatever differs from bms_probe.py is our answer.

    python3 instrument.py

Nothing here is guesswork -- it is a recording of a known-working exchange.
"""

import asyncio
import time
import sys

import bleak
from bleak import BleakScanner

T0 = time.monotonic()


def ts() -> str:
    return f"t+{time.monotonic() - T0:6.2f}s"


def hexs(data) -> str:
    try:
        return bytes(data).hex(" ")
    except Exception:
        return repr(data)


def patch():
    orig_connect = bleak.BleakClient.connect
    orig_write = bleak.BleakClient.write_gatt_char
    orig_read = bleak.BleakClient.read_gatt_char
    orig_notify = bleak.BleakClient.start_notify
    orig_disconnect = bleak.BleakClient.disconnect

    async def connect(self, *a, **kw):
        print(f"{ts()}  CONNECT      args={a} kwargs={kw}")
        r = await orig_connect(self, *a, **kw)
        try:
            print(f"{ts()}  ->  connected, MTU={self.mtu_size}")
        except Exception:
            print(f"{ts()}  ->  connected (MTU unavailable)")
        return r

    async def write_gatt_char(self, char, data, response=None, **kw):
        short = str(char)[4:8].upper() if str(char).startswith("0000") else str(char)
        ascii_hint = ""
        try:
            t = bytes(data).decode("ascii")
            if t.isprintable():
                ascii_hint = f'   "{t}"'
        except Exception:
            pass
        print(f"{ts()}  WRITE  {short}  response={response!r:<6} "
              f"len={len(bytes(data)):<3} {hexs(data)}{ascii_hint}")
        return await orig_write(self, char, data, response=response, **kw)

    async def read_gatt_char(self, char, **kw):
        short = str(char)[4:8].upper() if str(char).startswith("0000") else str(char)
        r = await orig_read(self, char, **kw)
        print(f"{ts()}  READ   {short}  -> {hexs(r)}")
        return r

    async def start_notify(self, char, cb, **kw):
        short = str(char)[4:8].upper() if str(char).startswith("0000") else str(char)
        print(f"{ts()}  SUBSCRIBE {short}  kwargs={kw}")

        def wrapped(sender, data):
            print(f"{ts()}  NOTIFY {short}  len={len(data):<3} {hexs(data)}")
            return cb(sender, data)

        return await orig_notify(self, char, wrapped, **kw)

    async def disconnect(self, *a, **kw):
        print(f"{ts()}  DISCONNECT")
        return await orig_disconnect(self, *a, **kw)

    bleak.BleakClient.connect = connect
    bleak.BleakClient.write_gatt_char = write_gatt_char
    bleak.BleakClient.read_gatt_char = read_gatt_char
    bleak.BleakClient.start_notify = start_notify
    bleak.BleakClient.disconnect = disconnect


async def main():
    patch()
    from aiobmsble.bms.tdt_bms import BMS      # imported after patching

    print("scanning ...")
    dev = None
    for d, adv in (await BleakScanner.discover(timeout=15, return_adv=True)).values():
        if any(c and "xdzn" in c.lower() for c in (adv.local_name, d.name)):
            dev = d
            print(f"found {adv.local_name} rssi={adv.rssi} dBm "
                  f"({'weak -- move closer' if adv.rssi < -80 else 'ok'})\n")
            break
    if dev is None:
        sys.exit("not found")

    global T0
    T0 = time.monotonic()
    async with BMS(dev) as bms:
        for i in range(2):
            print(f"\n--- update {i + 1} ---")
            data = await bms.async_update()
            print(f"\n{ts()}  RESULT  SOC={data.get('battery_level')}% "
                  f"V={data.get('voltage')} I={data.get('current')}")
            if i == 0:
                await asyncio.sleep(3)

if __name__ == "__main__":
    asyncio.run(main())
