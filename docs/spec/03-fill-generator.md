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
2. Generator math uses only `+ − × ÷`, comparisons and `sqrt` (all exactly rounded under IEEE 754), plus the exact operations `floor/ceil/nextafter`. **No `pow/exp/log/sin`.** Exponential mappings (Hz, dB) are done by the engine, never the generator.
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

Notation: `L` = lengthBeats, `g` = style grid (beats), `bar` = beatsPerBar (`GenSettings.beatsPerBar`, 01 §4; the engine fills it from the time signature). Details the pseudo-code leaves open are fixed in §5.1.

```
1. Clamp inputs; if L < g: emit one Source event covering [0,L) chosen as step 4 with e = e(0.5); goto 7.
2. Ending
   type   = endingOverride ? endingOverride-1 : style.ending.type
   beats  = endingOverride ? endingBeats : style.ending.beats             // Ending Length applies to overrides only
   endLen = (type == None) ? 0 : snapDown(min(beats, L/2), g)           // snapDown to grid, min g
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
             roll  = r3 < style.stutter.rollProb(e) -> sliceEnd = max(slice / style.stutter.rollDiv, min(slice, 1/64)) else slice
             rampMode = style.stutter.rampMode
             pitchOn  = r4 < style.stutter.pitchProb(e) * pitchAmount * 2
             pitchEnd = pitchOn ? style.stutter.pitchSemis * e * pitchAmount * 2 * dirSign(r5) : 0   // round to integer semis
             pitchStart = style.stutter.pitchFromZero ? 0 : pitchEnd
             decayDb = style.stutter.decayDb * e                              // clamped to [-12, 0]
   Reverse : revLen = len (largest float <= len); gain 0 dB -> 0 dB
   TapeStop/TapeStart : curve = style.tape.curve, rate = 0
   Silence : fadeOutMs = style.silence.fadeMs
   Pass    : -
   Post-pass: merge adjacent Pass events; forbid two Silence segments in a row (convert the 2nd to Pass).
5. Ending event(s) over [body, L)
   Silence      : Silence (fade = style.silence.fadeMs)
   TapeStop     : TapeStop (curve = style.tape.curve, endRate 0)
   ReverseSwell : Reverse (revLen = endLen, gain -24 dB -> 0 dB)
   Roll         : Stutter (slice min(1/8, endLen) -> 1/64, stepped, pitch 0 -> round(12*pitchAmount), decay 0)
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
             clamp(style.filter.from + (style.filter.to - style.filter.from) * e(t) * filterAmount*2, 0, 1), curve = the curve point's curve
             (depth filterAmount*2 is not capped at 1, so 50..100 % keeps deepening the sweep, like pitch/crush; see §5.1)
             (FILTER stream: if style.filter.wobble > 0, insert grid-aligned points ±wobble*f, max 64 points total; see §5.1)
         Resonance lane: constant style.filter.res (2 points). Mix lane: constant min(1, filterAmount*2) (2 points).
         Lane value semantics: cutoff Hz = 20*1000^v; Q = 0.5 + 11.5*v; mix = v.
7. Finalise: sort events by (lane, start); if numEvents > kMaxEvents -> regenerate with g doubled (max 3 tries, then a single Pass, no lanes);
   assert(validate(out)) in debug builds.
```

`style.*.prob(e)` and similar functions are **piecewise-linear tables over e** with up to 5 points (format in 04).

### 5.1 Resolved details (implementation in `core/src/Generator.cpp`)
- **Inputs:** knobs are clamped to [0,1] (NaN → 0); `L` to [1/64, 1024]; `beatsPerBar` outside [0.25, 256] → 4; `endingBeats` outside [0, 64] → 0. A null or invalid curve (§4) uses Ramp Up. An unknown `styleId` uses the table's first style (`plan.styleId` reports the style used); a null/empty table gives a single Pass.
- **Grid anchor:** grid lines are measured back from the fill **end**, like bar lines (`L − k·g`), so they coincide with the host grid when the fill ends on a downbeat. When `g ∤ L` the first cell `[0, L mod g)` is partial; every other boundary is on the grid. Segmentation snaps the segment **end** down to the next grid line (at least one cell ahead, at most `body`), which is `max(snapDown(len, g), g)` when `g | L`.
- **Bar lines:** `crossesBar` means a line `L − k·bar` (k ≥ 1) lies strictly inside the segment; `len` is cut back to it before snapping. Merged Pass runs may span bar lines.
- **Ending length:** `endLen` is a whole number of cells `max(1, floor(min(beats, L/2)/g))`; when `L < 2g` the whole fill can be the ending.
- **Post-pass order:** first Silence→Pass for the 2nd of two Silences (left to right, so S S S → S P S), then merge adjacent Passes. It applies to body segments; the ending event is appended afterwards.
- **Pitch:** `pitchEnd` is rounded half away from zero and clamped to ±24; `dirSign(r5)` = +1 (up), −1 (down), or `r5 < 0.5 ? +1 : −1` (both).
- **Gate/crush scope:** decisions are made per **body** segment after the post-pass (the ending gesture is never gated or crushed). Every body segment consumes its 4 GATE / 2 CRUSH draws even when skipped (Silence/TapeStop for gate, `crushAmount = 0`), `c1` is reserved. `e_mid` = e at the segment centre, `e_start`/`e_end` at its boundaries. Gate: `stepsPerBeat = rates[pickWeighted(rateWeights, d1)]`, `pattern = patterns[min(n−1, floor(d2·n))]`, duty clamped to [0.05, 1].
- **Crush merge:** a crush event directly following the previous one (shared boundary) with `bitsStart == prev.bitsEnd` and `downStart == prev.downEnd` extends it (new end and end params); the engine then interpolates linearly over the merged span.
- **Wobble:** positions `L − k·sw` (k ≥ 1, > 0) with `sw = 2·style.grid`, doubled until they fit into `64 − numCurvePoints`; uses the style grid (not a capacity-doubled one) so density never reaches the filter. One FILTER draw `f` per position in ascending order (also for a position that coincides with a curve point, which is then skipped). Value = `clamp(base(t) ± wobble·min(1, 2·filterAmount)·f, 0, 1)`, sign alternating `+, −, +, …` from the first position. With wobble active all cutoff points have curve 0 (linear).
- **Mix lane:** `min(1, filterAmount·2)`; filterAmount = 0 or style type Off → `filterType = Off` and no lanes.
- **Short fill (step 1):** the single event uses the 8 SOURCE draws of one segment (type from r1, params from r2..r7) and no modifier lanes.

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
1. **Golden plans:** for 6 styles × 3 lengths (1 beat, 1 bar, 4 bars in 4/4) × 3 seeds (1, 4242, 99999) × 2 curves (Ramp Up, Chaos), default knobs, a JSON dump of the plan (floats `%.9g`, doubles `%.17g`) is compared byte-for-byte against `core/tests/golden/<style>.json`, on all 3 OSes. Regenerate with `TG_UPDATE_GOLDEN=1 transitgen_tests "[golden]"` (and bump the style `version`, 04 §3).
2. **Fuzz:** 100k random GenSettings, each must pass `validate()`, stay within capacity and not crash. Run under ASan/UBSan.
3. **Locality:** for each row of §6, assert that unaffected lanes are byte-identical.
4. **Timing:** worst case < 50 µs (Release build, 8-bar fill, every style, density/intensity/amounts 100 %, Chaos; per case the minimum of 10 interleaved runs, worst over 600 cases). Informational under sanitizers.
5. **Musical sanity:** every boundary sits on the g grid (§5.1 anchor); the end of the fill is L; no effect event crosses a bar line when `respectBars`; averaged over seeds, intensity 0 gives fewer non-Pass events than intensity 1.
