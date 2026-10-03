#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
#
# Tests for section.py. Run directly; CI's checks job runs it.

import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import section  # noqa: E402

DOC = """# Title

## 7. Payload schemas

### 7.2 Schema `0x10`

```
# not a heading
```

#### 7.2.1 Gate block

gate text

### 7.20 Not 7.2

## 8. Next
"""


class SectionTest(unittest.TestCase):
    def setUp(self):
        self.lines = DOC.splitlines(keepends=True)
        self.heads = list(section.headings(self.lines))

    def test_fenced_comment_is_not_a_heading(self):
        self.assertNotIn("not a heading", [t for _, _, t in self.heads])

    def test_number_matches_exactly(self):
        k = section.find(self.heads, "7.2")
        self.assertEqual(self.heads[k][2], "7.2 Schema `0x10`")
        k = section.find(self.heads, "§7.20")
        self.assertEqual(self.heads[k][2], "7.20 Not 7.2")

    def test_text_query(self):
        k = section.find(self.heads, "gate BLOCK")
        self.assertEqual(self.heads[k][2], "7.2.1 Gate block")
        self.assertIsNone(section.find(self.heads, "absent"))

    def test_extent_includes_subsections_and_stops_at_sibling(self):
        k = section.find(self.heads, "7.2")
        s, e = section.extent(self.heads, k, len(self.lines))
        body = "".join(self.lines[s:e])
        self.assertIn("gate text", body)
        self.assertNotIn("7.20", body)

    def test_shallow_stops_at_subheading(self):
        k = section.find(self.heads, "7.2")
        s, e = section.extent(self.heads, k, len(self.lines), shallow=True)
        self.assertNotIn("Gate block", "".join(self.lines[s:e]))

    def test_last_section_runs_to_end(self):
        k = section.find(self.heads, "8")
        self.assertEqual(section.extent(self.heads, k, len(self.lines))[1], len(self.lines))

    def test_main_exit_codes(self):
        with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False) as f:
            f.write(DOC)
        try:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(section.main([f.name, "7.2.1"]), 0)
            self.assertIn("gate text", out.getvalue())
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(section.main([f.name, "99"]), 1)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(section.main([f.name]), 0)
        finally:
            os.unlink(f.name)


if __name__ == "__main__":
    unittest.main()
