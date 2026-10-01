// TransitGen plugin — host-automatable parameters (01 §6) and the per-block settings snapshot.
#pragma once

#include "transitgen/Settings.h"
#include "transitgen/StyleTable.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace tgp {

/// Permanent parameter IDs (01 §6): never rename, deprecate instead.
namespace pid {
inline constexpr const char* mode        = "mode";
inline constexpr const char* phraseBars  = "phrase_bars";
inline constexpr const char* phraseOff   = "phrase_offset";
inline constexpr const char* fillLen     = "fill_len";
inline constexpr const char* trigger     = "trigger";
inline constexpr const char* quantize    = "quantize";
inline constexpr const char* style       = "style";
inline constexpr const char* intensity   = "intensity";
inline constexpr const char* density     = "density";
inline constexpr const char* pitchAmt    = "pitch_amt";
inline constexpr const char* crushAmt    = "crush_amt";
inline constexpr const char* filterAmt   = "filter_amt";
inline constexpr const char* ending      = "ending";
inline constexpr const char* endingLen   = "ending_len";
inline constexpr const char* seed        = "seed";
inline constexpr const char* variation   = "variation";
inline constexpr const char* mix         = "mix";
inline constexpr const char* outGain     = "out_gain";
} // namespace pid

inline constexpr int kParamVersion = 1;   // ParameterID version hint for every M3 parameter
inline constexpr int kMinSeed = 1, kMaxSeed = 99999;

/// Style choice entries: choice index -> style id (factory styles sorted by id; user styles later).
struct StyleChoices {
    int                                numStyles = 0;
    std::array<uint16_t, tg::kMaxStyles> ids{};
    juce::StringArray                  names;
    int                                defaultIndex = 0;

    static StyleChoices fromTable(const tg::StyleTable* table);
    uint16_t idAt(int index) const noexcept;   // audio-thread safe
    int      indexOf(uint16_t id) const noexcept;   // -1 if unknown
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(const StyleChoices& styles);

/// Raw parameter pointers resolved once; read() is allocation-free and lock-free (01 §7).
class ParameterSnapshot {
public:
    void bind(juce::AudioProcessorValueTreeState& apvts, const StyleChoices& styles);
    /// Fills every parameter-driven field of `s` (curve/styles pointers are left untouched).
    void read(tg::EngineSettings& s) const noexcept;

private:
    const StyleChoices* styles_ = nullptr;
    std::atomic<float>* mode_ = nullptr;
    std::atomic<float>* phraseBars_ = nullptr;
    std::atomic<float>* phraseOff_ = nullptr;
    std::atomic<float>* fillLen_ = nullptr;
    std::atomic<float>* trigger_ = nullptr;
    std::atomic<float>* quantize_ = nullptr;
    std::atomic<float>* style_ = nullptr;
    std::atomic<float>* intensity_ = nullptr;
    std::atomic<float>* density_ = nullptr;
    std::atomic<float>* pitch_ = nullptr;
    std::atomic<float>* crush_ = nullptr;
    std::atomic<float>* filter_ = nullptr;
    std::atomic<float>* ending_ = nullptr;
    std::atomic<float>* endingLen_ = nullptr;
    std::atomic<float>* seed_ = nullptr;
    std::atomic<float>* variation_ = nullptr;
    std::atomic<float>* mix_ = nullptr;
    std::atomic<float>* outGain_ = nullptr;
};

} // namespace tgp
