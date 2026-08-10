#!/usr/bin/env python3
"""
WattCycle / XiangDian BMS preflight probe  (v2)

Connects, subscribes, and immediately sends a JBD request -- fast enough to beat
the BMS idle-disconnect timeout. Runs on macOS (CoreBluetooth) and Linux (BlueZ).

    source .venv/bin/activate
    pip install bleak
    python3 bms_probe.py --auto        # sweep write-type / notify-source combos

v2 changes:
  * corrected MAC (C0:D6:58:3C:49:A1 -- matches the 49A1 in the device name)
  * subscribes to FFF2 as well as FFF1; the nRF sniff shows FFF2 has a CCCD,
    and some JBD clones reply there instead
  * always prints characteristic properties on connect
  * --auto sweeps write-type x notify-source across reconnects and reports
    which combination produced a reply
  * --delay to let the subscription settle before writing
  * --repeat to re-send if the first request draws nothing
"""

import argparse
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

DEV_NAME = "XDZN_001_49A1"   # underscores, per the scan result
DEV_MAC = "C0:D6:3C:58:49:A1"          # Linux only; macOS hides MACs

CHR_FFF1 = "0000fff1-0000-1000-8000-00805f9b34fb"
CHR_FFF2 = "0000fff2-0000-1000-8000-00805f9b34fb"
CHR_FFFA = "0000fffa-0000-1000-8000-00805f9b34fb"

REG_BASIC, REG_CELLS, REG_HWVER = 0x03, 0x04, 0x05


def build_request(reg: int) -> bytes:
    payload = bytes([reg, 0x00])
    chk = (0x10000 - sum(payload)) & 0xFFFF
    return bytes([0xDD, 0xA5]) + payload + chk.to_bytes(2, "big") + bytes([0x77])


def decode_basic(d: bytes) -> dict:
    u16 = lambda i: int.from_bytes(d[i:i + 2], "big")
    i16 = lambda i: int.from_bytes(d[i:i + 2], "big", signed=True)
    temps = [round((u16(23 + 2 * k) - 2731) / 10, 1) for k in range(d[22])]
    return {"pack_V": u16(0) / 100, "current_A": i16(2) / 100,
            "residual_Ah": u16(4) / 100, "nominal_Ah": u16(6) / 100,
            "cycles": u16(8), "protection": f"0x{u16(16):04X}",
            "sw_version": f"0x{d[18]:02X}", "SOC_pct": d[19],
            "fet": {0: "off", 1: "chg", 2: "dsg", 3: "chg+dsg"}.get(d[20] & 0x03),
            "cells": d[21], "temps_C": temps}


class FrameAssembler:
    def __init__(self):
        self.buf, self.frames = bytearray(), []

    def feed(self, chunk: bytes):
        if not self.buf and (not chunk or chunk[0] != 0xDD):
            print(f"     ! fragment with no 0xDD start: {chunk.hex(' ')}")
            return
        self.buf += chunk
        if len(self.buf) >= 4:
            need = 4 + self.buf[3] + 3
            if len(self.buf) >= need:
                self.frames.append(bytes(self.buf[:need]))
                self.buf = bytearray(self.buf[need:])


def parse_frame(f: bytes):
    reg, status, length = f[1], f[2], f[3]
    payload = f[4:4 + length]
    got = int.from_bytes(f[4 + length:6 + length], "big")
    want = (0x10000 - (status + length + sum(payload))) & 0xFFFF
    ok = got == want and f[-1] == 0x77
    print(f"\n  frame reg=0x{reg:02X} status=0x{status:02X} len={length} "
          f"checksum={'OK' if ok else f'BAD (got 0x{got:04X} want 0x{want:04X})'}")
    if status:
        print("  ! non-zero status -- command rejected"); return
    try:
        if reg == REG_BASIC:
            for k, v in decode_basic(payload).items():
                print(f"    {k:<14} {v}")
        elif reg == REG_CELLS:
            cells = [int.from_bytes(payload[i:i+2], "big") for i in range(0, len(payload), 2)]
            print(f"    {len(cells)} cells: " + ", ".join(f"{mv}mV" for mv in cells))
            print(f"    delta: {max(cells) - min(cells)} mV")
        elif reg == REG_HWVER:
            print(f"    version: {payload.decode('ascii', 'replace')}")
    except Exception as e:
        print(f"    ! decode failed: {e}  raw={payload.hex(' ')}")


async def find_device(timeout=15.0, address=None):
    """Match by explicit address/UUID if given, else by name substring.

    macOS reports an opaque per-host UUID instead of a MAC, so --address takes
    whatever the scan printed. Name matching is substring + case-insensitive
    because advertised names vary (XDZN_001_49A1 vs XDZN-001-49A1).
    """
    if address:
        return await BleakScanner.find_device_by_address(address, timeout=timeout)

    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for dev, adv in found.values():
        for cand in (adv.local_name, dev.name):
            if cand and "xdzn" in cand.lower():
                print(f"  matched on name '{cand}'  rssi={adv.rssi} dBm")
                return dev
    if sys.platform.startswith("linux"):
        return await BleakScanner.find_device_by_address(DEV_MAC, timeout=8.0)
    return None


async def attempt(dev, reg, response, notify_both, delay, repeat, dump,
                  use_cache=True, conn_timeout=20.0):
    """One connect/subscribe/write cycle. Returns (n_frames, n_notifies, drop_t)."""
    dropped, t0 = asyncio.Event(), time.monotonic()
    asm, n_notify, drop_t = FrameAssembler(), 0, None

    def on_disconnect(_):
        nonlocal drop_t
        drop_t = time.monotonic() - t0
        print(f"  *** disconnected at t+{drop_t:.2f}s")
        dropped.set()

    def make_cb(src):
        def cb(_, data: bytearray):
            nonlocal n_notify
            n_notify += 1
            print(f"  <- t+{time.monotonic()-t0:.2f}s [{src}] {len(data)}B  {bytes(data).hex(' ')}")
            asm.feed(bytes(data))
        return cb

    client = BleakClient(dev, disconnected_callback=on_disconnect, timeout=conn_timeout)
    try:
        # dangerous_use_bleak_cache reuses BlueZ's cached attribute table instead
        # of re-running service discovery. Discovery takes seconds; this BMS hangs
        # up in ~3. Without the cache the connect can never complete.
        # dangerous_use_bleak_cache is a BlueZ-only kwarg; CoreBluetooth caches
        # services itself and doesn't accept it.
        if sys.platform.startswith("linux"):
            await client.connect(dangerous_use_bleak_cache=use_cache)
        else:
            await client.connect()
        t0 = time.monotonic()
        print(f"  connected  write={'with-resp' if response else 'no-resp'} "
              f"notify={'FFF1+FFF2' if notify_both else 'FFF1'}")

        for s in client.services:
            for c in s.characteristics:
                if c.uuid in (CHR_FFF1, CHR_FFF2, CHR_FFFA) or dump:
                    print(f"    {c.uuid[4:8].upper()}  {','.join(c.properties)}")

        await client.start_notify(CHR_FFF1, make_cb("FFF1"))
        if notify_both:
            try:
                await client.start_notify(CHR_FFF2, make_cb("FFF2"))
            except Exception as e:
                print(f"    (FFF2 subscribe failed: {e})")

        if delay:
            await asyncio.sleep(delay)

        req = build_request(reg)
        for i in range(repeat):
            if dropped.is_set():
                break
            print(f"  -> t+{time.monotonic()-t0:.2f}s  {req.hex(' ')}")
            try:
                await client.write_gatt_char(CHR_FFF2, req, response=response)
            except Exception as e:
                print(f"    ! write failed: {e}")
                break
            try:
                await asyncio.wait_for(dropped.wait(), timeout=2.0)
            except asyncio.TimeoutError:
                pass
            if asm.frames:
                break

        if not dropped.is_set():
            try:
                await asyncio.wait_for(dropped.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                print(f"  still connected at t+{time.monotonic()-t0:.2f}s -- no idle drop")
    finally:
        try:
            await client.disconnect()
        except Exception:
            pass

    for f in asm.frames:
        parse_frame(f)
    if asm.buf:
        print(f"  partial buffer: {bytes(asm.buf).hex(' ')}")
    return len(asm.frames), n_notify, drop_t


async def main(a):
    print(f"scanning for {DEV_NAME} ...")
    dev = await find_device(address=a.address)
    if dev is None:
        sys.exit("not found. Vendor app still connected? BMS asleep?")
    print(f"found {dev.name} @ {dev.address}\n")

    if not a.auto:
        await attempt(dev, a.reg, a.with_response, not a.fff1_only,
                      a.delay, a.repeat, a.dump, not a.no_cache, a.timeout)
        return

    combos = [(False, False), (True, False), (False, True), (True, True)]
    results = []
    for response, both in combos:
        print("=" * 64)
        try:
            frames, notifies, drop_t = await attempt(
                dev, a.reg, response, both, a.delay, a.repeat, a.dump,
                not a.no_cache, a.timeout)
        except Exception as e:
            print(f"  ! attempt failed: {e}")
            frames, notifies, drop_t = 0, 0, None
        results.append((response, both, frames, notifies, drop_t))
        if frames:
            print("\n>>> got a valid frame -- stopping sweep")
            break
        await asyncio.sleep(2.0)   # let the BMS settle between connections

    print("\n" + "=" * 64 + "\nSUMMARY")
    print(f"{'write':<12}{'notify':<12}{'frames':<8}{'notifies':<10}{'drop@'}")
    for response, both, frames, notifies, drop_t in results:
        print(f"{'with-resp' if response else 'no-resp':<12}"
              f"{'FFF1+FFF2' if both else 'FFF1':<12}{frames:<8}{notifies:<10}"
              f"{f'{drop_t:.2f}s' if drop_t else '-'}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--reg", type=lambda x: int(x, 0), default=REG_BASIC)
    ap.add_argument("--with-response", action="store_true")
    ap.add_argument("--fff1-only", action="store_true",
                    help="do not subscribe to FFF2")
    ap.add_argument("--auto", action="store_true",
                    help="sweep write-type x notify-source across reconnects")
    ap.add_argument("--delay", type=float, default=0.0,
                    help="seconds to wait after subscribing before writing")
    ap.add_argument("--repeat", type=int, default=2,
                    help="re-send the request this many times if nothing arrives")
    ap.add_argument("--dump", action="store_true",
                    help="print the full GATT table")
    ap.add_argument("--no-cache", action="store_true",
                    help="force fresh service discovery (Linux; usually times out "
                         "against this BMS -- see README note)")
    ap.add_argument("--timeout", type=float, default=20.0,
                    help="connect timeout in seconds")
    ap.add_argument("--address", default=None,
                    help="MAC (Linux) or CoreBluetooth UUID (macOS) to connect "
                         "to directly, skipping the name scan")
    asyncio.run(main(ap.parse_args()))
