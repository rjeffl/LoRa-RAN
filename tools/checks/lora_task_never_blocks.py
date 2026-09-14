#!/usr/bin/env python3
"""`lora_task` never blocks on the network, and never blocks on a queue.

Impl Plan 5.2 and Bridge PRD 1.3 property 2 state the rule in prose: a status
frame arriving during a WiFi or broker outage must still be received and
queued. It is the one place a naive "publish inline on receive" quietly loses
data, and prose has never stopped anyone writing that line.

WHAT THIS CHECKS. Inside the code owned by lora_task - the `lora_task` body in
task_runtime.cpp, and every file in the LoRa path once it exists - no blocking
primitive and no network call may appear:

  portMAX_DELAY            a queue wait with no bound
  delay(...)               the Arduino busy/blocking delay
  xQueueSend/xQueueReceive with a non-zero final argument
  WiFi. / PubSubClient / mqtt_ / publish(   network work in the frame path

vTaskDelay IS permitted: bounding a poll loop is not the same as waiting on
another task, and the LoRa path's own pacing has to live somewhere.

WHAT THIS DOES NOT CHECK. That the radio driver itself does not block, and that
a call reached indirectly from here does not. It reads one function's text. It
is a tripwire on the shape of the mistake, not a proof.

WHY A CHECK AT ALL. Root CLAUDE.md: a load-bearing premise must name the check
that would falsify it, or it "reads like diligence and behaves like nothing."
This premise is load-bearing for every telemetry frame the property sends.

    python3 tools/checks/lora_task_never_blocks.py
    python3 tools/checks/lora_task_never_blocks.py --self-test
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
BRIDGE_SRC = ROOT / "firmware" / "bridge" / "src"

# Whole files owned by lora_task. BF-16 split its work three ways - the driver, the
# receive ladder and media access - and all three run in lora_task. Absent files are
# skipped, so a check never fails for a file nobody has written yet.
#
# registry.cpp is here because lora_task calls its lock-free half through PeerKeys
# (BF-15). Its locked half is registry_runtime.cpp, which is deliberately NOT here: that
# file waits on a mutex, and lora_task must never reach it.
#
# media_access.cpp left this list on 2026-09-14, when it moved to lib/lran-link for the
# simnode to share. It is not unchecked: that library builds in a `native` environment
# with no Arduino or FreeRTOS header, so it cannot name anything this check looks for.
LORA_FILES = ("lora_link.cpp", "rx_ladder.cpp", "registry.cpp")

# Functions owned by lora_task inside files that also hold other tasks.
LORA_FUNCTIONS = (("task_runtime.cpp", "lora_task"),)

FORBIDDEN = (
    (re.compile(r"\bportMAX_DELAY\b"),
     "portMAX_DELAY - an unbounded wait in the one task that must not wait"),
    (re.compile(r"(?<![\w:.>])delay\s*\("),
     "delay() - the Arduino blocking delay; use vTaskDelay to pace a loop"),
    (re.compile(r"\bWiFi\s*\."),
     "WiFi in the frame path - reception must survive a network outage"),
    (re.compile(r"\bPubSubClient\b|\bmqtt_[a-z_]*\s*\(|(?<![\w:.>])publish\s*\("),
     "network publication inline - publication is queued, never inline"),
)

# Queue calls are checked by parsing the argument list rather than by a regex: the
# timeout is the LAST argument, and "last" needs balanced parentheses to find. A
# regex that tried produced a false positive on `xQueueSend(q, &m, 0) != pdTRUE`,
# which is the exact line this check exists to bless.
QUEUE_CALL = re.compile(r"\bxQueue(?:Send|Receive)[A-Za-z]*\s*\(")


def queue_call_timeouts(code):
    """(offset, last argument) for every queue call in `code`."""
    out = []
    for m in QUEUE_CALL.finditer(code):
        i = m.end()  # just past the opening parenthesis
        depth = 1
        args = [""]
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
        if depth == 0 and args:
            out.append((m.start(), args[-1].strip()))
    return out


def lora_regions(read):
    """(label, text) for every stretch of source lora_task owns."""
    out = []
    for name in LORA_FILES:
        text = read(name)
        if text is not None:
            out.append((name, text))
    for name, func in LORA_FUNCTIONS:
        text = read(name)
        if text is None:
            continue
        body = extract_function(text, func)
        if body is None:
            out.append((f"{name}:{func}", None))  # signals "not found"
        else:
            out.append((f"{name}:{func}", body))
    return out


def extract_function(text, name):
    """The body of `name`, brace-matched. None if it is not there."""
    start = text.find(f"void {name}(")
    if start < 0:
        return None
    open_brace = text.find("{", start)
    if open_brace < 0:
        return None
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace : i + 1]
    return None


def strip_comments(text):
    """Comments describe the rule constantly; only code may break it."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan(regions):
    findings = []
    for label, text in regions:
        if text is None:
            findings.append(f"{label}: function not found - renamed, or removed")
            continue
        code = strip_comments(text)
        for pattern, why in FORBIDDEN:
            for m in pattern.finditer(code):
                line = code[: m.start()].count("\n") + 1
                findings.append(f"{label} (line ~{line} of the region): {why}")
        for offset, timeout in queue_call_timeouts(code):
            if timeout != "0":
                line = code[:offset].count("\n") + 1
                findings.append(
                    f"{label} (line ~{line} of the region): a queue call with "
                    f"timeout `{timeout}` - the calls here are zero-tick")
    return findings


def self_test():
    """The detector against fixtures, both directions."""
    clean = """
    void lora_task(void*) {
      for (;;) {
        RxMessage m;
        if (xQueueSend(g_rx_queue, &m, 0) != pdTRUE) { count_drop(); }
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    """
    dirty = [
        ("void lora_task(void*) { xQueueReceive(q, &m, portMAX_DELAY); }", "portMAX_DELAY"),
        ("void lora_task(void*) { delay(100); }", "delay()"),
        ("void lora_task(void*) { WiFi.status(); }", "WiFi"),
        ("void lora_task(void*) { publish(topic, payload); }", "publication"),
        ("void lora_task(void*) { xQueueSend(q, &m, pdMS_TO_TICKS(5)); }", "timeout"),
    ]

    ok = True
    found = scan([("clean", extract_function(clean, "lora_task"))])
    if found:
        print(f"SELF-TEST FAIL: clean fixture flagged: {found}")
        ok = False

    for src, label in dirty:
        found = scan([("dirty", extract_function(src, "lora_task"))])
        if not found:
            print(f"SELF-TEST FAIL: {label} fixture not flagged")
            ok = False

    # A comment naming the rule must not trip it.
    commented = 'void lora_task(void*) { /* never portMAX_DELAY here */ int x = 0; (void)x; }'
    if scan([("commented", extract_function(commented, "lora_task"))]):
        print("SELF-TEST FAIL: a comment mentioning the rule was flagged")
        ok = False

    print("SELF-TEST OK" if ok else "SELF-TEST FAILED")
    return 0 if ok else 1


def main():
    if "--self-test" in sys.argv:
        return self_test()

    def read(name):
        p = BRIDGE_SRC / name
        return p.read_text() if p.exists() else None

    regions = lora_regions(read)
    if not regions:
        print("OK - no LoRa-path source yet (firmware/bridge/src absent)")
        return 0

    findings = scan(regions)
    if findings:
        print("FAIL: lora_task must never block on a queue or on the network.")
        print("Impl Plan 5.2, Bridge PRD 1.3 property 2.\n")
        for f in findings:
            print(f"  {f}")
        print("\nQueue sends from this task are zero-tick and count the drop;")
        print("see firmware/bridge/src/task_runtime.h. There is no blocking variant.")
        return 1

    print(f"OK - {len(regions)} LoRa-path region(s) clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
