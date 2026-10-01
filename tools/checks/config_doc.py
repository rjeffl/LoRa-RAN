#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# docs/gatelink/gatelink-config.md against /lib/lran-config/'s table. Task L4; GateLink
# Impl Plan 6.4 and Protocol Library Plan 4 (D44).
#
# THIS IS THE CHECK THAT FALSIFIES "the document matches the firmware". The document is
# printed by tools/config/dump_config_doc.cpp, which includes table.h itself, so there is
# no second description of a parameter to keep in step.
#
# A FAILURE HERE IS USUALLY NOT A BUG. It means a table row changed and the document was
# not regenerated. Regenerate with --write, read the diff, and commit it with the change
# that caused it.

import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOC = os.path.join(ROOT, "docs", "gatelink", "gatelink-config.md")
SOURCE = os.path.join(ROOT, "tools", "config", "dump_config_doc.cpp")
INCLUDES = ["lib/lran-protocol/include", "lib/lran-config/include"]


def generate(tmp):
    """The document the table produces, or exits with the compiler's message."""
    exe = os.path.join(tmp, "dump_config_doc")
    cmd = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
           "-o", exe, SOURCE]
    for inc in INCLUDES:
        cmd.append("-I" + os.path.join(ROOT, inc))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("ERR the dumper did not build:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)
    r = subprocess.run([exe], capture_output=True, text=True)
    if r.returncode != 0:
        print("ERR the dumper failed:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)
    return r.stdout


def main(argv=None):
    ap = argparse.ArgumentParser(description="gatelink-config.md against the table")
    ap.add_argument("--write", action="store_true",
                    help="regenerate the committed document instead of checking it")
    args = ap.parse_args(argv)

    with tempfile.TemporaryDirectory() as tmp:
        fresh = generate(tmp)

    rel = os.path.relpath(DOC, ROOT)
    if args.write:
        with open(DOC, "w") as f:
            f.write(fresh)
        print("wrote %s" % rel)
        return 0

    if not os.path.exists(DOC):
        print("ERR %s does not exist - run with --write" % rel, file=sys.stderr)
        return 1
    with open(DOC) as f:
        committed = f.read()
    if committed != fresh:
        print("ERR %s does not match lib/lran-config's table." % rel, file=sys.stderr)
        print("Regenerate: python3 tools/checks/config_doc.py --write", file=sys.stderr)
        return 1

    print("OK - %s matches the table" % rel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
