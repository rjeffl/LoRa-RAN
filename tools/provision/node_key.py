#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""Derive a node's key from the master in secrets.h, for that node's build - spec 9.1.

GateLink Impl Plan 6.8: GateLink holds its own node key and never LRAN_MASTER_KEY, so a
GateLink in the wrong hands yields that key alone. This prints the `#define` to paste
into the root secrets.h:

    python3 tools/provision/node_key.py               # GateLink, 0x01
    python3 tools/provision/node_key.py --node 0x02   # WellLink
    python3 tools/provision/node_key.py --self-test   # against the W4 kdf vectors

It reads the master from the root secrets.h, or from --secrets. It never prints the
master and never writes a file: the derived key goes to stdout and nowhere else.

The HKDF is tools/vectors/generate.py's, imported rather than rewritten, so the
derivation is the one the W4 kdf vectors pin and the C++ suite checks against. The
self-test reproduces those vectors through this tool's own path, the secrets.h parser
included.

Two masters are refused. The all-zero placeholder, because the key derived from zeros
is not zeros: it would pass the node's boot check (lran::key_is_placeholder) and boot
as provisioned with a key no bridge holds. And the W4 test master, which its fixture
says is never to be flashed.
"""

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
VECTORS = ROOT / "tools" / "vectors"
sys.path.insert(0, str(VECTORS))

import generate  # noqa: E402 - the HKDF the W4 vectors are built with

# spec 5.3 - the nodes a key is provisioned for, and the secrets.h field each reads.
# The bridge holds the master. The bench IDs 0xF0-0xF3 are the simnode's, which holds
# the master too (Bridge Impl Plan 10.3), so none of them is provisioned here.
FIELDS = {
    0x01: "LRAN_GATELINK_NODE_KEY",
    0x02: "LRAN_WELLLINK_NODE_KEY",
}

_MASTER_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+LRAN_MASTER_KEY\b(.*?)\}",
                            re.MULTILINE | re.DOTALL)
_BYTE = re.compile(r"0[xX]([0-9a-fA-F]{1,2})\b")


class ProvisionError(Exception):
    pass


def strip_comments(text):
    """C and C++ comments removed, so a commented-out key is never read as the key."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def parse_master(text):
    """The 32 bytes of LRAN_MASTER_KEY from a secrets.h's text."""
    m = _MASTER_DEFINE.search(strip_comments(text))
    if not m:
        raise ProvisionError("no `#define LRAN_MASTER_KEY { ... }` found")
    key = bytes(int(b, 16) for b in _BYTE.findall(m.group(1)))
    if len(key) != generate.KDF_LEN:
        raise ProvisionError("LRAN_MASTER_KEY has %d bytes; spec 9.1 says %d"
                             % (len(key), generate.KDF_LEN))
    return key


def refuse(master):
    if master == bytes(len(master)):
        raise ProvisionError("LRAN_MASTER_KEY is the all-zero placeholder. Fill in secrets.h "
                             "first: a key derived from zeros is not zeros, and the node "
                             "would boot as provisioned.")
    if master == generate.MASTER_KEY:
        raise ProvisionError("LRAN_MASTER_KEY is the W4 test master "
                             "(tools/vectors/test_master_key.json), which is never flashed.")


def derive(master, node_id):
    return generate.hkdf_sha256(master, generate.KDF_SALT, generate.kdf_info(node_id),
                                generate.KDF_LEN)


def render(node_id, key):
    """The secrets.h block, in the template's layout: eight bytes to a line."""
    rows = []
    for i in range(0, len(key), 8):
        rows.append("    " + ", ".join("0x%02x" % b for b in key[i:i + 8]))
    body = ", \\\n".join(rows)
    return "#define %s { \\\n%s }" % (FIELDS[node_id], body)


def provision(text, node_id):
    master = parse_master(text)
    refuse(master)
    return render(node_id, derive(master, node_id))


def self_test():
    failures = []

    def expect(label, ok):
        if not ok:
            failures.append(label)

    def secrets_text(master):
        rows = ", \\\n    ".join(", ".join("0x%02x" % b for b in master[i:i + 8])
                                 for i in range(0, 32, 8))
        return "// header\n#define LRAN_MASTER_KEY { \\\n    %s }\n#define OTA \"x\"\n" % rows

    # The tool's path end to end, parser included, against every W4 kdf vector it can
    # provision. The test master is refused by provision(), so derive() is called here.
    doc = json.loads((VECTORS / "vectors_kdf.json").read_text(encoding="utf-8"))
    checked = 0
    for v in doc["vectors"]:
        if "node_key" not in v:
            continue
        master = parse_master(secrets_text(bytes.fromhex(v["master_key"])))
        got = derive(master, int(v["node_id"], 16)).hex()
        expect("vector %s" % v["name"], got == v["node_key"])
        checked += 1
    expect("at least the three required kdf vectors", checked >= 3)

    # A real-looking master provisions, and the output carries no byte run of it.
    master = bytes(range(0x20, 0x40))
    out = provision(secrets_text(master), 0x01)
    expect("output names the GateLink field", out.startswith("#define LRAN_GATELINK_NODE_KEY {"))
    flat = "".join(re.findall(r"0x([0-9a-f]{2})", out))
    expect("output is the derived key", flat == derive(master, 0x01).hex())
    expect("output does not carry the master", master.hex()[:16] not in flat)

    # A commented-out define above the real one is not read.
    text = "// #define LRAN_MASTER_KEY { 0x11 }\n" + secrets_text(master)
    expect("commented-out define ignored", parse_master(text) == master)

    for label, text in [
        ("placeholder refused", secrets_text(bytes(32))),
        ("test master refused", secrets_text(generate.MASTER_KEY)),
        ("short key refused", "#define LRAN_MASTER_KEY { 0x01, 0x02 }\n"),
        ("missing define refused", "#define WIFI_SSID \"x\"\n"),
    ]:
        try:
            provision(text, 0x01)
            expect(label, False)
        except ProvisionError:
            pass

    for f in failures:
        print("SELF-TEST FAILED: " + f)
    if failures:
        return 1
    print("OK - %d W4 kdf vectors reproduced; refusals behave" % checked)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--node", default="0x01",
                    help="node ID, hex: %s (default 0x01)"
                    % ", ".join("0x%02x" % n for n in FIELDS))
    ap.add_argument("--secrets", type=pathlib.Path, default=ROOT / "secrets.h",
                    help="the secrets.h holding LRAN_MASTER_KEY (default: repo root)")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    node_id = int(args.node, 16)
    if node_id not in FIELDS:
        print("node_key: 0x%02x is not provisioned with its own key. Nodes: %s"
              % (node_id, ", ".join("0x%02x" % n for n in FIELDS)), file=sys.stderr)
        return 2
    try:
        text = args.secrets.read_text(encoding="utf-8")
        print(provision(text, node_id))
    except (OSError, ProvisionError) as e:
        print("node_key: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
