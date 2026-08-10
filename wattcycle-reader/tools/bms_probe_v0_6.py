#!/usr/bin/env python3
"""
XDZN BMS protocol discovery probe  (v0.6)

v0.2-v0.5 established:
  * FFF1 = notify+read, FFF2 = write+write-no-resp+read, FFFA = same as FFF2
  * FFF2 cannot notify (CBATTError 6) -- the CCCD seen in the iOS sniff was noise
  * JBD frames written to FFF2 are ACKed at ATT level but produce no reply,
    and do not reset the ~3.9s inactivity timer
  => plumbing is fine, protocol is wrong.

v0.6 does three things:
  --inventory   dump every service/characteristic with properties, and GATT-read
                every readable characteristic (free data, no protocol needed)
  --sweep       for each writable characteristic x each candidate protocol:
                connect, subscribe to ALL notify chars, send, watch for anything
  --raw HEX     send one arbitrary payload to one characteristic

    python3 bms_probe.py --inventory
    python3 bms_probe.py --sweep
"""

import argparse
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

DEV_NAME_MATCH = "xdzn"
DEV_MAC = "C0:D6:3C:58:49:A1"        # Linux only; macOS uses a per-host UUID


# --- candidate protocols ----------------------------------------------------

def jbd_frame(reg=0x03) -> bytes:
    """Xiaoxiang / JBD / Overkill Solar: DD A5 <reg> 00 <chk16> 77"""
    p = bytes([reg, 0x00])
    return bytes([0xDD, 0xA5]) + p + ((0x10000 - sum(p)) & 0xFFFF).to_bytes(2, "big") + b"\x77"


def daly_frame(cmd=0x90, host=0x80) -> bytes:
    """Daly: A5 <host> <cmd> 08 <8 data bytes> <chk8>  (13 bytes)"""
    body = bytes([0xA5, host, cmd, 0x08]) + bytes(8)
    return body + bytes([sum(body) & 0xFF])


PROTOCOLS = {
    "jbd_basic":   jbd_frame(0x03),
    "jbd_version": jbd_frame(0x05),
    "daly_soc_80": daly_frame(0x90, 0x80),
    "daly_soc_40": daly_frame(0x90, 0x40),
}


# --- JBD decode (kept in case a JBD-shaped reply ever appears) ---------------

def try_decode_jbd(f: bytes):
    if len(f) < 7 or f[0] != 0xDD or f[-1] != 0x77:
        return
    reg, status, length = f[1], f[2], f[3]
    d = f[4:4 + length]
    if status or len(d) < 23:
        return
    u16 = lambda i: int.from_bytes(d[i:i + 2], "big")
    i16 = lambda i: int.from_bytes(d[i:i + 2], "big", signed=True)
    print(f"    JBD basic: {u16(0)/100:.2f}V  {i16(2)/100:.2f}A  SOC={d[19]}%  "
          f"cells={d[21]}  temps=" +
          ",".join(f"{(u16(23+2*k)-2731)/10:.1f}C" for k in range(d[22])))


# --- helpers ----------------------------------------------------------------

async def find_device(timeout, address):
    if address:
        return await BleakScanner.find_device_by_address(address, timeout=timeout)
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for dev, adv in found.values():
        for cand in (adv.local_name, dev.name):
            if cand and DEV_NAME_MATCH in cand.lower():
                print(f"  matched '{cand}' rssi={adv.rssi} dBm")
                return dev
    if sys.platform.startswith("linux"):
        return await BleakScanner.find_device_by_address(DEV_MAC, timeout=8.0)
    return None


async def connect(dev, on_disconnect, timeout):
    c = BleakClient(dev, disconnected_callback=on_disconnect, timeout=timeout)
    if sys.platform.startswith("linux"):
        await c.connect(dangerous_use_bleak_cache=True)
    else:
        await c.connect()
    return c


def classify(client):
    """Return (notify_uuids, write_uuids, read_uuids)."""
    n, w, r = [], [], []
    for s in client.services:
        for c in s.characteristics:
            p = c.properties
            if "notify" in p or "indicate" in p:
                n.append(c.uuid)
            if "write" in p or "write-without-response" in p:
                w.append(c.uuid)
            if "read" in p:
                r.append(c.uuid)
    return n, w, r


def short(uuid: str) -> str:
    """FFF1 for 16-bit-derived UUIDs, last block otherwise."""
    return uuid[4:8].upper() if uuid.startswith("0000") else uuid[-4:].upper()


# --- modes ------------------------------------------------------------------

async def inventory(dev, timeout):
    dropped = asyncio.Event()
    t0 = time.monotonic()
    client = await connect(dev, lambda _: dropped.set(), timeout)
    try:
        print(f"connected t+{time.monotonic()-t0:.2f}s\n")
        for s in client.services:
            print(f"service {s.uuid}")
            for c in s.characteristics:
                print(f"  {short(c.uuid):<6} {c.uuid}  [{','.join(c.properties)}]")
                for d in c.descriptors:
                    print(f"         desc {d.uuid}")
        print("\nreading every readable characteristic:")
        _, _, readable = classify(client)
        for u in readable:
            if dropped.is_set():
                print("  (disconnected mid-read)")
                break
            try:
                v = await client.read_gatt_char(u)
                txt = bytes(v).decode("ascii", "replace").strip()
                printable = "".join(ch if 32 <= ord(ch) < 127 else "." for ch in txt)
                print(f"  {short(u):<6} {len(v):>3}B  {bytes(v).hex(' ')}   |{printable}|")
            except Exception as e:
                print(f"  {short(u):<6} read failed: {e}")
    finally:
        try:
            await client.disconnect()
        except Exception:
            pass


async def one_shot(dev, write_uuid, frame, label, timeout, settle):
    """Connect, subscribe everything notifiable, send one frame, observe."""
    dropped = asyncio.Event()
    t0 = time.monotonic()
    hits = []

    def on_disc(_):
        print(f"    drop t+{time.monotonic()-t0:.2f}s")
        dropped.set()

    client = await connect(dev, on_disc, timeout)
    try:
        t0 = time.monotonic()
        notify_uuids, _, _ = classify(client)

        def cb_for(u):
            def cb(_, data: bytearray):
                hits.append((u, bytes(data)))
                print(f"    <- t+{time.monotonic()-t0:.2f}s [{short(u)}] "
                      f"{len(data)}B {bytes(data).hex(' ')}")
                try_decode_jbd(bytes(data))
            return cb

        subscribed = []
        for u in notify_uuids:
            try:
                await client.start_notify(u, cb_for(u))
                subscribed.append(short(u))
            except Exception:
                pass
        print(f"  {label:<22} -> {short(write_uuid):<6} "
              f"(listening: {','.join(subscribed) or 'none'})")

        if settle:
            await asyncio.sleep(settle)
        try:
            await client.write_gatt_char(write_uuid, frame, response=False)
        except Exception as e1:
            try:
                await client.write_gatt_char(write_uuid, frame, response=True)
            except Exception as e2:
                print(f"    write failed both ways: {e2}")
                return hits
        try:
            await asyncio.wait_for(dropped.wait(), timeout=6.0)
        except asyncio.TimeoutError:
            print(f"    still up at t+{time.monotonic()-t0:.2f}s (no idle drop!)")
    finally:
        try:
            await client.disconnect()
        except Exception:
            pass
    return hits


async def sweep(dev, timeout, settle, only_proto):
    dropped = asyncio.Event()
    client = await connect(dev, lambda _: dropped.set(), timeout)
    _, writable, _ = classify(client)
    await client.disconnect()
    print(f"writable characteristics: {', '.join(short(u) for u in writable)}\n")

    protos = {k: v for k, v in PROTOCOLS.items() if not only_proto or k == only_proto}
    results = []
    for wu in writable:
        for name, frame in protos.items():
            print("-" * 60)
            try:
                hits = await one_shot(dev, wu, frame, f"{name} {frame.hex()}",
                                      timeout, settle)
            except Exception as e:
                print(f"    attempt failed: {e}")
                hits = []
            results.append((short(wu), name, len(hits)))
            if hits:
                print("\n>>> RESPONSE -- stopping sweep")
                break
            await asyncio.sleep(1.5)
        else:
            continue
        break

    print("\n" + "=" * 60 + "\nSUMMARY")
    print(f"{'char':<8}{'protocol':<14}{'notifications'}")
    for c, n, h in results:
        print(f"{c:<8}{n:<14}{h}")


async def main(a):
    print("scanning ...")
    dev = await find_device(15.0, a.address)
    if dev is None:
        sys.exit("not found")
    print(f"found {dev.name} @ {dev.address}\n")
    if a.inventory:
        await inventory(dev, a.timeout)
    elif a.raw:
        frame = bytes.fromhex(a.raw.replace(" ", ""))
        await one_shot(dev, a.char, frame, "raw " + frame.hex(), a.timeout, a.settle)
    else:
        await sweep(dev, a.timeout, a.settle, a.proto)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--inventory", action="store_true",
                    help="dump GATT table and read every readable characteristic")
    ap.add_argument("--sweep", action="store_true", help="(default) protocol sweep")
    ap.add_argument("--proto", default=None, choices=list(PROTOCOLS),
                    help="restrict the sweep to one protocol")
    ap.add_argument("--raw", default=None, help="hex payload to send, e.g. 'dda50300fffd77'")
    ap.add_argument("--char", default="0000fff2-0000-1000-8000-00805f9b34fb",
                    help="target characteristic UUID for --raw")
    ap.add_argument("--settle", type=float, default=0.2,
                    help="pause after subscribing, before writing")
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--address", default=None,
                    help="MAC (Linux) or CoreBluetooth UUID (macOS)")
    asyncio.run(main(ap.parse_args()))
