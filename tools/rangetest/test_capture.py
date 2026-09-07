#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 <holder>          # D31 open - see LRAN-Decision-Register
"""Host tests for capture.py.

Run with PlatformIO's python, which is the one that has pyserial - capture.py exits
at import without it:

    ~/.platformio/penv/bin/python tools/rangetest/test_capture.py

WHY THIS FILE EXISTS. capture.py held the serial port open for 3.5 s during role
selection and never read it, so ~19 kB of a boot-time survey dump went into a tty
buffer that overflowed and dropped it. That destroyed two of seven sites in the
2026-09-05 campaign, and because the loss was deterministic it read as a firmware
fault for two days. No test in the repo could have caught it: every test covered the
firmware, and none covered the tool that turns the firmware's output into evidence.

So the invariant under test is small and blunt: THE BOOT WINDOW MUST READ THE PORT.
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture  # noqa: E402


class FakeSerial:
    """A board that talks during the boot window, and counts who listened."""

    def __init__(self, chunks):
        self._chunks = list(chunks)
        self.written = b""
        self.reads = 0
        self.flushes = 0

    def write(self, b):
        self.written += b

    def flush(self):
        self.flushes += 1

    def read(self, _n):
        self.reads += 1
        return self._chunks.pop(0) if self._chunks else b""


class FakeClock:
    """Injected time, so a 3.5 s window costs nothing to test."""

    def __init__(self):
        self.t = 0.0

    def now(self):
        return self.t

    def sleep(self, dt):
        self.t += dt


FAILED = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok   {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name}  {detail}")


def test_boot_window_drains_the_port():
    """The regression. Bytes arriving during role selection must be returned."""
    clock = FakeClock()
    ser = FakeSerial([b"# settings\n", b"row1\n", b"row2\n"])
    out = capture.drive_boot_window(ser, b"v", now=clock.now, sleep=clock.sleep)
    check("boot window reads the port", ser.reads > 0,
          "the blind time.sleep loop is back")
    check("boot window returns what it read",
          b"".join(out) == b"# settings\nrow1\nrow2\n",
          f"got {b''.join(out)!r}")


def test_boot_window_drains_with_no_role_too():
    """--reset without --role was blind in exactly the same way."""
    clock = FakeClock()
    ser = FakeSerial([b"boot output\n"])
    out = capture.drive_boot_window(ser, None, now=clock.now, sleep=clock.sleep)
    check("no-role window still reads", ser.reads > 0)
    check("no-role window returns bytes", b"".join(out) == b"boot output\n")
    check("no-role window writes nothing", ser.written == b"",
          f"wrote {ser.written!r} with no role to select")


def test_role_key_is_sent_repeatedly_across_the_window():
    """A single timed write races a boot of non-fixed length and is simply lost.

    The board then comes up INITIATOR, which in survey mode is a board that
    transmits. Observed exactly that way on hardware.
    """
    clock = FakeClock()
    ser = FakeSerial([])
    capture.drive_boot_window(ser, b"v", now=clock.now, sleep=clock.sleep)
    check("role key sent more than once", ser.written.count(b"v") > 1,
          f"sent {ser.written.count(b'v')} times")
    check("role key is the only thing written", set(ser.written) == {ord("v")})


def test_boot_window_spans_the_selection_window():
    """Shorter than the firmware's 3 s window and the role can be missed entirely."""
    clock = FakeClock()
    ser = FakeSerial([])
    capture.drive_boot_window(ser, b"v", now=clock.now, sleep=clock.sleep)
    check("window covers the firmware's 3 s role window", clock.t >= 3.0,
          f"window was {clock.t:.2f}s")


def test_completion_markers_are_matched_as_substrings():
    """Wording a new firmware message carelessly truncates a capture.

    R11 added '# survey RUNNING/HELD' lines; none may contain a marker.
    """
    r11_lines = [
        "# survey RUNNING at site 3 welllink-well",
        "# survey HELD - walk to site 4 irrigation-pump, then press PRG to start"
        " the dwell",
        "# resuming campaign at site 6 propane-tank - HELD, press PRG to start the"
        " dwell",
        "# hold_discipline=1",
    ]
    for line in r11_lines:
        hit = [m for m in capture.COMPLETION_MARKERS if m in line]
        check(f"no marker hidden in {line[:34]!r}...", not hit, f"matched {hit}")


def test_trace_counts_malformed_rows_rather_than_dropping_them(tmp):
    """Repo rule 4 applied to the tool: a silent discard is how 325 rows vanished."""
    t = capture.Trace(str(tmp), note=None)
    t.open("site_index,site_name,bin_index", [])
    t.row("0,bridge-house,0")
    t.malformed += 1                      # what the parser does on a short row
    t.close("test")
    text = tmp.read_text()
    check("malformed count reaches the trace", "1 malformed data line(s)" in text,
          text)
    check("the warning says the trace has holes", "holes" in text)


def test_trace_without_malformed_rows_carries_no_warning(tmp):
    t = capture.Trace(str(tmp), note=None)
    t.open("site_index,site_name,bin_index", [])
    t.row("0,bridge-house,0")
    t.close("test")
    text = tmp.read_text()
    check("clean trace has no WARNING", "WARNING" not in text, text)
    check("clean trace still says how it ended", "capture ended: test" in text)


def test_pa_config_lines_reach_the_trace_header(tmp):
    """Handoff 6 requirement 7 - the boot PA record has to survive the tool.

    The firmware prints these AFTER radio begin() but BEFORE the CSV header, which
    is the window where capture.py collects `key=value` lines into `meta`. Nothing
    else enforces that: a line that fails SETTING_RE is counted as unparsed and
    silently absent from the trace, which is the same class of quiet loss that ate
    325 survey rows. The C++ side asserts the format (test_pa_config); this asserts
    the tool accepts it and writes it out.

    Transcribed from format_pa_config() in firmware/range-test/src/pa_config.cpp.
    """
    emitted = ["pa_optimize=1", "pa_duty_cycle=1", "pa_hp_max=2", "pa_val=3",
               "pa_table=RadioLib-7.7.1-paOptTable"]
    for line in emitted:
        check(f"capture.py collects {line!r}",
              capture.SETTING_RE.match(line) is not None,
              "SETTING_RE rejected it - the line would be dropped as unparsed")

    # The out-of-range form, which is the one a reader most needs to see.
    check("capture.py collects 'pa_entry=none'",
          capture.SETTING_RE.match("pa_entry=none") is not None)

    t = capture.Trace(str(tmp), note=None)
    t.open("tp_index,freq_hz,sf", emitted)
    t.row("0,915000000,7")
    t.close("test")
    text = tmp.read_text()
    for line in emitted:
        check(f"{line!r} is written into the trace header", f"# {line}\n" in text,
              text)
    # Above the data, not somewhere after it: a configuration record that follows
    # the rows it describes is one a reader has to hunt for.
    check("the PA record precedes the CSV header",
          text.index("# pa_optimize=1") < text.index("tp_index,freq_hz,sf"))


def main():
    import tempfile
    print("capture.py host tests")
    test_boot_window_drains_the_port()
    test_boot_window_drains_with_no_role_too()
    test_role_key_is_sent_repeatedly_across_the_window()
    test_boot_window_spans_the_selection_window()
    test_completion_markers_are_matched_as_substrings()
    with tempfile.TemporaryDirectory() as d:
        test_trace_counts_malformed_rows_rather_than_dropping_them(
            Path(d) / "a.csv")
        test_trace_without_malformed_rows_carries_no_warning(Path(d) / "b.csv")
        test_pa_config_lines_reach_the_trace_header(Path(d) / "c.csv")

    if FAILED:
        print(f"\n{len(FAILED)} FAILED: {', '.join(FAILED)}")
        return 1
    print("\nall capture.py tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
