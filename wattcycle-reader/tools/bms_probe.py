#!/usr/bin/env python3
"""
WattCycle / XiangDian BMS preflight probe.

Connects to the BMS, subscribes to notifications, and immediately sends a JBD
basic-info request -- fast enough to beat the BMS idle-disconnect timeout that
makes this impossible to do by hand in a phone app.

Runs on macOS (CoreBluetooth) and Linux (BlueZ) via bleak.

    python3 -m venv .venv && source .venv/bin/activate
    pip install bleak
    python3 bms_probe.py

Notes:
  * Close the vendor phone app first. One central at a time.
  * On Linux you get real MAC addresses. On macOS CoreBluetooth hides them and
    gives you an opaque UUID instead, so we match on advertised name there.
"""

import argparse
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

# --- target -----------------------------------------------------------------
DEV_NAME = "XDZN-001-49A1"
DEV_MAC = "C0:D6:58:3C:A1:49"          # Linux only; ignored on macOS

SVC_JBD = "0000fff0-0000-1000-8000-00805f9b34fb"
CHR_NOTIFY = "0000fff1-0000-1000-8000-00805f9b34fb"
CHR_WRITE = "0000fff2-0000-1000-8000-00805f9b34fb"

# --- JBD protocol -----------------------------------------------------------
REG_BASIC = 0x03
REG_CELLS = 0x04
REG_HWVER = 0x05


def build_request(reg: int) -> bytes:
    """DD A5 <reg> 00 <chk_hi> <chk_lo> 77"""
    payload = bytes([reg, 0x00])
    chk = (0x10000 - sum(payload)) & 0xFFFF
    return bytes([0xDD, 0xA5]) + payload + chk.to_bytes(2, "big") + bytes([0x77])


def decode_basic(d: bytes) -> dict:
    def u16(i):
        return int.from_bytes(d[i:i + 2], "big")

    def i16(i):
        return int.from_bytes(d[i:i + 2], "big", signed=True)

    n_ntc = d[22]
    temps = [round((u16(23 + 2 * k) - 2731) / 10, 1) for k in range(n_ntc)]
    return {
        "pack_V": u16(0) / 100,
        "current_A": i16(2) / 100,
        "residual_Ah": u16(4) / 100,
        "nominal_Ah": u16(6) / 100,
        "cycles": u16(8),
        "protection": f"0x{u16(16):04X}",
        "sw_version": f"0x{d[18]:02X}",
        "SOC_pct": d[19],
        "fet": {0: "off", 1: "chg", 2: "dsg", 3: "chg+dsg"}.get(d[20] & 0x03),
        "cells": d[21],
        "temps_C": temps,
    }


def decode_cells(d: bytes) -> list:
    return [int.from_bytes(d[i:i + 2], "big") for i in range(0, len(d), 2)]


class FrameAssembler:
    """Accumulate notification fragments until a complete 0x77-terminated frame."""

    def __init__(self):
        self.buf = bytearray()
        self.frames = []

    def feed(self, chunk: bytes):
        if not self.buf and (not chunk or chunk[0] != 0xDD):
            print(f"  ! fragment with no 0xDD start, ignoring: {chunk.hex(' ')}")
            return
        self.buf += chunk
        if len(self.buf) >= 4:
            expected = 4 + self.buf[3] + 3   # hdr(4) + payload + chk(2) + 0x77
            if len(self.buf) >= expected:
                frame, self.buf = bytes(self.buf[:expected]), bytearray(self.buf[expected:])
                self.frames.append(frame)


def parse_frame(f: bytes):
    reg, status, length = f[1], f[2], f[3]
    payload = f[4:4 + length]
    got = int.from_bytes(f[4 + length:6 + length], "big")
    want = (0x10000 - (status + length + sum(payload))) & 0xFFFF
    ok = got == want and f[-1] == 0x77
    print(f"\n  frame reg=0x{reg:02X} status=0x{status:02X} len={length} "
          f"checksum={'OK' if ok else f'BAD (got 0x{got:04X} want 0x{want:04X})'}")
    if status != 0x00:
        print("  ! non-zero status -- command rejected by the BMS")
        return
    try:
        if reg == REG_BASIC:
            for k, v in decode_basic(payload).items():
                print(f"    {k:<14} {v}")
        elif reg == REG_CELLS:
            cells = decode_cells(payload)
            print(f"    {len(cells)} cells: " + ", ".join(f"{mv} mV" for mv in cells))
            print(f"    delta: {max(cells) - min(cells)} mV")
        elif reg == REG_HWVER:
            print(f"    version: {payload.decode('ascii', 'replace')}")
    except Exception as e:  # noqa: BLE001
        print(f"    ! decode failed: {e}  raw={payload.hex(' ')}")


async def probe(reg: int, response: bool, dump: bool):
    print(f"scanning for {DEV_NAME} ...")
    dev = await BleakScanner.find_device_by_name(DEV_NAME, timeout=15.0)
    if dev is None and sys.platform.startswith("linux"):
        dev = await BleakScanner.find_device_by_address(DEV_MAC, timeout=10.0)
    if dev is None:
        sys.exit("not found. Is the vendor app still connected? Is the BMS asleep?")
    print(f"found {dev.name} @ {dev.address}")

    dropped = asyncio.Event()
    t0 = time.monotonic()

    def on_disconnect(_):
        print(f"\n*** disconnected at t+{time.monotonic() - t0:.2f}s")
        dropped.set()

    asm = FrameAssembler()

    def on_notify(_, data: bytearray):
        print(f"  <- t+{time.monotonic() - t0:.2f}s  {len(data)}B  {bytes(data).hex(' ')}")
        asm.feed(bytes(data))

    async with BleakClient(dev, disconnected_callback=on_disconnect) as client:
        t0 = time.monotonic()
        print(f"connected at t+0.00s")

        if dump:
            for s in client.services:
                print(f"  service {s.uuid}")
                for c in s.characteristics:
                    print(f"    char {c.uuid}  {','.join(c.properties)}")

        # Speed matters here -- subscribe and write before the idle timeout.
        await client.start_notify(CHR_NOTIFY, on_notify)
        req = build_request(reg)
        print(f"  -> t+{time.monotonic() - t0:.2f}s  {req.hex(' ')} "
              f"({'with' if response else 'without'} response)")
        await client.write_gatt_char(CHR_WRITE, req, response=response)

        try:
            await asyncio.wait_for(dropped.wait(), timeout=8.0)
        except asyncio.TimeoutError:
            print(f"\nstill connected at t+{time.monotonic() - t0:.2f}s -- no idle drop")

    for f in asm.frames:
        parse_frame(f)
    if not asm.frames:
        print("\nno complete frames received.")
        if asm.buf:
            print(f"partial buffer: {bytes(asm.buf).hex(' ')}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--reg", type=lambda x: int(x, 0), default=REG_BASIC,
                    help="register: 0x03 basic, 0x04 cells, 0x05 version")
    ap.add_argument("--with-response", action="store_true",
                    help="use write-with-response instead of write-without-response")
    ap.add_argument("--dump", action="store_true",
                    help="print the full GATT table on connect")
    a = ap.parse_args()
    asyncio.run(probe(a.reg, a.with_response, a.dump))
