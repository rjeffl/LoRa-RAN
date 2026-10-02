#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Robert J. Lee
"""GateLink's `io_task` never blocks on anything but its own period.

GateLink Impl Plan 5.2 and PRD R-5.2a: the I/O service must never be starved. io_task
times every relay pulse, and a pulse whose trailing edge is late is a command of the wrong
length. The bridge enforces the same property for its lora_task with
lora_task_never_blocks.py; this is GateLink's equivalent (Impl Plan 7.5).

WHAT THIS CHECKS. Inside the `io_task` body in firmware/gatelink/src/task_runtime.cpp, with
comments removed, the only wait is vTaskDelayUntil. None of these may appear:

  portMAX_DELAY                    an unbounded wait
  delay(...) / vTaskDelay(...)     a wait that is not the period
  xSemaphoreTake / ulTaskNotifyTake / xEventGroupWaitBits / xTaskNotifyWait
                                   with a non-zero timeout: a lock or a signal
  xQueueSend / xQueueReceive       with a non-zero timeout
  Serial                           a full USB CDC buffer blocks its writer

WHAT THIS DOES NOT CHECK. That a function io_task calls does not block. It reads one
function's text. It is a tripwire on the shape of the mistake, not a proof.

    python3 tools/checks/io_task_never_blocks.py
    python3 tools/checks/io_task_never_blocks.py --self-test
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
RUNTIME = ROOT / "firmware" / "gatelink" / "src" / "task_runtime.cpp"
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from lora_task_never_blocks import extract_function, strip_comments  # noqa: E402

FORBIDDEN = (
    (re.compile(r"\bportMAX_DELAY\b"), "portMAX_DELAY - an unbounded wait"),
    (re.compile(r"(?<![\w:.>])delay\s*\("), "delay() - a wait that is not the period"),
    (re.compile(r"\bvTaskDelay\s*\("),
     "vTaskDelay() - use vTaskDelayUntil, so the period does not drift by a pass"),
    (re.compile(r"\bSerial\s*\."),
     "Serial - a full USB CDC buffer blocks its writer; log through log_task"),
)

# Calls whose LAST argument is a timeout, which must be literally 0.
TIMED_CALL = re.compile(
    r"\b(?:xQueue(?:Send|Receive)[A-Za-z]*|xSemaphoreTake[A-Za-z]*|ulTaskNotifyTake"
    r"|xTaskNotifyWait|xEventGroupWaitBits)\s*\(")


def last_arguments(code):
    """(offset, call name, last argument) for every timed call in `code`."""
    out = []
    for m in TIMED_CALL.finditer(code):
        i, depth, args = m.end(), 1, [""]
        while i < len(code) and depth > 0:
            c = code[i]
            if c in "([":
                depth += 1
            elif c in ")]":
                depth -= 1
                if depth == 0:
                    break
            if depth == 1 and c == ",":
                args.append("")
            else:
                args[-1] += c
            i += 1
        if depth == 0:
            out.append((m.start(), m.group(0).rstrip("( "), args[-1].strip()))
    return out


def scan(body):
    if body is None:
        return ["task_runtime.cpp: io_task not found - renamed, or removed"]
    code = strip_comments(body)
    findings = []
    for pattern, why in FORBIDDEN:
        for m in pattern.finditer(code):
            findings.append(f"io_task (line ~{code[:m.start()].count(chr(10)) + 1}): {why}")
    for offset, name, timeout in last_arguments(code):
        if timeout != "0":
            findings.append(f"io_task (line ~{code[:offset].count(chr(10)) + 1}): {name} "
                            f"with timeout `{timeout}` - io_task waits only on its period")
    return findings


def self_test():
    clean = """
    void io_task(void*) {
      TickType_t last = xTaskGetTickCount();
      for (;;) {
        if (xQueueReceive(g_relay_queue, &req, 0) == pdTRUE) { start_pulse(req); }
        vTaskDelayUntil(&last, period_ticks(TaskId::Io));  // never vTaskDelay
      }
    }
    """
    dirty = [
        ("void io_task(void*) { xQueueReceive(q, &m, portMAX_DELAY); }", "portMAX_DELAY"),
        ("void io_task(void*) { delay(5); }", "delay()"),
        ("void io_task(void*) { vTaskDelay(pdMS_TO_TICKS(100)); }", "vTaskDelay()"),
        ("void io_task(void*) { Serial.println(x); }", "Serial"),
        ("void io_task(void*) { xSemaphoreTake(g_spi, pdMS_TO_TICKS(10)); }", "semaphore"),
        ("void io_task(void*) { ulTaskNotifyTake(pdTRUE, 1); }", "notify"),
        ("void io_task(void*) { xQueueSend(q, &m, (1)); }", "queue timeout"),
    ]
    ok = True
    if scan(extract_function(clean, "io_task")):
        print(f"SELF-TEST FAIL: clean fixture flagged: {scan(extract_function(clean, 'io_task'))}")
        ok = False
    for src, label in dirty:
        if not scan(extract_function(src, "io_task")):
            print(f"SELF-TEST FAIL: {label} fixture not flagged")
            ok = False
    if not scan(None):
        print("SELF-TEST FAIL: a missing io_task was not flagged")
        ok = False
    print("SELF-TEST OK" if ok else "SELF-TEST FAILED")
    return 0 if ok else 1


def main():
    if "--self-test" in sys.argv:
        return self_test()
    if not RUNTIME.exists():
        print("FAIL: firmware/gatelink/src/task_runtime.cpp not found")
        return 1
    findings = scan(extract_function(RUNTIME.read_text(), "io_task"))
    if findings:
        print("FAIL: io_task must never block on anything but its own period.")
        print("GateLink Impl Plan 5.2, PRD R-5.2a.\n")
        for f in findings:
            print(f"  {f}")
        return 1
    print("OK - io_task waits only on its period")
    return 0


if __name__ == "__main__":
    sys.exit(main())
