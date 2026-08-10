#!/usr/bin/env python3
"""
TDT smart BMS reader / raw frame tool  (v0.7)

Independent implementation of the protocol as documented in poc-plan.md §5.
Deliberately NOT a wrapper around aiobmsble: if this and aiobmsble agree, the
written spec is correct and the C++ port can be built from the doc alone.

    python3 bms_probe.py                    # poll every 2.5s, decoded output
    python3 bms_probe.py --raw              # also dump raw frame hex
    python3 bms_probe.py --cmd 0x8d --once  # single arbitrary command
    python3 bms_probe.py --selftest         # decode captured frames, no hardware

Protocol summary (see §5):
  connect -> write b"HiLink" to FFFA -> read FFFA (expect 0x01) ->
  subscribe FFF1 -> requests to FFF2, responses notify on FFF1
  request:  1E 00 01 03 00 <cmd> 00 00 <crc_hi> <crc_lo> 0D
  response: 7E 00 01 03 00 <cmd> 00 <len> <payload> <crc_hi> <crc_lo> 0D
  CRC-16/MODBUS over all preceding bytes, transmitted BIG-endian.
  Frame length is 8 + len + 3. Do NOT frame on the 0x0D terminator: 0x0D
  occurs inside payloads (cell voltage 3.465V = 0x0D89).
"""

import argparse
import asyncio
import sys
import time

try:                                   # --selftest runs without bleak installed
    from bleak import BleakClient, BleakScanner
except ImportError:
    BleakClient = BleakScanner = None

NAME_MATCH = "xdzn"
MAC = "C0:D6:3C:58:49:A1"                                   # Linux only
FFF1 = "0000fff1-0000-1000-8000-00805f9b34fb"               # notify
FFF2 = "0000fff2-0000-1000-8000-00805f9b34fb"               # requests
FFFA = "0000fffa-0000-1000-8000-00805f9b34fb"               # handshake (!)
HANDSHAKE = b"HiLink"

HEAD_REQ, HEAD_RSP, TERM = 0x1E, 0x7E, 0x0D
CMD_STATUS, CMD_ALARM, CMD_INFO = 0x8C, 0x8D, 0x92
HDR_LEN, TAIL_LEN = 8, 3


# --- framing ----------------------------------------------------------------

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_request(cmd: int) -> bytes:
    body = bytes([HEAD_REQ, 0x00, 0x01, 0x03, 0x00, cmd, 0x00, 0x00])
    return body + crc16_modbus(body).to_bytes(2, "big") + bytes([TERM])


class Assembler:
    """Length-driven reassembly. Terminator-driven framing would corrupt 0x8C."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, chunk: bytes):
        self.buf += chunk
        out = []
        while True:
            # resync: drop anything before a plausible frame head
            i = self.buf.find(HEAD_RSP)
            if i < 0:
                self.buf.clear()
                break
            if i:
                del self.buf[:i]
            if len(self.buf) < HDR_LEN:
                break
            total = HDR_LEN + self.buf[HDR_LEN - 1] + TAIL_LEN
            if len(self.buf) < total:
                break
            frame, self.buf = bytes(self.buf[:total]), bytearray(self.buf[total:])
            out.append(frame)
        return out


def check(frame: bytes):
    """Return (ok, cmd, payload, why)."""
    if frame[-1] != TERM:
        return False, None, b"", f"bad terminator 0x{frame[-1]:02X}"
    tx = int.from_bytes(frame[-3:-1], "big")
    calc = crc16_modbus(frame[:-3])
    if tx != calc:
        return False, None, b"", f"CRC tx=0x{tx:04X} calc=0x{calc:04X}"
    return True, frame[5], frame[HDR_LEN:-TAIL_LEN], ""


# --- decoding ---------------------------------------------------------------

def u16(b, i):
    return int.from_bytes(b[i:i + 2], "big")


def decode_status(p: bytes) -> dict:
    i = 0
    n = p[i]; i += 1
    cells = [u16(p, i + 2 * k) / 1000 for k in range(n)]; i += 2 * n
    m = p[i]; i += 1
    temps = [round((u16(p, i + 2 * k) - 2731) / 10, 1) for k in range(m)]; i += 2 * m
    cur = u16(p, i)
    discharging = bool(cur & 0x4000)
    amps = (cur & 0x3FFF) / 100
    return {
        "cells_V": cells,
        "delta_mV": round((max(cells) - min(cells)) * 1000),
        "temps_C": temps,
        "current_A": -amps if (discharging and amps) else amps,
        "current_raw": f"0x{cur:04X}",
        "pack_V": u16(p, i + 2) / 100,
        "remaining_Ah": u16(p, i + 4) / 10,
        "nominal_Ah": u16(p, i + 6) / 10,
        "cycles": u16(p, i + 8),
        "soh_pct": u16(p, i + 10) / 10,
        "soc_pct": u16(p, i + 12),
    }


def decode_alarm(p: bytes) -> dict:
    """Partial: §5.7 documents only that nonzero in the alarm region = problem."""
    i = 0
    n = p[i]; i += 1
    cell_alarms = p[i:i + n]; i += n
    m = p[i]; i += 1
    temp_alarms = p[i:i + m]; i += m
    rest = p[i:]
    return {
        "cell_alarms": cell_alarms.hex(" "),
        "temp_alarms": temp_alarms.hex(" "),
        "status_region": rest.hex(" "),
        "any_problem": any(cell_alarms) or any(temp_alarms),
    }


def decode_info(p: bytes) -> dict:
    txt = p[1:].decode("ascii", "replace")
    return {"raw_ascii": "".join(c if 32 <= ord(c) < 127 else "." for c in txt)}


DECODERS = {CMD_STATUS: decode_status, CMD_ALARM: decode_alarm, CMD_INFO: decode_info}


def render(cmd: int, data: dict) -> str:
    if cmd != CMD_STATUS:
        return "\n".join(f"    {k:<16}{v}" for k, v in data.items())
    d = data
    flow = ("discharging" if d["current_A"] < 0 else
            "charging" if d["current_A"] > 0 else "at rest")
    return (
        f"    SOC          {d['soc_pct']}%   ({d['remaining_Ah']} / {d['nominal_Ah']} Ah)\n"
        f"    pack         {d['pack_V']:.2f} V\n"
        f"    current      {d['current_A']:+.2f} A  {flow}  [raw {d['current_raw']}]\n"
        f"    cells        " + "  ".join(f"{v:.3f}" for v in d["cells_V"]) +
        f"   delta {d['delta_mV']} mV\n"
        f"    temps        " + "  ".join(f"{t:.1f}C" for t in d["temps_C"]) + "\n"
        f"    cycles       {d['cycles']}   SOH {d['soh_pct']}%"
    )


# --- self test (no hardware) ------------------------------------------------

CAPTURES = {
    CMD_STATUS: bytes.fromhex(
        "7e000103008c0020"
        "040d890da10d9c0d9b040b820b9d0b7f0b7e"
        "40000570 03e703e8 0001 03e8 0064".replace(" ", "") + "55a30d"),
    CMD_ALARM: (b"\x7e\x00\x01\x03\x00\x8d\x00\x18"
                b"\x04\x00\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00"
                b"\x00\x00\x00\x00\x06\x29\x00\x00\x00\x00\x00\x00"
                b"\xbd\x3f\x0d"),
}


def selftest() -> int:
    print("request frames (must match the doc):")
    for c in (CMD_STATUS, CMD_ALARM, CMD_INFO):
        print(f"  0x{c:02X}: {build_request(c).hex(' ')}")

    print("\nreassembly under fragmentation:")
    fails = 0
    for cmd, frame in CAPTURES.items():
        for size in (20, 7, 1):          # MTU-sized, awkward, worst case
            asm = Assembler()
            got = []
            for k in range(0, len(frame), size):
                got += asm.feed(frame[k:k + size])
            ok = got == [frame]
            fails += not ok
            print(f"  0x{cmd:02X} in {size:>2}-byte chunks: "
                  f"{'ok' if ok else 'FAIL'}  ({len(got)} frame(s))")

    print("\ndecode vs known-good values:")
    ok, cmd, payload, why = check(CAPTURES[CMD_STATUS])
    if not ok:
        print(f"  CRC FAIL: {why}")
        return 1
    d = decode_status(payload)
    expect = {"soc_pct": 100, "pack_V": 13.92, "current_A": 0.0, "cycles": 1,
              "remaining_Ah": 99.9, "nominal_Ah": 100.0}
    for k, v in expect.items():
        good = d[k] == v
        fails += not good
        print(f"  {k:<14}{d[k]!r:<10} expected {v!r:<10} {'ok' if good else 'FAIL'}")
    for k, v in (("cells_V", [3.465, 3.489, 3.484, 3.483]),
                 ("temps_C", [21.5, 24.2, 21.2, 21.1])):
        good = d[k] == v
        fails += not good
        print(f"  {k:<14}{'ok' if good else f'FAIL {d[k]} != {v}'}")

    print("\nnote: 0x8C contains "
          f"{CAPTURES[CMD_STATUS][:-1].count(TERM)} literal 0x0D bytes before the "
          "terminator -- length-driven framing is mandatory")
    print(f"\n{'ALL PASS' if not fails else f'{fails} FAILURE(S)'}")
    return 1 if fails else 0


# --- live -------------------------------------------------------------------

async def run(a):
    if BleakScanner is None:
        sys.exit("bleak not installed:  pip install bleak")
    print("scanning ...")
    if a.address:
        dev = await BleakScanner.find_device_by_address(a.address, timeout=15)
    else:
        dev = None
        for d, adv in (await BleakScanner.discover(timeout=15, return_adv=True)).values():
            if any(c and NAME_MATCH in c.lower() for c in (adv.local_name, d.name)):
                dev, rssi = d, adv.rssi
                print(f"  {adv.local_name}  rssi={rssi} dBm")
                break
        if dev is None and sys.platform.startswith("linux"):
            dev = await BleakScanner.find_device_by_address(MAC, timeout=8)
    if dev is None:
        sys.exit("not found")

    async def session(hs_mode, first_cmd, settle, req_mode, label, poll=False):
        """One connection. Returns list of (cmd, frame) received."""
        dropped = asyncio.Event()
        asm = Assembler()
        pending: asyncio.Queue = asyncio.Queue()
        got = []
        t0 = time.monotonic()

        def on_disc(_):
            print(f"    drop t+{time.monotonic()-t0:.2f}s")
            dropped.set()

        def on_notify(_, data: bytearray):
            if a.raw:
                print(f"    <- t+{time.monotonic()-t0:.2f}s {len(data):>3}B "
                      f"{bytes(data).hex(' ')}")
            for f in asm.feed(bytes(data)):
                pending.put_nowait(f)

        client = BleakClient(dev, disconnected_callback=on_disc, timeout=20)
        if sys.platform.startswith("linux"):
            await client.connect(dangerous_use_bleak_cache=True)
        else:
            await client.connect()
        t0 = time.monotonic()
        try:
            print(f"  {label}   MTU={getattr(client, 'mtu_size', '?')}")
            # Handshake goes to FFFA -- NOT FFF2. Then read FFFA back: 0x01 = ok.
            await client.write_gatt_char(FFFA, HANDSHAKE, response=hs_mode)
            ack = await client.read_gatt_char(FFFA)
            print(f"    handshake ack from FFFA: {bytes(ack).hex() or '(empty)'}"
                  f"{'  OK' if bytes(ack)[:1] == b'\x01' else '  UNEXPECTED'}")
            await client.start_notify(FFF1, on_notify)
            await asyncio.sleep(settle)

            async def ask(cmd, tries=2, wait=0.7):
                req = build_request(cmd)
                for k in range(tries):
                    if dropped.is_set():
                        return None
                    if a.raw:
                        print(f"    -> t+{time.monotonic()-t0:.2f}s #{k+1} "
                              f"0x{cmd:02X}  {req.hex(' ')}")
                    try:
                        await client.write_gatt_char(FFF2, req, response=req_mode)
                    except Exception as e:
                        print(f"    write failed: {e}")
                        return None
                    try:
                        return await asyncio.wait_for(pending.get(), timeout=wait)
                    except asyncio.TimeoutError:
                        continue
                return None

            seq = [first_cmd] if first_cmd == a.cmd else [first_cmd, a.cmd]
            for cmd in seq:
                f = await ask(cmd)
                if f is None:
                    print(f"    0x{cmd:02X}: no response")
                    break
                got.append((cmd, f))
                report(f, cmd, t0)

            if got and poll:
                n = len(got)
                while not dropped.is_set():
                    try:
                        await asyncio.wait_for(dropped.wait(), timeout=a.interval)
                    except asyncio.TimeoutError:
                        pass
                    if dropped.is_set():
                        break
                    f = await ask(a.cmd)
                    if f is None:
                        print("    poll: no response")
                        break
                    n += 1
                    report(f, a.cmd, t0)
                    if a.count and n >= a.count:
                        break
                print(f"    held {time.monotonic()-t0:.1f}s over {n} exchanges")
        finally:
            try:
                await client.disconnect()
            except Exception:
                pass
        return got

    def report(frame, cmd_hint, t0):
        ok, cmd, payload, why = check(frame)
        print(f"\n  [t+{time.monotonic()-t0:5.1f}s] cmd 0x{cmd or cmd_hint:02X} "
              f"{len(frame)}B  {'CRC ok' if ok else 'CRC FAIL: ' + why}")
        if a.raw:
            print(f"    raw {frame.hex(' ')}")
        if ok:
            dec = DECODERS.get(cmd)
            print(render(cmd, dec(payload)) if dec else
                  f"    payload {payload.hex(' ')}")

    if a.matrix:
        # Sweep the things that differ from the known-good aiobmsble sequence.
        combos = [
            (True,  CMD_INFO,   0.05, True),
            (True,  CMD_INFO,   0.50, True),
            (False, CMD_INFO,   0.05, True),
            (False, CMD_INFO,   0.50, True),
            (True,  CMD_INFO,   0.05, False),
            (False, CMD_INFO,   0.05, False),
            (True,  CMD_STATUS, 0.05, True),
            (False, CMD_STATUS, 0.05, True),
        ]
        results = []
        for hs, first, settle, rm in combos:
            label = (f"handshake={'W' if hs else '!W'}  first=0x{first:02X}  "
                     f"settle={settle}s  req={'W' if rm else '!W'}")
            print("-" * 66)
            try:
                got = await session(hs, first, settle, rm, label)
            except Exception as e:
                print(f"    session failed: {e}")
                got = []
            results.append((label, len(got)))
            if got:
                print("\n>>> RESPONSE -- stopping sweep")
                break
            await asyncio.sleep(1.5)
        print("\n" + "=" * 66 + "\nSUMMARY")
        for label, n in results:
            print(f"  {n}  {label}")
    else:
        await session(a.hs_mode, CMD_INFO if not a.no_identify else a.cmd,
                      a.settle, a.req_mode,
                      f"handshake={'W' if a.hs_mode else '!W'} settle={a.settle}s",
                      poll=True)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true",
                    help="verify framing and decode against captured frames, no BLE")
    ap.add_argument("--cmd", type=lambda x: int(x, 0), default=CMD_STATUS,
                    help="command byte: 0x8c status, 0x8d alarms, 0x92 info")
    ap.add_argument("--interval", type=float, default=2.5,
                    help="poll period; under the ~3.9s idle timeout by default")
    ap.add_argument("--count", type=int, default=0, help="stop after N polls")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--raw", action="store_true", help="dump raw frame hex")
    ap.add_argument("--info", action="store_true",
                    help="shorthand for --cmd 0x92 --once")
    ap.add_argument("--matrix", action="store_true",
                    help="sweep handshake mode / first command / settle delay")
    ap.add_argument("--hs-mode", dest="hs_mode", action="store_true", default=True,
                    help="handshake with response (default)")
    ap.add_argument("--hs-nw", dest="hs_mode", action="store_false",
                    help="handshake without response")
    ap.add_argument("--req-nw", dest="req_mode", action="store_false", default=True,
                    help="requests without response (default: with)")
    ap.add_argument("--settle", type=float, default=0.05,
                    help="delay after subscribing, before first request")
    ap.add_argument("--no-identify", action="store_true",
                    help="skip the 0x92 identify step")
    ap.add_argument("--address", default=None)
    args = ap.parse_args()
    if args.info:
        args.cmd, args.once = CMD_INFO, True
    sys.exit(selftest() if args.selftest else asyncio.run(run(args)) or 0)
