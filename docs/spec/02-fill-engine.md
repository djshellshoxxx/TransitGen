# TransitGen Specification — 02 Real-time Fill Engine

**Status:** Draft v0.1 · **Depends on:** 01 Core contract · **Implements:** `core/include/transitgen/*.h`, `core/src/*.cpp`

The engine turns host transport + settings + a plan provider into sample-accurate, click-free, deterministic audio. Everything below runs on the audio thread except where noted. All constants are in `transitgen/Constants.h`; shared math (`mixSeed`, `bend`) is in `transitgen/math.h` and is used verbatim by 03.

## 0. Conventions and constants
- `sr` sample rate, `spb = sr·60/bpm` samples per beat (double). `t` = **engine time**: int64 samples since `prepare()`/`reset()`, advanced by `numSamples` per block. `f = t − fillStartT` = fill-local sample index.
- `beatToOffset(b, blockStartBeat) = ceil((b − blockStartBeat)·spb − ε)`, ε = `kBeatEpsSamples = 1e-6` samples (see §12, amendment A1). The same function converts plan beats to fill-local samples: `S(b) = ceil(b·spbFill − ε)`.
- Channels: 1 or 2 are processed; channels beyond `kMaxChannels = 2` are passed through untouched. All per-frame state (read heads, envelopes) is shared across channels; only the interpolation taps are per-channel.

| Constant | Value | Use |
|---|---|---|
| `kSourceFadeMs` | 3 | source-boundary crossfade, fill entry/exit ramp |
| `kCutFadeMs` | 10 | early release / abort fade |
| `kRepeatFadeMs` | 2 | stutter repeat crossfade and window seam (clamped to L_k/4, W_k/4) |
| `kMinRepeatSamples` | 32 | shortest stutter repeat |
| `kFilterUpdateInterval` | 16 | SVF coefficient anchor spacing (samples) |
| `kMaxLookbackBeats` | 8 | capture capacity in beats at `kMinBpm` |
| `kMinBpm` | 30 | capacity sizing |
| `kDefaultBpm` | 120 | missing host tempo |
| `kTapeInitialLag` | 3 | samples; Hermite needs 2 future taps |
| `kTapeMuteRate` | 0.02 | tape gain = rc(min(1, v/0.02)) |
| `kJumpToleranceBeats` | 0.005 | PPQ discontinuity threshold |
| `kMinEntryMs` | 10 | shortest mid-fill entry remainder |

**Fade shapes.** All ramps use the raised cosine `rc(x) = 0.5 − 0.5·cos(πx)`, `x ∈ [0,1]`, from a 4097-entry table (index `floor(x·4096)`). It is C¹ at both ends, so a ramp of amplitude `A` over `F` samples contributes at most `A·(π/F)²/2` to the second difference (§7.3), whereas a linear or sin/cos ramp leaves a corner of `A/F` (or `A·π/2F`) at its ends. Two crossfade kinds: **sum-preserving** `(1 − rc, rc)` and **equal-power** `(cos(π/2·rc), sin(π/2·rc))`.

## 1. TransportTracker
Input per block: `TransportInfo` (01 §5) + `numSamples`. Output `BlockTime { int64 t0; double startBeat; double bpm; double spb; double barLen; double gridOrigin; bool clock; bool jumped; }`.

1. **Tempo**: `bpm = info.bpm` if `playing && hasPpq && bpm > 0`, else the last valid host bpm, else `kDefaultBpm`. Clamp to [`kMinBpm`, 999]. `spb = sr·60/bpm` is constant within a block.
2. **Time signature**: `tsNum ≥ 1 && tsDen ≥ 1` else 4/4. `barLen = tsNum·4/tsDen`.
3. **Bar start**: `gridOrigin = hasBarStart ? barStartPpq − barLen·floor((barStartPpq + 1e-9)/barLen) : 0`. Fallback: bars are at multiples of `barLen` from PPQ 0. Bar index `= floor((beat − gridOrigin)/barLen)`.
4. **Playing with PPQ** (`clock = false`): `startBeat = info.ppq`. Expected position `exp = prevStartBeat + prevN/prevSpb`. `jumped = |startBeat − exp| > kJumpToleranceBeats` or the clock source changed (stop→play, play→stop) or `reset()`. Backwards PPQ (loop) is just a jump. `looping` is telemetry only.
5. **Stopped or no PPQ** (`clock = true`): an internal clock `clockT` (int64) runs: `startBeat = clockT/spb`, `clockT += numSamples`. It is the sole clock for the standalone and for MIDI/Automation when the host is stopped. On play→stop, `clockT` is seeded so `startBeat` continues from the last host position (`clockT = round(exp·spb)`) — no jump. Stop→play is a jump (the scheduler re-arms). The internal clock is integer-anchored so block partitioning cannot change it.
6. **Tempo change mid-block/mid-fill**: tempo is per block (01 §2). Scheduling always converts with the current block's `spb`; the active fill keeps its frozen `spbFill` (§2.5). Tempo changes do not count as jumps (the expectation uses the previous block's tempo; ramps stay well under the tolerance).
7. **Missing info**: no PPQ → internal clock; no bpm → last/120; no bar start → fallback; `numSamples == 0` → no state change.

## 2. FillScheduler
State: `lastStartedPhrase` (int64, `INT64_MIN` = none), `pending {bool, double beat, bool immediate, FillRequest req}`, `notBeforeT` (int64), trigger edge memory `prevTrigger`, `armed`. The engine asks `nextAction(bt, settings, midi, fromOffset, numSamples, playerActive) → {offset ∈ [from, n], kind ∈ {None, Start, Cut}, req, playOffset}` repeatedly within a block (§10 loop), so any number of sample-accurate actions per block are possible.

Common: `fillBeats(settings) = fill_len` in beats (bar choices × `barLen`), then clamped per mode. Grid for `quantize`: Off → immediate; 1/16..1/2 → multiples of 0.25/0.5/1/2 beats; Bar → `barLen`; all anchored at `gridOrigin`. `q(b) = gridOrigin + step·ceil((b − gridOrigin)/step − 1e-9)`.

### 2.1 Auto-Phrase
`P = phrase_bars·barLen`, `o = gridOrigin + phrase_offset·barLen`, `Lf = min(fillBeats, P)`. Phrase index `i = floor((b − o)/P)` (as the generator defines it). Region of phrase `i`: `[o + (i+1)P − Lf, o + (i+1)P)`. Per query at block beat `b`:
- If `b` is inside region `i` and `i != lastStartedPhrase` and the remainder ≥ `kMinEntryMs` → **Start now** (mid-fill entry: `playOffset = round((b − regionStart)·spb)`). This covers loops/jumps landing inside a fill region and play starting inside one.
- Else the next region start `r ≥ b` (`i` or `i+1`) → Start at `beatToOffset(r)` if `< n`, `playOffset = 0`.
- On `jumped` the engine Cuts an active fill in Auto-Phrase and Automation mode (position-driven); MIDI fills keep playing.
- On Start, `lastStartedPhrase = i`. On `jumped`, `lastStartedPhrase = none` (a loop replays the fill). Back-to-back fills (`Lf = P`) chain through exit-fade → entry-fade (§7), never overlap.
- Seed: `variation == PerPhrase ? mixSeed(seed, i) : seed`.

### 2.2 Automation
`trigger` is read once per block (01 §7), so edges are block-quantized by contract. Rising edge (or trigger high after `jumped`/re-arm while idle) at block start → `pending.beat = q(b0)` (Off → immediate at offset 0). Pending starts fire at `beatToOffset(pending.beat)` clamped to ≥ `notBeforeT − t0`; if the beat is already past by more than `fillBeats` (jump) the pending is dropped. Falling edge while a fill is active → **Cut** at offset 0 (fade `kCutFadeMs`). Length = `fillBeats`.

### 2.3 MIDI
`MidiEvent {int sampleOffset; uint8 status, data1, data2}` sorted by offset, consumed in order. Note-on (velocity > 0) at offset `s`, beat `b = b0 + s/spb`: start at `q(b)` (Off → exactly `s`). **Length map** by pitch class (octave ignored): C 1/4, C# 1/2, D 1, D# 2, E 3, F 4 beats, F# 2 bars, G 4 bars, G# 8 bars, A/A#/B = `fill_len`. **Velocity → intensity** `= vel/127` when `midiVelocityToIntensity` (default on), else the `intensity` param. **One-shot** (`midiHold = false`): note-off ignored. **Hold**: note-off of the triggering note → Cut. Note-on while active → Cut now, new start at `max(q(b), notBeforeT)` (retrigger). Pitch bend/CC ignored in v1.

### 2.4 Freezing the plan snapshot
At the Start sample the engine builds `FillRequest` (= `GenSettings` fields + `bpm, sampleRate, phraseIndex, tsNum, tsDen, mode, midiNote, velocity`) from the **current** settings, calls `provider.makePlan(req, plan)`, runs `validate()`, snapshots `mix`, `out_gain` and `spbFill = spb` of that block. Nothing of this changes during the fill: parameter edits apply to the next fill (keeps determinism and makes the plan shown in the UI exactly what played). An invalid plan or `makePlan` returning false skips the fill (`telemetry.rejectedPlans++`), the scheduler state advances as if it had played.

### 2.5 Capacity, overlap, length
- Plan capacity is `kMaxEvents`/`kMaxLanePoints` (01); the provider must respect it, `validate()` enforces it. Capture capacity overruns are clamped per source (§4).
- Fills never overlap: one `PlanPlayer`. A new Start while active is only reachable through Cut (`notBeforeT = cutT + cutFade`). Auto-Phrase clamps `Lf ≤ P`.
- Fill length in samples `N = S(lengthBeats)`; the plan's `lengthBeats` must equal `req.lengthBeats` (validated, tolerance 1e-9).

## 3. Plan provider interface
```cpp
struct IPlanProvider { virtual ~IPlanProvider() = default;
  virtual bool makePlan(const FillRequest&, FillPlan& out) noexcept = 0; }; // audio thread, alloc-free, < 50 µs
```
`Engine::setPlanProvider(IPlanProvider*)` (before processing; pointer must outlive the engine). The real provider (03) wraps `generate()` and reads the lock-free published curve/style pointers from `req`. `TestPlanProvider` returns a stored template plan, rescaled to `req.lengthBeats` (events and lane points scaled by `req.lengthBeats/template.lengthBeats`), with `seed = req.seed`.

## 4. CaptureBuffer
Ring buffer per channel, always written with the **dry input before any processing** of a block, so any read at absolute position `≤ t` of the current block is valid dry audio (this is also how the engine obtains `dry` for in-place processing). `capacity = nextPow2(ceil(kMaxLookbackBeats·60/kMinBpm·sr) + maxBlock + 8)`: 8 beats at 30 BPM = 16 s → 2^20 frames at 48 kHz (4 MB/ch), 2^22 at 192 kHz (16 MB/ch). This bounds: Reverse `p0` (≤ 8 beats), Stutter windows (`sliceStart ≤ 8` beats), tape lag (TapeStop lag ≤ event length ≤ 8 beats at rate 0). Valid range `[max(0, writeEnd − capacity), writeEnd)`; reads outside return 0 and bump `telemetry.captureUnderruns`. Request clamping: Reverse `R = min(S(p0), validSpan)`; Stutter `W_k` as §5.2 (≤ available); tape `lag ≤ capacity − maxBlock − 8` (read head dragged forward). Zeroed at `prepare()`/`reset()`.

## 5. PlanPlayer and SourceStage
At Start: `events` → sample bounds `[S(start), S(start+len))`, `N = S(lengthBeats)`, cursors per lane, modifier states reset, `f = playOffset`. Per sample: source → gate → filter → crush → mix (§7). Exactly one source event is current; at a boundary the outgoing source keeps rendering for the crossfade length (§7.1). All read positions below are fill-local (`absolute = fillStartT + pos`), interpolation is 4-point cubic Hermite (Catmull-Rom): `h(x, i, φ)` uses `x[i−1..i+2]`, `φ ∈ [0,1)`; at `φ = 0` it returns `x[i]` bit-exactly. Rationale: for ±2 octaves it keeps images ≥ 50 dB down at a quarter of the cost of a 16-tap windowed sinc, is stateless (seekable, trivially deterministic, no history to reset at boundaries) and exact for linear ramps (testable). Linear interpolation was rejected for its 6 dB/oct HF roll-off and audible imaging on pitched rolls.

**Pass**: `w = x[f]`.

**Silence**: `w = 0`. The boundary crossfade into it has length `max(kMinFade = 32 samples, S_ms(p0))` instead of `kSourceFadeMs` (`p0 = 0` = "hard", still 32 samples so the step is tamed).

### 5.2 Stutter (forward capture)
`E, Nev` event start/length; `ℓ0 = p0`, `ℓ1 = (p1 > 0 ? p1 : p0)` beats; `u_k = (R_k − E)/Nev` progress of repeat `k` starting at `R_k` (`R_0 = E`).
- Target length `ℓ(u) = ℓ0·(ℓ1/ℓ0)^u` (geometric roll). Smooth (`p2 = 1`): `ℓ_k = ℓ(u_k)`. Stepped (`p2 = 0`): `ℓ_k = ℓ0·2^round(log2(ℓ(u_k)/ℓ0))` (octave steps of the start length, so a 1/8→1/32 roll goes 1/8,1/16,1/32). `L_k = max(kMinRepeatSamples, S(ℓ_k))`; `R_{k+1} = R_k + L_k`.
- **Repeat 0 is live**: `w = x[f]`, unpitched, gain 1 (the first hit is the real one, and future audio is not available for upward pitch).
- Repeat `k ≥ 1` is a **looped window** `[E, E + W_k)`, `W_k = min(L_k, R_k − E)` — always fully recorded dry audio. Pitch `r_k = 2^(semis(u_k)/12)`, `semis(u) = p3 + (p4 − p3)·u`. Sample `j` of repeat `k`: `q = fmod(j·r_k, W_k)`, `pos = E + q` (Hermite taps read linearly; `E + W_k + 1 ≤ R_k ≤ t` so nothing beyond the write head is touched). Gain `g_k = 10^(p5·k/20)`.
- **Seam**: a loop point is a discontinuity, so over the last `S_k = min(S_ms(kRepeatFadeMs), W_k/4)` samples of the window the head crossfades (equal-power) from `x[E + q]` to `x[E + q − W_k]`, the audio just *before* the window, which then runs on into the window start without a cut. Reads before `E` are past audio (0 if before the capture start).
- **Repeat crossfade**: at `R_{k+1}` the outgoing head keeps looping its own window (with its seam) for `F_k = min(S_ms(kRepeatFadeMs), L_k/4)` samples and is crossfaded **sum-preserving** with the new head: with `W_k = L_k` and `r = 1` both heads are phase-aligned copies of the same window, where an equal-power sum would bump +3 dB. The last repeat is cut by the next source event's own crossfade.

### 5.3 Reverse
`R = min(S(p0), validSpan)`. Sample `j = f − E`: `j < R → w = x[E − 1 − j]·G(j)` (integer reads), `j ≥ R → w = 0`. `G` = linear-in-dB ramp `p1 → p2` over `Nev`, multiplied by `rc((R − j)/F_r)` over the last `F_r = min(F, R/2)` samples before exhaustion. At `E` the output mirrors the input around `E − 1`, which is value-continuous.

### 5.4 TapeStop / TapeStart
Read head `rp` (double, fill-local). At `E`: `rp = E − kTapeInitialLag`. Per sample `u = (f − E)/Nev`, `s = bend(u, p0)` (math.h; `p0 ∈ [−1,1]`, −1 = fast early drop/"log", 0 linear, +1 late drop/"exp"):
- TapeStop: `v = 1 − (1 − p1)·s`; TapeStart: `v = p1 + (1 − p1)·s`.
- `w = rc(min(1, v/kTapeMuteRate))·h(rp)`, then `rp += v`. The gain reaches 0 at zero speed (no held DC sample, a full stop lands in silence) and the raised cosine removes the corner where the clamp hits 1.
- Lag clamp: `if f − rp > maxLag: rp = f − maxLag`.
- Rejoin: the following source event (normally Pass) crossfades from the lagged head to live over `kSourceFadeMs`. A TapeStart ends at `v = 1` so the crossfade is between live and a delayed copy, which the sum-preserving fade handles without a level bump.

## 6. Modifiers
Lane cursors are independent of the source lane. Each modifier is a pure function of `f` plus per-sample-advanced state, so partition cannot matter.

**Gate** (`p0` stepsPerBeat, `p1` duty, `p2` pattern bits, `p3/p4` attack/release ms, `p5` depth). `stepLen = spbFill/p0`; `s = floor((f − E)/stepLen)`, `phase = (f − E − s·stepLen)/stepLen`; `pattern = (uint16)p2`, `0 → 0xFFFF`; open iff bit `(s mod 16)` set and `phase < p1`. Openness target `1` (open, or no gate event active) else `0`; `o` is a slew limiter: per-sample step `1/S_ms(attack)` upward, `1/S_ms(release)` downward (0 ms = instant). Gain `= 1 − p5·(1 − rc(o))`: the raised cosine makes attack/release corner-free. `o`, the release rate and depth persist across events, so an event ending while closed releases naturally to 1.

**Filter** (plan-wide `filterType`, lanes `FilterCutoff/Resonance/Mix`; inactive if `Off` or the cutoff lane has no points). TPT SVF (Zavalishin): `g = tan(π·fc/sr)`, `k = 1/Q`, `a1 = 1/(1 + g(g + k))`, `a2 = g·a1`, `a3 = g·a2`; per sample `v3 = x − ic2; v1 = a1·ic1 + a2·v3; v2 = ic2 + a2·ic1 + a3·v3; ic1 = 2v1 − ic1; ic2 = 2v2 − ic2; LP = v2, BP = v1, HP = x − k·v1 − v2`. Lane mapping (shared with 03): `fc = 20·1000^v` Hz clamped to `0.45·sr`, `Q = 0.5 + 11.5·v`, mix `= v` (lanes missing → res 0, mix 1). Lane evaluation: segment `a→b` with bend `k` (clamped ±0.99): `s = (1+k)/(1−k)`, `x' = x·s/(1 + (s−1)x)`, `value = a + (b−a)x'`. Coefficients: at every anchor `f mod 16 == 0` the lanes are evaluated at beat `f/spbFill` and `g, k, mix` are ramped linearly per sample over the next 16 samples (one-anchor lag, 0.33 ms, inaudible; avoids per-sample `tan`). State zeroed at fill start. `y = x + mix·(filt − x)`.

**Crush** (`p0/p1` bits, `p2/p3` downsample, `p4` mix). `u = (f − E)/Nev`, `bits = p0 + (p1 − p0)u`, `D = p2 + (p3 − p2)u`, `q = 2^(bits − 1)`. S&H: `if phase ≥ 1 { phase −= 1; held = round(x·q)/q }; phase += 1/D` (`phase = 1` at `E`, so the first sample is taken and holds last exactly `D` samples for integer `D`). Edge ramp `e = rc(clamp(min(f − E + 1, Eend − f)/F, 0, 1))`; `y = x + p4·e·(held − x)`.

## 7. Click-free transitions
### 7.1 Source boundaries
Every source change (event boundary, entry of Silence) renders both sources for `F = min(S_ms(kSourceFadeMs), Nev_new/2)` samples: `a = rc(j/F)`, `w = (1 − a)·old + a·new`. **Sum-preserving**, because the two sides are frequently the *same* stream (Pass → live first stutter slice, Pass → tape head at rate 1, tape at rate 1 → Pass, a repeat → the next repeat of the same window); an equal-power sum of identical signals bumps +3 dB and leaves slope corners. For uncorrelated sides the sum-preserving fade dips at most 3 dB for 1.5 ms, which is inaudible. Equal-power is used only where the sides are guaranteed different: the stutter window seam (§5.2).

### 7.2 Fill entry/exit and mix
`e(f) = min(entry, exit, cut)` with linear ramps `entry = (f − playOffset + 1)/F` (a mid-fill entry fades too), `exit = (N − f)/F` (pre-rolled, so the fill never overshoots its region) and `cut = 1 − (f − cutF)/S_ms(kCutFadeMs)` after a Cut (the fill ends when it reaches 0); `m = mix·rc(e)`. Sum-preserving for the same reason as §7.1 (dry and wet are often identical at entry).
Output `y = (m ≥ 1) ? G·w : x + m·(G·w − x)`, `G = 10^(out_gain/20)`. With `m = 1, G = 1` the output is bit-exactly `w`; with `m = 0`, or whenever `w == x` (Pass, live stutter slice) and `G = 1`, it is bit-exactly `x` — a fill that starts with Pass is transparent through its ramps. **Idle**: the engine does not touch the buffers (apart from the capture write), hence output ≡ input.

### 7.3 Measurable "no click"
Second difference `Δ²y[n] = y[n] − 2y[n−1] + y[n−2]` is a cheap high-pass; a click is a broadband burst. Definition used by the tests: for a band-limited test input `x` (sine ≤ 1 kHz at 48 kHz) and unpitched/unresampled sources, `max|Δ²y| ≤ 4·max|Δ²x| + 1e-4` over every window `[B − 2F, B + 2F]` around a boundary `B` (fill entry/exit, each source event start, each repeat start, modifier event edges, cut). For Crush (inherently stepped) the boundary window must not exceed the event-interior maximum ×1.5. For pitched or tape material the bound is scaled by `max(1, r²)`. The ratio 4 (+12 dB) tolerates the raised-cosine ramp term `A·(π/F)²/2` of a 2–3 ms fade with `A` up to the full signal difference, plus the `2·Δenv·Δx` cross term. A linear ramp would fail this bound (`A/F ≈ 7e-3` for `A = 1`, `F = 144`), which is why every ramp is a raised cosine.

## 8. Determinism and block-size invariance
Output is bit-identical for any block partition and for real-time vs. offline, given the same sample-accurate inputs. Rules:
1. All state advances **per sample** (or per fixed sub-block anchored to `f mod 16`), never per block. No per-block parameter smoothing; `mix/out_gain` are frozen per fill (§2.4).
2. Positions derive from integers: `t`, `f`, event bounds `S(b)` (computed once at fill start from `spbFill`), the internal clock `clockT`. The only float-to-sample conversion that depends on the block is the fill-start offset, whose error is bounded far below `ε` (§12 A1).
3. No wall clock, no block counters, no `rand()`. Seeds come from `mixSeed`.
4. Inputs with block granularity by contract (params once per block, host PPQ) are the host's responsibility; the engine guarantees invariance whenever these are identical functions of absolute sample time (constant parameters in the tests).
5. Scalar per-sample loops, no `-ffast-math`, no FMA contraction (`-ffp-contract=off`), identical operation order in every path. Fade shapes only through the precomputed tables; `tan/pow/exp2` are evaluated at anchors or repeat starts from `f`.
6. Capture reads of the current block are valid only for `pos ≤ t` (the whole block is written first); every read head is proven to stay `≤ t` (stutter wrap, tape lag ≥ 3, reverse `< E`).

## 9. Real-time safety, performance, telemetry
- `prepare()` allocates (capture, fade tables); nothing after. No locks, no I/O, no exceptions, no virtual calls in the per-sample loop (one `makePlan` virtual call per fill start). Loops are bounded by `numSamples`, `kMaxEvents`.
- Budget: `< 2 %` of one core at 48 kHz stereo = 0.4 µs per frame. Idle cost: one memcpy per channel. Fill cost: Hermite (≈ 20 flops), SVF (10), gate/crush/mix (≈ 15), two sources only inside crossfades: ≈ 100 flops/frame. Measured: ≈ 135 ns/frame with every effect active (`-O2`, one core of the CI container), 3× headroom. A perf test prints the measured ns/frame and asserts `< 400 ns` in Release.
- **Telemetry** (`EngineTelemetry`, single-writer atomics, relaxed): `playing, usingInternalClock, bpm, beat` (block start), `fillActive, fillProgress (0..1), fillSeed, phraseIndex, currentSourceType, fillCount, lastFillStartT, rejectedPlans, captureUnderruns, planEpoch` + `lastPlan()` double buffer: writer copies the plan to `plans[epoch & 1]` then publishes `epoch + 1` (release); reader loads epoch, reads `plans[(epoch−1) & 1]`, re-checks epoch (seqlock). Written at fill start/end and once per block.

## 10. Block processing loop
```
BlockTime bt = tracker.update(info, n); capture.write(in, n); settings read; s = 0
while s < n: a = scheduler.nextAction(bt, s, n, active, act)        // a ∈ [s, n]
             e = active ? player.render(s, a) : a                  // stops early at fill end → returns e < a
             if e < a: s = e; continue                              // idle again: re-query (back-to-back, mid entry)
             if a < n: apply(act)  (Start → makePlan/validate/start; Cut → player.cut())
             s = a
telemetry update
```
Progress is guaranteed because every action consumes state (MIDI event, pending, edge); a guard of 64 iterations per block exists for safety.

## 11. Test plan (core/tests)
1. Plan validation: valid plan; gap/overlap/short source lane; lane not starting at 0 / ending at length / out-of-range value; too many events; length mismatch.
2. Transport: beat↔offset conversion; tempo change between blocks (no jump, new spb); loop jump flagged; stop→internal clock continuity; missing PPQ/bpm fallbacks.
3. Auto-Phrase positions: 120 BPM 4/4, phrase 4 bars, fill 1 bar, offsets 0 and 1 bar, `lastFillStartT` must hit beats 12, 28 / 16, 32; per-phrase seeds differ; mid-fill entry after a loop jump into a region; fill_len > phrase clamps.
4. Automation: rising edge quantized to 1/4; early release fades to dry within 10 ms. MIDI: note length map, velocity → intensity, hold note-off cut, retrigger.
5. Sources on known signals: Pass bit-exact; Silence exact 0; Reverse on a ramp equals `x[E−1−j]`; Stutter on a ramp: repeat 1 equals window, +12 semis reads every 2nd sample, −6 dB decay; TapeStop linear curve on a ramp reproduces `c·rp_j·g` within 1e-4; Gate pattern on DC gives exact 0/1 steps; Filter LP passes DC, HP removes DC, LP at 100 Hz attenuates 10 kHz by > 40 dB; Crush: DC 0.3 at 2 bits → 0.5, downsample 4 holds 4 samples.
6. Click detector (§7.3) over a plan containing all source types, repeats, gate, crush, entry/exit; fill entry into a stutter and exit from silence; an early-release cut mid-reverse; a mid-fill re-entry after a loop jump.
7. Idle bit-exactness: 10 s of noise, no fill → `memcmp` equal; and inside a Pass-only fill at mix 100 % / 0 dB.
8. Block-size invariance: 10 s stereo input rendered with block sizes 1, 7, 64, 512 and a seeded random partition, Auto-Phrase and MIDI scenarios, `memcmp` identical.
9. No allocation: global `operator new` counter is zero across all `process()` calls; capture capacity formula.
10. Performance: ns/frame printed; soft assert < 400 ns in Release.

## 12. Proposed amendments to 01
- **A1 (epsilon):** 01 §2 uses `ε = 1e-9` inside `ceil((b − blockStartBeat)·spb − 1e-9)`. The quantity is in samples, and for positions beyond ≈ 2^22 samples (≈ 90 s at 48 kHz) `1e-9` is below the double ULP of the product, so it cannot absorb rounding and a grid-aligned boundary can land one sample late depending on the block start. The engine uses `kBeatEpsSamples = 1e-6` samples (still 10^4× below audibility). Proposal: state the epsilon in samples as `1e-6`.
- **A2 (GenSettings location):** `GenSettings`, `EnergyCurve`/`StyleTable` forward declarations live in `transitgen/FillPlan.h` so the engine's `FillRequest` can embed them without depending on 03.
- **A3 (MIDI view):** 01 names `MidiEventView` without defining it; defined in `transitgen/Transport.h` (§2.3).
- **A4 (mix/out_gain):** 01 says they apply only during a fill; the engine additionally freezes them per fill (§2.4). If live mix automation inside a fill is wanted, it must be sample-accurate from the host side.
