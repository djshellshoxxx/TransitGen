// TransitGen core — genre style rules as a fixed-capacity POD (04 §1). Built on the message
// thread by StyleLoader, published immutable to the audio thread (01 §7).
#pragma once

#include "transitgen/FillPlan.h"

#include <cstdint>

namespace tg {

constexpr int kMaxTablePoints  = 5;
constexpr int kMaxSegmentLens  = 6;
constexpr int kMaxSlices       = 6;
constexpr int kMaxGateRates    = 6;
constexpr int kMaxGatePatterns = 8;
constexpr int kMaxStyles       = 64;
constexpr int kStyleNameLen    = 32;   // including the terminator
constexpr int kNumSourceTypes  = 6;    // Pass..Silence, in EventType order

enum class EndingType : uint8_t { None = 0, Silence, TapeStop, ReverseSwell, Roll, kCount };
enum class PitchDir   : uint8_t { Up = 0, Down, Both };

/// Piecewise-linear function of energy, 1..5 points sorted by e, clamped at the ends (04 §1).
struct EnergyTable {
    int   n = 0;                       // 0 = constant 0 (e.g. a missing source.prob entry)
    float e[kMaxTablePoints]{};
    float v[kMaxTablePoints]{};

    double operator()(double x) const noexcept
    {
        if (n <= 0) return 0.0;
        if (x <= static_cast<double>(e[0])) return v[0];
        for (int i = 1; i < n; ++i) {
            if (x <= static_cast<double>(e[i])) {
                const double e0 = e[i - 1], e1 = e[i];
                const double u = e1 > e0 ? (x - e0) / (e1 - e0) : 1.0;
                return static_cast<double>(v[i - 1]) + (static_cast<double>(v[i]) - static_cast<double>(v[i - 1])) * u;
            }
        }
        return v[n - 1];
    }
};

struct Style {
    uint16_t id = 0;
    int      version = 0;
    char     name[kStyleNameLen]{};
    double   grid = 0.25;                         // beats
    bool     respectBars = true;

    int         numSegmentLens = 0;
    double      segmentLen[kMaxSegmentLens]{};    // beats
    EnergyTable segmentWeight[kMaxSegmentLens]{};
    EnergyTable sourceProb[kNumSourceTypes]{};    // indexed by EventType Pass..Silence

    struct {
        int         numSlices = 0;
        double      slices[kMaxSlices]{};
        EnergyTable sliceWeight[kMaxSlices]{};
        EnergyTable rollProb{};
        int         rollDiv = 2;
        uint8_t     rampMode = 0;
        EnergyTable pitchProb{};
        float       pitchSemis = 0.0f;
        PitchDir    pitchDir = PitchDir::Up;
        bool        pitchFromZero = true;
        float       decayDb = 0.0f;
    } stutter;

    float tapeCurve = 0.0f;
    float silenceFadeMs = 0.0f;

    struct {
        EnergyTable prob{}, duty{};
        int         numRates = 0;
        int         rates[kMaxGateRates]{};
        float       rateWeights[kMaxGateRates]{};
        int         numPatterns = 0;
        uint16_t    patterns[kMaxGatePatterns]{}; // bit i = step i (string character i)
        float       attackMs = 0.0f, releaseMs = 0.0f, depth = 1.0f;
    } gate;

    struct {
        EnergyTable prob{};
        float       minBits = 16.0f, maxDown = 1.0f, mix = 0.0f;
    } crush;

    struct {
        FilterType type = FilterType::Off;
        float      from = 0.0f, to = 0.0f, res = 0.0f, wobble = 0.0f;   // normalized lane values
    } filter;

    struct {
        EndingType type = EndingType::None;
        float      beats = 0.5f;
    } ending;
};

struct StyleTable {
    int   numStyles = 0;
    Style styles[kMaxStyles]{};

    const Style* find(uint16_t id) const noexcept
    {
        for (int i = 0; i < numStyles && i < kMaxStyles; ++i)
            if (styles[i].id == id) return &styles[i];
        return nullptr;
    }
};

} // namespace tg
