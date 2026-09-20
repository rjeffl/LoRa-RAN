# LRAN D1 — the parallel capture, 917.4 MHz beside 917.2 MHz

**Document:** `LRAN-D1-Parallel-Capture-Analysis`
**Version:** 0.2
**Status:** **Measurement record.** It reads one run and decides nothing. **The operator
accepted 917.4 MHz on 2026-09-20 on this reading**, and
[`LRAN-Decision-Register`](./LRAN-Decision-Register.md) **§3.4.1** is the record
**Parent document:** [`LRAN-D1-Frequency-Change-Brief`](./LRAN-D1-Frequency-Change-Brief.md) §5
**Binding protocol:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) **v0.12** (`ver = 2`)
**Decision status:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) — **the only
place D1's and D33's status is recorded**
**Last updated:** 2026-09-20

## The result

**917.4 MHz is the quieter channel, on every measure the brief asked for.** Over 24 hours
side by side, 917.2 MHz carried 1.45 times the occupancy, and it carried more in **every one
of the 24 hours**. Its −90 to −80 dBm band held 713 buckets against 917.4 MHz's 68.

**Brief §5.3 test 1 fails for 917.2 MHz** on its second and third conditions. Occupancy above
−110 dBm was higher than 917.4 MHz's hour by hour, and the episodic source near −89 dBm that
the first 917.2 MHz capture found is stronger there than anything at 917.4 MHz over the same
hours. By that test 917.2 MHz is not accepted.

**917.4 MHz's one structured occupant is the Davis station**, already recorded against D33
standing condition 3, and this run confirms its period over a full day rather than an hour.

**Nothing in brief §6 moves if D1's frequency stands**: no constant, no boot banner, no test
assertion, no specification revision and no board. The brief's own recommendation — move to
917.2 MHz — is the thing this run argues against.

## The run

| | 917.4 MHz | 917.2 MHz |
|---|---|---|
| Board | bridge board, Heltec V3 flat case | simnode Heltec, handheld case |
| Image | `chan-capture` `35c8471` | `chan-capture` `6bf9a38` |
| File | `docs/bridge/data/d1-par-917400-flat-office-2026-09-19.log` | `docs/bridge/data/d1-par-917200-handheld-office-2026-09-19.log` |

Both captures opened at 2026-09-19T17:56:58Z and closed at 2026-09-20T17:56:58Z: 23.99 h of
samples over 86,350 buckets, **8,634,999 samples each, one skipped sample each**, and 1440
`CHANSUM` minutes each with no gap. Both boards sat in the office about 1.5 m apart, at their
target locations, neither moved and neither transmitting. The external monitor stayed
disconnected, because it had raised both floors in the calibration hour.

**Read this against the office calibration hour**, not the bench one. Both boards on
917.4 MHz then read floors of −115.9 and −116.0 dBm and occupancy of 0.712 % and 0.750 %.

## The day, by the numbers

| | 917.4 MHz | 917.2 MHz |
|---|---|---|
| Occupancy above −110 dBm | **0.2095 %** | **0.3050 %** |
| Median floor | −115.9 dBm | −116.0 dBm |
| Strongest sample | −42.0 dBm | −39.0 dBm |
| Buckets written individually | 2670 | 3714 |

Buckets by peak band, over the whole run:

| Band | 917.4 MHz | 917.2 MHz |
|---|---|---|
| −110 to −100 dBm | 1204 | 1892 |
| −100 to −90 dBm | 838 | 1043 |
| **−90 to −80 dBm** | **68** | **713** |
| −80 to −70 dBm | 49 | 32 |
| −70 to −60 dBm | **510** | 33 |
| −60 dBm and up | 1 | 1 |

**The −110 to −100 dBm row carries a known bias and is weighed lightly.** The simnode
Heltec's floor sits 0.1 dB lower, so more excursions clear the fixed −110 dBm threshold on
that board; the calibration hour showed the same split, 89 buckets against 46.

## Why the excess is the channel's and not the property's

**Hour-by-hour occupancy on the two channels correlates at r = 0.978.** Activity on the
property moves both receivers together, which is exactly what the brief's parallel design
was built to expose: a schedule on the property cannot masquerade as a difference between
frequencies. The 917.2 MHz excess sits on top of that common signal.

On top of it, **917.2 MHz read higher in 24 of 24 full hours**, by 1.06 to 2.64 times, mean
1.45. No hour ran the other way.

**The gain bias does not explain the −90 to −80 dBm band.** The calibration hour put the two
boards up to 8 dB apart on the Davis, which measures direction as much as either receiver. A
source at −89 dBm on the simnode Heltec could therefore land a band lower on the bridge
board. It does not here. If that source reached 917.4 MHz 8 dB down it would appear in the
−100 to −90 dBm band. That band instead tracks closely across the two files: 838 buckets
against 1043, a difference of 205, nowhere near the 713 it would have to absorb.

## The Davis, over a full day

**The 917.4 MHz −70 to −60 dBm band is the Davis Vantage Pro2**, and 24 hours confirm the
period the 2026-09-18 entry established from a shorter run. A least-squares fit seeded at
130.69 s gives:

- fitted period **130.6882 s**;
- **median residual 0.54 s**, across 660 occurrences;
- **92 % of the 509 caught events within 2 s of the grid**;
- catch rate 0.77, consistent with a burst shorter than the 10 ms sampling interval.

The band holds 16 to 25 buckets an hour, every hour, day and night.

**`rssi_report.py` nevertheless prints "not periodic" for this band.** That is a defect in
the analysis tool, not a change in the Davis; *The periodicity verdict does not survive a
long capture* below has it.

## The 917.2 MHz source is not the Davis, and it is not an evening source

**It runs in all 24 hours.** The −90 to −80 dBm band holds 15 to 51 buckets an hour on
917.2 MHz, with its heaviest hours at 04 and 12–13 UTC. The 2026-09-18 entry read it as an
evening source because the capture that found it ran from 17:57 to 23:49 UTC. Over a full day
it has no evening concentration. **917.2 MHz carries it continuously, not for about six hours
a day**, which is worse than the brief assumed when it weighed this source against the
Davis's 6.7 ms every 130.69 s.

**It is aperiodic and it is not the Davis.** The gaps between its events cluster at 130 s
often enough to suggest the Davis at a reduced level, so that was tested two ways and rejected
both times:

- **Coincidence.** Only 22 of its 577 events, 3.8 %, fall within ±2 s of a 917.4 MHz Davis
  event — against **4.0 %** for the same test with the times shifted by 65 s, half a period.
  The two are no more aligned than chance.
- **Grid fit.** Only 13 of 577, 2 %, land within 2 s of a 130.69 s grid. A 185 s grid, the
  next most common gap, takes 1 %.

**The source is unidentified**, as it was on 2026-09-18. What this run adds is that it is
continuous, aperiodic, and absent from 917.4 MHz.

## One wideband event, on both channels

At **2026-09-20T13:25:22Z** both boards recorded their strongest excursion of the day in the
same second — **−42.0 dBm at 917.4 MHz and −39.0 dBm at 917.2 MHz**, one bucket wide, 4 and 9
samples above threshold. It is the only −60 dBm-and-up bucket in either file.

**It does not bear on D1.** A source that reads on both channels does not separate them, as
brief §5.3 says. It is at least 200 kHz wide and was keyed once in 24 hours, and it remains
unidentified.

## The periodicity verdict does not survive a long capture

**`periodicity()` in `tools/simctl/rssi_analyze.py` reports the Davis as "not periodic",** on
a day where the same events fit a 130.6882 s clock to a median of 0.54 s. Two defects combine,
and both grow with capture length:

1. **A sub-period gap is charged a whole period.** `max(1, int(round(g / guess_ms)))` maps any
   gap shorter than half the guess to 0, which `max(1, ...)` raises to 1, so a foreign event
   between two real occurrences adds a period that did not happen. Over this run it inflated
   the occurrence count from 660 to 687 and pulled the fitted period from 130.69 s down to
   **125.59 s**, a 4 % error.
2. **The verdict gates on the maximum residual**, with no allowance for an outlier, so one
   foreign event in the band flips a clean clock to "not periodic". The maximum here was
   660.7 s against a median of 0.54 s.

Neither shows over one hour, because foreign events in a narrow band are rare enough not to
appear. **The verdict is what this repository cites when it attributes an occupant**, D33
standing condition 3's Davis included, so it is worth fixing before the next attribution.
`firmware/chan-capture/CLAUDE.md` carries the same warning for whoever runs a capture next.

**The figures in this document were computed by seeding the fit with a known period and
reading the residuals directly**, not by trusting the tool's verdict.

## What this run does not answer

- **The 917.2 MHz source is unidentified**, and so is the 13:25:22Z wideband event.
- **917.6 MHz is untested against 917.4 MHz over the same hours.** Brief §5.3 test 2 prefers
  917.6 MHz only if it beats 917.2 MHz, and day 2 was to be run only if day 1 ruled out
  917.2 MHz. If the operator keeps 917.4 MHz, day 2 answers nothing D1 needs. The 18 bursts
  at −45 to −47 dBm in the 2026-09-19 917.6 MHz capture stay open either way.
- **M26 is still open.** The property's Z-Wave and Insteon equipment is named but no link's
  data rate is confirmed, and §3.1's inventory is what D33 standing condition 3 was reasoned
  against.
- **This document records no decision.** D1's and D33's status live in the Decision Register.
  **§3.4.1 records the operator's acceptance of 917.4 MHz on 2026-09-20**, restates D33's
  standing condition 3 around characterising occupants rather than finding none, and accepts
  the Davis under it. The brief is marked superseded there rather than edited.

## Changelog

| Version | Date | Change |
|---|---|---|
| 0.1 | 2026-09-20 | First issue. Reads the 24-hour parallel capture of 2026-09-19 to 2026-09-20 against brief §5.3 |
| 0.2 | 2026-09-20 | The operator accepted 917.4 MHz. Status and the closing section point at Register §3.4.1; the reading itself is unchanged |
