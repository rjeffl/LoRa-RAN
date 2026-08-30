# LRAN Protocol v0.6 — library task list

**For:** Claude Code, working in `/lib/lran-protocol/` and `/tools/vectors/`
**Binding specification:** `LRAN-Protocol-Specification` v0.6 (`ver = 2`)
**Supersedes:** `LRAN-Protocol-v0_5-Implementation-Tasks` (all tasks complete)
**Starting state:** 104 tests under `native`, 107 on target, 71 W4 vectors, zero
host/target divergence
**Companion:** `/lib/lran-protocol/engineering-log.md`

---

## 0. Scope

Small list. Most of v0.6 is decision capture — the fixed-channel resolution, the ambient
survey requirement, the CAD coexistence notes — and lands in the range test and node
firmwares, not here.

What reaches this library is **two of the four open items the v0.5 entry closed with**.
Both were correctly identified there and deferred for a spec answer; v0.6 answers them.
The other two are decisions rather than work: W13 needs a choice recorded (G5), and W12
is unchanged and still outside this library.

Everything else the v0.5 entry built stands as built. The counter registry,
`kCounterRegistry` driving `total_dropped()`, the late-fragment rule, the HKDF guard,
`static_assert(kNodeKeyLen == 32)` and V4's double gating are all confirmed by v0.6, not
revised. Only the measured figures around them were updated.

### Guardrails

1. **`ver` stays at `2`.** Nothing here touches a header field, the authentication scope
   or a schema layout. If a task appears to need one, stop and report.
2. **Do not add a second buffer to `Reassembler` without saying why** — see G1. The v0.5
   entry anticipated needing one; I think it does not, and if it turns out to, that is
   a finding worth writing down rather than absorbing.
3. Standing: no Arduino, ESP-IDF, mbedTLS, `millis()`, `Serial`, `malloc` or `new`
   reachable from `include/` or `src/`; `-Wall -Wextra -Werror` stays; nothing discards
   silently.

### Branch

One branch, `spec/v0.6-cleanup`, gated on the five suites green under `native` and the
regenerated vectors passing on host and target.

---

## G1 — a single-frame frame never touches reassembly state

**Spec:** §11.2.

The v0.5 entry found the defect and applied the rule-4 fix: a single-frame frame
arriving mid-set called `begin()`, resetting a live multi-fragment set with nothing
counted, and the discard is now counted `rx_reassembly_abandoned`. The entry flagged
that this might not be the intended resting place. It is not.

§11.2 now states that a frame declaring `frag` total 1 **may not begin, join, displace
or expire a set**, even when it shares `(src, ctx_id, schema)` with a live one. It is
not a fragment of anything; its payload is complete on arrival.

**On the second buffer.** The v0.5 entry expected this to need one. Check that
assumption before budgeting for it: a `frag` total of 1 needs no staging at all, so if
the reassembler is currently the only route by which a caller gets a payload out, the
work is a delivery path that bypasses it rather than storage that duplicates it. If a
second buffer genuinely turns out to be necessary, that is a finding — say so in the log
with the reason, rather than adding 204 bytes quietly.

**Acceptance:**

- `test_single_frame_does_not_disturb_live_set` — begin a multi-fragment set, deliver an
  unrelated single frame from the same peer, complete the set, assert the payload is
  correct and that no reassembly counter moved.
- The single frame is still delivered to the caller. Do not fix the displacement by
  dropping it.
- **Report the `Reassembler` footprint delta on target.** Expected: zero.

## G2 — `rx_reassembly_abandoned` loses one of its callers

**Spec:** §11.3, now scoped to a displacing frame carrying `frag` total > 1.

This is separated from G1 because it inverts an existing assertion rather than adding
one. The v0.5 entry deliberately added coverage that a single frame **does** move
`rx_reassembly_abandoned`; that test has to flip, and a test that flips is easier to
miss than a test that is missing.

After G1 the counter has exactly one caller: one set displacing another with no slot
free. That restores its intended meaning — the receiver is undersized or a peer is
interleaving sets — which is what makes it worth distinguishing from
`rx_reassembly_timeout` at all.

**Acceptance:** the existing single-frame assertion is inverted, not deleted, so the new
behaviour is pinned where the old one was. `test_displaced_set_counts_abandoned_not_timeout`
keeps passing unchanged.

## G3 — `Status` identifiers follow the wire code

**Spec:** §14.1, new third column.

The v0.5 entry's last open item: `CtxMismatch` sits beside `rx_rejected_ctx` while §9.4
says `REJECTED_CTX`. Three names for one condition, which is the same drift §14.1 was
built to retire, reappearing one layer up. The registry named counters and stages and no
wire code, so there was nothing to converge on. Now there is.

**Do:** rename `CtxMismatch` → `RejectedCtx` and `BadMac` → `RejectedMac`, then walk the
rest of the `Status` enum against §14.1's wire-code column and reconcile anything else
that has drifted. §14.1's rule is a SHOULD — the specification does not dictate C++
identifiers — but the wire code is the name that cannot be changed later, so it is the
one to converge on.

**Acceptance:** a test in the same shape as C1's registry test, spelling the mapping out
independently of whatever table the code uses to express it. Duplicating the list is the
point.

## G4 — regenerate the vectors

**Spec:** §13.2.

Two things reach `/tools/vectors/`:

- **G1 is vector-expressible.** Begin a set, inject a single frame, complete the set,
  assert completion and that no reassembly counter moved. The `frag` group already does
  ordered delivery, so the shape exists. Add it — this is genuinely derived content, not
  adjudicated, which is worth having after a round where most of the new material was
  handed over.
- **G3 may reach the vectors** if any of them carries an expected decode-outcome name
  alongside its counter. Check; rename if so.

Then run the generator, `check.py`, `embed.py`, and `test_vectors` on host and target.

**Acceptance:** no existing frame byte changes. v0.6 alters no header field,
authentication scope or schema layout, so a byte diff of the frame groups is the
independent confirmation of that — the same check the v0.5 regeneration ran, and it
should come back the same way.

## G5 — W13: record the decision, do not build for it

**Spec:** §11.2 dead-space clause, §18 W13.

The v0.5 entry is right that the `frag` vector shape cannot express a differing-length
duplicate. That is a **boundary of the method rather than a gap in it**: §11.1 fixes
every non-final fragment to one length, so a conforming sender cannot produce the case,
and W4's generator emits conforming senders by construction.

**Do:** choose, and write the choice into W13's note. Accepting the existing `test_frag`
coverage as sufficient is defensible and cheap, and is what I would do — a raw-frames
vector form is a meaningful amount of tooling for one non-conforming-sender clause. If
you take the other view, say what the raw-frames form would look like before building
it.

Either way W13 stops being open. It is the only normative clause in §11 with no vector
behind it, and leaving it unresolved means that stays true by default rather than by
decision.

## G6 — housekeeping

- **Log header, line 4.** Still reads `Binding specification: LRAN-Protocol-Specification
  v0.3`. This was V6 in the v0.5 list and did not get picked up. It is v0.6.
- **Engineering log entry** for this branch, as usual: what changed, the `Reassembler`
  footprint delta from G1, and the W13 decision with its reasoning.

---

## Not in this list

**W12** — §9.4 steps 4–6 still have no home, unchanged by v0.6. Largest open item, a
decision outside this library, and it wants settling before the second firmware is
written rather than after.

**W9** — needs the second board and an SX1262 driver, both of which arrive with the
range test firmware. Nothing built so far has touched the radio; v0.6 records that
against the open item.

**Everything radio-facing** — the fixed-channel decision, the ambient survey requirement
before D1 fixes a frequency, and the CAD coexistence notes — belongs to
`LRAN-Range-Test-Firmware-Pass1-Tasks`, not here.
