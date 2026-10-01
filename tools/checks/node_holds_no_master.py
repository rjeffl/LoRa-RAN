#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""No firmware but the bridge and the simnode reads LRAN_MASTER_KEY - spec 9.1.

GateLink Impl Plan 6.8: a node holds its own derived key and never the master, so a node
in the wrong hands yields that node's key alone. Every firmware includes the one root
secrets.h, which defines the master for the bridge, so nothing in the build stops a node
reading it. One line in a node's main.cpp would put the fleet's key on a board bolted to
a gate post, and the node would work exactly as before.

Root CLAUDE.md: a load-bearing premise must name the check that would falsify it. This
is that check.

WHAT THIS CHECKS. With comments and string literals removed, no file under firmware/
names LRAN_MASTER_KEY, except in the firmwares ALLOWED lists. A banner string or a
comment explaining the rule does not count.

WHAT THIS DOES NOT CHECK. That the master is absent from a built image, or that a node
cannot reach it through a macro named otherwise. It reads text. It is a tripwire on the
shape of the mistake, not a proof.

    python3 tools/checks/node_holds_no_master.py
    python3 tools/checks/node_holds_no_master.py --self-test
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
FIRMWARE = ROOT / "firmware"
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from chan_capture_never_transmits import strip_comments_and_strings  # noqa: E402

# The firmwares that hold the master, and why. Nothing else is added without a decision.
ALLOWED = {
    "bridge": "derives every node's key on demand (spec 9.1)",
    "simnode": "bench convenience; a real node never does (Bridge Impl Plan 10.3)",
}
EXTS = (".cpp", ".h", ".c", ".hpp", ".ini")
MASTER = re.compile(r"(?:\b|(?<=-D))LRAN_MASTER_KEY\b")  # -DLRAN_... has no \b


def findings_in(name, text):
    # platformio.ini comments are ';' or '#' lines; C comments do not apply there.
    if name.endswith(".ini"):
        code = "\n".join("" if l.lstrip().startswith((";", "#")) else l.split(";")[0]
                         for l in text.splitlines())
    else:
        code = strip_comments_and_strings(text)
    return ["%s:%d" % (name, n) for n, line in enumerate(code.splitlines(), 1)
            if MASTER.search(line)]


def check(firmware=FIRMWARE):
    found = []
    for project in sorted(p for p in firmware.iterdir() if p.is_dir()):
        if project.name in ALLOWED:
            continue
        for p in sorted(project.rglob("*")):
            if p.suffix not in EXTS or ".pio" in p.parts or not p.is_file():
                continue
            found += findings_in(str(p.relative_to(ROOT)), p.read_text(errors="replace"))
    return found


def self_test():
    cases = [
        ("the master read", "const uint8_t m[] = LRAN_MASTER_KEY;", True),
        ("a test on the macro", "#if defined(LRAN_MASTER_KEY)", True),
        ("a build flag", "build_flags = -DLRAN_MASTER_KEY=1", True),
        ("in a comment", "// never read LRAN_MASTER_KEY here", False),
        ("in a block comment", "/* LRAN_MASTER_KEY\n */ int x;", False),
        ("in a string", "Serial.println(\"no LRAN_MASTER_KEY on this node\");", False),
        ("the node key", "const uint8_t k[] = LRAN_GATELINK_NODE_KEY;", False),
        ("a longer name", "int LRAN_MASTER_KEY_LEN_X = 0;", False),
    ]
    failed = 0
    for label, text, should_fail in cases:
        name = "fixture.ini" if text.startswith("build_flags") else "fixture.cpp"
        if bool(findings_in(name, text)) != should_fail:
            failed += 1
            print("SELF-TEST FAILED: %s - expected %s"
                  % (label, "a finding" if should_fail else "none"))
    ini = "; LRAN_MASTER_KEY is the bridge's\n[env]\n"
    if findings_in("fixture.ini", ini):
        failed += 1
        print("SELF-TEST FAILED: an .ini comment - expected none")
    if failed:
        return 1
    print("OK - %d fixtures behave" % (len(cases) + 1))
    return 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
    found = check()
    if found:
        print("Only %s may read LRAN_MASTER_KEY (spec 9.1, GateLink Impl Plan 6.8):"
              % " and ".join(sorted(ALLOWED)))
        for f in found:
            print("  " + f)
        print("A node takes its own key, LRAN_GATELINK_NODE_KEY for GateLink.")
        return 1
    print("OK - no firmware outside %s reads LRAN_MASTER_KEY" % ", ".join(sorted(ALLOWED)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
