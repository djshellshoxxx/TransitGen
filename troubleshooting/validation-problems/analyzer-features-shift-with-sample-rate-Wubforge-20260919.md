---
plugin: Wubforge
date: 2026-09-19
problem_type: validation_failure
component: juce_dsp
symptoms:
  - "8 of 41 factory presets fail their identity rules at 96 kHz, 0 fail at 44.1 kHz"
  - "Spectral centroid and Sub presence fail on presets that sound identical at every rate"
  - "Talking Bass reports 8.25 cycles/bar at 44.1 kHz and 1.00 cycles/bar at 48 kHz"
root_cause: missing_constraint
juce_version: 8.0.6
resolution_type: code_fix
severity: critical
tags: [validator, pbp-analyzer, fft, sample-rate, spectral-centroid, autocorrelation]
---

# Troubleshooting: PBP features measured different things at different sample rates

## Problem
Every automated test runs the engine at 44.1 kHz, where the factory bank scores
41/41. Running the same bank through `wubforge-cli validate` at other rates - the
CLI defaults to 48 kHz - failed 1 preset at 48 kHz and 8 at 96 kHz. The audio was
fine; the measurements were not.

## Environment
- Plugin: Wubforge 1.0.0
- JUCE Version: 8.0.6
- Affected: `Source/Engine/Validator/PBPAnalyzer.h`, `Source/Engine/Preset/FactoryPresets.cpp`
- Date: 2026-09-19

## Symptoms
- `wubforge-cli validate --sr 44100` → 41/41. `--sr 48000` → 40/41.
  `--sr 96000` → 33/41.
- The 96 kHz failures were `Spectral centroid` (Neuro Stack, Riddim Wob, Riddim
  Triplet, Deathstep Roar) and `Sub presence` (Jump Up Bounce, Bass House Stab,
  Acid 303, Dub Siren) - all bass-identity rules, on presets that pass easily at
  44.1 kHz.
- Talking Bass measured 8.25 cycles/bar at 44.1 kHz, 1.00 at 48 kHz and 1.00 at
  96 kHz, from one render of one preset.

## What Didn't Work

**Attempted Solution 1:** Assume the engine itself is sample-rate dependent.
- **Why it failed:** It is not. Every cleanly periodic preset measures the same
  rate at every sample rate - Classic Wub 1/8 reads 4.64, 4.66, 4.66 Hz at 44.1,
  48 and 96 kHz. Only the marginal ones move.

**Attempted Solution 2:** Retune Talking Bass's modulation amount until it passed.
- **Why it failed:** The measurement was not a smooth function of the amount.
  0.60 passed everywhere, 0.70 reported "no cycle at all" at 96 kHz, 0.85 passed,
  1.00 failed the rate rule at 44.1 kHz. Tuning a preset against a measurement
  that jumps like that is fitting noise - and it is how the preset ended up at
  0.35, which passed at 44.1 kHz and nowhere else.

## Solution

Three separate defects, all in the analyzer except the last.

**1. The FFT window was fixed in samples, not in time.**

```cpp
// Before: 2048 points, whatever that means at this rate.
static constexpr int kFFTOrder = 11;
static constexpr int kFFTSize = 1 << kFFTOrder;

// After: 46.4 ms, whatever that costs in points.
static constexpr double kWindowSeconds = 2048.0 / 44100.0;
static int fftOrderFor (double sampleRate) noexcept
{
    const double idealPoints = juce::jmax (1.0, sampleRate * kWindowSeconds);
    return juce::jlimit (10, 14, (int) std::lround (std::log2 (idealPoints)));
}
```

At 96 kHz a 2048-point window is 21 ms with a 46.9 Hz bin, so the 20-120 Hz sub
band collapsed from five bins to two and the sub RMS measurement lost most of its
resolution. Choosing the order per rate keeps the window near 46 ms and the bin
near 21 Hz from 22.05 kHz to 192 kHz. This fixed all four `Sub presence` failures.

**2. The spectral centroid integrated all the way to Nyquist.**

```cpp
// After: the perceptual bands stop at the top of hearing.
const int topBin = juce::jmin (fftSize / 2 - 1, (int) (20000.0f / binHz));
for (int b = 1; b <= topBin; ++b)
```

At 96 kHz the spectrum reaches 48 kHz, and 22-48 kHz of drive harmonics and
bit-crusher images - none of it audible - were dragging the centroid upward past
the bass rule's ceiling. This fixed all four `Spectral centroid` failures.

**3. The period detector gave up when the correlation never crossed zero.**

The detector skipped the autocorrelation's initial decay by walking forward to
the first negative value, and returned "no cycle" if it never found one. A real
wobble riding on a bar-length swell keeps the whole correlation positive, so a
plainly audible modulation measured 0.00 cycles/bar. It now falls back to the
first local minimum, which is the same landmark without needing a sign change.
Its harmonic correction also tries every divisor from 2 to 16 and keeps the
largest that qualifies, instead of descending one factor at a time - a 1/8
wobble under a one-bar pattern correlates best at the bar, eight times the true
period, and the half-bar and quarter-bar lags in between are genuinely weak, so
a step-by-step descent stalled at the bar.

**4. Talking Bass was tuned to the measurement rather than to the sound.**

Its growl-layer filter sweep went from 0.35 to 0.85 (`FactoryPresets.cpp`). A
vowel morph moves very little spectral centroid, so at 0.35 the sweep that was
supposed to carry the wobble was inside the measurement noise. At 0.85 it is the
dominant motion, and the preset measures 21-24% depth and 8 cycles/bar at 44.1,
48, 96 and 192 kHz alike.

## Why This Works

Every PBP feature is stated in Hz, in dB or in cycles per bar - never in bins or
in samples. Any quantity the analyzer computes must therefore be invariant to the
sample rate, and the two constants that were not (the window length in samples
and the upper integration limit at Nyquist) were exactly the two that moved the
failing measurements. Fixing them makes the features mean one thing everywhere,
which is what lets a preset's identity be a property of the preset.

The detector changes are a separate class of fix: they do not make the features
rate-independent, they stop the analysis from silently failing on inputs whose
autocorrelation does not look like a textbook one. Together they turned a bank
that scored 33/41 at 96 kHz into one that scores 41/41 at 44.1, 48, 88.2, 96 and
192 kHz.

## Prevention

- Run the validator across sample rates, not just the one the tests use. The
  whole bank at five rates is `for sr in 44100 48000 88200 96000 192000` around
  one CLI call, and it found three real defects the 44.1 kHz suite could not see.
- Any analysis constant expressed in samples or bins is a sample-rate dependency
  in disguise. State the window in seconds and band edges in Hz, then derive the
  sample counts.
- A preset that sits exactly on a threshold is a bug report about the preset, not
  a pass. If the only way to clear the floor is a value that cannot be moved in
  either direction without failing, the sound is not doing what the rule is
  asking for.
- Spectral features on a plugin that can run at 192 kHz must define their upper
  limit. Nyquist is not a perceptual boundary.

## Related Issues

- See also: [safety-limiter-defeatable-by-automatable-parameter-Wubforge-20260919.md](../dsp-issues/safety-limiter-defeatable-by-automatable-parameter-Wubforge-20260919.md)
  - the other defect the same testing pass turned up, and the same shape of
  mistake: a check that measured the wrong side of the thing it was guarding.

## Known remaining gap

At 22.05 kHz, Half-Time Lurch measures 89.8% sub presence against a 90% floor
(99.4% at 44.1 kHz). It is a one-decibel difference in sub level on the slowest
wobble in the bank, at a rate no DAW uses for production, and it is left as it
stands rather than retuned at the risk of the rates that matter.
