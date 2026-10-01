# TransitGen Specification — 03 Fill Generator

`void tg::generate(const GenSettings&, FillPlan& out)` turns the user's intent into a FillPlan (see 01 §3–4).

## 1. Requirements
| # | Requirement |
|---|---|
| G1 | **Pure + deterministic:** output depends only on `GenSettings`, and is identical across OS, compiler and CPU. |
| G2 | **Allocation-free, no locks,** < 50 µs worst case (it runs on the audio thread at fill start, and on the message thread for display). |
| G3 | **Knob locality:** changing a knob changes only the lanes it is meant to affect (table §6). Re-rolling the seed changes everything. |
| G4 | **Always valid:** the output passes `validate()` for every input, including extreme values. |
| G5 | **Musical:** boundaries land on the style grid, and the fill's end connects into the next downbeat. |

## 2. Cross-platform determinism rules
1. The RNG is our own (§3). Never use `std::*_distribution`: their algorithms differ between standard libraries.
2. Generator math uses only `+ − × ÷`, comparisons and `sqrt` (all exactly rounded under IEEE 754). **No `pow/exp/log/sin`.** Exponential mappings (Hz, dB) are done by the engine, never the generator.
3. Compile `core/` with `-ffp-contract=off` (MSVC `/fp:precise`), never fast-math.
4. Golden-plan tests (§8) run in CI on Windows, macOS and Linux.

## 3. RNG
- **PCG32** (XSH-RR, 64-bit state). Seeded as `state = splitmix64(seed)`, `inc = (splitmix64(seed ^ streamTag) << 1) | 1`.
- Each lane has its own stream, so knob locality holds:

| Stream | Tag | Used for |
|---|---|---|
| SOURCE | `0x534F5552` | segmentation, source types, source params |
| GATE | `0x47415445` | gate decisions |
| CRUSH | `0x43525553` | crush decisions |
| FILTER | `0x46494C54` | filter variations |

- `nextFloat()` = `(next() >> 8) * 0x1p-24f`, giving [0,1).
- `nextInt(n)` uses Lemire's bounded method with rejection.
- `pickWeighted(w[], n)`: linear scan over the cumulative sum vs `nextFloat()*sum`. Weights ≤ 0 are skipped, and if all weights are 0 it returns the last index.
- **Fixed draw budget:** every source segment consumes exactly **8** draws from SOURCE, whatever branch it takes (unused draws are discarded). This keeps later segments stable when one segment's type changes.
- `mixSeed(seed, phraseIndex)` and the curve-bend function live in `core/include/transitgen/math.h` (they are shared with the engine).

## 4. Energy curve
- Up to **16 points** `{t ∈ [0,1], value ∈ [0,1], curve ∈ [−1,1]}`, sorted by t, with the first at t = 0 and the last at t = 1.
- **Bend** for a segment from a to b with curve k (clamped to ±0.99): `s = (1+k)/(1−k)`, `x' = x·s / (1 + (s−1)·x)`, `v = a + (b−a)·x'`.
- **Effective energy:** `e(t) = clamp(E(t) · intensity / 0.7, 0, 1)`. So the default intensity of 70 % reproduces the curve exactly as drawn, and 100 % makes it ×1.43 hotter.

**Factory curve presets** (each point is t:value:curve):
| Name | Points |
|---|---|
| Ramp Up *(default)* | 0:0.1:0.3 · 1:1:0 |
| Ramp Down | 0:1:−0.3 · 1:0.1:0 |
| Build & Cut | 0:0.2:0.4 · 0.85:1:0 · 0.86:0:0 · 1:0:0 |
| Swell | 0:0:0.5 · 0.5:1:−0.5 · 1:0:0 |
| Pulse | 0:0.3:0 · 0.25:0.9:0 · 0.5:0.3:0 · 0.75:0.9:0 · 1:0.3:0 |
| Plateau | 0:0.2:0.6 · 0.3:0.85:0 · 1:0.85:0 |
| Chaos | 0:0.5:0 · 0.15:0.9:0 · 0.3:0.2:0 · 0.45:1:0 · 0.6:0.35:0 · 0.8:0.95:0 · 1:0.6:0 |
| Flat | 0:0.6:0 · 1:0.6:0 |

## 5. Algorithm

Notation: `L` = lengthBeats, `g` = style grid (beats), `bar` = beatsPerBar (**amendment to 01:** add `double beatsPerBar` to `GenSettings`; the engine fills it from the time signature).

```
1. Clamp inputs; if L < g: emit one Source event covering [0,L) chosen as step 4 with e = e(0.5); goto 7.
2. Ending
   type   = endingOverride ? endingOverride-1 : style.ending.type
   endLen = (type == None) ? 0 : snapDown(min(endingBeats, L/2), g)     // snapDown to grid, min g
   body   = L - endLen
3. Segmentation of [0, body)   (SOURCE stream)
   pos = 0
   while pos < body - 1e-9:
       r[0..7] = 8 draws
       e   = e(pos / L)
       w_i = style.segment.weight_i(e) * lenBias(len_i, density)     // see below
       len = style.segment.len[pickWeighted(w, r0)]
       len = min(len, body - pos)
       if style.respectBars and crossesBar(pos, len): len = distToNextBarLine(pos)
              // bar lines are measured back from the fill END (the fill ends on a downbeat)
       len = max(snapDown(len, g), g); clamp to body - pos
       type = pickWeighted(style.source.prob(e) over {Pass,Stutter,Reverse,TapeStop,TapeStart,Silence}, r1)
       params by type (below, using r2..r7)
       append; pos += len
   lenBias(len, d) = 1 + (0.5 - d) * 2 * (len / g - 1) / max(1, maxLen/g - 1)   // density > 0.5 favours short segments; clamp >= 0.05
4. Source params
   Stutter : slice = pick style.stutter.slices weighted by style.stutter.sliceWeight_i(e), r2; slice = min(slice, len)
             roll  = r3 < style.stutter.rollProb(e) -> sliceEnd = max(slice / style.stutter.rollDiv, 1/64) else slice
             rampMode = style.stutter.rampMode
             pitchOn  = r4 < style.stutter.pitchProb(e) * pitchAmount * 2
             pitchEnd = pitchOn ? style.stutter.pitchSemis * e * pitchAmount * 2 * dirSign(r5) : 0   // round to integer semis
             pitchStart = style.stutter.pitchFromZero ? 0 : pitchEnd
             decayDb = style.stutter.decayDb * e
   Reverse : revLen = len; gain 0 dB -> 0 dB
   TapeStop/TapeStart : curve = style.tape.curve, rate = 0
   Silence : fadeOutMs = style.silence.fadeMs
   Pass    : -
   Post-pass: merge adjacent Pass events; forbid two Silence segments in a row (convert the 2nd to Pass).
5. Ending event(s) over [body, L)
   Silence      : Silence (fade = style.silence.fadeMs)
   TapeStop     : TapeStop (curve = style.tape.curve, endRate 0)
   ReverseSwell : Reverse (revLen = endLen, gain -24 dB -> 0 dB)
   Roll         : Stutter (slice 1/8 -> 1/64, stepped, pitch 0 -> round(12*pitchAmount), decay 0)
6. Modifier lanes
   Gate  (GATE stream; per source segment not Silence/TapeStop, 4 draws each):
         if d0 < style.gate.prob(e_mid): Gate event = segment span,
            stepsPerBeat = pick style.gate.rates (d1), duty = style.gate.duty(e_mid),
            pattern = style.gate.patterns[pick (d2)], attack/release = style.gate.att/rel, depth = style.gate.depth
   Crush (CRUSH stream; per segment, 2 draws):
         if crushAmount > 0 and c0 < style.crush.prob(e_mid) * crushAmount * 2:
            bits: lerp(16, style.crush.minBits, e_start*crushAmount*2 clamp1) -> same at e_end
            downsample: lerp(1, style.crush.maxDown, ...) likewise; mix = style.crush.mix
            merge with previous crush event if contiguous and params continuous
   Filter: filterType = (filterAmount == 0) ? Off : style.filter.type
         Cutoff lane: one point per energy-curve breakpoint (t*L) + endpoints, value =
             style.filter.from + (style.filter.to - style.filter.from) * e(t) * min(1, filterAmount*2), curve = the curve point's curve
             (FILTER stream: if style.filter.wobble > 0, insert grid-aligned points ±wobble*f0, max 64 points total)
         Resonance lane: constant style.filter.res (2 points). Mix lane: constant min(1, filterAmount*2) (2 points).
         Lane value semantics: cutoff Hz = 20*1000^v; Q = 0.5 + 11.5*v; mix = v.
7. Finalise: sort events by (lane, start); if numEvents > kMaxEvents -> regenerate with g doubled (max 3 tries, then a single Pass);
   assert(validate(out)) in debug builds.
```

`style.*.prob(e)` and similar functions are **piecewise-linear tables over e** with up to 5 points (format in 04).

## 6. Knob locality
| Setting changed | Source lane | Gate | Crush | Filter |
|---|---|---|---|---|
| seed, style, length, curve, intensity, ending | ✔ | ✔ | ✔ | ✔ |
| density | ✔ | ✔ (follows segments) | ✔ (follows segments) | — |
| pitch_amt | params only (no re-segmentation) | — | — | — |
| crush_amt | — | — | ✔ | — |
| filter_amt | — | — | — | ✔ |

## 7. Seed handling
- **Re-roll:** `seed = 1 + nextInt(99999)` from a UI-thread RNG seeded from the clock. The old seed is pushed to the history (32 entries, newest first, no duplicates).
- **Lock:** the UI disables Re-roll and Per-Phrase variation is treated as Fixed.
- **Variation = Per Phrase:** the engine passes `mixSeed(seed, phraseIndex)` as `GenSettings.seed`.
- **Frozen plan:** when present, the engine uses the stored plan instead of calling `generate()` (06 §3).

## 8. Tests
1. **Golden plans:** for 6 styles × 3 lengths × 3 seeds × 2 curves, a JSON dump of the plan is compared byte-for-byte against committed files, on all 3 OSes.
2. **Fuzz:** 100k random GenSettings, each must pass `validate()`, stay within capacity and not crash. Run under ASan/UBSan.
3. **Locality:** for each row of §6, assert that unaffected lanes are byte-identical.
4. **Timing:** worst case < 50 µs (Release build, 8-bar fill, densest style).
5. **Musical sanity:** every boundary sits on the g grid; the end of the fill is exactly L.
