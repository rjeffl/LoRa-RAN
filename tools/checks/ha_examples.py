#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# The committed discovery examples under ha/discovery/ against what the firmware would
# actually publish. Task BF-23; Impl Plan 4.4.
#
# WHY THIS EXISTS RATHER THAN TRUST. Impl Plan 4.4 asks for example payloads "for
# reference and for bench testing without a running HA", and an example nothing reads
# drifts from the code the first time a table row changes - silently, and in the one
# artifact somebody reaches for when Home Assistant is not cooperating. This is the
# check that falsifies "the examples match the firmware" (root CLAUDE.md: a load-bearing
# premise must name the check that would falsify it).
#
# IT COMPILES discovery.cpp ITSELF. tools/ha/dump_discovery.cpp links the firmware's own
# sources, so there is no second implementation to keep in step - the examples are an
# output of the code under test, not a description of it.
#
# A FAILURE HERE IS USUALLY NOT A BUG. It means a discovery table changed and the
# examples were not regenerated. Regenerate with --write, read the diff, and commit it
# with the change that caused it - that diff is the record of what Home Assistant will
# see differently, which is worth reading before a fleet does.

import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXAMPLES = os.path.join(ROOT, "ha", "discovery")

# The same sources the native environment builds, plus the dumper's own main().
SOURCES = [
    "tools/ha/dump_discovery.cpp",
    "firmware/bridge/src/discovery.cpp",
    "firmware/bridge/src/command.cpp",
    "firmware/bridge/src/net_policy.cpp",
    "firmware/bridge/src/registry.cpp",
    "firmware/bridge/src/rx_ladder.cpp",
]
SOURCE_GLOBS = [
    "lib/lran-protocol/src",
    "lib/lran-protocol/src/schema",
    "lib/lran-protocol/platform/native",
]
INCLUDES = [
    "firmware/bridge/src",
    "lib/lran-protocol/include",
    "lib/lran-link/include",
    "lib/lran-config/include",
]


def sources():
    out = [os.path.join(ROOT, s) for s in SOURCES]
    for d in SOURCE_GLOBS:
        full = os.path.join(ROOT, d)
        for name in sorted(os.listdir(full)):
            if name.endswith(".cpp"):
                out.append(os.path.join(full, name))
    return out


def build(tmp):
    """Compiles the dumper. Returns its path, or exits with the compiler's message."""
    exe = os.path.join(tmp, "dump_discovery")
    cmd = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-o", exe]
    for inc in INCLUDES:
        cmd.append("-I" + os.path.join(ROOT, inc))
    cmd += sources()
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("ERR the dumper did not build:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)
    return exe


def generate(tmp):
    """Every config the firmware would publish, as {filename: text}."""
    out = os.path.join(tmp, "out")
    os.makedirs(out)
    exe = build(tmp)
    r = subprocess.run([exe, out], capture_output=True, text=True)
    if r.returncode != 0:
        print("ERR the dumper failed:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)
    return {n: open(os.path.join(out, n)).read() for n in sorted(os.listdir(out))}


def main(argv=None):
    ap = argparse.ArgumentParser(description="ha/discovery/ against the firmware")
    ap.add_argument("--write", action="store_true",
                    help="regenerate the committed examples instead of checking them")
    args = ap.parse_args(argv)

    with tempfile.TemporaryDirectory() as tmp:
        fresh = generate(tmp)

    if args.write:
        os.makedirs(EXAMPLES, exist_ok=True)
        for name in sorted(os.listdir(EXAMPLES)):
            if name.endswith(".json") and name not in fresh:
                os.remove(os.path.join(EXAMPLES, name))
        for name, text in fresh.items():
            with open(os.path.join(EXAMPLES, name), "w") as f:
                f.write(text)
        print("wrote %d configs to ha/discovery/" % len(fresh))
        return 0

    if not os.path.isdir(EXAMPLES):
        print("ERR ha/discovery/ does not exist - run with --write", file=sys.stderr)
        return 1

    committed = {n: open(os.path.join(EXAMPLES, n)).read()
                 for n in sorted(os.listdir(EXAMPLES)) if n.endswith(".json")}

    problems = []
    for name in sorted(set(fresh) | set(committed)):
        if name not in committed:
            problems.append("missing from ha/discovery/: %s" % name)
        elif name not in fresh:
            problems.append("committed but no longer produced: %s" % name)
        elif committed[name] != fresh[name]:
            problems.append("differs from the firmware: %s\n    committed: %s\n    "
                            "firmware:  %s" % (name, committed[name].strip(),
                                               fresh[name].strip()))

    if problems:
        print("ERR ha/discovery/ does not match the firmware:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        print("\nRegenerate: python3 tools/checks/ha_examples.py --write", file=sys.stderr)
        return 1

    print("OK - %d discovery configs match the firmware" % len(fresh))
    return 0


if __name__ == "__main__":
    sys.exit(main())
