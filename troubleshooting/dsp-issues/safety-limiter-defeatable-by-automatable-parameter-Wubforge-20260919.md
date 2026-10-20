---
plugin: Wubforge
date: 2026-09-19
problem_type: dsp_issue
component: apvts
symptoms:
  - "Fuzz.tenThousandRandomParameterStates fails: fuzz state 9110 peak 14.391"
  - "Output peaks at +23 dBFS although the chain ends in a brick-wall limiter"
  - "1 failure in 10,000 random parameter states; the other 9,999 stay under the ceiling"
root_cause: missing_constraint
juce_version: 8.0.6
resolution_type: code_fix
severity: critical
tags: [limiter, apvts, parameter-layout, output-level, fuzz, safety]
---

# Troubleshooting: A safety limiter with an off switch is not a safety limiter

## Problem
The 10,000-state parameter fuzz found one state whose output peaked at 14.391
(+23.2 dBFS) even though the FX chain ends in a brick-wall limiter at -0.3 dBFS.
The limiter had not failed: a parameter had switched it off.

## Environment
- Plugin: Wubforge 1.0.0
- JUCE Version: 8.0.6
- Affected: `Source/Common/Parameters.cpp` (APVTS layout), `Source/Engine/FX/FXChain.h`,
  `Source/Engine/WubforgeEngine.h`, Phase 10 testing
- Date: 2026-09-19

## Symptoms
- `WubforgeTests.exe --slow` reports 2 failed checks, both in
  `Fuzz.tenThousandRandomParameterStates`, at `TestFuzz.cpp:186` and `:192`.
- The reported state is `fuzz state 9110: peak 14.391`; the worst peak across
  the whole run is that same 14.391, so exactly one state in 10,000 crosses the
  test's threshold of 8.0.
- Everything else is green: 142 tests, 780,344 checks, 2 failures.

## What Didn't Work

**Attempted Solution 1:** Treat it as a runaway - an unstable filter or a
feedback path that had stopped decaying.
- **Why it failed:** It is not a runaway. Replaying state 9110 and neutralising
  one stage at a time showed the level was stable across blocks (12.5, 14.4,
  14.3, 9.6) and came from stacked static gain, not from growth:

  | variant | peak |
  |---|---|
  | baseline | 14.391 |
  | multiband makeup set to 0 dB | 4.921 |
  | global drive amount and trim set to 0 | 2.680 |
  | reverb off / unfrozen / delay off / chorus off / bit crusher off | 14.391 |

  The state had a 35.8 dB drive with +7.8 dB of trim and two compressor bands at
  +9.6 dB of makeup. Those gains are doing exactly what the user asked for.

**Attempted Solution 2:** Raise the test's threshold past 14.4.
- **Why it failed:** It hides the actual defect. The state's `master_gain` was
  -15.3 dB, so the level arriving at the limiter was about 84x - the chain has
  no cap of its own, and a different random state could have been far louder.
  The limiter is what bounds the output, and the question worth answering was
  why it had not.

## Solution

The replay showed the answer immediately: state 9110 had `limiter_on = 0`.

`limiter_on` was registered in the APVTS parameter layout, which makes it an
automatable parameter that any host can write - even though `fxchain.md` section
9 specifies the master limiter as "safety only. Not visible to user", and no
part of the UI ever exposed it. The parameter was a slip, and the fuzz was the
only code in the project that ever set it.

**Code changes:**

```cpp
// Before (broken) - Source/Common/Parameters.cpp:
addFloat (p, pid::masterGain, "Master Gain", Range (-60.0f, 12.0f), -3.0f, "dB", false);
addBool  (p, pid::limiterOn,  "Limiter", true, false);

// After (fixed):
addFloat (p, pid::masterGain, "Master Gain", Range (-60.0f, 12.0f), -3.0f, "dB", false);
// No limiter parameter. The master limiter is a safety device (fxchain.md
// section 9: "safety only. Not visible to user"), and a parameter is a
// switch a host can automate [...]. It is wired permanently on instead.
```

```cpp
// Source/Engine/WubforgeEngine.h - the mapping is gone, not defaulted:
f.masterGainDb = p.get (pid::masterGain, -3.0f);
// f.limiterOn is deliberately not assigned: the safety limiter is
// always on in the running plugin. See Parameters.cpp.
```

`pid::limiterOn` was removed from `Identifiers.h`. `FXChain::Settings::limiterOn`
stays, defaulting to true, because `FXChain,noSettingCombinationExceedsPreLimiterHeadroom`
switches the limiter off to measure the pre-limiter level - but nothing maps a
parameter onto it any more, so only a test can reach it.

The fuzz checks were rewritten around the two levels rather than one:

```cpp
// Before: one threshold, on the wrong side of the limiter.
if (! r.finite || r.peak > 8.0f)

// After: the limiter's ceiling on the way out, a runaway bar on the way in.
constexpr float kCeiling = 0.9661f;   // nothing leaves above -0.3 dBFS, ever
constexpr float kRunaway = 64.0f;     // +36 dBFS pre-limiter is growth, not loudness
if (r.prePeak > kRunaway) return "pre-limiter runaway, peak ...";
if (r.peak > kCeiling)    return "output above the limiter ceiling, peak ...";
```

## Why This Works

The root cause was a missing constraint, not a DSP error. The chain's gain
stages are allowed to stack - drive trim, three bands of makeup and master gain
are all user gain controls, and a hostile combination of them legitimately
reaches the low twenties of dBFS. The design answer to that is the brick wall at
the end, which is why the spec puts it there and calls it invisible. Registering
it as a parameter quietly turned "cannot be defeated" into "is defeated in about
half of all random states", and made the worst case a function of how much gain
a user could stack rather than a fixed ceiling.

Removing the parameter restores the invariant, and the rewritten fuzz check now
asserts it directly: the output peak is checked against the limiter's ceiling
rather than against a loose number that happened to sit above the loudest state
anyone had seen. Measuring the pre-limiter level separately keeps the test's
original power - a genuine runaway still fails, because the limiter clamps the
output but cannot hide what arrives at its input.

## Prevention

- A safety device must not be reachable from the parameter layout. If the spec
  says "not visible to user", that means it is not in `createParameterLayout`
  either - a host does not need a UI control to automate a parameter.
- When a fuzz test asserts a level, assert it against the thing that is supposed
  to bound it. `peak > 8.0f` passed for years of random states because it sat
  above anything the chain produced with the limiter on; it could not tell a
  defeated limiter from a loud preset.
- Any parameter that no UI reads is worth a second look. `grep` for the
  identifier across `Source/UI` before assuming it is wired to something.
- Fuzz failures are reproducible: replay the RNG to the failing index and
  neutralise stages one at a time. Beware that the failing state depends on
  argument evaluation order if the RNG is drawn inside a call's argument list -
  hoisting those draws into locals changes the sequence and the state no longer
  reproduces.

## Related Issues

No related issues documented yet.
