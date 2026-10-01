# TransitGen Specification — 01 Core Contract

This document fixes the interfaces that every other spec and module depends on. If anything elsewhere conflicts with it, this document wins until it is amended.

## 1. Architecture

```
                      ┌──────────── message thread ────────────┐
 UI (JUCE) ─► Params (APVTS) ─► SettingsSnapshot ─► generate() ─► FillPlan (for display)
                      └────────────────────────────────────────┘
                                   │ lock-free publish (curve, frozen plan, style table)
 ┌──────────────────────────── audio thread ─────────────────────────────────┐
 │ Host transport ─► TransportTracker ─► FillScheduler ──trigger──► generate() │
 │                                           │                   (alloc-free)  │
 │                                           ▼                                 │
 │ Audio in ─► CaptureBuffer ─► PlanPlayer ─► SourceStage ─► Gate ─► Filter ─► │
 │              (always records dry)          Crush ─► FillMix ─► OutGain ─► out │
 └─────────────────────────────────────────────────────────────────────────────┘
```

**Repository layout**
```
core/            plain C++20, no JUCE, no allocation after prepare()
  include/transitgen/*.h
  src/*.cpp
  tests/         Catch2 unit tests
plugin/          JUCE wrapper: PluginProcessor, PluginEditor, UI components
styles/          factory style JSON (compiled into binary as data)
docs/spec/       this specification
```

## 2. Timing conventions
- Musical time is `double` **beats** (quarter notes). Positions inside a plan are **relative to fill start**.
- Sample time is `int64_t`. Conversion uses the tempo of the current block: `samplesPerBeat = sampleRate * 60 / bpm`.
- Bar length in beats = `numerator * 4.0 / denominator`.
- **Boundary epsilon:** a boundary at beat `b` renders at sample `ceil((b - blockStartBeat) * samplesPerBeat - 1e-6)` (epsilon in *samples*; `1e-9` is below double precision after ~90 s at 48 kHz — amendment A1 from 02 §12).
- All event boundaries are rendered **at an exact sample index**, never rounded to a block edge.

## 3. FillPlan data model (C++)

```cpp
namespace tg {

constexpr int kMaxEvents        = 256;
constexpr int kMaxLanePoints    = 64;

enum class Lane : uint8_t { Source = 0, Gate = 1, Crush = 2 };

enum class EventType : uint8_t {
    // Source lane (exactly one covers every instant of the fill, no gaps, no overlap)
    Pass = 0, Stutter, Reverse, TapeStop, TapeStart, Silence,
    // Gate lane (optional, non-overlapping)
    Gate = 32,
    // Crush lane (optional, non-overlapping)
    Crush = 48,
};

struct FillEvent {
    Lane      lane;
    EventType type;
    double    startBeat;     // relative to fill start, >= 0
    double    lengthBeats;   // > 0
    float     p[8];          // type-specific params, see table below
};

enum class AutoTarget : uint8_t { FilterCutoff = 0, FilterResonance, FilterMix, kCount };

struct LanePoint { double beat; float value; float curve; }; // curve: -1..1 segment bend to next point

struct AutomationLane {
    int       numPoints = 0;          // 0 = lane inactive
    LanePoint points[kMaxLanePoints]; // sorted by beat, first at 0, last at lengthBeats
};

enum class FilterType : uint8_t { Off = 0, LowPass, HighPass, BandPass };

struct FillPlan {
    uint32_t  seed = 0;
    uint16_t  styleId = 0;
    double    lengthBeats = 0;
    FilterType filterType = FilterType::Off;
    int       numEvents = 0;
    FillEvent events[kMaxEvents];                // sorted by (lane, startBeat)
    AutomationLane lanes[(int)AutoTarget::kCount];
};

} // namespace tg
```

### Event parameter table (`p[]` index → meaning)
| Type | p0 | p1 | p2 | p3 | p4 | p5 |
|---|---|---|---|---|---|---|
| Pass | — | | | | | |
| Stutter | sliceStartBeats | sliceEndBeats (roll target; = p0 for constant) | rampMode (0 stepped, 1 smooth) | pitchStartSemis (−24..24) | pitchEndSemis | decayDbPerRepeat (0..−12) |
| Reverse | lengthBeats of audio reversed (≤ event length) | gainStartDb | gainEndDb | | | |
| TapeStop | curve (−1 late .. 0 linear .. +1 early, see math.h `bend`) | endRate (0..1, usually 0) | | | | |
| TapeStart | curve | startRate (0..1, usually 0) | | | | |
| Silence | fadeOutMs (0 = hard) | | | | | |
| Gate | stepsPerBeat (1,2,3,4,6,8) | duty (0.05..1) | pattern bits low 16 (as float-encoded int) | attackMs | releaseMs | depth (0..1) |
| Crush | bitsStart (1..16) | bitsEnd | downsampleStart (1..64) | downsampleEnd | mix (0..1) | |

Validation (`validate(const FillPlan&)` in core, used by tests and before playback):
- The Source lane covers `[0, lengthBeats)` exactly: contiguous, with no gaps or overlaps.
- Gate/Crush events don't overlap within their lane and lie inside `[0, lengthBeats]`.
- Lane points are sorted, the first is at 0 and the last is at lengthBeats, and values are in range.
- `numEvents <= kMaxEvents`.

## 4. Generator interface
```cpp
struct GenSettings {       // POD snapshot of everything generation depends on
    uint32_t seed;
    uint16_t styleId;
    double   lengthBeats;
    double   beatsPerBar;  // from time signature: num * 4 / den
    float    intensity;    // 0..1 (0.7 = curve as drawn, see 03 §4)
    float    density;      // 0..1 (0.5 = style default)
    float    pitchAmount;  // 0..1
    float    crushAmount;  // 0..1
    float    filterAmount; // 0..1
    uint8_t  endingOverride; // 0 = style default, else EndingType+1
    float    endingBeats;
    const EnergyCurve* curve;
    const StyleTable*  styles;
};
// Pure, deterministic, allocation-free, < 50 µs. Same input -> identical plan on every platform.
void generate(const GenSettings&, FillPlan& out);
```

## 5. Engine interface
```cpp
struct TransportInfo { bool playing; bool hasPpq; double ppq; double bpm;
                       int tsNum, tsDen; bool hasBarStart; double barStartPpq; bool looping; };

class Engine {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels);   // allocates
    void reset();                                                          // clears state, no alloc
    void setSettings(const EngineSettings&);   // audio thread, per block (trigger mode, mix, etc.)
    void process(float* const* channels, int numChannels, int numSamples,
                 const TransportInfo&, const MidiEventView& midi);
    // UI feedback (lock-free reads from other threads)
    const EngineTelemetry& telemetry() const;
};
```
The details are in [02-fill-engine.md](02-fill-engine.md). `MidiEventView`, `TransportInfo` and `BlockTime` are defined in `core/include/transitgen/Transport.h`, and `GenSettings` in `FillPlan.h` (amendments A2/A3).

## 6. Parameters (host-automatable)
IDs are permanent. Never rename one; deprecate it instead.

| ID | Name | Type / range | Default | Notes |
|---|---|---|---|---|
| `mode` | Trigger Mode | choice: Auto-Phrase, Automation, MIDI | Auto-Phrase | |
| `phrase_bars` | Phrase Length | choice: 4, 8, 16, 32 bars | 8 | Auto-Phrase |
| `phrase_offset` | Phrase Offset | int 0..31 bars | 0 | shifts the phrase grid |
| `fill_len` | Fill Length | choice: 1/4, 1/2, 1, 2, 3, 4 beats, 2, 4, 8 bars | 4 beats (= 1 bar in 4/4) | |
| `trigger` | Fill Trigger | bool | off | Automation mode, rising edge |
| `quantize` | Trigger Quantize | choice: Off, 1/16, 1/8, 1/4, 1/2, Bar | 1/4 | MIDI + Automation modes |
| `style` | Style | choice (factory + user index) | Dubstep | |
| `intensity` | Intensity | 0..100 % | 70 % | scales the energy curve |
| `density` | Density | 0..100 % | 50 % | |
| `pitch_amt` | Pitch | 0..100 % | 50 % | |
| `crush_amt` | Crush | 0..100 % | 50 % | |
| `filter_amt` | Filter | 0..100 % | 50 % | |
| `ending` | Ending | choice: Style, None, Silence, Tape Stop, Reverse Swell, Roll | Style | |
| `ending_len` | Ending Length | choice: 1/4, 1/2, 1, 2 beats | 1/2 | |
| `seed` | Seed | int 1..99999 | 1 | automatable, so seeds can vary per section |
| `variation` | Variation | choice: Fixed, Per Phrase | Per Phrase | Per Phrase: seed' = hash(seed, phraseIndex) |
| `mix` | Mix | 0..100 % | 100 % | applies only during a fill; value frozen at fill start (A4) |
| `out_gain` | Output | −12..+12 dB | 0 dB | applies only during a fill (keeps idle bit-exact); frozen at fill start (A4) |

**Non-parameter state:** energy curve points, frozen plan (optional), seed history (32 entries), UI size, selected curve preset name.

## 7. Thread model
| Data | Writer | Reader | Mechanism |
|---|---|---|---|
| Parameters | host / UI | audio | `std::atomic<float>` via APVTS, read once per block |
| Energy curve, style table, frozen plan | message | audio | immutable object + atomic pointer swap; old object freed on message thread after the audio thread acknowledges (epoch counter). Audio never frees. |
| Telemetry (playhead, active fill, current plan id/seed) | audio | UI | single-writer atomics / seqlock struct |
| Last-played plan (for UI) | audio | UI | double buffer + atomic index (plan is POD) |
