# Bridge Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Impl Plan §5.3 names this file.

---

## 2026-09-10 — BF-11: task structure, and a rule that does not reach a queue

**`firmware/bridge/` now creates seven FreeRTOS tasks and two queues.** The numbers
Impl Plan §5.2 left as bands are chosen and recorded in §5.2.1; what follows is the
part that is not a table.

**Root rule 4 does not cover a queue overflow, and it should not be stretched to.**
The rule reads: *"Never discard a frame silently. Every discard increments a named
counter and maps to one `Status` value and one §14 stage."* A frame dropped because
the RX queue is full has already passed the whole §14 ladder — it is valid, addressed
to this node and authenticated. There is no stage to map it to and no `Status` that
describes it, and inventing one would put a bridge-local condition into an
enumeration the specification owns and every node shares.

**What the rule is actually protecting is honoured:** each queue carries `sent`,
`dropped` and `high_water` in `QueueAccounting`, published as bridge diagnostics
rather than in schema `0xF0`, which is node health and normative (§14.1). **The
distinction to keep:** `lran::Counters` is the wire's registry; queue statistics are
this firmware's own. Mixing them would make a bridge's software problem read as a
node's link problem on a Home Assistant chart.

**Drop newest, not oldest, and one policy rather than two.** Dropping the oldest is
better for state, which is idempotent — but wrong for events, which are not
interchangeable and drive email and SMS. A single policy that is wrong for events
beats a per-call-site choice made before the publication policy exists; **BF-24 and
BF-25 own the refinement**, with the event dedup rules in hand.

**The never-block rule now has a check rather than a paragraph.**
`tools/checks/lora_task_never_blocks.py` fails if `portMAX_DELAY`, `delay()`, a WiFi
or publish call, or a queue call with a non-zero timeout appears in the code
`lora_task` owns. It reads one function's text, so it is a tripwire on the shape of
the mistake and not a proof — it cannot see into RadioLib, and it cannot follow an
indirect call. **It caught its own false positive during development**: the first
version flagged `xQueueSend(q, &m, 0) != pdTRUE`, which is the exact line it exists
to bless, so the timeout test parses the argument list instead of matching a regex.
The `--self-test` fixtures now include that line.

**The check does not run in CI**, because the bridge is not in CI yet (Bridge
Firmware Tasks §1.2). It is the second thing to add when the workflow is next edited,
after `pio test -d firmware/bridge -e native`.

**Two seams exist deliberately before their consumers do.** `lora_task_idle()`
returns true unconditionally so BF-13's OTA deferral (R-5.3d) has something to defer
against rather than inventing its own; and the publish queue's depth and accounting
are fixed now although its message type arrives with BF-12. Queue boundaries are the
part of a task structure that is expensive to move later, which is BF-11's whole
reason for being an Opus task.

**Stack sizes are guesses and are marked as such.** 4096 words for `lora`, 6144 for
`mqtt` and `app` because ArduinoJson serializes a discovery config on those stacks.
The way to correct one is `uxTaskGetStackHighWaterMark` through the diagnostic
topics — not a number doubled after a crash.
