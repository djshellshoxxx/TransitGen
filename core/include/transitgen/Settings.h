// TransitGen core — engine settings, fill requests and the plan provider interface (02 §2, §3).
#pragma once

#include "transitgen/FillPlan.h"

#include <cstdint>

namespace tg {

enum class TriggerMode : uint8_t { AutoPhrase = 0, Automation, Midi };
enum class FillLength  : uint8_t { Beat1_4 = 0, Beat1_2, Beat1, Beat2, Beat3, Beat4, Bars2, Bars4, Bars8 };
enum class Quantize    : uint8_t { Off = 0, N16, N8, N4, N2, Bar };
enum class Variation   : uint8_t { Fixed = 0, PerPhrase };

/// Snapshot of all host parameters the engine reads (once per block, 01 §7).
struct EngineSettings {
    TriggerMode mode = TriggerMode::AutoPhrase;
    int         phraseBars = 8;          // 4, 8, 16, 32
    int         phraseOffset = 0;        // bars
    FillLength  fillLen = FillLength::Beat4;
    bool        trigger = false;
    Quantize    quantize = Quantize::N4;
    uint16_t    styleId = 0;
    float       intensity = 0.7f;
    float       density = 0.5f;
    float       pitchAmount = 0.5f;
    float       crushAmount = 0.5f;
    float       filterAmount = 0.5f;
    uint8_t     endingOverride = 0;
    float       endingBeats = 0.5f;
    uint32_t    seed = 1;
    Variation   variation = Variation::PerPhrase;
    float       mix = 1.0f;              // 0..1
    float       outGainDb = 0.0f;
    bool        midiHold = false;        // false = one-shot
    bool        midiVelocityToIntensity = true;
    const EnergyCurve* curve = nullptr;
    const StyleTable*  styles = nullptr;
};

/// Beats of a FillLength choice given the bar length of the current time signature.
inline double fillLengthBeats(FillLength f, double barLen) noexcept
{
    switch (f) {
        case FillLength::Beat1_4: return 0.25;
        case FillLength::Beat1_2: return 0.5;
        case FillLength::Beat1:   return 1.0;
        case FillLength::Beat2:   return 2.0;
        case FillLength::Beat3:   return 3.0;
        case FillLength::Beat4:   return 4.0;
        case FillLength::Bars2:   return 2.0 * barLen;
        case FillLength::Bars4:   return 4.0 * barLen;
        case FillLength::Bars8:   return 8.0 * barLen;
    }
    return 4.0;
}

/// Quantize grid step in beats (0 = off).
inline double quantizeStepBeats(Quantize q, double barLen) noexcept
{
    switch (q) {
        case Quantize::Off: return 0.0;
        case Quantize::N16: return 0.25;
        case Quantize::N8:  return 0.5;
        case Quantize::N4:  return 1.0;
        case Quantize::N2:  return 2.0;
        case Quantize::Bar: return barLen;
    }
    return 1.0;
}

/// Everything the provider needs to build the plan for one fill. Frozen at fill start.
struct FillRequest {
    GenSettings gen{};
    double      bpm = 120.0;
    double      sampleRate = 48000.0;
    int64_t     phraseIndex = 0;
    int         tsNum = 4, tsDen = 4;
    TriggerMode mode = TriggerMode::AutoPhrase;
    int         midiNote = -1;      // -1 when not MIDI-triggered
    float       velocity = 0.0f;    // 0..1
};

/// Called on the audio thread at fill start. Must be allocation-free and bounded.
struct IPlanProvider {
    virtual ~IPlanProvider() = default;
    virtual bool makePlan(const FillRequest& req, FillPlan& out) noexcept = 0;
};

} // namespace tg
