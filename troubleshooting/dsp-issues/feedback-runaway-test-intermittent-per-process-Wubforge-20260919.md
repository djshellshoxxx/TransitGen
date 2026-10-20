---
plugin: Wubforge
date: 2026-09-19
problem_type: dsp_issue
component: juce_dsp
symptoms:
  - "FXChain.feedbackRunawayIsContained fails in roughly a third of full-suite runs"
  - "Same binary, same inputs: one run peaks at 0.29, the next at 21.5 or 50.6"
  - "Always passes when the test is run on its own, or with any instrumentation added"
root_cause: logic_error
juce_version: 8.0.6
resolution_type: code_fix
severity: moderate
tags: [fx-chain, feedback, heisenbug, nondeterminism, open-issue, comb-filter]
---

# Troubleshooting: feedbackRunawayIsContained is intermittent (OPEN)

> **Status: unresolved.** This documents a live investigation so the next session
> starts from the evidence instead of repeating it. The test is left as it is -
> it asserts a real invariant and it is not wrong to fail.

## Problem
`FXChain.feedbackRunawayIsContained` sends an impulse through a deliberately
extreme chain (comb filter at full resonance, phaser and delay feedback above
their caps, reverb decay at maximum, both sends at 1.0, limiter off) and requires
the 5-second tail to decay. In a full-suite run it fails roughly one time in
three, with peaks of 3.3, 12.1, 16.5, 19.0, 21.5 or 50.6 where a passing run
measures 0.290334 every time.

## Environment
- Plugin: Wubforge 1.0.0
- JUCE Version: 8.0.6
- Affected: `Tests/TestFXChain.cpp:448`, `Source/Engine/FX/FXChain.h`
- Machine: Windows 10, 4 cores, 3.88 GB RAM, MSVC 14.44, Release /O2
- Date: 2026-09-19

## Symptoms
- `WubforgeTests.exe` (fast suite): the test fails in 2 of 6, 1 of 2, 3 of 4 runs
  depending on the session; every other check in the suite (780,000 of them) is
  stable across the same runs.
- The failing checks are `maximum feedback stays bounded (peak N)` and
  `feedback decays rather than grows`; the output is always finite.
- Run in isolation (`--filter FXChain.feedbackRunaway` or `--filter FXChain`) it
  passes every time.
- A failing run takes ~700 ms where a passing one takes ~1000 ms: the tail is
  loud, so the arithmetic never enters the denormal range that slows the passing
  run down.

## What Didn't Work

Everything below was measured, not assumed. Each one rules something out.

**1. Uninitialized filter state in the chain.**
Running 4096 samples of silence through the chain before the impulse gives a
peak of exactly 0. The chain starts clean in both passing and failing runs.

**2. Denormal (FTZ/DAZ) mode leaking in from an earlier test.**
`_mm_getcsr()` reads 0x1fb2 in every run, passing or failing: masks set,
flush-to-zero and denormals-are-zero both off, and the denormal status flag set
by prior arithmetic. No difference.

**3. Nondeterminism in the modulation feeding the chain.**
Summing every destination value over all 240,000 samples gives exactly
0.000000000000 - the harness drives the chain with no modulation at all.

**4. Memory corruption (out-of-bounds write, use-after-free).**
Built the whole test target with `/fsanitize=address` (`build-asan/`) and ran the
fast suite: zero ASan reports. The only failure under ASan was
`Validator.runsWithinItsTimeBudget` (283 ms against a 250 ms budget), which is
just ASan's slowdown.

**5. Uninitialized heap reads.**
Re-ran under ASan with `ASAN_OPTIONS=malloc_fill_byte=255:max_malloc_fill_size=16777216`,
so every fresh allocation arrives full of 0xFF (NaN as a float). All 18 FXChain
tests pass, including this one, and nothing goes non-finite.

**6. A marginally unstable chain amplifying rounding noise.**
Rendered the impulse and the same impulse scaled by 1 + 1e-6 through seven
ablations (comb, phaser, delay, reverb, and pairs). In every one the difference
between the two renders *shrinks* over the 5 seconds - the configuration that
passes is contractive, not chaotic.

**7. Uninitialized members anywhere in the chain.**
Audited every class the path touches. All scalar members carry initialisers, all
`std::vector`s are `assign`ed with zeros in `prepare`, all `std::array`s hold
class types whose own members are initialised.

**8. Mutable global state left behind by an earlier test.**
The only function-local static in the engine is `WavetableBank::get()`, which is
const after construction and is not on this path. Every other static is `const`.

**9. A missing coefficient in the oversampler.**
`Halfband2x` sets `branchA`/`branchB` in its constructor but leaves `downA`/`downB`
at `a = 0`, which looks like a bug. It is not: with `a = 0` each section is
`-z^-2`, so the downsampler averages the two polyphase phases, which is exactly
what makes the up/down cascade the halfband filter. Setting the same coefficients
on the way down makes the cascade `0.5(A^2 + B^2)`, which combs -
`FXChain.subSplitPreservesFlatMagnitude` caught it immediately at -5.27 dB at
2 kHz. `setCoefficients` is dead code, and the change was reverted.

## What is established

- Inside one process the computation is perfectly deterministic: three fresh
  harnesses in the same test give bit-identical 0.290334, and six consecutive
  processes dumping the full 240,000-sample render produced six identical files.
- Across processes the result varies, but only when other tests have run first.
- Any instrumentation - a print, a checksum, an extra render - makes it stop
  happening, which is why the passing runs above are not proof of a fix.

## Where to look next

1. Feed the chain's own state (not just its output) to a checksum every 1000
   samples and dump it, then diff a passing against a failing dump to find the
   first diverging sample and which stage owns it. The dump has to be written
   with as little extra code on the audio path as possible, because the bug
   hides from instrumentation.
2. Check whether the chain has a second attractor at these settings: drive the
   impulse at 0.5, 1.0 and 2.0 amplitude and see whether one of them settles
   into a self-oscillation instead of decaying. If it does, the fix is a lower
   feedback cap on the comb (currently `0.55 + gfRes * 0.38`, so 0.93 at full
   resonance) rather than a hunt for the perturbation.
3. Consider the machine: this box has 3.88 GB of RAM and about 300 MB free, and
   has been swapping through hours of MSVC builds. A single bit flip in the
   1.9 MB render buffer would look exactly like this and would not be caught by
   any sanitizer. Reproducing on a second machine would settle it.

## Impact on the shipped plugin

Bounded but not zero. The test disables the safety limiter to measure the raw
chain, which the plugin can no longer do (see
[the limiter parameter removal](safety-limiter-defeatable-by-automatable-parameter-Wubforge-20260919.md)),
so the worst a user hears is the limiter holding -0.3 dBFS on a patch built from
a maxed comb filter plus maxed sends. Nothing can leave the plugin above that
ceiling. The open question is whether the chain can sustain an oscillation that
the limiter then flattens, which would be audible as a loud constant tone rather
than as a decaying tail.

## Related Issues

- See also: [safety-limiter-defeatable-by-automatable-parameter-Wubforge-20260919.md](safety-limiter-defeatable-by-automatable-parameter-Wubforge-20260919.md)
