#!/usr/bin/env python3
"""Every live "binding protocol" citation names the specification's current version.

The specification reached v0.9 on 2026-09-06 and thirteen citations across the
repository still said v0.8 two days later. Nothing caught it, and nothing could
have: v0.9 changed no frame layout, header field, enumeration value, schema or
authentication scope, so no test failed and no W4 vector regenerated. The version
number was the only signal, and root CLAUDE.md tells a reader to check it by hand.

A stale citation is not cosmetic. The rule it feeds is "a node document citing an
older protocol version than the specification's own has not been reconciled with
the intervening revisions - say so rather than building against it." A document
that is reconciled but still says v0.8 trains people to ignore that rule, which is
worse than the drift itself.

WHAT THIS CHECKS. Lines that assert what a target is built against - "Binding
protocol:", "Binding specification:", "Binding spec:", the root CLAUDE.md's
"Currently vX.Y", and the System PRD's document-set row. Each must name the
version in the specification's own header, and each that also states a wire
version must name the one in that header.

WHAT IT DOES NOT CHECK. Prose that mentions a version in passing, changelog
entries, engineering-log entries and anything under docs/archive/ - those are
dated records of what was true when written, and correcting one by rewriting it
destroys the thing that made it useful. Adding a citation in a new shape adds it
here too; the trigger list is deliberately explicit rather than a guess at intent.

    python3 tools/checks/spec_citation_version.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SPEC = ROOT / "docs" / "shared" / "LRAN-Protocol-Specification.md"

SCAN_EXTS = {".md", ".ini", ".cpp", ".h", ".hpp", ".py"}
SKIP_DIRS = {".git", ".pio", "node_modules"}
# Dated records and superseded revisions. See the module docstring.
SKIP_PATHS = (
    pathlib.Path("docs/archive"),
    pathlib.Path("docs/rangetest/engineering-log.md"),
    pathlib.Path("docs/protocol-lib/engineering-log.md"),
    # Superseded decision briefs. Each cites the specification it was written against,
    # and the register row that superseded it is the live record; re-stamping a brief
    # would claim it had been reconciled with a revision it predates.
    pathlib.Path("docs/shared/LRAN-D1-PHY-Decision-Brief.md"),
    pathlib.Path("docs/shared/LRAN-P8-CommandGate-Brief.md"),
    pathlib.Path("docs/shared/LRAN-Spec-v0.12-Brief.md"),
    pathlib.Path("docs/shared/LRAN-D1-Frequency-Change-Brief.md"),
    pathlib.Path("docs/shared/LRAN-Config-Set-Brief.md"),
    # A measurement record reads one run against the specification of its day and
    # decides nothing, so the same reasoning applies to it.
    pathlib.Path("docs/shared/LRAN-D1-Parallel-Capture-Analysis.md"),
)

# A citation asserts what something is built against. Each pattern captures the
# version it names in group "ver".
CITATION_PATTERNS = (
    # **Binding protocol:** [...](...) **v0.14**   /   `...` v0.13 (`ver = 2`)
    re.compile(r"Binding (?:protocol|specification|spec)\b[^\n]*?v(?P<ver>\d+\.\d+)", re.I),
    # Root CLAUDE.md's document table: **Currently v0.14, `ver = 2`**
    re.compile(r"Currently\s+\*{0,2}v(?P<ver>\d+\.\d+)", re.I),
    # System PRD §12 and HANDOFF: `LRAN-Protocol-Specification` is v0.14
    re.compile(r"LRAN-Protocol-Specification`?\*{0,2}\s+is\s+\*{0,2}v(?P<ver>\d+\.\d+)", re.I),
    # System PRD §12 document-set row.
    re.compile(
        r"LRAN-Protocol-Specification\.md\)[^\n|]*\|[^\n|]*\|\s*\*{0,2}v(?P<ver>\d+\.\d+)"
    ),
)

# A citation may also state the wire version. Checked only where present.
WIRE_PATTERN = re.compile(r"ver\s*=\s*(?P<wire>\d+)")

# A changelog section describes past citation moves - "Binding protocol citation
# moves v0.2 -> v0.6" - and is a dated record like any other. The whole section is
# skipped rather than the bullet line, because entries wrap and the version often
# lands on a continuation line. Skipping the file instead would lose the header
# citation that sits above the changelog in the same document.
HEADING = re.compile(r"^(?P<hashes>#{1,6})\s+(?P<title>.*)$")
CHANGELOG_TITLE = re.compile(r"changelog\b", re.I)


def spec_versions() -> tuple[str, str]:
    """Return (document version, wire version) from the specification's header."""
    head = SPEC.read_text(errors="replace").split("---", 1)[0]
    doc = re.search(r"^\*\*Version:\*\*\s*(\d+\.\d+)", head, re.M)
    wire = re.search(r"^\*\*Protocol version on the wire:\*\*\s*`?ver\s*=\s*(\d+)", head, re.M)
    if not doc or not wire:
        sys.exit(f"FAIL: cannot read version header from {SPEC.relative_to(ROOT)}")
    return doc.group(1), wire.group(1)


def skipped(rel: pathlib.Path) -> bool:
    return any(rel == p or p in rel.parents for p in SKIP_PATHS)


def main() -> int:
    want_doc, want_wire = spec_versions()
    spec_rel = SPEC.relative_to(ROOT)
    stale: list[str] = []
    checked = 0

    for path in ROOT.rglob("*"):
        if path.suffix not in SCAN_EXTS or not path.is_file():
            continue
        if SKIP_DIRS & set(path.parts):
            continue
        rel = path.relative_to(ROOT)
        if rel == spec_rel or skipped(rel):
            continue

        in_changelog_at = 0  # heading depth of the changelog section, 0 when outside
        for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            heading = HEADING.match(line)
            if heading:
                depth = len(heading.group("hashes"))
                if CHANGELOG_TITLE.search(heading.group("title")):
                    in_changelog_at = depth
                elif in_changelog_at and depth <= in_changelog_at:
                    in_changelog_at = 0
            if in_changelog_at:
                continue
            match = next((m for p in CITATION_PATTERNS if (m := p.search(line))), None)
            if not match:
                continue
            checked += 1
            found_doc = match.group("ver")
            wire = WIRE_PATTERN.search(line)
            if found_doc != want_doc:
                stale.append(f"{rel}:{n}: cites v{found_doc}, specification is v{want_doc}")
            elif wire and wire.group("wire") != want_wire:
                stale.append(
                    f"{rel}:{n}: cites ver = {wire.group('wire')}, specification is ver = {want_wire}"
                )

    if stale:
        print(f"FAIL: binding citations disagree with {spec_rel} (v{want_doc}, ver = {want_wire}):")
        for s in stale:
            print("  " + s)
        print(
            "\nReconcile the document with the intervening revisions, THEN update the"
            "\ncitation. Bumping the number alone is the failure this check exists to"
            "\nmake visible. Record what the document inherits in its changelog."
        )
        return 1

    print(f"OK - {checked} binding citations name v{want_doc} (ver = {want_wire})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
