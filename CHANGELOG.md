# Changelog

## v1.0 — 2026-08-07

First release.

### Survey
- 64-step sweep across six bands: three wide (300–348, 387–464, 779–928 MHz) and three focused
  ISM allocations (433, 868, 915).
- Max-hold across sweeps, so a transmitter that keys up briefly is still caught.
- Band noise floor from the lower quartile of the sweep; candidates are local maxima 10 dB above
  it, with ±2 bins suppressed around each so one carrier is one entry.

### Hunt
- Heat ladder `COLD · COOL · WARM · HOT · BURNING · ON TOP` from margin over the noise floor.
- Signal level as the 7/8th percentile of a 2.5 s window, so a duty-cycled transmitter reports the
  level it actually transmits at.
- Warmer/colder from a least-squares slope over the last 0.6 s, with a deadband derived from the
  fit's own residual — it reports nothing rather than guessing on a noisy signal.
- Distance mark and a "×closer" readout at 6 dB per halving.
- Rising-pitch, rising-rate audio so the hunt can be run without looking at the screen.
- Offset attenuator (0/200/400/800 kHz), automatic or manual, with band-edge validation and a full
  history reset on every step change.
- Signal page: `CONTINUOUS` / `PERIODIC` / `INTERMITTENT` / `QUIET` from duty cycle and burst
  timing, with the beacon period in seconds.

### Bearing
- 12-sector rose filled by elapsed time over an 8/12/20 second turn.
- Reports peak-to-median contrast and refuses to name a direction below 6 dB or before 9 of the 12
  sectors have been walked.

### Engineering
- The direction-finding engine is pure integer arithmetic with no Flipper dependencies, verified by
  916 host checks in `test/`.
- Built and checked against ufbt release (API 87.1) and dev (API 88.2).
