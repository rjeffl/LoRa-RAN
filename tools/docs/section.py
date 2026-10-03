#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Print one section of a Markdown document, or its outline.
#
#   section.py DOC                 outline: line, size and heading of every section
#   section.py DOC 7.2             the section numbered 7.2, down to the next heading
#                                  at its level or above
#   section.py DOC "BMS block"     the first heading containing that text
#   section.py DOC 7.2 --shallow   stop at the first subheading instead
#
# WHY. The bridge plan is 280 KB and the specification 241 KB. Sessions found a section
# by grepping for its heading and then guessing a sed line range, which cost a call per
# guess and often pulled in the neighbouring section too. A heading is the unit the
# documents cite, so the tool takes a heading.
#
# Fenced code blocks are skipped when looking for headings, because a "# comment" inside
# one is not a heading.

import argparse
import re
import sys

HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE = re.compile(r"^\s*(```|~~~)")
NUMBER = re.compile(r"^§?\s*(\d+(?:\.\d+)*)\.?(?:\s|$)")


def headings(lines):
    """Yield (index, level, text) for each heading outside a fenced block."""
    fenced = False
    for i, line in enumerate(lines):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced:
            continue
        m = HEADING.match(line)
        if m:
            yield i, len(m.group(1)), m.group(2)


def find(heads, query):
    """Return the position in heads of the heading that query names, or None.

    A query that is a section number ("7.2", "§7.2") matches a heading that starts with
    that number exactly, so 7.2 never matches 7.20. Any other query matches the first
    heading containing it, ignoring case.
    """
    m = NUMBER.match(query.strip())
    if m:
        want = m.group(1)
        for k, (_, _, text) in enumerate(heads):
            n = NUMBER.match(text)
            if n and n.group(1) == want:
                return k
    q = query.strip().lower()
    for k, (_, _, text) in enumerate(heads):
        if q in text.lower():
            return k
    return None


def extent(heads, k, total, shallow=False):
    """Return the [start, end) line range of the section at heads[k]."""
    start, level, _ = heads[k]
    for i, lvl, _ in heads[k + 1:]:
        if lvl <= level or shallow:
            return start, i
    return start, total


def outline(lines, heads):
    out = []
    for k, (i, level, text) in enumerate(heads):
        s, e = extent(heads, k, len(lines), shallow=True)
        size = sum(len(x) for x in lines[s:e])
        out.append(f"{i + 1:6d} {size / 1024:6.1f}K {'  ' * (level - 1)}{text}")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else None)
    ap.add_argument("doc")
    ap.add_argument("query", nargs="?")
    ap.add_argument("--shallow", action="store_true",
                    help="stop at the first subheading")
    a = ap.parse_args(argv)
    with open(a.doc, encoding="utf-8") as f:
        lines = f.readlines()
    heads = list(headings(lines))
    if a.query is None:
        print("\n".join(outline(lines, heads)))
        return 0
    k = find(heads, a.query)
    if k is None:
        print(f"section.py: no heading matches {a.query!r} in {a.doc}", file=sys.stderr)
        return 1
    s, e = extent(heads, k, len(lines), a.shallow)
    print(f"[{a.doc}:{s + 1}-{e}]")
    sys.stdout.write("".join(lines[s:e]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
