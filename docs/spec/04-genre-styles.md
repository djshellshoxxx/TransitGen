# TransitGen Specification — 04 Genre Styles

A style is a JSON file of rules the generator (03) follows. Factory styles live in `styles/NN-name.json` (NN = id, which fixes the load order) and are compiled into the binary: `core/cmake/EmbedStyles.cmake` generates a source file with each file as a string literal, so there is no runtime file I/O. They are parsed on the message thread into an immutable `StyleTable` (POD, published to the audio thread per 01 §7). User styles (v1.x) use the same format from the user folder.

## 1. Format

**Tables:** `T = [[e, v], ...]` are piecewise-linear functions of energy e ∈ [0,1], with 1–5 points sorted by e and clamped at the ends.

| Key | Type | Meaning |
|---|---|---|
| `id` | int | stable id (factory 1–99, user 100+) |
| `name`, `version` | string, int | |
| `grid` | beats | boundary grid (0.25 = 1/16, 1/3 = 1/8 triplet, 1/6 = 1/16 triplet) |
| `respectBars` | bool | segments don't cross bar lines |
| `segment.len` | beats[] (≤ 6) | candidate segment lengths |
| `segment.weight` | T[] | one table per length |
| `source.prob` | {type: T} | relative weights for Pass, Stutter, Reverse, TapeStop, TapeStart, Silence (missing = 0) |
| `stutter.slices` | beats[] (≤ 6) | slice candidates |
| `stutter.sliceWeight` | T[] | one table per slice |
| `stutter.rollProb` | T | chance a stutter rolls (slice shrinks) |
| `stutter.rollDiv` | 2, 4 or 8 | final slice = slice / rollDiv |
| `stutter.rampMode` | 0 stepped / 1 smooth | |
| `stutter.pitchProb` | T | |
| `stutter.pitchSemis` | 0–24 | max pitch move at e = 1, pitch = 100 % |
| `stutter.pitchDir` | "up", "down", "both" | |
| `stutter.pitchFromZero` | bool | true = glide from 0 to the target, false = a constant offset |
| `stutter.decayDb` | ≤ 0 | per-repeat decay at e = 1 |
| `tape.curve` | −1..1 | tape stop/start bend |
| `silence.fadeMs` | 0–50 | |
| `gate.prob`, `gate.duty` | T | |
| `gate.rates`, `gate.rateWeights` | int[], float[] | steps per beat |
| `gate.patterns` | string[] (1–8, 16 chars of 0/1) | character i = step i (bit i of the event's pattern), at least one `1` |
| `gate.attackMs`, `gate.releaseMs`, `gate.depth` | float | |
| `crush.prob` | T | |
| `crush.minBits`, `crush.maxDown`, `crush.mix` | float | |
| `filter.type` | "Off", "LowPass", "HighPass", "BandPass" | |
| `filter.from`, `filter.to`, `filter.res`, `filter.wobble` | 0..1 | normalized lane values (Hz = 20·1000^v) |
| `ending.type`, `ending.beats` | "None", "Silence", "TapeStop", "ReverseSwell", "Roll"; beats | `ending.beats` is used when the Ending param is "Style"; an override uses Ending Length (03 §5 step 2) |

**Validation on load:** schema + range checks. An invalid user style is rejected with a message in the UI; a factory style failing validation is a build failure (CI test).
- Strict JSON (no comments or trailing commas); unknown keys and duplicate keys are errors; every key in the table is required except individual `source.prob` entries.
- `id`: integer, factory 1–99, user 100–65535, unique in the table. `name`: 1–31 bytes. `version`: integer ≥ 1.
- `grid` ∈ [1/64, 4]; `segment.len` ∈ [1/64, 32]; `stutter.slices` ∈ [1/256, 8] (capture bound, 02 §4); `stutter.decayDb` ∈ [−12, 0]; `gate.rates` ⊂ {1,2,3,4,6,8} (≤ 6 entries) with as many `rateWeights` ≥ 0, not all 0; `attackMs`, `releaseMs` ∈ [0, 1000]; `crush.minBits` ∈ [1,16], `maxDown` ∈ [1,64]; `ending.beats` ∈ [1/64, 16].
- Tables: 1–5 `[e, v]` pairs, e ∈ [0,1] strictly increasing. Weights (`segment.weight`, `source.prob`, `stutter.sliceWeight`) v ∈ [0, 1000]; probabilities (`rollProb`, `pitchProb`, `gate.prob`, `crush.prob`) v ∈ [0,1]; `gate.duty` v ∈ [0.05, 1]. Each weight family must have a positive sum at every energy (checked at all breakpoints and 0, 1).
- Errors name the key path (`stutter.rollDiv: must be 2, 4 or 8`) or the position of a syntax error (`line 3, column 7: expected ':'`).
- The in-memory `StyleTable` (`transitgen/StyleTable.h`) holds up to 64 styles; gate pattern strings are converted to 16-bit masks at load time.

## 2. Factory styles

### 2.1 Dubstep (id 1): half-time weight, big rolls into the drop, tape stop
```json
{"id":1,"name":"Dubstep","version":1,"grid":0.25,"respectBars":true,
 "segment":{"len":[0.5,1,2],"weight":[[[0,0.2],[1,1]],[[0,1],[1,0.6]],[[0,1],[1,0.1]]]},
 "source":{"prob":{"Pass":[[0,0.6],[1,0.05]],"Stutter":[[0,0.35],[1,0.8]],"Reverse":[[0,0.05],[1,0.1]],"Silence":[[0,0],[1,0.05]]}},
 "stutter":{"slices":[0.5,0.25,0.125,0.0625],"sliceWeight":[[[0,1],[1,0.1]],[[0,0.8],[1,0.5]],[[0,0.2],[1,1]],[[0,0],[1,0.8]]],
  "rollProb":[[0,0.1],[1,0.7]],"rollDiv":4,"rampMode":0,"pitchProb":[[0,0],[1,0.5]],"pitchSemis":12,"pitchDir":"up","pitchFromZero":true,"decayDb":-1},
 "tape":{"curve":0.3},"silence":{"fadeMs":3},
 "gate":{"prob":[[0,0],[1,0.25]],"rates":[2,4],"rateWeights":[1,1],"duty":[[0,0.7],[1,0.4]],"patterns":["1111111111111111","1011101110111011"],"attackMs":1,"releaseMs":10,"depth":1},
 "crush":{"prob":[[0,0],[1,0.3]],"minBits":6,"maxDown":6,"mix":0.8},
 "filter":{"type":"HighPass","from":0.0,"to":0.5,"res":0.3,"wobble":0},
 "ending":{"type":"Silence","beats":0.5}}
```

### 2.2 Drum & Bass (id 2): fast 1/16–1/32 rolls, reverse snares, HP build
```json
{"id":2,"name":"Drum & Bass","version":1,"grid":0.25,"respectBars":true,
 "segment":{"len":[0.25,0.5,1],"weight":[[[0,0.3],[1,1]],[[0,1],[1,0.7]],[[0,0.8],[1,0.2]]]},
 "source":{"prob":{"Pass":[[0,0.5],[1,0.1]],"Stutter":[[0,0.35],[1,0.75]],"Reverse":[[0,0.15],[1,0.15]]}},
 "stutter":{"slices":[0.25,0.125,0.0625],"sliceWeight":[[[0,1],[1,0.3]],[[0,0.6],[1,1]],[[0,0.1],[1,0.8]]],
  "rollProb":[[0,0.2],[1,0.6]],"rollDiv":2,"rampMode":0,"pitchProb":[[0,0],[1,0.3]],"pitchSemis":7,"pitchDir":"both","pitchFromZero":false,"decayDb":-0.5},
 "tape":{"curve":0},"silence":{"fadeMs":2},
 "gate":{"prob":[[0,0.05],[1,0.2]],"rates":[4],"rateWeights":[1],"duty":[[0,0.6],[1,0.4]],"patterns":["1111111111111111"],"attackMs":0.5,"releaseMs":5,"depth":1},
 "crush":{"prob":[[0,0],[1,0.15]],"minBits":8,"maxDown":4,"mix":0.6},
 "filter":{"type":"HighPass","from":0.05,"to":0.6,"res":0.2,"wobble":0},
 "ending":{"type":"ReverseSwell","beats":1}}
```

### 2.3 Trap (id 3): triplet hi-hat-style rolls, pitch-down stutters, tape stop
```json
{"id":3,"name":"Trap","version":1,"grid":0.16666666666666666,"respectBars":true,
 "segment":{"len":[0.3333333333333333,0.6666666666666666,1,2],"weight":[[[0,0.2],[1,1]],[[0,0.6],[1,0.8]],[[0,1],[1,0.4]],[[0,0.8],[1,0.1]]]},
 "source":{"prob":{"Pass":[[0,0.6],[1,0.15]],"Stutter":[[0,0.3],[1,0.7]],"TapeStop":[[0,0.05],[1,0.1]],"Silence":[[0,0.05],[1,0.05]]}},
 "stutter":{"slices":[0.3333333333333333,0.16666666666666666,0.08333333333333333],"sliceWeight":[[[0,1],[1,0.3]],[[0,0.5],[1,1]],[[0,0],[1,0.7]]],
  "rollProb":[[0,0.15],[1,0.5]],"rollDiv":2,"rampMode":0,"pitchProb":[[0,0.1],[1,0.6]],"pitchSemis":12,"pitchDir":"down","pitchFromZero":true,"decayDb":-1.5},
 "tape":{"curve":0.5},"silence":{"fadeMs":2},
 "gate":{"prob":[[0,0],[1,0.15]],"rates":[3,6],"rateWeights":[1,1],"duty":[[0,0.6],[1,0.5]],"patterns":["1111111111111111","1101101101101101"],"attackMs":1,"releaseMs":8,"depth":1},
 "crush":{"prob":[[0,0],[1,0.1]],"minBits":8,"maxDown":4,"mix":0.5},
 "filter":{"type":"LowPass","from":1.0,"to":0.55,"res":0.15,"wobble":0},
 "ending":{"type":"TapeStop","beats":1}}
```

### 2.4 Hyperpop (id 4): crushed, pitched up, extreme gates, chaotic
```json
{"id":4,"name":"Hyperpop","version":1,"grid":0.25,"respectBars":false,
 "segment":{"len":[0.25,0.5,1],"weight":[[[0,0.5],[1,1]],[[0,1],[1,0.6]],[[0,0.6],[1,0.2]]]},
 "source":{"prob":{"Pass":[[0,0.4],[1,0.1]],"Stutter":[[0,0.4],[1,0.6]],"Reverse":[[0,0.1],[1,0.15]],"TapeStart":[[0,0.05],[1,0.05]],"Silence":[[0,0.05],[1,0.1]]}},
 "stutter":{"slices":[0.25,0.125,0.0625,0.03125],"sliceWeight":[[[0,1],[1,0.3]],[[0,0.6],[1,0.8]],[[0,0.2],[1,1]],[[0,0],[1,0.6]]],
  "rollProb":[[0,0.2],[1,0.6]],"rollDiv":4,"rampMode":1,"pitchProb":[[0,0.3],[1,0.9]],"pitchSemis":24,"pitchDir":"up","pitchFromZero":true,"decayDb":0},
 "tape":{"curve":-0.3},"silence":{"fadeMs":1},
 "gate":{"prob":[[0,0.15],[1,0.5]],"rates":[4,8],"rateWeights":[1,1],"duty":[[0,0.5],[1,0.25]],"patterns":["1111111111111111","1010110110101101","1110111011101110"],"attackMs":0.5,"releaseMs":3,"depth":1},
 "crush":{"prob":[[0,0.2],[1,0.8]],"minBits":3,"maxDown":16,"mix":1},
 "filter":{"type":"BandPass","from":0.55,"to":0.75,"res":0.4,"wobble":0.1},
 "ending":{"type":"Roll","beats":0.5}}
```

### 2.5 Techno (id 5): subtle gates, filter sweep, few stutters
```json
{"id":5,"name":"Techno","version":1,"grid":0.25,"respectBars":true,
 "segment":{"len":[1,2,4],"weight":[[[0,0.3],[1,1]],[[0,1],[1,0.7]],[[0,0.8],[1,0.2]]]},
 "source":{"prob":{"Pass":[[0,0.85],[1,0.55]],"Stutter":[[0,0.15],[1,0.4]],"Silence":[[0,0],[1,0.05]]}},
 "stutter":{"slices":[1,0.5,0.25],"sliceWeight":[[[0,1],[1,0.3]],[[0,0.6],[1,1]],[[0,0.1],[1,0.6]]],
  "rollProb":[[0,0],[1,0.3]],"rollDiv":2,"rampMode":0,"pitchProb":[[0,0],[1,0]],"pitchSemis":0,"pitchDir":"up","pitchFromZero":true,"decayDb":-0.5},
 "tape":{"curve":0},"silence":{"fadeMs":5},
 "gate":{"prob":[[0,0.3],[1,0.7]],"rates":[2,4],"rateWeights":[1,2],"duty":[[0,0.8],[1,0.45]],"patterns":["1111111111111111","1011101110111011","1110111011101110"],"attackMs":2,"releaseMs":15,"depth":0.85},
 "crush":{"prob":[[0,0],[1,0.05]],"minBits":10,"maxDown":2,"mix":0.4},
 "filter":{"type":"HighPass","from":0.0,"to":0.45,"res":0.35,"wobble":0},
 "ending":{"type":"None","beats":0.5}}
```

### 2.6 IDM (id 6): dense, irregular, smooth rolls, every effect
```json
{"id":6,"name":"IDM","version":1,"grid":0.125,"respectBars":false,
 "segment":{"len":[0.125,0.25,0.375,0.5,0.75],"weight":[[[0,0.2],[1,1]],[[0,0.6],[1,1]],[[0,0.6],[1,0.6]],[[0,1],[1,0.4]],[[0,0.8],[1,0.2]]]},
 "source":{"prob":{"Pass":[[0,0.35],[1,0.1]],"Stutter":[[0,0.35],[1,0.5]],"Reverse":[[0,0.15],[1,0.15]],"TapeStop":[[0,0.05],[1,0.08]],"TapeStart":[[0,0.05],[1,0.07]],"Silence":[[0,0.05],[1,0.1]]}},
 "stutter":{"slices":[0.25,0.125,0.0625,0.03125,0.015625],"sliceWeight":[[[0,1],[1,0.2]],[[0,0.8],[1,0.6]],[[0,0.4],[1,1]],[[0,0.1],[1,0.8]],[[0,0],[1,0.5]]],
  "rollProb":[[0,0.3],[1,0.7]],"rollDiv":8,"rampMode":1,"pitchProb":[[0,0.2],[1,0.6]],"pitchSemis":12,"pitchDir":"both","pitchFromZero":false,"decayDb":-2},
 "tape":{"curve":0.2},"silence":{"fadeMs":1},
 "gate":{"prob":[[0,0.2],[1,0.5]],"rates":[3,4,6,8],"rateWeights":[1,1,1,1],"duty":[[0,0.6],[1,0.3]],"patterns":["1111111111111111","1001010010010100","1101011011010110"],"attackMs":0.5,"releaseMs":4,"depth":1},
 "crush":{"prob":[[0,0.1],[1,0.4]],"minBits":4,"maxDown":12,"mix":0.8},
 "filter":{"type":"BandPass","from":0.4,"to":0.7,"res":0.5,"wobble":0.15},
 "ending":{"type":"Roll","beats":0.5}}
```

## 3. Tuning process
Styles are tuned by ear against reference tracks. Each change bumps `version` and regenerates the golden plans (03 §8). A style's sound may change between plugin versions only when it is marked in the release notes; projects saved with an older version keep the old style version through the frozen-plan option (06 §3).
