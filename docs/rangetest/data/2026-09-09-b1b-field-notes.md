# B1B Field Notes

## Position Notes
**G1**
- Top of loop driveway about 1/2 way to gate
- 0.5m AGL
- Elev ~2135’
- 35.04467° N, 83.36102° W
- A couple of small trees and shrubs in LOS to house.

**G2**
- Location same as previous P1.  At the gate controller.
- 0.8m AGL mounted to the back of a concrete column
- Elev ~2200
- 35.04478° N, 83.36052° W
- Not previously noted, but a 24” D tree trunk does partially block direct LOS between the gate controller antenna location and the house.

## Heltec A/B at gate
When starting up the Heltec as responder, it did not start up in the armed state waiting for a press and launched immediately into P1 test before everything in final position. Ran a second position test with heltec at the same location just in case the first pass was corrupted.

## Clarification, same day, after the first analysis pass

The analysis first read the Heltec A/B as sited differently from the Wio and discounted it.
**It was not.** Recorded here because the position notes above were read as implying
otherwise:

- **P1 (2026-09-04) and G2 (2026-09-09) are the same location.** The notes above are a more
  complete description of a spot the earlier walk already used, not a different spot.
- **Both boards sat in the same place for the A/B** — the Heltec and the XIAO+Wio alike,
  **resting on top of the gate controller enclosure with the antenna vertical.** The
  enclosure is the thing on the back of the concrete column; "0.8 m AGL on the back of a
  concrete column" and "on top of the gate controller enclosure" describe one position.
- **The deployed antenna lands within 6 in of this test position** once the GateLink node is
  installed and its antenna mounted.

So the A/B is a board substitution at one position, and B1b measured the gate where GateLink
will actually radiate rather than near it.
