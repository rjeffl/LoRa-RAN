#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""LRAN protocol test-vector generator - open item W4, milestone P6.

Derived from the prose of `LRAN-Protocol-Specification` and from nothing else:
v0.6 for the original set, and the section each later vector cites. The exception is
where a vector is marked `"origin": "adjudicated"` - see below. This file deliberately shares no code with /lib/lran-protocol/: the CRC-16,
the header serializer, the payload builders and the fragmenter are all written
here from the specification text. Only `hashlib` and `hmac` are borrowed, and
those are independent implementations of published primitives (§9.1, README).

Running it rewrites the four vector files in place:

    python3 tools/vectors/generate.py

Every constant, offset and rule below cites the section that fixes it. Where the
specification is ambiguous the reading taken is marked `AMBIGUITY:` in a comment
so the choice is visible at the point it is made.

**Provenance.** Every vector carries `origin`:

  "derived"     - worked out from the specification prose alone. This is the only
                  kind of vector that independently witnesses anything.
  "adjudicated" - encodes a resolution that reached this generator from outside the
                  prose: from the C++ codec's behaviour, or from a ruling on a v0.4
                  contradiction. v0.5 states these rules now, so a reader starting
                  today would derive them - but this generator did not, and
                  agreement on them is therefore much weaker evidence than
                  agreement on a derived vector. The field is bookkeeping about
                  what a vector witnesses, not about whether it is correct.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent

FORMAT = "lran-test-vectors/1"          # README, common envelope
SPEC = "LRAN-Protocol-Specification v0.14"
WIRE_VER = 2                            # §5.1 - `ver` = 2, unchanged since v0.3

DERIVED = "derived"
ADJUDICATED = "adjudicated"

# ---------------------------------------------------------------------------
# §3.1 - derived size constants. LRAN_MAX_FRAME is the only hand-chosen number.
# ---------------------------------------------------------------------------
LRAN_MAX_FRAME = 222
LRAN_HDR_LEN = 16                                                   # §5
LRAN_CRC_LEN = 2                                                    # §2.1
LRAN_MAC_LEN = 8                                                    # §9.3
LRAN_MAX_PAYLOAD_AUTH = LRAN_MAX_FRAME - LRAN_HDR_LEN - LRAN_MAC_LEN - LRAN_CRC_LEN   # 196
LRAN_MAX_PAYLOAD_PLAIN = LRAN_MAX_FRAME - LRAN_HDR_LEN - LRAN_CRC_LEN                 # 204
LRAN_MAX_SCHEMA_PAYLOAD = LRAN_MAX_PAYLOAD_AUTH                                       # 196
LRAN_PING_MAX_ECHO = LRAN_MAX_PAYLOAD_PLAIN - 2                                       # 202, §6.6.1

# §4.6 - sentinels, never zero, for "unavailable"
INT16_MIN = -32768
UINT16_MAX = 0xFFFF
UINT32_MAX = 0xFFFFFFFF

# ---------------------------------------------------------------------------
# §6 - message types. The name-to-value mapping is part of what is witnessed,
# so the vectors carry the name and this table is the only place it is resolved.
# ---------------------------------------------------------------------------
MSG_TYPE = {
    "COMMAND": 0x01,
    "COMMAND_ACK": 0x02,
    "POLL": 0x03,
    "STATUS": 0x04,
    "EVENT": 0x05,
    "ERROR": 0x06,
    "PING": 0x07,
    "HEX_REQ": 0x08,
    "HEX_RSP": 0x09,
    "CONFIG": 0x0A,
    "CONFIG_ACK": 0x0B,
}

# §5.3 - node IDs
NODE_BRIDGE = 0x00
NODE_GATELINK = 0x01
NODE_WELLLINK = 0x02
NODE_SIMNODE0 = 0xF0
NODE_SIMNODE1 = 0xF1
NODE_SIMNODE2 = 0xF2
NODE_SIMNODE3 = 0xF3
NODE_BROADCAST = 0xFF

# §10.1 - each node's boot context. Fixed here so vectors are reproducible.
CTX = {
    NODE_GATELINK: 0x89ABCDEF,
    NODE_WELLLINK: 0x1234ABCD,
    NODE_SIMNODE0: 0x0BADC0DE,
}

# ---------------------------------------------------------------------------
# §9.1 - key derivation
# ---------------------------------------------------------------------------
KDF_SALT = b"lran-v1"        # §9.1 - domain separator, stable across `ver` bumps
KDF_INFO_PREFIX = b"node-"   # §9.1 - five ASCII bytes, then the RAW address byte
# §9.1 - "L = 32", stated outright in v0.5. v0.4 never gave the output length and
# this generator inferred it from the key width HMAC-SHA256 consumes; it is no
# longer an inference. One SHA-256 block, so a single HKDF expand iteration.
KDF_LEN = 32


def hkdf_sha256(ikm: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    """RFC 5869 HKDF-SHA256, extract-then-expand, written out explicitly."""
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    okm = b""
    block = b""
    counter = 1
    while len(okm) < length:
        block = hmac.new(prk, block + info + bytes([counter]), hashlib.sha256).digest()
        okm += block
        counter += 1
    return okm[:length]


def kdf_info(node_id: int) -> bytes:
    # §9.1 - "node-" || <raw address byte>, six bytes. NOT "node-1"/"node-01".
    return KDF_INFO_PREFIX + bytes([node_id])


def load_master_key() -> bytes:
    doc = json.loads((HERE / "test_master_key.json").read_text(encoding="utf-8"))
    key = bytes.fromhex(doc["master_key"])
    assert len(key) == doc["master_key_len"] == 32
    # The fixture advertises itself as ASCII; check that, it is a cheap guard
    # against a hand-edit of the fixture.
    assert key == doc["master_key_ascii"].encode("ascii")
    return key


MASTER_KEY = load_master_key()
_NODE_KEY_CACHE: dict[int, bytes] = {}


def node_key(node_id: int) -> bytes:
    """§9.1 - the key of the NON-BRIDGE peer, whichever way the frame travels."""
    if node_id not in _NODE_KEY_CACHE:
        _NODE_KEY_CACHE[node_id] = hkdf_sha256(MASTER_KEY, KDF_SALT, kdf_info(node_id), KDF_LEN)
    return _NODE_KEY_CACHE[node_id]


def key_label(node_id: int | None) -> str | None:
    # README - `key` identifies the derived key, e.g. "node:0x01"; null = no MAC.
    return None if node_id is None else "node:0x%02x" % node_id


# ---------------------------------------------------------------------------
# §2.1 - CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final
# XOR. Bitwise on purpose - a table would be one more thing to get wrong, and
# check.py implements it the other way round as a second witness.
# ---------------------------------------------------------------------------
def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# §4.1/§4.2 - little-endian, explicit byte-wise serialization. Never a struct.
# ---------------------------------------------------------------------------
def u8(value: int) -> bytes:
    assert 0 <= value <= 0xFF, value
    return bytes([value])


def i8(value: int) -> bytes:
    assert -128 <= value <= 127, value                       # §4.5 two's complement
    return bytes([value & 0xFF])


def u16(value: int) -> bytes:
    assert 0 <= value <= 0xFFFF, value
    return bytes([value & 0xFF, (value >> 8) & 0xFF])


def i16(value: int) -> bytes:
    assert -32768 <= value <= 32767, value
    return u16(value & 0xFFFF)


def u32(value: int) -> bytes:
    assert 0 <= value <= 0xFFFFFFFF, value
    return bytes([value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, (value >> 24) & 0xFF])


class Payload:
    """A payload built at explicit offsets, so every §7 table row is checkable.

    `w.u16(10, batt_mv)` asserts the field really lands at offset 10.
    """

    def __init__(self) -> None:
        self.buf = bytearray()

    def _at(self, off: int) -> None:
        assert len(self.buf) == off, "field expected at offset %d, cursor at %d" % (off, len(self.buf))

    def u8(self, off, v):
        self._at(off); self.buf += u8(v); return self

    def i8(self, off, v):
        self._at(off); self.buf += i8(v); return self

    def u16(self, off, v):
        self._at(off); self.buf += u16(v); return self

    def i16(self, off, v):
        self._at(off); self.buf += i16(v); return self

    def u32(self, off, v):
        self._at(off); self.buf += u32(v); return self

    def raw(self, off, b):
        self._at(off); self.buf += bytes(b); return self

    def done(self, expected_len: int) -> bytes:
        assert len(self.buf) == expected_len, "payload is %d bytes, §7 says %d" % (len(self.buf), expected_len)
        return bytes(self.buf)


# ---------------------------------------------------------------------------
# §5 - the 16-byte header, field by field
#   [ver][type][src][dst][seq:2][ctx_id:4][frag][schema][hdr_flags][rsv:3]
# ---------------------------------------------------------------------------
def serialize_header(*, ver, type_id, src, dst, seq, ctx_id, frag, schema, hdr_flags,
                     rsv=b"\x00\x00\x00") -> bytes:
    hdr = b""
    hdr += u8(ver)          # §5.1 byte 0
    hdr += u8(type_id)      # §5.2 byte 1
    hdr += u8(src)          # §5.3 byte 2
    hdr += u8(dst)          # §5.3 byte 3
    hdr += u16(seq)         # §5.4 bytes 4-5
    hdr += u32(ctx_id)      # §5.5 bytes 6-9
    hdr += u8(frag)         # §5.6 byte 10
    hdr += u8(schema)       # §5.7 byte 11
    hdr += u8(hdr_flags)    # §5.8 byte 12
    assert len(rsv) == 3
    hdr += bytes(rsv)       # §5.9 bytes 13-15, written zero (§4.3)
    assert len(hdr) == LRAN_HDR_LEN
    return hdr


def frag_byte(index: int, total: int) -> int:
    # §5.6 - high nibble = index (0-based), low nibble = total (1-based).
    assert 1 <= total <= 15, total
    assert 0 <= index < total, (index, total)
    return ((index & 0x0F) << 4) | (total & 0x0F)


def compute_mac(header: bytes, payload: bytes, key: bytes) -> bytes:
    # §9.3 - HMAC-SHA256 over the entire 16-byte header || the entire payload,
    # truncated to the first 8 bytes. The CRC16 is NOT covered.
    assert len(header) == LRAN_HDR_LEN
    return hmac.new(key, header + payload, hashlib.sha256).digest()[:LRAN_MAC_LEN]


def build_frame(*, ver=WIRE_VER, type_id, src, dst, seq, ctx_id, frag, schema=0x00,
                hdr_flags=0x00, payload=b"", mac_node=None, rsv=b"\x00\x00\x00") -> bytes:
    """§3 - header || payload || [mac] || crc16(little-endian)."""
    header = serialize_header(ver=ver, type_id=type_id, src=src, dst=dst, seq=seq,
                              ctx_id=ctx_id, frag=frag, schema=schema,
                              hdr_flags=hdr_flags, rsv=rsv)
    body = header + payload
    if mac_node is not None:
        body += compute_mac(header, payload, node_key(mac_node))
    return body + u16(crc16_ccitt_false(body))   # §2.1 appended little-endian


def reseal(frame: bytes) -> bytes:
    """Recompute the trailing CRC16 over a frame whose body was edited (README:
    a negative vector breaks exactly one thing, and repairs the CRC unless the
    CRC is what is under test)."""
    body = frame[:-LRAN_CRC_LEN]
    return body + u16(crc16_ccitt_false(body))


# ---------------------------------------------------------------------------
# §11.1 - fragmentation is a pure function of (payload, chunk).
# ---------------------------------------------------------------------------
def split_payload(payload: bytes, chunk: int) -> list[bytes]:
    assert chunk >= 1
    if len(payload) == 0:
        return [b""]                                   # the empty payload is one fragment
    frags = [payload[i:i + chunk] for i in range(0, len(payload), chunk)]
    # §11.1 - every fragment but the highest index carries the same length; the
    # last is the remainder, never longer, never empty unless the whole payload is.
    for f in frags[:-1]:
        assert len(f) == chunk
    assert 0 < len(frags[-1]) <= chunk
    assert len(frags) <= 15, "§11.1 caps a set at 15 fragments, got %d" % len(frags)
    return frags


# ---------------------------------------------------------------------------
# §7 payload builders
# ---------------------------------------------------------------------------
def p_command(cmd, arg=0, arg2=0) -> bytes:                     # §6.2, 4 bytes
    return Payload().u8(0, cmd).u8(1, arg).u16(2, arg2).done(4)


def p_command_ack(ack_seq, result, detail=0) -> bytes:          # §6.3, 6 bytes
    return Payload().u16(0, ack_seq).u8(2, result).u8(3, detail).u16(4, 0).done(6)


def p_poll(poll_flags) -> bytes:                                # §6.4, 1 byte
    return Payload().u8(0, poll_flags).done(1)


def p_error(err_code, detail, ref_seq) -> bytes:                # §6.5, 4 bytes
    return Payload().u8(0, err_code).u8(1, detail).u16(2, ref_seq).done(4)


def p_ping(ping_flags, data: bytes) -> bytes:                   # §6.6, 2 + N
    n = len(data)
    assert n <= LRAN_PING_MAX_ECHO, "§6.6.1 caps the echo at %d" % LRAN_PING_MAX_ECHO
    return Payload().u8(0, ping_flags).u8(1, n).raw(2, data).done(2 + n)


def pattern_fill(seq: int, n: int) -> bytes:
    # §6.6.3 - data[i] = (uint8)((seq & 0xFF) + i)
    return bytes(((seq & 0xFF) + i) & 0xFF for i in range(n))


def p_hex_req(flags, hex_string: bytes) -> bytes:               # §7.6, 2 + N
    n = len(hex_string)
    return Payload().u8(0, flags).u8(1, n).raw(2, hex_string).done(2 + n)


def p_hex_rsp(status, hex_string: bytes) -> bytes:              # §7.6, 2 + N
    n = len(hex_string)
    return Payload().u8(0, status).u8(1, n).raw(2, hex_string).done(2 + n)


def p_status_0x10(**f) -> bytes:
    """§7.2 - GateLink status v1, 78 bytes. Offsets are asserted as they are written."""
    w = Payload()
    # §7.2.1 gate block, offsets 0-9
    w.u8(0, f["gate_state"])
    w.u8(1, f["input_bits"])
    w.u8(2, f["hold"])
    w.u8(3, f["movement_cause"])
    w.u8(4, f["last_direction"])
    w.u8(5, f["detect_flags"])
    w.u32(6, f["last_traversal_age_s"])       # §7.2.9 uint32; UINT32_MAX = none
    # §7.2.2 MPPT block, offsets 10-35
    w.u16(10, f["batt_mv"])
    w.i16(12, f["batt_ma"])
    w.u16(14, f["pv_cv"])                     # 10 mV units
    w.u16(16, f["pv_w"])
    w.i16(18, f["load_ma"])                   # INT16_MIN = not available
    w.u16(20, f["yield_today"])
    w.u16(22, f["yield_yest"])
    w.u16(24, f["pmax_today"])
    w.u32(26, f["yield_total"])
    w.u8(30, f["charge_state"])
    w.u8(31, f["mppt_err"])
    w.u8(32, f["mppt_tracker"])
    w.u8(33, f["mppt_flags"])                 # §7.2.6
    w.i16(34, f["mppt_temp_c10"])             # INT16_MIN = not available
    # §7.2.3 BMS block, offsets 36-63
    w.u8(36, f["bms_soc"])                    # 0xFF = not available
    w.u8(37, f["bms_flags"])                  # §7.2.7
    w.u16(38, f["pack_mv"])
    w.i16(40, f["pack_ma"])                   # sign convention pending, W6
    w.u8(42, f["cell_count"])
    w.u8(43, f["bms_rssi_neg"])               # 0 = no link
    for i in range(4):
        w.u16(44 + 2 * i, f["cell_mv"][i])
    for i in range(4):
        w.i8(52 + i, f["cell_temp_c"][i])
    w.u16(56, f["bms_cycles"])
    w.u16(58, f["bms_capacity_dah"])
    w.u16(60, f["bms_alarms"])
    w.u16(62, f["bms_age_s"])                 # saturates at UINT16_MAX
    # §7.2.4 node block, offsets 64-77
    w.u32(64, f["uptime_s"])
    w.u16(68, f["boot_count"])
    w.u16(70, f["node_mv"])
    w.i16(72, f["node_ma"])
    w.i16(74, f["enclosure_temp_c10"])
    w.u8(76, f["node_flags"])                 # §7.2.8
    w.u8(77, f["status_reason"])              # §8.7
    return w.done(78)


def p_event_0x11(**f) -> bytes:
    """§7.3 - GateLink event v1, 16 bytes."""
    w = Payload()
    w.u8(0, f["event_type"])                  # §8.9
    w.u8(1, f["event_flags"])
    w.u8(2, f["hold_source"])                 # §8.4
    w.u8(3, f["direction"])                   # §8.6
    w.u8(4, f["gate_state"])                  # §8.3
    w.u8(5, f["input_bits"])
    w.u16(6, f["detail"])
    w.u32(8, f["event_id"])
    w.u32(12, f["uptime_s"])
    return w.done(16)


def p_status_0xf0(**f) -> bytes:
    """§7.5 - generic node health, 20 bytes, carried as a STATUS."""
    w = Payload()
    w.u32(0, f["uptime_s"])
    w.u16(4, f["boot_count"])
    w.u16(6, f["rx_frames"])
    w.u16(8, f["tx_frames"])
    w.u16(10, f["rx_dropped"])
    w.u16(12, f["cad_backoffs"])
    w.i16(14, f["last_rssi_dbm"])
    w.i16(16, f["last_snr_db10"])
    w.u8(18, f["proto_ver"])
    w.u8(19, f["health_flags"])
    return w.done(20)


# §7.4 - CONFIG / CONFIG_ACK, schema 0x12
PTYPE_U8, PTYPE_U16, PTYPE_U32, PTYPE_I16, PTYPE_I32, PTYPE_BOOL = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
_PTYPE_LEN = {PTYPE_U8: 1, PTYPE_U16: 2, PTYPE_U32: 4, PTYPE_I16: 2, PTYPE_I32: 4, PTYPE_BOOL: 1}


def _encode_value(ptype: int, value: int) -> bytes:
    # §7.4 - value, little-endian, `len` bytes
    if ptype in (PTYPE_U8, PTYPE_BOOL):
        return u8(value)
    if ptype == PTYPE_U16:
        return u16(value)
    if ptype == PTYPE_U32:
        return u32(value)
    if ptype == PTYPE_I16:
        return i16(value)
    if ptype == PTYPE_I32:
        return u32(value & 0xFFFFFFFF)
    raise AssertionError("unknown ptype 0x%02x" % ptype)


def p_config(op: int, entries) -> bytes:
    """§7.4 - [op][count][ entries: uint16 param_id, uint8 ptype, uint8 len, value ]"""
    out = u8(op) + u8(len(entries))
    for param_id, ptype, value in entries:
        val = _encode_value(ptype, value)
        assert len(val) == _PTYPE_LEN[ptype]
        out += u16(param_id) + u8(ptype) + u8(len(val)) + val
    assert len(out) <= LRAN_MAX_SCHEMA_PAYLOAD, "§3.1 caps a schema payload at %d" % LRAN_MAX_SCHEMA_PAYLOAD
    return out


def p_config_ack(op: int, persist_status: int, results, more_follows: bool = False) -> bytes:
    """§7.4 - [op][persist_status][count][ uint16 param_id, uint8 status, uint8 ptype,
    uint8 len, uint8[len] EFFECTIVE value ]

    §7.4.1 - bit 7 of count is MORE_FOLLOWS: another CONFIG_ACK of this answer follows.
    Bits 6:0 are the result count, which §7.4.1 bounds at 32 because 193 bytes remain for
    results once op, persist_status and count are taken."""
    assert len(results) < 0x80, "§7.4.1 - count bit 7 is MORE_FOLLOWS, not a count bit"
    out = u8(op) + u8(persist_status) + u8(len(results) | (0x80 if more_follows else 0))
    for param_id, status, ptype, value in results:
        val = _encode_value(ptype, value)
        out += u16(param_id) + u8(status) + u8(ptype) + u8(len(val)) + val
    assert len(out) <= LRAN_MAX_SCHEMA_PAYLOAD
    return out


# ---------------------------------------------------------------------------
# §19 - single-frame length table, asserted on every fixed-length vector.
# ---------------------------------------------------------------------------
EXPECTED_FRAME_LEN = {
    ("COMMAND", 0x00): 30,
    ("COMMAND_ACK", 0x00): 24,
    ("POLL", 0x00): 19,
    ("STATUS", 0x10): 96,
    ("STATUS", 0xFE): 96,      # §7.1 - mirrors 0x10
    ("STATUS", 0xF0): 38,
    ("EVENT", 0x11): 34,
    ("ERROR", 0x00): 22,
}

# §14.1 - the counter registry, normative as of v0.5: every counter, the stage that
# raises it, and whether it sums into `rx_dropped`. A vector may name nothing else.
COUNTERS = {
    "rx_crc_err": ("1", True),
    "rx_runt": ("2", True),
    "rx_oversize": ("2a", True),
    "rx_bad_crc": ("3", True),
    "rx_bad_ver": ("4", True),
    "rx_not_addressed": ("5", True),
    "rx_unknown_hdr_ext": ("5a", True),
    "rx_bad_frag": ("5b", True),
    "rx_unknown_type": ("6", True),
    "rx_unknown_schema": ("7", True),
    "rx_bad_length": ("8", True),
    "rx_not_fragmentable": ("8a", True),
    "rx_rejected_ctx": ("9", True),
    "rx_rejected_mac": ("9", True),
    "rx_unknown_src": ("9a", True),
    "rx_reassembly_timeout": ("10", True),
    "rx_fragment_overflow": ("10", True),
    "rx_reassembly_abandoned": ("10", True),
    "rx_rejected_seq": ("11", True),
    # Counted, NOT summed into rx_dropped (§14.1): each is normal traffic, and a
    # health metric that climbs during correct operation is worse than none.
    "rx_frag_duplicate": ("10", False),
    "rx_frag_late": ("10", False),
    "rx_dup_command": ("11", False),
}
# §14.1 names counters; it still names no `Status` value, so the status strings in
# these vectors remain a convention shared with the C++ consumer. A Status name and
# the wire ERROR code it maps to need not match: a `frag` total of 0 is counted
# rx_bad_frag but reports ERROR(BAD_LENGTH) (§5.6), and stage 8a is counted
# rx_not_fragmentable but likewise reports BAD_LENGTH (§11.4).

# ---------------------------------------------------------------------------
# vector assembly helpers
# ---------------------------------------------------------------------------
def header_json(*, type_name, src, dst, seq, ctx_id, frag, schema, hdr_flags, ver=WIRE_VER):
    # README - numeric fields decimal, bitfields as hex strings.
    return {
        "ver": ver,
        "type": type_name,
        "src": src,
        "dst": dst,
        "seq": seq,
        "ctx_id": ctx_id,
        "frag": "0x%02x" % frag,
        "schema": "0x%02x" % schema,
        "hdr_flags": "0x%02x" % hdr_flags,
    }


def single(name, spec_ref, *, type_name, src, dst, seq, ctx_id, payload,
           schema=0x00, frag=0x01, hdr_flags=0x00, ver=WIRE_VER, mac_node=None,
           self_id, expect_ctx_id=0, note=None, status="Ok", decode_only=False,
           origin=DERIVED):
    # §5.8 - bits 6:0 of hdr_flags are "write 0, ignore on receive". The sender
    # half and the receiver half are deliberately asymmetric, so a frame that
    # exercises the receiver rule is one no conforming encoder emits: it must be
    # marked `decode_only` (README) and cannot double as an encode vector.
    sender_rule_broken = bool(hdr_flags & 0x7F)
    assert sender_rule_broken == decode_only, (
        "%s: hdr_flags reserved bits and decode_only must agree (§5.8)" % name)
    frame = build_frame(ver=ver, type_id=MSG_TYPE[type_name], src=src, dst=dst, seq=seq,
                        ctx_id=ctx_id, frag=frag, schema=schema, hdr_flags=hdr_flags,
                        payload=payload, mac_node=mac_node)
    expected = EXPECTED_FRAME_LEN.get((type_name, schema))
    if expected is not None and mac_node is None:
        assert len(frame) == expected, "§19 says %s/0x%02x is %d B, built %d" % (
            type_name, schema, expected, len(frame))
    assert len(frame) <= LRAN_MAX_FRAME, "§3 caps a frame at %d, built %d" % (LRAN_MAX_FRAME, len(frame))
    assert origin in (DERIVED, ADJUDICATED)
    vec = {"name": name, "spec_ref": spec_ref, "origin": origin}
    if decode_only:
        vec["decode_only"] = True
    if note:
        vec["note"] = note
    vec["header"] = header_json(type_name=type_name, src=src, dst=dst, seq=seq, ctx_id=ctx_id,
                                frag=frag, schema=schema, hdr_flags=hdr_flags, ver=ver)
    vec["payload"] = payload.hex()
    vec["key"] = key_label(mac_node)
    vec["frame"] = frame.hex()
    vec["frame_len"] = len(frame)
    vec["decode"] = {
        "self": self_id,
        "expect_ctx_id": expect_ctx_id,
        "status": status,
        "payload": payload.hex(),
        "mac_present": mac_node is not None,
    }
    return vec


def frag_set(name, spec_ref, *, type_name, src, dst, seq, ctx_id, payload, frag_chunk,
             schema=0x00, hdr_flags=0x00, mac_node=None, self_id, expect_ctx_id=0,
             delivery_order=None, note=None, counter=None, status="Ok", origin=DERIVED,
             interpose=None):
    chunks = split_payload(payload, frag_chunk)
    total = len(chunks)
    frames = []
    for index, chunk in enumerate(chunks):
        frames.append(build_frame(type_id=MSG_TYPE[type_name], src=src, dst=dst, seq=seq,
                                  ctx_id=ctx_id, frag=frag_byte(index, total), schema=schema,
                                  hdr_flags=hdr_flags, payload=chunk, mac_node=mac_node))
    for f in frames:
        assert len(f) <= LRAN_MAX_FRAME
    if delivery_order is None:
        delivery_order = list(range(total))
    assert all(0 <= i < total for i in delivery_order)
    assert set(delivery_order) == set(range(total)), "every index must arrive at least once"
    cap = LRAN_MAX_PAYLOAD_PLAIN if type_name == "PING" else LRAN_MAX_SCHEMA_PAYLOAD  # §11.2
    assert len(payload) <= cap, "§11.2 reassembly cap is %d, payload is %d" % (cap, len(payload))
    # §11.2 - reassembly is concatenation in ascending index order, whatever the
    # arrival order was. Rebuild it that way rather than trusting `payload`.
    # A repeat before the set completes is an overwrite (rx_frag_duplicate); one
    # after it completes matches the retained key of the last completed set and is
    # discarded (rx_frag_late). The two are different rules with different counters,
    # so a vector must exercise one or the other, never both.
    store: dict[int, bytes] = {}
    completed_at = None
    dup_live = dup_late = 0
    for position, i in enumerate(delivery_order):
        if completed_at is not None:
            dup_late += 1            # §11.2 - discarded, the set is not reopened
            continue
        if i in store:
            dup_live += 1            # §11.2 - overwrite within a live set
        store[i] = chunks[i]
        if len(store) == total:
            completed_at = position
    assert completed_at is not None, "the set never completes"
    reassembled = b"".join(store[i] for i in range(total))
    assert reassembled == payload
    if counter == "rx_frag_duplicate":
        assert dup_live > 0 and dup_late == 0, (name, dup_live, dup_late)
    elif counter == "rx_frag_late":
        assert dup_late > 0 and dup_live == 0, (name, dup_live, dup_late)
    else:
        assert dup_live == 0 and dup_late == 0, (
            "%s: delivery_order repeats an index but names no counter" % name)

    assert origin in (DERIVED, ADJUDICATED)
    vec = {"name": name, "spec_ref": spec_ref, "origin": origin}
    if note:
        vec["note"] = note
    vec["header"] = header_json(type_name=type_name, src=src, dst=dst, seq=seq, ctx_id=ctx_id,
                                frag=frag_byte(0, total), schema=schema, hdr_flags=hdr_flags)
    vec["payload"] = payload.hex()
    vec["frag_chunk"] = frag_chunk
    vec["key"] = key_label(mac_node)
    vec["frames"] = [f.hex() for f in frames]
    vec["fragment_lens"] = [len(c) for c in chunks]
    decode = {
        "self": self_id,
        "expect_ctx_id": expect_ctx_id,
        "delivery_order": list(delivery_order),
        "status": status,
        "reassembled": reassembled.hex(),
    }
    # §11.2 - the set completed, and nothing a late fragment does may reopen it or
    # start a new one. 0 for every vector here; it is the assertion that gives the
    # late-fragment case something to fail on.
    decode["live_sets_after"] = 0
    if counter:
        # §11.2, §14.1 - the counter that must move. rx_frag_duplicate and
        # rx_frag_late are both excluded from rx_dropped: neither is a fault.
        assert counter in COUNTERS, counter
        decode["counter"] = counter
    # §11.2 - a frame declaring a `frag` total of 1 delivered INTO the middle of this
    # set. It is not a fragment of anything: it may not begin, join, displace or
    # expire the set, and it must still be delivered whole to the caller. It is a
    # complete frame of its own, so it carries its own header, payload and frame
    # bytes rather than an index into `frames`.
    if interpose is not None:
        after = interpose["after"]
        assert 0 < after < len(delivery_order), (
            "%s: the interposed frame must land INSIDE the set, not before or after "
            "it" % name)
        assert after <= completed_at, (
            "%s: the interposed frame arrives after the set completed, which "
            "witnesses the retained-key rule rather than the single-frame rule" % name)
        i_frame = build_frame(type_id=MSG_TYPE[interpose["type_name"]],
                              src=interpose["src"], dst=interpose["dst"],
                              seq=interpose["seq"], ctx_id=interpose["ctx_id"],
                              frag=frag_byte(0, 1), schema=interpose.get("schema", 0x00),
                              hdr_flags=0x00, payload=interpose["payload"],
                              mac_node=interpose.get("mac_node"))
        # The whole point is that it is NOT part of the set: were it to share the
        # set's full key the case under test would be a duplicate fragment instead.
        assert (interpose["seq"], interpose.get("schema", 0x00),
                interpose["type_name"]) != (seq, schema, type_name), (
            "%s: the interposed frame shares the set's key" % name)
        assert i_frame not in frames, "%s: the interposed frame IS a fragment" % name
        vec["interpose"] = {
            "after": after,
            "spec_ref": interpose.get("spec_ref", "§11.2"),
            "note": interpose.get("note"),
            "header": header_json(type_name=interpose["type_name"], src=interpose["src"],
                                  dst=interpose["dst"], seq=interpose["seq"],
                                  ctx_id=interpose["ctx_id"], frag=frag_byte(0, 1),
                                  schema=interpose.get("schema", 0x00), hdr_flags=0x00),
            "payload": interpose["payload"].hex(),
            "key": key_label(interpose.get("mac_node")),
            "frame": i_frame.hex(),
            "frame_len": len(i_frame),
            "decode": {"status": "Ok", "payload": interpose["payload"].hex()},
        }
        if vec["interpose"]["note"] is None:
            del vec["interpose"]["note"]

    vec["decode"] = decode
    return vec


def negative(name, spec_ref, *, frame: bytes, self_id, status, counter, stage,
             expect_ctx_id=0, note=None, origin=DERIVED):
    # §14.1 fixes the counter for every stage, so every negative vector traces to it.
    if "§14.1" not in spec_ref:
        spec_ref += ", §14.1"
    assert origin in (DERIVED, ADJUDICATED)
    assert counter in COUNTERS, "%s: %r is not in the §14.1 registry" % (name, counter)
    assert COUNTERS[counter][0] == stage, (
        "%s: §14.1 raises %s at stage %s, vector says stage %s"
        % (name, counter, COUNTERS[counter][0], stage))
    vec = {"name": name, "spec_ref": spec_ref, "origin": origin}
    if note:
        vec["note"] = note
    vec["frame"] = frame.hex()
    vec["decode"] = {
        "self": self_id,
        "expect_ctx_id": expect_ctx_id,
        "status": status,
        "counter": counter,
        "stage": stage,
    }
    return vec


# ---------------------------------------------------------------------------
# group: kdf  (§9.1) - generated and checked FIRST
# ---------------------------------------------------------------------------
def build_kdf():
    vectors = []
    named = [
        (NODE_GATELINK, "node_key_gatelink", "GateLink. §9.1 prints these six info bytes literally."),
        (NODE_WELLLINK, "node_key_welllink", "WellLink, reserved address, provisionable today."),
        (NODE_SIMNODE0, "node_key_simnode0", "Bench node 0. §5.3 enumerates bench IDs because keys are per ID."),
        (NODE_SIMNODE1, "node_key_simnode1", None),
        (NODE_SIMNODE2, "node_key_simnode2", None),
        (NODE_SIMNODE3, "node_key_simnode3", None),
    ]
    for node_id, name, note in named:
        info = kdf_info(node_id)
        assert len(info) == 6                     # §9.1 - six bytes, always
        vec = {"name": name, "spec_ref": "§9.1", "origin": DERIVED}
        if note:
            vec["note"] = note
        vec["node_id"] = "0x%02x" % node_id
        vec["salt"] = KDF_SALT.hex()
        vec["info"] = info.hex()
        vec["master_key"] = MASTER_KEY.hex()
        vec["node_key"] = node_key(node_id).hex()
        vectors.append(vec)

    # Sanity, before anything downstream is built: GateLink's info bytes are the
    # ones §9.1 and the README both spell out.
    assert kdf_info(NODE_GATELINK) == bytes.fromhex("6e6f64652d01")
    assert len(node_key(NODE_GATELINK)) == 32
    assert node_key(NODE_GATELINK) != node_key(NODE_WELLLINK)
    return vectors


# ---------------------------------------------------------------------------
# group: single  (§6, §7, §19)
# ---------------------------------------------------------------------------
GATE_CTX = CTX[NODE_GATELINK]
# §10.6 - the context GateLink takes on a ROLL_CONTEXT. Random on a real node; fixed
# here, non-zero and different from GATE_CTX, as §10.6 requires.
GATE_CTX_ROLLED = 0x3C5A9E71

STATUS_NOMINAL = dict(
    gate_state=0x03,          # §8.3 OPEN_COUNTDOWN
    input_bits=0x03,          # IN1 OPEN + IN2 MOVING
    hold=0x00,                # §8.4 not held
    movement_cause=0x05,      # §8.5 EXTERNAL_MOMENTARY
    last_direction=0x01,      # §8.6 ENTRY
    detect_flags=0x03,        # §7.2.5 SAFETY + EXIT asserted
    last_traversal_age_s=1234,
    batt_mv=13280, batt_ma=-1450, pv_cv=1832, pv_w=42, load_ma=310,
    yield_today=57, yield_yest=91, pmax_today=61, yield_total=123456,
    charge_state=3, mppt_err=0, mppt_tracker=1, mppt_flags=0x01, mppt_temp_c10=241,
    bms_soc=87, bms_flags=0x07, pack_mv=13290, pack_ma=-1450, cell_count=4,
    bms_rssi_neg=80, cell_mv=[3321, 3323, 3320, 3326], cell_temp_c=[21, 21, 22, 20],
    bms_cycles=37, bms_capacity_dah=1000, bms_alarms=0x0000, bms_age_s=12,
    uptime_s=86461, boot_count=7, node_mv=13275, node_ma=95,
    enclosure_temp_c10=283, node_flags=0x0B, status_reason=0x00,   # §8.7 POLL_RESPONSE
)

# §4.6 - every field with a documented "unavailable" value at that value.
STATUS_UNAVAILABLE = dict(
    gate_state=0x00,          # §8.3 UNKNOWN, pre-first-read
    input_bits=0x00, hold=0x00, movement_cause=0x00, last_direction=0x00,
    detect_flags=0x00,
    last_traversal_age_s=UINT32_MAX,     # §7.2.1 - none since boot
    batt_mv=UINT16_MAX, batt_ma=INT16_MIN, pv_cv=UINT16_MAX, pv_w=UINT16_MAX,
    load_ma=INT16_MIN,                   # §7.2.2 - documented
    yield_today=UINT16_MAX, yield_yest=UINT16_MAX, pmax_today=UINT16_MAX,
    yield_total=UINT32_MAX, charge_state=0, mppt_err=0, mppt_tracker=0,
    mppt_flags=0x02,                     # §7.2.6 bit 1 - VE.Direct frame stale
    mppt_temp_c10=INT16_MIN,             # §7.2.2 - documented
    bms_soc=0xFF,                        # §7.2.3 - documented
    bms_flags=0xC0,                      # §7.2.7 - soc_source = 3 (unknown), not valid
    pack_mv=UINT16_MAX, pack_ma=INT16_MIN, cell_count=0,
    bms_rssi_neg=0,                      # §7.2.3 - 0 = no link
    cell_mv=[UINT16_MAX] * 4, cell_temp_c=[-128] * 4,
    bms_cycles=UINT16_MAX, bms_capacity_dah=UINT16_MAX, bms_alarms=0x0000,
    bms_age_s=UINT16_MAX,                # §7.2.3 - saturates
    uptime_s=61, boot_count=0,           # §7.2.4 - 0 if unavailable
    node_mv=UINT16_MAX, node_ma=INT16_MIN, enclosure_temp_c10=INT16_MIN,
    node_flags=0x40,                     # §7.2.8 bit 6 - traversal age not persisted
    status_reason=0x09,                  # §8.7 BOOT
)


def build_single():
    v = []

    # --- POLL (§6.4, §19: 19 B) -------------------------------------------
    v.append(single("poll_full_status", "§6.4, §19",
                    type_name="POLL", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=4660,
                    ctx_id=GATE_CTX, payload=p_poll(0x01), self_id=NODE_GATELINK,
                    note="poll_flags bit 0 - request full status."))
    v.append(single("poll_config_readback", "§6.4, §7.4",
                    type_name="POLL", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=4661,
                    ctx_id=GATE_CTX, payload=p_poll(0x02), self_id=NODE_GATELINK,
                    note="poll_flags bit 1 - config readback, the §7.4 recovery for a lost CONFIG_ACK."))
    v.append(single("poll_broadcast_dst", "§5.3, §14 stage 5",
                    type_name="POLL", src=NODE_BRIDGE, dst=NODE_BROADCAST, seq=4662,
                    ctx_id=GATE_CTX, payload=p_poll(0x01), self_id=NODE_GATELINK,
                    note="dst 0xFF is accepted by every node (§5.3)."))
    v.append(single("poll_reserved_hdr_flags_ignored", "§5.8, §4.3",
                    type_name="POLL", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=4663,
                    ctx_id=GATE_CTX, payload=p_poll(0x01), hdr_flags=0x40,
                    self_id=NODE_GATELINK, decode_only=True,
                    note="hdr_flags bit 6 set. §5.8 requires a sender write 0 here, so no encoder emits this frame; a receiver must ignore it, not validate it as zero."))

    # --- COMMAND (§6.2, §9.2, §19: 30 B) ----------------------------------
    v.append(single("command_open", "§6.2, §8.1, §9.3, §19",
                    type_name="COMMAND", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=1,
                    ctx_id=GATE_CTX, payload=p_command(0x01), mac_node=NODE_GATELINK,
                    self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="OPEN. MAC over the whole 16-byte header plus payload (§9.3)."))
    v.append(single("command_close_immediate", "§6.2, §8.1",
                    type_name="COMMAND", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=2,
                    ctx_id=GATE_CTX, payload=p_command(0x02, arg=1),
                    mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX))
    v.append(single("command_set_debug_mode", "§6.2, §8.1",
                    type_name="COMMAND", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=3,
                    ctx_id=GATE_CTX, payload=p_command(0x20, arg=0, arg2=0x0105),
                    mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="arg2 is a little-endian uint16 bitmask (§4.1)."))
    v.append(single("command_reboot_guarded", "§6.2, §8.1",
                    type_name="COMMAND", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=4,
                    ctx_id=GATE_CTX, payload=p_command(0x7F, arg=0xA5),
                    mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="REBOOT requires arg = 0xA5 as a confirmation guard."))
    v.append(single("command_roll_context", "§6.2, §8.1, §9.4, §10.6",
                    type_name="COMMAND", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=1,
                    ctx_id=GATE_CTX, payload=p_command(0x12, arg=0xA5),
                    mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="ROLL_CONTEXT, sent under the node's current ctx_id. seq 1 is what a bridge holds after its own restart; §9.4 skips steps 4-6 for this command, so the node does not check it."))
    v.append(single("command_to_simnode0", "§5.3, §9.1",
                    type_name="COMMAND", src=NODE_BRIDGE, dst=NODE_SIMNODE0, seq=7,
                    ctx_id=CTX[NODE_SIMNODE0], payload=p_command(0x10),
                    mac_node=NODE_SIMNODE0, self_id=NODE_SIMNODE0,
                    expect_ctx_id=CTX[NODE_SIMNODE0],
                    note="Same command bytes as GateLink would see, different key: §9.1 keys follow the address."))

    # --- COMMAND_ACK (§6.3, §19: 24 B) ------------------------------------
    v.append(single("command_ack_accepted", "§6.3, §8.2, §19",
                    type_name="COMMAND_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=1,
                    ctx_id=GATE_CTX, payload=p_command_ack(1, 0x00), self_id=NODE_BRIDGE,
                    note="ACCEPTED means dispatched, not that the gate moved (§6.3)."))
    v.append(single("command_ack_duplicate_cached", "§6.3, §10.4",
                    type_name="COMMAND_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=2,
                    ctx_id=GATE_CTX, payload=p_command_ack(1, 0x07), self_id=NODE_BRIDGE,
                    note="A retry reuses the COMMAND's seq, so the node returns the cached ACK - not a second pulse."))
    v.append(single("command_ack_rejected_ctx", "§6.3, §10.3",
                    type_name="COMMAND_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=3,
                    ctx_id=GATE_CTX, payload=p_command_ack(9, 0x03), self_id=NODE_BRIDGE,
                    note="Carries the node's OWN ctx_id in the header - that is what the bridge adopts (§10.3)."))
    v.append(single("command_ack_roll_context_new_ctx", "§6.3, §10.6",
                    type_name="COMMAND_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=1,
                    ctx_id=GATE_CTX_ROLLED, payload=p_command_ack(1, 0x00), self_id=NODE_BRIDGE,
                    note="ACCEPTED for a ROLL_CONTEXT: ack_seq is the request's seq, and the header carries the NEW ctx_id, which the bridge adopts. The first frame of the new context, so its status seq is 1."))

    # --- STATUS (§7.2, §7.5, §19) -----------------------------------------
    v.append(single("status_gatelink_0x10_nominal", "§7.2, §7.1, §19",
                    type_name="STATUS", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=100,
                    ctx_id=GATE_CTX, schema=0x10, payload=p_status_0x10(**STATUS_NOMINAL),
                    self_id=NODE_BRIDGE, note="96-byte frame: the largest real frame the system emits."))
    v.append(single("status_gatelink_0x10_all_unavailable", "§4.6, §7.2, §7.2.9",
                    type_name="STATUS", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=101,
                    ctx_id=GATE_CTX, schema=0x10, payload=p_status_0x10(**STATUS_UNAVAILABLE),
                    self_id=NODE_BRIDGE,
                    note="Sentinels, not zero: UINT32_MAX traversal age, INT16_MIN load_ma, 0xFF SOC, 0 RSSI."))
    v.append(single("status_health_0xf0", "§7.5, §7.1, §19",
                    type_name="STATUS", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=102,
                    ctx_id=GATE_CTX, schema=0xF0,
                    payload=p_status_0xf0(uptime_s=3600, boot_count=7, rx_frames=1024,
                                          tx_frames=1000, rx_dropped=3, cad_backoffs=12,
                                          last_rssi_dbm=-97, last_snr_db10=85, proto_ver=2,
                                          health_flags=0x00),
                    self_id=NODE_BRIDGE,
                    note="0xF0 is a schema carried by STATUS, never a message type (§7.5)."))
    v.append(single("status_bridge_self_report_0xf0", "§7.5",
                    type_name="STATUS", src=NODE_BRIDGE, dst=NODE_BRIDGE, seq=1,
                    ctx_id=GATE_CTX, schema=0xF0,
                    payload=p_status_0xf0(uptime_s=172800, boot_count=2, rx_frames=40000 & 0xFFFF,
                                          tx_frames=20000, rx_dropped=0, cad_backoffs=0,
                                          last_rssi_dbm=-84, last_snr_db10=-15, proto_ver=2,
                                          health_flags=0x01),
                    self_id=NODE_BRIDGE,
                    note="§7.5 - the bridge self-reports under src 0x00. Negative SNR is two's complement (§4.5)."))
    v.append(single("status_simnode_0xfe", "§7.1, §17.3",
                    type_name="STATUS", src=NODE_SIMNODE0, dst=NODE_BRIDGE, seq=5,
                    ctx_id=CTX[NODE_SIMNODE0], schema=0xFE,
                    payload=p_status_0x10(**dict(STATUS_NOMINAL, status_reason=0xFF)),
                    self_id=NODE_BRIDGE,
                    note="Bench schema mirroring 0x10; status_reason DEBUG_SYNTHETIC must reach MQTT (§8.7)."))

    # --- EVENT (§7.3, §19: 34 B) ------------------------------------------
    v.append(single("event_vehicle_while_held_open", "§7.3, §8.9, §19",
                    type_name="EVENT", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=200,
                    ctx_id=GATE_CTX, schema=0x11,
                    payload=p_event_0x11(event_type=0x01, event_flags=0x00, hold_source=0x01,
                                         direction=0x03, gate_state=0x04, input_bits=0x01,
                                         detail=0, event_id=42, uptime_s=86400),
                    self_id=NODE_BRIDGE,
                    note="direction UNDETERMINED: alert on the first edge, refine later (§7.3)."))
    v.append(single("event_followup_direction_resolved", "§7.3",
                    type_name="EVENT", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=201,
                    ctx_id=GATE_CTX, schema=0x11,
                    payload=p_event_0x11(event_type=0x01, event_flags=0x01, hold_source=0x01,
                                         direction=0x01, gate_state=0x04, input_bits=0x05,
                                         detail=0, event_id=42, uptime_s=86402),
                    self_id=NODE_BRIDGE,
                    note="Same event_id, event_flags bit 0 set - the follow-up that resolves direction."))
    v.append(single("event_fire_asserted", "§7.3, §8.9",
                    type_name="EVENT", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=202,
                    ctx_id=GATE_CTX, schema=0x11,
                    payload=p_event_0x11(event_type=0x02, event_flags=0x00, hold_source=0x03,
                                         direction=0x00, gate_state=0x02, input_bits=0x12,
                                         detail=0, event_id=43, uptime_s=86500),
                    self_id=NODE_BRIDGE))
    v.append(single("event_phy_reverted", "§7.3, §8.9, §12.4.2",
                    type_name="EVENT", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=203,
                    ctx_id=GATE_CTX, schema=0x11,
                    payload=p_event_0x11(event_type=0x0B, event_flags=0x00, hold_source=0x00,
                                         direction=0x00, gate_state=0x02, input_bits=0x00,
                                         detail=0x0001, event_id=44, uptime_s=86600),
                    self_id=NODE_BRIDGE,
                    note="PHY_REVERTED, v0.14 (D59). detail 0x0001: the trial window expired; 0x0002 would be a reboot mid-trial."))

    # --- ERROR (§6.5, §19: 22 B) ------------------------------------------
    v.append(single("error_bad_length", "§6.5, §8.8, §19",
                    type_name="ERROR", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=300,
                    ctx_id=GATE_CTX, payload=p_error(0x02, 0x00, 4660), self_id=NODE_BRIDGE,
                    note="ref_seq is the offending frame's seq, or 0."))
    v.append(single("error_unknown_hdr_ext", "§5.8, §8.8, §14 stage 5a",
                    type_name="ERROR", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=301,
                    ctx_id=GATE_CTX, payload=p_error(0x0A, 0x80, 4661), self_id=NODE_BRIDGE,
                    note="The reply a node owes for a CRITICAL_EXT it does not implement."))

    # --- PING (§6.6, §19: 20 + N) -----------------------------------------
    v.append(single("ping_empty_echo", "§6.6, §19",
                    type_name="PING", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=500,
                    ctx_id=GATE_CTX, payload=p_ping(0x00, b""), self_id=NODE_GATELINK,
                    note="n = 0: the 20-byte floor of the 20+N line."))
    ping_seq = 501
    v.append(single("ping_pattern_fill_8", "§6.6.3",
                    type_name="PING", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=ping_seq,
                    ctx_id=GATE_CTX,
                    payload=p_ping(0x01, pattern_fill(ping_seq, 8)), self_id=NODE_GATELINK,
                    note="data[i] = (seq & 0xFF) + i, so seq 501 -> 0xF5, 0xF6, ... wrapping at 0xFF."))
    max_seq = 502
    v.append(single("ping_max_222_bytes", "§6.6.1, §3.1, §19",
                    type_name="PING", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=max_seq,
                    ctx_id=GATE_CTX,
                    payload=p_ping(0x01, pattern_fill(max_seq, LRAN_PING_MAX_ECHO)),
                    self_id=NODE_GATELINK,
                    note="n = 202 gives a frame of exactly LRAN_MAX_FRAME = 222 bytes."))
    v.append(single("ping_echo_reply_swapped", "§6.6, §17.3",
                    type_name="PING", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=ping_seq,
                    ctx_id=GATE_CTX,
                    payload=p_ping(0x01, pattern_fill(ping_seq, 8)), self_id=NODE_BRIDGE,
                    note="The responder swaps src/dst and preserves seq, ping_flags and the echo bytes."))

    # --- HEX_REQ / HEX_RSP (§7.6, §19) ------------------------------------
    hex_get = b":7F0ED0071"
    v.append(single("hex_req_get_plain", "§7.6, §9.2, §19",
                    type_name="HEX_REQ", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=600,
                    ctx_id=GATE_CTX, payload=p_hex_req(0x00, hex_get), self_id=NODE_GATELINK,
                    note="Command nibble '7' (Get) - read-only, no MAC (§7.6). Frame is 20+N."))
    hex_set = b":8F0ED00C8008"
    v.append(single("hex_req_set_authenticated", "§7.6, §9.2, §19",
                    type_name="HEX_REQ", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=601,
                    ctx_id=GATE_CTX, payload=p_hex_req(0x01, hex_set),
                    mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="Command nibble '8' (Set) MUST carry a MAC: frame is 28+N, N=13 -> 41 B."))
    hex_restart = b":64F"
    v.append(single("hex_req_restart_authenticated", "§7.6, §9.2",
                    type_name="HEX_REQ", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=602,
                    ctx_id=GATE_CTX, payload=p_hex_req(0x01, hex_restart),
                    mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="Command nibble '6' (Restart) is write-class too."))
    v.append(single("hex_rsp_ok", "§7.6, §8.13, §19",
                    type_name="HEX_RSP", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=603,
                    ctx_id=GATE_CTX, payload=p_hex_rsp(0x00, b":7F0ED00C800E9"),
                    self_id=NODE_BRIDGE,
                    note="§19 errata: HEX_RSP is 20+N, not 21+N - the payload is 2+N like HEX_REQ."))
    v.append(single("hex_rsp_timeout", "§7.6, §8.13",
                    type_name="HEX_RSP", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=604,
                    ctx_id=GATE_CTX, payload=p_hex_rsp(0x01, b""), self_id=NODE_BRIDGE,
                    note="n = 0 on error; a timeout is reported, never answered with silence."))

    # --- CONFIG / CONFIG_ACK (§7.4, §19) ----------------------------------
    cfg_small = p_config(0x01, [(0x0101, PTYPE_U16, 1500), (0x0102, PTYPE_BOOL, 1)])
    v.append(single("config_set_two_entries", "§7.4, §8.10, §9.2, §19",
                    type_name="CONFIG", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=700,
                    ctx_id=GATE_CTX, schema=0x12, payload=cfg_small, mac_node=NODE_GATELINK,
                    self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="op = SET; entries are param_id/ptype/len/value, value little-endian."))
    v.append(single("config_get_all", "§7.4, §8.10",
                    type_name="CONFIG", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=701,
                    ctx_id=GATE_CTX, schema=0x12, payload=p_config(0x03, []),
                    mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                    note="GET_ALL carries count = 0 and no entries: a 2-byte payload."))
    ack_small = p_config_ack(0x01, 0x01, [(0x0101, 0x02, PTYPE_U16, 1200),
                                          (0x0102, 0x00, PTYPE_BOOL, 1)])
    v.append(single("config_ack_clamped_not_persisted", "§7.4, §8.11, §8.12",
                    type_name="CONFIG_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=702,
                    ctx_id=GATE_CTX, schema=0x12, payload=ack_small, self_id=NODE_BRIDGE,
                    note="CLAMPED entry carries the EFFECTIVE value (1200), persist_status = APPLIED_NOT_PERSISTED."))

    # §7.4.1 - an answer too large for one frame, as two messages. The pair is the point:
    # the marked one and the final one differ in bit 7 of one byte and nowhere else, so a
    # codec that ignores the bit reads the first as 133 results and fails on length.
    ack_part1 = p_config_ack(0x03, 0x00, [(0x0100, 0x00, PTYPE_U8, 8),
                                          (0x0101, 0x00, PTYPE_U16, 5000)],
                             more_follows=True)
    v.append(single("config_ack_get_all_more_follows", "§7.4.1",
                    type_name="CONFIG_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=703,
                    ctx_id=GATE_CTX, schema=0x12, payload=ack_part1, self_id=NODE_BRIDGE,
                    note="First message of a split GET_ALL answer: count byte reads 0x82, "
                         "MORE_FOLLOWS set over a count of 2."))
    ack_part2 = p_config_ack(0x03, 0x00, [(0x0102, 0x00, PTYPE_U8, 5),
                                          (0x0114, 0x00, PTYPE_I16, -4)])
    v.append(single("config_ack_get_all_final", "§7.4.1",
                    type_name="CONFIG_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=703,
                    ctx_id=GATE_CTX, schema=0x12, payload=ack_part2, self_id=NODE_BRIDGE,
                    note="Last message of the same answer, same seq: count byte reads 0x02. "
                         "A receiver closes the transaction here, not on the first message."))
    return v


# ---------------------------------------------------------------------------
# group: frag  (§11)
# ---------------------------------------------------------------------------
def build_frag():
    v = []

    # §6.6.2 - a 202-byte echo at frag_chunk 14 is the full 15-fragment set.
    ping_seq = 800
    ping_payload = p_ping(0x01, pattern_fill(ping_seq, LRAN_PING_MAX_ECHO))
    assert len(ping_payload) == LRAN_MAX_PAYLOAD_PLAIN == 204
    common = dict(type_name="PING", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=ping_seq,
                  ctx_id=GATE_CTX, payload=ping_payload, frag_chunk=14,
                  self_id=NODE_GATELINK)
    v.append(frag_set("ping_chunk14_fifteen_fragments", "§6.6.2, §11.1", **common,
                      note="ceil(204/14) = 15: fourteen 14-byte fragments then an 8-byte remainder."))
    v.append(frag_set("ping_chunk14_reversed", "§11.2", **dict(common, delivery_order=list(range(14, -1, -1))),
                      note="Arrival order is irrelevant: reassembly is concatenation in ascending index order."))
    shuffled = [7, 0, 14, 3, 11, 1, 9, 2, 13, 5, 8, 4, 12, 6, 10]
    v.append(frag_set("ping_chunk14_shuffled", "§11.2", **dict(common, delivery_order=shuffled),
                      note="Same bytes out. A receiver must not place a fragment before every lower index has arrived."))
    # §11.2 - "a duplicate index within a LIVE set overwrites the stored fragment".
    # The repeats therefore arrive before index 14 completes the set. A fragment
    # arriving AFTER completion is not covered by §11.2 (which defines expiry only
    # for incomplete sets) and is deliberately not tested here.
    dup = [0, 1, 2, 3, 4, 4, 5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14]
    v.append(frag_set("ping_chunk14_duplicate_index", "§11.2", **dict(common, delivery_order=dup),
                      counter="rx_frag_duplicate",
                      note="Index 4 arrives three times while the set is still live: each repeat overwrites and is counted, and none of them changes the reassembled bytes."))

    # §11.2 (new in v0.5) - a fragment matching the most recently completed set is
    # discarded and counted rx_frag_late, with no ERROR returned and no new set
    # started. ADJUDICATED: v0.4 was silent here, this generator reported the
    # silence rather than guessing, and the rule was chosen against the codec's
    # prior behaviour before v0.5 wrote it down.
    late = list(range(15)) + [4, 14]
    v.append(frag_set("ping_chunk14_late_fragment", "§11.2, §14.1",
                      **dict(common, delivery_order=late), counter="rx_frag_late",
                      origin=ADJUDICATED,
                      note="Index 4 and index 14 arrive again after index 14 completed the set: both match the retained key, both are discarded and counted, no ERROR is returned, and live_sets_after stays 0 - one echoed fragment must not open a set that can never complete."))

    # A two-fragment PING, the smallest interesting set.
    small_seq = 801
    small_payload = p_ping(0x00, bytes(range(0x40, 0x40 + 30)))
    v.append(frag_set("ping_two_fragments_uneven_tail", "§11.1",
                      type_name="PING", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=small_seq,
                      ctx_id=GATE_CTX, payload=small_payload, frag_chunk=20,
                      self_id=NODE_GATELINK,
                      note="32-byte payload at chunk 20: lens 20 then 12 - the tail is the remainder, never longer."))

    # §7.4 - the first fragmented frames the system is expected to produce.
    cfg_entries = [(0x0200 + i, PTYPE_U32, 1000 + i) for i in range(24)]
    cfg_payload = p_config(0x01, cfg_entries)
    assert len(cfg_payload) == 2 + 24 * 8 == 194     # §7.4 - 24 uint32 entries fills a frame
    v.append(frag_set("config_24_entries_chunk100_authenticated", "§7.4, §11.1, §11.3",
                      type_name="CONFIG", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=900,
                      ctx_id=GATE_CTX, schema=0x12, payload=cfg_payload, frag_chunk=100,
                      mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                      note="Every fragment carries its own header, its own MAC and its own CRC16 (§11.1)."))
    v.append(frag_set("config_24_entries_chunk64_authenticated", "§7.4, §11.1",
                      type_name="CONFIG", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=901,
                      ctx_id=GATE_CTX, schema=0x12, payload=cfg_payload, frag_chunk=64,
                      mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                      note="Four fragments: 64, 64, 64, 2. The MAC differs per fragment because the frag byte is inside it (§9.3)."))
    v.append(frag_set("config_24_entries_chunk64_shuffled", "§11.2, §11.3",
                      type_name="CONFIG", src=NODE_BRIDGE, dst=NODE_GATELINK, seq=901,
                      ctx_id=GATE_CTX, schema=0x12, payload=cfg_payload, frag_chunk=64,
                      mac_node=NODE_GATELINK, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX,
                      delivery_order=[2, 0, 3, 1],
                      note="Authentication is per fragment on arrival; dedup and replay run once on the completed set (§9.4)."))

    # §7.4 - a full CONFIG_ACK readback, 21 uint32 results, unauthenticated.
    ack_results = [(0x0200 + i, 0x00, PTYPE_U32, 1000 + i) for i in range(21)]
    ack_payload = p_config_ack(0x03, 0x00, ack_results)
    assert len(ack_payload) == 3 + 21 * 9 == 192     # §7.4 - fills a frame at 21 entries
    v.append(frag_set("config_ack_21_results_chunk96", "§7.4, §11.4",
                      type_name="CONFIG_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=902,
                      ctx_id=GATE_CTX, schema=0x12, payload=ack_payload, frag_chunk=96,
                      self_id=NODE_BRIDGE,
                      note="The readback that recovers a lost ACK is itself fragmented; CONFIG_ACK carries no MAC (§9.2)."))

    # §11.2 - THE SINGLE-FRAME RULE, and the case on the bridge it was written for:
    # a node's periodic health STATUS arriving while that same node's fragmented
    # CONFIG_ACK is still in flight. Same src, same ctx_id, a different conversation.
    # The STATUS must be delivered whole, the set must complete into the same bytes
    # it would have without it, and no reassembly counter may move - a status frame
    # arriving on schedule is not an abandonment and not a timeout.
    v.append(frag_set("config_ack_21_results_single_frame_interposed", "§11.2, §14.1",
                      type_name="CONFIG_ACK", src=NODE_GATELINK, dst=NODE_BRIDGE, seq=903,
                      ctx_id=GATE_CTX, schema=0x12, payload=ack_payload, frag_chunk=96,
                      self_id=NODE_BRIDGE,
                      interpose=dict(
                          after=1,
                          type_name="STATUS", src=NODE_GATELINK, dst=NODE_BRIDGE,
                          seq=904, ctx_id=GATE_CTX, schema=0xF0,
                          payload=p_status_0xf0(uptime_s=7200, boot_count=3, rx_frames=250,
                                                tx_frames=249, rx_dropped=0, cad_backoffs=4,
                                                last_rssi_dbm=-97, last_snr_db10=55,
                                                proto_ver=WIRE_VER, health_flags=0x00),
                          note="A health STATUS from the same node, mid-set. Complete on arrival, so nothing is held for it."),
                      note="§11.2: a frame declaring frag total 1 may not begin, join, displace or expire a set, even one sharing (src, ctx_id) with it - and is still delivered."))
    return v


# ---------------------------------------------------------------------------
# group: negative  (§14)
# ---------------------------------------------------------------------------
def build_negative():
    v = []

    def good_poll(seq=4660, **kw):
        args = dict(type_id=MSG_TYPE["POLL"], src=NODE_BRIDGE, dst=NODE_GATELINK, seq=seq,
                    ctx_id=GATE_CTX, frag=0x01, schema=0x00, payload=p_poll(0x01))
        args.update(kw)
        return build_frame(**args)

    # stage 2 - length >= 18
    runt_body = good_poll()[:15]                    # a truncated header
    runt = runt_body + u16(crc16_ccitt_false(runt_body))
    assert len(runt) == 17
    v.append(negative("runt_17_bytes", "§14 stage 2, §3.1", frame=runt,
                      self_id=NODE_GATELINK, status="Runt", counter="rx_runt", stage="2",
                      note="17 bytes cannot hold a 16-byte header plus a CRC16. The CRC over the truncated buffer is valid, so only the length can catch it."))

    # stage 2a - length <= 222. The PHY hands up to 255 bytes.
    over_payload = u8(0x00) + u8(203) + pattern_fill(0, 203)     # 205 B, one over the plain cap
    oversize = build_frame(type_id=MSG_TYPE["PING"], src=NODE_BRIDGE, dst=NODE_GATELINK,
                           seq=4670, ctx_id=GATE_CTX, frag=0x01, payload=over_payload)
    assert len(oversize) == 223
    v.append(negative("oversize_223_bytes", "§3, §14 stage 2a", frame=oversize,
                      self_id=NODE_GATELINK, status="Oversize", counter="rx_oversize", stage="2a",
                      note="Emitting this is a sender bug; receiving it is a counted runtime discard, never an assertion (§3)."))

    # stage 3 - application CRC16
    bad_crc = bytearray(good_poll(seq=4671))
    bad_crc[-1] ^= 0xFF                              # differs from the valid value by construction
    bad_crc = bytes(bad_crc)
    assert crc16_ccitt_false(bad_crc[:-2]) != int.from_bytes(bad_crc[-2:], "little")
    v.append(negative("bad_app_crc16", "§2.1, §14 stage 3", frame=bad_crc,
                      self_id=NODE_GATELINK, status="BadCrc", counter="rx_bad_crc", stage="3",
                      note="High CRC byte inverted. This is the one vector whose CRC is deliberately NOT repaired."))

    # stage 4 - ver known
    bad_ver = bytearray(good_poll(seq=4672))
    bad_ver[0] = 3                                   # differs from ver = 2
    v.append(negative("unknown_ver_3", "§5.1, §13.1, §14 stage 4", frame=reseal(bytes(bad_ver)),
                      self_id=NODE_GATELINK, status="BadVersion", counter="rx_bad_ver", stage="4",
                      note="A node accepts only its own ver; only the bridge accepts N-1 (§13.1). CRC repaired."))

    # stage 5 - dst is self or 0xFF
    wrong_dst = bytearray(good_poll(seq=4673))
    wrong_dst[3] = NODE_WELLLINK                     # not self (0x01), not broadcast
    v.append(negative("wrong_dst_not_addressed", "§5.3, §14 stage 5", frame=reseal(bytes(wrong_dst)),
                      self_id=NODE_GATELINK, status="NotAddressed", counter="rx_not_addressed", stage="5",
                      note="Addressed to WellLink. Discarded silently by GateLink - no ERROR reply is owed (§14)."))

    # stage 5a - CRITICAL_EXT
    crit = bytearray(good_poll(seq=4674))
    crit[12] = 0x80                                  # hdr_flags bit 7
    v.append(negative("critical_ext_unimplemented", "§5.8, §14 stage 5a", frame=reseal(bytes(crit)),
                      self_id=NODE_GATELINK, status="UnknownHdrExt", counter="rx_unknown_hdr_ext", stage="5a",
                      note="No extension is defined yet (W8), so every receiver must discard loudly with ERROR(UNKNOWN_HDR_EXT)."))

    # stage 5b - frag well formed
    frag_zero = bytearray(good_poll(seq=4675))
    frag_zero[10] = 0x00                             # §5.6 - total 0 is malformed, not "single frame"
    v.append(negative("frag_total_zero", "§5.6, §14 stage 5b", frame=reseal(bytes(frag_zero)),
                      self_id=NODE_GATELINK, status="BadFrag", counter="rx_bad_frag", stage="5b",
                      note="0x00 is a malformed header, NOT a single-frame marker; 0x01 is the single-frame value."))
    # stage 10 - §11.2: "a fragment index >= the declared total ... is discarded
    # with ERROR(FRAGMENT_OVERFLOW)". §14 stage 5b's wording ("frag well formed -
    # total >= 1, index < total") reads the other way and runs first; the two
    # sections contradict each other. The v0.4 implementation task list settles it
    # for §11.2 and reserves stage 5b for total == 0 alone. Both frames below are
    # PING, which §11.4 makes fragmentable, so the index is the only fault in them
    # and stage 8a cannot claim them first.
    over_idx = build_frame(type_id=MSG_TYPE["PING"], src=NODE_BRIDGE, dst=NODE_GATELINK,
                           seq=4676, ctx_id=GATE_CTX, frag=0x53, payload=pattern_fill(4676, 14))
    v.append(negative("frag_index_ge_total", "§5.6, §11.2, §14 stage 10", frame=over_idx,
                      self_id=NODE_GATELINK, status="FragmentOverflow", counter="rx_fragment_overflow",
                      stage="10", origin=ADJUDICATED,
                      note="frag 0x53: index 5 of a declared total of 3. Wire error is ERROR(FRAGMENT_OVERFLOW) (§11.2)."))
    eq_idx = build_frame(type_id=MSG_TYPE["PING"], src=NODE_BRIDGE, dst=NODE_GATELINK,
                         seq=4677, ctx_id=GATE_CTX, frag=0x22, payload=pattern_fill(4677, 14))
    v.append(negative("frag_index_equals_total", "§5.6, §11.2, §14 stage 10", frame=eq_idx,
                      self_id=NODE_GATELINK, status="FragmentOverflow", counter="rx_fragment_overflow",
                      stage="10", origin=ADJUDICATED,
                      note="frag 0x22: a 0-based index equal to the 1-based total is the off-by-one an encoder actually makes."))

    # stage 6 - type known
    unk_type = bytearray(good_poll(seq=4678))
    unk_type[1] = 0x0C                               # §17.2 - reserved for future types
    unk_type = unk_type[:LRAN_HDR_LEN] + unk_type[-2:]   # drop the POLL payload: 18 B, no length excuse
    v.append(negative("unknown_type_0x0c", "§6, §13.2, §14 stage 6", frame=reseal(bytes(unk_type)),
                      self_id=NODE_GATELINK, status="UnknownType", counter="rx_unknown_type", stage="6",
                      note="Adding a type needs no ver bump precisely because this discard exists (§13.2)."))

    reserved_type = bytearray(good_poll(seq=4685))
    reserved_type[1] = 0x00                          # §6 - 0x00 is *reserved*, not a type
    reserved_type = reserved_type[:LRAN_HDR_LEN] + reserved_type[-2:]
    v.append(negative("reserved_type_0x00", "§6, §14 stage 6", frame=reseal(bytes(reserved_type)),
                      self_id=NODE_GATELINK, status="UnknownType", counter="rx_unknown_type", stage="6",
                      note="§6 reserves 0x00; a zeroed type byte is the shape a truncated or zero-filled buffer takes, so it must be rejected as unknown rather than treated as a default."))

    # stage 7 - (type, schema) pair
    pair = build_frame(type_id=MSG_TYPE["STATUS"], src=NODE_GATELINK, dst=NODE_BRIDGE, seq=4679,
                       ctx_id=GATE_CTX, frag=0x01, schema=0x11,
                       payload=p_event_0x11(event_type=0x01, event_flags=0, hold_source=0,
                                            direction=0, gate_state=0, input_bits=0, detail=0,
                                            event_id=1, uptime_s=1))
    v.append(negative("status_carrying_event_schema", "§7.1, §14 stage 7", frame=pair,
                      self_id=NODE_BRIDGE, status="UnknownSchema", counter="rx_unknown_schema", stage="7",
                      note="Schema 0x11 is well known but is an EVENT schema. The (type, schema) PAIR is the unit of validation, so this fails at 7, not 8."))
    unk_schema = build_frame(type_id=MSG_TYPE["STATUS"], src=NODE_GATELINK, dst=NODE_BRIDGE, seq=4680,
                             ctx_id=GATE_CTX, frag=0x01, schema=0x99,
                             payload=p_status_0xf0(uptime_s=1, boot_count=0, rx_frames=0, tx_frames=0,
                                                   rx_dropped=0, cad_backoffs=0, last_rssi_dbm=0,
                                                   last_snr_db10=0, proto_ver=2, health_flags=0))
    v.append(negative("unknown_schema_0x99", "§7.1, §14 stage 7", frame=unk_schema,
                      self_id=NODE_BRIDGE, status="UnknownSchema", counter="rx_unknown_schema", stage="7",
                      note="0x99 is unassigned in §7.1 and §17.2."))

    # stage 8 - payload length matches (type, schema), unfragmented frames only
    long_poll = build_frame(type_id=MSG_TYPE["POLL"], src=NODE_BRIDGE, dst=NODE_GATELINK, seq=4681,
                            ctx_id=GATE_CTX, frag=0x01, payload=b"\x01\x00")
    v.append(negative("poll_payload_two_bytes", "§4.4, §14 stage 8", frame=long_poll,
                      self_id=NODE_GATELINK, status="BadLength", counter="rx_bad_length", stage="8",
                      note="POLL is fixed at 1 byte (§6.4). A length mismatch is discarded, never parsed best-effort."))
    short_status = build_frame(type_id=MSG_TYPE["STATUS"], src=NODE_GATELINK, dst=NODE_BRIDGE, seq=4682,
                               ctx_id=GATE_CTX, frag=0x01, schema=0x10,
                               payload=p_status_0x10(**STATUS_NOMINAL)[:76])
    v.append(negative("status_0x10_76_bytes", "§7.2, §14 stage 8", frame=short_status,
                      self_id=NODE_BRIDGE, status="BadLength", counter="rx_bad_length", stage="8",
                      note="76 bytes is v0.2's status length. Schema 0x10 is 78 B in v0.3+ (§7.2.9), so the old size must be rejected, not truncated-parsed."))

    # stage 8a - type is fragmentable
    frag_hex = build_frame(type_id=MSG_TYPE["HEX_REQ"], src=NODE_BRIDGE, dst=NODE_GATELINK, seq=4683,
                           ctx_id=GATE_CTX, frag=frag_byte(0, 2), payload=p_hex_req(0x00, b":7F0ED0071"))
    v.append(negative("fragmented_hex_req", "§11.4, §14 stage 8a", frame=frag_hex,
                      self_id=NODE_GATELINK, status="NotFragmentable", counter="rx_not_fragmentable", stage="8a",
                      note="HEX_REQ is single-frame in v1: `n` lives only in fragment 0 and the MAC requirement depends on a nibble inside the payload (§11.4). Wire error is ERROR(BAD_LENGTH); the counter is its own, because a peer fragmenting an unfragmentable type is a different fault from a bad length."))
    frag_cmd = build_frame(type_id=MSG_TYPE["COMMAND"], src=NODE_BRIDGE, dst=NODE_GATELINK, seq=4684,
                           ctx_id=GATE_CTX, frag=frag_byte(0, 2), payload=p_command(0x01),
                           mac_node=NODE_GATELINK)
    v.append(negative("fragmented_command", "§11.4, §14 stage 8a", frame=frag_cmd,
                      self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX, status="NotFragmentable",
                      counter="rx_not_fragmentable", stage="8a",
                      note="A 4-byte COMMAND is never fragmentable. The MAC is valid, so stage 8a must reject it before stage 9 accepts it. Wire error is ERROR(BAD_LENGTH) (§11.4), counter rx_not_fragmentable."))

    # stage 9 - per-frame authentication (§9.4 steps 2 and 3)
    ctx_bad = build_frame(type_id=MSG_TYPE["COMMAND"], src=NODE_BRIDGE, dst=NODE_GATELINK, seq=5,
                          ctx_id=GATE_CTX ^ 0x00000001, frag=0x01, payload=p_command(0x01),
                          mac_node=NODE_GATELINK)
    v.append(negative("command_ctx_mismatch", "§9.4 step 2, §10.3, §14 stage 9", frame=ctx_bad,
                      self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX, status="RejectedCtx",
                      counter="rx_rejected_ctx", stage="9", origin=ADJUDICATED,
                      note="MAC is valid over this header, so only the ctx check can fail: the node replies COMMAND_ACK(REJECTED_CTX) carrying its own ctx_id."))
    roll_stale = build_frame(type_id=MSG_TYPE["COMMAND"], src=NODE_BRIDGE, dst=NODE_GATELINK, seq=9,
                             ctx_id=GATE_CTX, frag=0x01, payload=p_command(0x12, arg=0xA5),
                             mac_node=NODE_GATELINK)
    v.append(negative("roll_context_replayed_after_roll", "§9.4 step 2, §10.6, §14 stage 9",
                      frame=roll_stale, self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX_ROLLED,
                      status="RejectedCtx", counter="rx_rejected_ctx", stage="9",
                      note="A ROLL_CONTEXT captured under the old ctx_id, replayed after the node rolled to a new one. The MAC is valid, so only the ctx check can fail. This is what bounds a replayed roll (§10.6): once acted on, the request cannot be acted on again. Its seq differs from command_roll_context's only because no negative may reproduce a valid frame byte for byte."))
    forged = bytearray(build_frame(type_id=MSG_TYPE["COMMAND"], src=NODE_BRIDGE, dst=NODE_GATELINK,
                                   seq=6, ctx_id=GATE_CTX, frag=0x01, payload=p_command(0x01),
                                   mac_node=NODE_GATELINK))
    forged[-3] ^= 0xFF                               # last MAC byte, guaranteed different
    forged = reseal(bytes(forged))
    v.append(negative("command_corrupt_mac", "§9.3, §9.4 step 3, §14 stage 9", frame=forged,
                      self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX, status="RejectedMac",
                      counter="rx_rejected_mac", stage="9", origin=ADJUDICATED,
                      note="One MAC byte inverted, CRC repaired. Verification must be constant time (§9.4)."))
    swapped_key = build_frame(type_id=MSG_TYPE["COMMAND"], src=NODE_BRIDGE, dst=NODE_GATELINK,
                              seq=8, ctx_id=GATE_CTX, frag=0x01, payload=p_command(0x03),
                              mac_node=NODE_SIMNODE0)
    v.append(negative("command_signed_with_wrong_node_key", "§9.1, §14 stage 9", frame=swapped_key,
                      self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX, status="RejectedMac",
                      counter="rx_rejected_mac", stage="9", origin=ADJUDICATED,
                      note="Signed with simnode-0's key but addressed to GateLink. This is the property §5.3 relies on: a bench node cannot forge a HOLD_OPEN to the gate."))
    unsigned = build_frame(type_id=MSG_TYPE["COMMAND"], src=NODE_BRIDGE, dst=NODE_GATELINK,
                           seq=9, ctx_id=GATE_CTX, frag=0x01, payload=p_command(0x01))
    assert len(unsigned) == 22                       # 30 minus the absent MAC
    v.append(negative("command_with_no_mac", "§9.2, §14 stage 8", frame=unsigned,
                      self_id=NODE_GATELINK, expect_ctx_id=GATE_CTX, status="BadLength",
                      counter="rx_bad_length", stage="8",
                      note="An authenticated type with the MAC omitted is 8 bytes short, so the length check catches it first. §9.2 forbids ever emitting one."))
    return v


# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------
def envelope(group: str, vectors: list) -> dict:
    return {
        "format": FORMAT,
        "spec": SPEC,
        "wire_ver": WIRE_VER,
        "group": group,
        "generated_by": "tools/vectors/generate.py",
        "vectors": vectors,
    }


_NUM_ARRAY = re.compile(r"\[\s+((?:\d+,\s+)*\d+)\s+\]")


def dumps(doc: dict) -> str:
    """2-space JSON, with all-numeric arrays collapsed onto one line so
    `fragment_lens` and `delivery_order` stay readable in a diff."""
    text = json.dumps(doc, indent=2, ensure_ascii=False)
    text = _NUM_ARRAY.sub(lambda m: "[" + " ".join(m.group(1).split()) + "]", text)
    return text + "\n"


def write(group: str, filename: str, vectors: list) -> None:
    path = HERE / filename
    path.write_text(dumps(envelope(group, vectors)), encoding="utf-8")
    print("%-24s %2d vectors -> %s" % (group, len(vectors), path.relative_to(HERE.parents[1])))


def assert_no_negative_reproduces_a_valid_frame(single_v, frag_v, negative_v) -> int:
    """A "corrupted" frame that happens to reproduce a valid one verifies
    legitimately and passes as a false negative, silently. Checked at generation
    time because that is where a one-byte edit to a builder introduces it."""
    valid = {}
    for vec in single_v:
        valid[vec["frame"]] = vec["name"]
    for vec in frag_v:
        for index, frame in enumerate(vec["frames"]):
            valid.setdefault(frame, "%s[%d]" % (vec["name"], index))
    seen = {}
    for vec in negative_v:
        frame = vec["frame"]
        assert frame not in valid, (
            "%s reproduces the valid frame of %s byte for byte: it would verify "
            "legitimately and pass as a false negative" % (vec["name"], valid[frame]))
        assert frame not in seen, (
            "%s and %s are the same bytes, so one of them proves nothing about the "
            "stage it names" % (vec["name"], seen[frame]))
        seen[frame] = vec["name"]
    return len(valid)


def main() -> None:
    # §9.1 first: if key derivation is wrong every authenticated vector below is
    # wrong in a way no counter points at.
    kdf = build_kdf()
    single_v = build_single()
    frag_v = build_frag()
    negative_v = build_negative()
    valid_frames = assert_no_negative_reproduces_a_valid_frame(single_v, frag_v, negative_v)
    write("kdf", "vectors_kdf.json", kdf)
    write("single", "vectors_single.json", single_v)
    write("frag", "vectors_frag.json", frag_v)
    write("negative", "vectors_negative.json", negative_v)
    every = kdf + single_v + frag_v + negative_v
    adjudicated = [v["name"] for v in every if v.get("origin") == ADJUDICATED]
    print("%-24s %2d distinct valid frames, none reproduced by a negative vector"
          % ("cross-check", valid_frames))
    print("%-24s %2d derived, %d adjudicated (%s)"
          % ("provenance", len(every) - len(adjudicated), len(adjudicated),
             ", ".join(adjudicated)))


if __name__ == "__main__":
    main()
