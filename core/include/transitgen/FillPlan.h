// TransitGen core — FillPlan data model and generator settings (01 §3, §4).
#pragma once

#include <cstdint>

namespace tg {

constexpr int kMaxEvents     = 256;
constexpr int kMaxLanePoints = 64;

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
    float     p[8];          // type-specific params, see 01 §3 table
};

enum class AutoTarget : uint8_t { FilterCutoff = 0, FilterResonance, FilterMix, kCount };

struct LanePoint { double beat; float value; float curve; }; // curve: -1..1 segment bend to next point

struct AutomationLane {
    int       numPoints = 0;          // 0 = lane inactive
    LanePoint points[kMaxLanePoints]; // sorted by beat, first at 0, last at lengthBeats
};

enum class FilterType : uint8_t { Off = 0, LowPass, HighPass, BandPass };

struct FillPlan {
    uint32_t   seed = 0;
    uint16_t   styleId = 0;
    double     lengthBeats = 0;
    FilterType filterType = FilterType::Off;
    int        numEvents = 0;
    FillEvent  events[kMaxEvents];                // sorted by (lane, startBeat)
    AutomationLane lanes[(int)AutoTarget::kCount];
};

/// Opaque to the engine; defined by 03 / 04.
struct EnergyCurve;
struct StyleTable;

struct GenSettings {       // POD snapshot of everything generation depends on
    uint32_t seed;
    uint16_t styleId;
    double   lengthBeats;
    float    intensity;    // 0..1
    float    density;      // 0..1 (0.5 = style default)
    float    pitchAmount;  // 0..1
    float    crushAmount;  // 0..1
    float    filterAmount; // 0..1
    uint8_t  endingOverride; // 0 = style default, else EndingType+1
    float    endingBeats;
    const EnergyCurve* curve;
    const StyleTable*  styles;
};

struct ValidationResult {
    bool        ok = true;
    const char* reason = "";
    int         index = -1;   // offending event / lane point, or -1
    explicit operator bool() const noexcept { return ok; }
};

/// Validates the structural invariants of 01 §3. Allocation-free, O(numEvents).
/// `expectedLengthBeats` < 0 skips the length check.
ValidationResult validate(const FillPlan& plan, double expectedLengthBeats = -1.0) noexcept;

inline bool isSourceType(EventType t) noexcept { return t <= EventType::Silence; }

} // namespace tg
