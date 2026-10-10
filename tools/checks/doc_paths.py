#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
"""Every live citation of a document path names a file that exists.

On 2026-10-03, three citations in the protocol specification, two in the System PRD, one
in the Decision Register and two in the GateLink PRD named paths under `/docs/` that had
never existed. Nothing caught them: a link is not compiled, and a reader who follows one
to nothing assumes the file was never written. The bms-protocol path had been reported a
day before, and was deferred because fixing the spec costs a version bump. This check
moves the cost to the commit that moves or renames a document, which is the cheapest place
to pay it.

WHAT THIS CHECKS, in every tracked text file outside the records listed below:

  - Markdown link targets, `[text](target)`, resolved from the linking file's directory.
    Links with a scheme (`https:`, `mailto:`) and bare anchors are skipped.
  - Path tokens ending `.md` under a top-level directory (`docs/`, `lib/`, `firmware/`,
    `tools/`, `ha/`, `.github/`), with or without a leading `/`, in prose and code
    comments alike. `/docs/x.md` means `docs/x.md` from the repository root.

WHAT IT SKIPS, because these keep their wording by rule (root CLAUDE.md, Writing):

  - `docs/archive/`, engineering logs, and `wattcycle-reader/`, whose paths are relative
    to its own project and whose documents are versioned records.
  - `doc-findings.md` files, which name broken references because that is their job.
  - This file, whose examples and HISTORICAL entries name paths that do not exist.
  - Documents whose header says `**Status:** **Superseded`.
  - Inside a Markdown document, any section whose heading says `Changelog` or
    `superseded`, down to the next heading of the same or higher level.
  - The (file, path) pairs in HISTORICAL, each with its reason.

A planned document is listed in PLANNED with what writes it. The check fails when a
planned file exists, so the entry is dropped when the file is written.

A FAILURE NAMES the citing line and, when a file of that name exists elsewhere, where it
is now. Fix the citation. Do not add to HISTORICAL to make a live citation pass.
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent

TEXT_SUFFIXES = {".md", ".c", ".cpp", ".h", ".hpp", ".py", ".ini", ".yml", ".yaml",
                 ".json", ".sh", ".txt"}

# Planned documents: path -> what writes it.
PLANNED = {
    "docs/gatelink/1050-config.md": "GateLink task GL2 (Implementation Plan §7.4)",
}

# Citations of a path that is gone, kept on purpose: (citing file, cited path) -> reason.
HISTORICAL = {
    ("docs/shared/LRAN-Protocol-Specification.md", "/docs/protocol-changelog.md"):
        "§13.2's v0.12 correction note quotes the path it corrected",
}

LINK = re.compile(r"\]\(([^)\s]+)\)")
TOKEN = re.compile(r"(?<![\w./<>-])(/?(?:docs|lib|firmware|tools|ha|\.github)/[\w./-]*?\.md)\b")
HEADING = re.compile(r"^(#+)\s+(.*)")
SKIP_SECTION = re.compile(r"changelog|superseded", re.IGNORECASE)


def skipped_file(rel):
    parts = rel.split("/")
    return (rel.startswith("docs/archive/") or rel.startswith("wattcycle-reader/")
            or parts[-1].startswith("engineering-log") or parts[-1] == "doc-findings.md"
            or rel == "tools/checks/doc_paths.py")


def live_lines(rel, text):
    """(line number, line) for every line not in a skipped section."""
    lines = text.splitlines()
    if not rel.endswith(".md"):
        return list(enumerate(lines, 1))
    if any(l.startswith("**Status:** **Superseded") for l in lines[:12]):
        return []
    out, skip_level, fence = [], None, False
    for n, line in enumerate(lines, 1):
        if line.startswith("```"):
            fence = not fence
        m = None if fence else HEADING.match(line)
        if m:
            level = len(m.group(1))
            if skip_level is not None and level <= skip_level:
                skip_level = None
            if skip_level is None and SKIP_SECTION.search(m.group(2)):
                skip_level = level
        if skip_level is None:
            out.append((n, line))
    return out


def exists(rel_dir, target, from_root):
    if from_root and (ROOT / target.lstrip("/")).exists():
        return True
    return (ROOT / rel_dir / target).exists()


def suggest(target, by_name):
    found = by_name.get(pathlib.PurePosixPath(target).name, [])
    return " - did you mean %s?" % " or ".join(found) if found else ""


def main():
    tracked = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True,
                             text=True, check=True).stdout.split()
    by_name = {}
    for rel in tracked:
        by_name.setdefault(pathlib.PurePosixPath(rel).name, []).append(rel)

    failures, historical_seen = [], set()
    for path, owner in PLANNED.items():
        if (ROOT / path).exists():
            failures.append("%s is written now: drop it from PLANNED (%s)" % (path, owner))

    for rel in tracked:
        if pathlib.PurePosixPath(rel).suffix not in TEXT_SUFFIXES or skipped_file(rel):
            continue
        try:
            text = (ROOT / rel).read_text(encoding="utf-8")
        except (UnicodeDecodeError, FileNotFoundError):
            continue
        rel_dir = str(pathlib.PurePosixPath(rel).parent)
        for n, line in live_lines(rel, text):
            cited = []
            if rel.endswith(".md"):
                for m in LINK.finditer(line):
                    target = m.group(1).split("#")[0]
                    if target and not re.match(r"[a-z][a-z0-9+.-]*:", target):
                        cited.append((target, False))
            cited += [(m.group(1), True) for m in TOKEN.finditer(line)]
            for target, from_root in cited:
                if exists(rel_dir, target, from_root) or target.lstrip("/") in PLANNED:
                    continue
                if (rel, target) in HISTORICAL:
                    historical_seen.add((rel, target))
                    continue
                failures.append("%s:%d cites %s, which does not exist%s"
                                % (rel, n, target, suggest(target, by_name)))

    for key in HISTORICAL:
        if key not in historical_seen:
            failures.append("HISTORICAL entry %s -> %s is no longer cited: drop it" % key)

    if failures:
        print("FAIL: %d document path citation(s) name no file:" % len(failures))
        for f in failures:
            print("  " + f)
        print("\nFix the citation where the document moved. A dated record keeps its"
              " wording: put it in a skipped section, or in HISTORICAL with the reason.")
        return 1
    print("ok: every live document path citation names a file")
    return 0


if __name__ == "__main__":
    sys.exit(main())
