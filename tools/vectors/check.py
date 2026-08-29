#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 <copyright holder - D31, still open>
"""Self-check for the LRAN test vectors - open item W4, milestone P6.

Loads the four generated JSON files and re-derives, from each vector's own
declared inputs, every value the vector asserts:

  kdf       node_key, from master_key/salt/info
  single    frame, from header + payload + key; and its §14 outcome
  frag      every fragment frame, the §11.1 chunk lens, and the §11.2 reassembly
  negative  the §14 stage, Status and counter, from the frame bytes alone

It is written to be a *third* reading, not a copy of generate.py: the CRC here
is table-driven where the generator's is bitwise, the header is unpacked with
`struct` where the generator writes bytes one at a time, and the receive ladder
is implemented here and nowhere else. Nothing is imported from generate.py.

    python3 tools/vectors/check.py       # exit 0 = every vector re-derived

Exits non-zero and names the failing vector on any mismatch. This is what
catches a hand-edited vector file or a generator change that silently altered
output (README, "Regenerating").
"""

from __future__ import annotations

import hashlib
import hmac
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

FORMAT = "lran-test-vectors/1"
SPEC = "LRAN-Protocol-Specification v0.4"
WIRE_VER = 2

LRAN_MAX_FRAME = 222          # §3
LRAN_HDR_LEN = 16             # §5
LRAN_CRC_LEN = 2              # §2.1
LRAN_MAC_LEN = 8              # §9.3
LRAN_MIN_FRAME = LRAN_HDR_LEN + LRAN_CRC_LEN                              # §14 stage 2
LRAN_MAX_PAYLOAD_AUTH = LRAN_MAX_FRAME - LRAN_HDR_LEN - LRAN_MAC_LEN - LRAN_CRC_LEN
LRAN_MAX_PAYLOAD_PLAIN = LRAN_MAX_FRAME - LRAN_HDR_LEN - LRAN_CRC_LEN
LRAN_MAX_SCHEMA_PAYLOAD = LRAN_MAX_PAYLOAD_AUTH
LRAN_PING_MAX_ECHO = LRAN_MAX_PAYLOAD_PLAIN - 2

TYPE_VALUE = {                                                            # §6
    "COMMAND": 0x01, "COMMAND_ACK": 0x02, "POLL": 0x03, "STATUS": 0x04,
    "EVENT": 0x05, "ERROR": 0x06, "PING": 0x07, "HEX_REQ": 0x08,
    "HEX_RSP": 0x09, "CONFIG": 0x0A, "CONFIG_ACK": 0x0B,
}
TYPE_NAME = {v: k for k, v in TYPE_VALUE.items()}

# §7.1 - the carrying-type column. The (type, schema) PAIR is the unit.
SCHEMA_CARRIERS = {
    0x10: {"STATUS"},
    0x11: {"EVENT"},
    0x12: {"CONFIG", "CONFIG_ACK"},
    0xF0: {"STATUS"},
    0xFE: {"STATUS"},
}
SCHEMALESS_TYPES = {"COMMAND", "COMMAND_ACK", "POLL", "ERROR", "PING", "HEX_REQ", "HEX_RSP"}

# §7.1, §7.2, §7.3, §7.5 - fixed payload lengths
FIXED_PAYLOAD_LEN = {
    ("COMMAND", 0x00): 4, ("COMMAND_ACK", 0x00): 6, ("POLL", 0x00): 1,
    ("ERROR", 0x00): 4, ("STATUS", 0x10): 78, ("STATUS", 0xFE): 78,
    ("STATUS", 0xF0): 20, ("EVENT", 0x11): 16,
}
# §19 - single-frame sizes, re-declared here as a second reading of the table
EXPECTED_FRAME_LEN = {
    ("COMMAND", 0x00): 30, ("COMMAND_ACK", 0x00): 24, ("POLL", 0x00): 19,
    ("STATUS", 0x10): 96, ("STATUS", 0xFE): 96, ("STATUS", 0xF0): 38,
    ("EVENT", 0x11): 34, ("ERROR", 0x00): 22,
}
FRAGMENTABLE = {"CONFIG", "CONFIG_ACK", "PING", "STATUS", "EVENT"}         # §11.4

# ---------------------------------------------------------------------------
# primitives - table-driven CRC, so it is not the generator's loop rewritten
# ---------------------------------------------------------------------------
def _crc_table():
    table = []
    for byte in range(256):
        value = byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) if (value & 0x8000) else (value << 1)
            value &= 0xFFFF
        table.append(value)
    return table


_CRC_TABLE = _crc_table()


def crc16(data: bytes) -> int:
    """§2.1 - CRC-16/CCITT-FALSE."""
    crc = 0xFFFF
    for byte in data:
        crc = ((crc << 8) & 0xFFFF) ^ _CRC_TABLE[((crc >> 8) ^ byte) & 0xFF]
    return crc


def hkdf(ikm: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    out = bytearray()
    prev = b""
    for counter in range(1, (length + 31) // 32 + 1):
        prev = hmac.new(prk, prev + info + struct.pack("B", counter), hashlib.sha256).digest()
        out += prev
    return bytes(out[:length])


MASTER = json.loads((HERE / "test_master_key.json").read_text(encoding="utf-8"))
MASTER_KEY = bytes.fromhex(MASTER["master_key"])


def derive(node_id: int) -> bytes:
    # §9.1 - salt "lran-v1", info "node-" || raw address byte, 32-byte output
    return hkdf(MASTER_KEY, b"lran-v1", b"node-" + struct.pack("B", node_id), 32)


def key_from_label(label):
    if label is None:
        return None
    assert label.startswith("node:"), label
    return derive(int(label.split(":")[1], 16))


# ---------------------------------------------------------------------------
# framing
# ---------------------------------------------------------------------------
def pack_header(ver, type_id, src, dst, seq, ctx_id, frag, schema, hdr_flags, rsv=b"\0\0\0"):
    # §5, §4.1 - little-endian, in the order the layout diagram gives
    return struct.pack("<BBBBHIBBB", ver, type_id, src, dst, seq, ctx_id, frag, schema, hdr_flags) + rsv


def unpack_header(frame: bytes):
    ver, type_id, src, dst, seq, ctx_id, frag, schema, hdr_flags = struct.unpack_from("<BBBBHIBBB", frame, 0)
    return dict(ver=ver, type_id=type_id, src=src, dst=dst, seq=seq, ctx_id=ctx_id,
                frag=frag, schema=schema, hdr_flags=hdr_flags)


def assemble(header: bytes, payload: bytes, key) -> bytes:
    body = header + payload
    if key is not None:
        body += hmac.new(key, body, hashlib.sha256).digest()[:LRAN_MAC_LEN]   # §9.3
    return body + struct.pack("<H", crc16(body))                              # §2.1


def peer_of(src: int, dst: int) -> int:
    """§9.1 - the key follows the non-bridge peer: dst outbound, src inbound."""
    return dst if src == 0x00 else src


# ---------------------------------------------------------------------------
# §14 - the receive ladder, implemented once, here
# ---------------------------------------------------------------------------
class Reject(Exception):
    def __init__(self, status, counter, stage):
        super().__init__("%s/%s/stage %s" % (status, counter, stage))
        self.result = (status, counter, stage)


def _parse_config(payload, ack: bool):
    """§7.4 - walk the entries; a set that does not consume the payload exactly
    is a length fault."""
    need = 3 if ack else 2
    if len(payload) < need:
        return False
    count = payload[2] if ack else payload[1]
    off = need
    for _ in range(count):
        head = 5 if ack else 4
        if off + head > len(payload):
            return False
        ln = payload[off + head - 1]
        off += head + ln
    return off == len(payload)


def receive(frame: bytes, self_id: int, expect_ctx_id: int):
    """Returns ("Ok", payload, mac_present) or raises Reject."""
    if len(frame) < LRAN_MIN_FRAME:
        raise Reject("Runt", "rx_runt", "2")                                  # stage 2
    if len(frame) > LRAN_MAX_FRAME:
        raise Reject("Oversize", "rx_oversize", "2a")                         # stage 2a
    if crc16(frame[:-LRAN_CRC_LEN]) != struct.unpack("<H", frame[-LRAN_CRC_LEN:])[0]:
        raise Reject("BadCrc", "rx_bad_crc", "3")                             # stage 3
    h = unpack_header(frame)
    if h["ver"] != WIRE_VER:
        raise Reject("BadVersion", "rx_bad_ver", "4")                         # stage 4
    if h["dst"] != self_id and h["dst"] != 0xFF:
        raise Reject("NotAddressed", "rx_not_addressed", "5")                 # stage 5
    if h["hdr_flags"] & 0x80:
        raise Reject("UnknownHdrExt", "rx_unknown_hdr_ext", "5a")             # stage 5a
    total = h["frag"] & 0x0F                                                  # §5.6
    index = (h["frag"] >> 4) & 0x0F
    # §14 stage 5b reads "total >= 1, index < total", but §11.2 gives index >= total
    # to ERROR(FRAGMENT_OVERFLOW), which is stage 10. Adjudicated for §11.2: stage 5b
    # is a total of 0 and nothing else.
    if total == 0:
        raise Reject("BadFrag", "rx_bad_frag", "5b")                          # stage 5b
    if h["type_id"] not in TYPE_NAME:
        raise Reject("UnknownType", "rx_unknown_type", "6")                   # stage 6
    tname = TYPE_NAME[h["type_id"]]
    schema = h["schema"]
    if tname in SCHEMALESS_TYPES:
        pass                       # §5.7 - schema is written 0 and ignored here
    elif schema not in SCHEMA_CARRIERS or tname not in SCHEMA_CARRIERS[schema]:
        raise Reject("UnknownSchema", "rx_unknown_schema", "7")               # stage 7

    body = frame[LRAN_HDR_LEN:-LRAN_CRC_LEN]

    # Where the payload ends and the MAC begins (§9.2, §7.6).
    if tname in ("COMMAND", "CONFIG"):
        mac_present = True
    elif tname == "HEX_REQ":
        # §7.6 - only the command nibble decides; n locates the boundary.
        mac_present = len(body) >= 2 and len(body) == 2 + body[1] + LRAN_MAC_LEN
    else:
        mac_present = False
    if mac_present:
        if len(body) < LRAN_MAC_LEN:
            raise Reject("BadLength", "rx_bad_length", "8")
        payload, mac = body[:-LRAN_MAC_LEN], body[-LRAN_MAC_LEN:]
    else:
        payload, mac = body, b""

    if total == 1:                 # §14 stage 8 - unfragmented frames only
        ok = True
        key = (tname, schema if tname not in SCHEMALESS_TYPES else 0x00)
        if key in FIXED_PAYLOAD_LEN:
            ok = len(payload) == FIXED_PAYLOAD_LEN[key]
        elif tname == "PING":                                                 # §6.6
            ok = len(payload) >= 2 and payload[1] == len(payload) - 2 and payload[1] <= LRAN_PING_MAX_ECHO
        elif tname in ("HEX_REQ", "HEX_RSP"):                                 # §7.6
            ok = len(payload) >= 2 and payload[1] == len(payload) - 2
        elif tname == "CONFIG":
            ok = _parse_config(payload, ack=False)
        elif tname == "CONFIG_ACK":
            ok = _parse_config(payload, ack=True)
        if not ok:
            raise Reject("BadLength", "rx_bad_length", "8")
        cap = LRAN_MAX_PAYLOAD_PLAIN if tname == "PING" else LRAN_MAX_SCHEMA_PAYLOAD
        if schema != 0x00 and len(payload) > cap:
            raise Reject("BadLength", "rx_bad_length", "8")
    else:                          # §14 stage 8a - fragmentable types only
        if tname not in FRAGMENTABLE:
            raise Reject("NotFragmentable", "rx_not_fragmentable", "8a")

    if mac_present or tname in ("COMMAND", "CONFIG"):                         # stage 9
        # §9.4 step 2 - ctx check; expect_ctx_id 0 means SKIP, never "expect zero"
        if expect_ctx_id != 0 and h["ctx_id"] != expect_ctx_id:
            raise Reject("CtxMismatch", "rx_ctx_mismatch", "9")
        # §9.4 step 3 - MAC over this frame's own header and its own payload
        key = derive(peer_of(h["src"], h["dst"]))
        want = hmac.new(key, frame[:LRAN_HDR_LEN] + payload, hashlib.sha256).digest()[:LRAN_MAC_LEN]
        if not hmac.compare_digest(want, mac):
            raise Reject("BadMac", "rx_bad_mac", "9")

    if index >= total:             # §11.2, §14 stage 10 - ERROR(FRAGMENT_OVERFLOW)
        # §11.3 spells the sibling reassembly counters with the rx_ prefix, and this
        # one is a discard, so it is summed into rx_dropped (§14, schema 0xF0).
        raise Reject("FragmentOverflow", "rx_fragment_overflow", "10")
    return "Ok", payload, mac_present


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------
FAILURES: list[str] = []
COUNT = 0


def fail(where: str, msg: str) -> None:
    FAILURES.append("%s: %s" % (where, msg))


def expect(cond: bool, where: str, msg: str) -> bool:
    if not cond:
        fail(where, msg)
    return bool(cond)


def load(filename: str, group: str) -> list:
    doc = json.loads((HERE / filename).read_text(encoding="utf-8"))
    where = filename
    expect(doc.get("format") == FORMAT, where, "format is %r" % doc.get("format"))
    expect(doc.get("spec") == SPEC, where, "spec is %r" % doc.get("spec"))
    expect(doc.get("wire_ver") == WIRE_VER, where, "wire_ver is %r" % doc.get("wire_ver"))
    expect(doc.get("group") == group, where, "group is %r" % doc.get("group"))
    expect(doc.get("generated_by") == "tools/vectors/generate.py", where, "generated_by is wrong")
    names = [v["name"] for v in doc["vectors"]]
    expect(len(names) == len(set(names)), where, "duplicate vector names")
    return doc["vectors"]


def check_kdf():
    global COUNT
    for v in load("vectors_kdf.json", "kdf"):
        w = "kdf/" + v["name"]
        COUNT += 1
        node_id = int(v["node_id"], 16)
        salt = bytes.fromhex(v["salt"])
        info = bytes.fromhex(v["info"])
        master = bytes.fromhex(v["master_key"])
        expect(salt == b"lran-v1", w, "salt is not the §9.1 domain separator")
        expect(info == b"node-" + bytes([node_id]), w,
               "info is %r, §9.1 requires 'node-' || raw byte" % info)
        expect(len(info) == 6, w, "info is %d bytes, §9.1 says six" % len(info))
        expect(master == MASTER_KEY, w, "master_key differs from test_master_key.json")
        got = hkdf(master, salt, info, 32).hex()
        expect(got == v["node_key"], w, "node_key mismatch\n  stored  %s\n  derived %s"
               % (v["node_key"], got))


def check_single():
    global COUNT
    for v in load("vectors_single.json", "single"):
        w = "single/" + v["name"]
        COUNT += 1
        h = v["header"]
        expect(h["ver"] == WIRE_VER, w, "ver is %r" % h["ver"])
        tname = h["type"]
        if not expect(tname in TYPE_VALUE, w, "unknown type name %r" % tname):
            continue
        frag = int(h["frag"], 16)
        schema = int(h["schema"], 16)
        hdr_flags = int(h["hdr_flags"], 16)
        payload = bytes.fromhex(v["payload"])
        key = key_from_label(v["key"])
        header = pack_header(h["ver"], TYPE_VALUE[tname], h["src"], h["dst"], h["seq"],
                             h["ctx_id"], frag, schema, hdr_flags)
        built = assemble(header, payload, key)
        expect(built.hex() == v["frame"], w, "frame mismatch\n  stored  %s\n  derived %s"
               % (v["frame"], built.hex()))
        expect(v["frame_len"] == len(built), w, "frame_len says %d, frame is %d bytes"
               % (v["frame_len"], len(built)))
        expect(len(built) <= LRAN_MAX_FRAME, w, "frame exceeds LRAN_MAX_FRAME")
        # §19 length table, for the fixed-length types
        sizes_key = (tname, schema if tname not in SCHEMALESS_TYPES else 0x00)
        if sizes_key in EXPECTED_FRAME_LEN and key is None:
            expect(len(built) == EXPECTED_FRAME_LEN[sizes_key], w,
                   "§19 says %d B, frame is %d B" % (EXPECTED_FRAME_LEN[sizes_key], len(built)))
        # §5.8 - a frame with reserved hdr_flags bits or non-zero reserved bytes
        # is one no conforming encoder emits, and must be marked decode_only
        # (README). The converse holds too: decode_only must not hide a frame the
        # generator simply got wrong.
        sender_rule_broken = bool(hdr_flags & 0x7F) or built[13:16] != b"\x00\x00\x00"
        expect(bool(v.get("decode_only", False)) == sender_rule_broken, w,
               "decode_only is %r but the frame %s a §5.8/§5.9 sender rule"
               % (v.get("decode_only", False), "breaks" if sender_rule_broken else "obeys"))
        d = v["decode"]
        expect(d["mac_present"] == (key is not None), w, "mac_present disagrees with key")
        try:
            status, got_payload, mac_present = receive(built, d["self"], d["expect_ctx_id"])
        except Reject as r:
            fail(w, "receive ladder rejected a positive vector: %s" % r)
            continue
        expect(status == d["status"], w, "status %r, ladder says %r" % (d["status"], status))
        expect(got_payload.hex() == d["payload"], w, "decoded payload differs from decode.payload")
        expect(got_payload == payload, w, "decoded payload differs from the declared payload")
        expect(mac_present == d["mac_present"], w, "ladder disagrees about mac_present")


def check_frag():
    global COUNT
    for v in load("vectors_frag.json", "frag"):
        w = "frag/" + v["name"]
        COUNT += 1
        h = v["header"]
        tname = h["type"]
        schema = int(h["schema"], 16)
        hdr_flags = int(h["hdr_flags"], 16)
        payload = bytes.fromhex(v["payload"])
        chunk = v["frag_chunk"]
        key = key_from_label(v["key"])
        # §11.1 - fragmentation is a pure function of (payload, chunk)
        chunks = [payload[i:i + chunk] for i in range(0, len(payload), chunk)] or [b""]
        total = len(chunks)
        expect(total <= 15, w, "§11.1 caps a set at 15 fragments, this one has %d" % total)
        expect(total > 1, w, "a frag vector with one fragment witnesses nothing")
        expect(all(len(c) == chunk for c in chunks[:-1]), w,
               "§11.1: every fragment but the last must carry the same length")
        expect(0 < len(chunks[-1]) <= chunk or len(payload) == 0, w,
               "§11.1: the last fragment is the remainder, never longer, never empty")
        expect(v["fragment_lens"] == [len(c) for c in chunks], w,
               "fragment_lens %r, derived %r" % (v["fragment_lens"], [len(c) for c in chunks]))
        expect(len(v["frames"]) == total, w, "%d frames stored, %d derived" % (len(v["frames"]), total))
        cap = LRAN_MAX_PAYLOAD_PLAIN if tname == "PING" else LRAN_MAX_SCHEMA_PAYLOAD
        expect(len(payload) <= cap, w, "§11.2 reassembly cap is %d, payload is %d" % (cap, len(payload)))
        d = v["decode"]
        for index, chunk_bytes in enumerate(chunks):
            frag = ((index & 0x0F) << 4) | (total & 0x0F)                     # §5.6
            header = pack_header(h["ver"], TYPE_VALUE[tname], h["src"], h["dst"], h["seq"],
                                 h["ctx_id"], frag, schema, hdr_flags)
            built = assemble(header, chunk_bytes, key)
            expect(built.hex() == v["frames"][index], w,
                   "fragment %d mismatch\n  stored  %s\n  derived %s"
                   % (index, v["frames"][index], built.hex()))
            expect(len(built) <= LRAN_MAX_FRAME, w, "fragment %d exceeds LRAN_MAX_FRAME" % index)
            try:
                receive(built, d["self"], d["expect_ctx_id"])
            except Reject as r:
                fail(w, "fragment %d rejected by the ladder: %s" % (index, r))
        # §11.2 - reassembly is concatenation in ascending INDEX order, whatever
        # the arrival order; a repeated index overwrites.
        order = d["delivery_order"]
        expect(sorted(set(order)) == list(range(total)), w,
               "delivery_order %r does not cover 0..%d" % (order, total - 1))
        store = {}
        duplicates = 0
        late_duplicates = 0
        completed_at = None
        for position, i in enumerate(order):
            if i in store:
                duplicates += 1
                # §11.2 defines an overwrite only "within a LIVE set". A repeat
                # arriving after the set completed is not covered by v0.4 at all,
                # so a vector must not assert an outcome for it.
                if completed_at is not None:
                    late_duplicates += 1
            store[i] = chunks[i]
            if completed_at is None and len(store) == total:
                completed_at = position
        expect(late_duplicates == 0, w,
               "delivery_order repeats an index after the set completed; §11.2 covers "
               "duplicates within a live set only")
        reassembled = b"".join(store[i] for i in range(total))
        expect(reassembled.hex() == d["reassembled"], w, "reassembled bytes differ")
        expect(reassembled == payload, w, "reassembly does not reproduce the declared payload")
        if "counter" in d:
            expect(d["counter"] == "rx_frag_duplicate" and duplicates > 0, w,
                   "counter %r declared but delivery_order has %d duplicates" % (d["counter"], duplicates))
        else:
            expect(duplicates == 0, w, "delivery_order repeats an index but no counter is declared")


def check_negative():
    global COUNT
    for v in load("vectors_negative.json", "negative"):
        w = "negative/" + v["name"]
        COUNT += 1
        frame = bytes.fromhex(v["frame"])
        d = v["decode"]
        try:
            status, _payload, _mac = receive(frame, d["self"], d["expect_ctx_id"])
            fail(w, "the ladder ACCEPTED a negative vector (status %s); expected %s at stage %s"
                 % (status, d["status"], d["stage"]))
            continue
        except Reject as r:
            status, counter, stage = r.result
        expect(status == d["status"], w, "status %r, ladder says %r" % (d["status"], status))
        expect(counter == d["counter"], w, "counter %r, ladder says %r" % (d["counter"], counter))
        expect(stage == d["stage"], w, "stage %r, ladder says %r" % (d["stage"], stage))


def main() -> int:
    check_kdf()          # §9.1 first - everything downstream depends on it
    check_single()
    check_frag()
    check_negative()
    if FAILURES:
        print("FAIL - %d problem(s) in %d vectors:\n" % (len(FAILURES), COUNT))
        for f in FAILURES:
            print("  " + f)
        return 1
    print("OK - %d vectors re-derived and matched" % COUNT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
