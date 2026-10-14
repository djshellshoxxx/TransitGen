#include "Parameters.h"

#include <algorithm>
#include <cmath>

namespace tgp {

namespace {
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using API = juce::AudioParameterInt;

juce::ParameterID id(const char* s) { return juce::ParameterID{ s, kParamVersion }; }

std::unique_ptr<APF> percent(const char* pid, const char* name, float def)
{
    return std::make_unique<APF>(id(pid), name, juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), def,
                                 juce::AudioParameterFloatAttributes().withLabel("%"));
}

constexpr float kEndingBeats[] = { 0.25f, 0.5f, 1.0f, 2.0f };
constexpr int   kPhraseBars[] = { 4, 8, 16, 32 };

int indexParam(const std::atomic<float>* p, int count) noexcept
{
    const int i = static_cast<int>(std::lround(p->load(std::memory_order_relaxed)));
    return std::clamp(i, 0, count - 1);
}
} // namespace

StyleChoices StyleChoices::fromTable(const tg::StyleTable* table)
{
    StyleChoices c;
    if (table != nullptr) {
        std::array<const tg::Style*, tg::kMaxStyles> sorted{};
        const int n = std::min(table->numStyles, tg::kMaxStyles);
        for (int i = 0; i < n; ++i) sorted[static_cast<size_t>(i)] = &table->styles[i];
        std::sort(sorted.begin(), sorted.begin() + n, [](auto* a, auto* b) { return a->id < b->id; });
        for (int i = 0; i < n; ++i) {
            c.ids[static_cast<size_t>(i)] = sorted[static_cast<size_t>(i)]->id;
            c.names.add(juce::String::fromUTF8(sorted[static_cast<size_t>(i)]->name));
        }
        c.numStyles = n;
    }
    if (c.numStyles == 0) {   // no style table: the generator falls back to a single Pass event
        c.numStyles = 1;
        c.ids[0] = 1;
        c.names.add("Dubstep");
    }
    c.defaultIndex = std::max(0, c.indexOf(1));   // Dubstep (01 §6)
    return c;
}

uint16_t StyleChoices::idAt(int index) const noexcept
{
    return ids[static_cast<size_t>(std::clamp(index, 0, numStyles - 1))];
}

int StyleChoices::indexOf(uint16_t styleId) const noexcept
{
    for (int i = 0; i < numStyles; ++i)
        if (ids[static_cast<size_t>(i)] == styleId) return i;
    return -1;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(const StyleChoices& styles)
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<APC>(id(pid::mode), "Trigger Mode", juce::StringArray{ "Auto-Phrase", "Automation", "MIDI" }, 0));
    l.add(std::make_unique<APC>(id(pid::phraseBars), "Phrase Length", juce::StringArray{ "4 bars", "8 bars", "16 bars", "32 bars" }, 1));
    l.add(std::make_unique<API>(id(pid::phraseOff), "Phrase Offset", 0, 31, 0, juce::AudioParameterIntAttributes().withLabel("bars")));
    l.add(std::make_unique<APC>(id(pid::fillLen), "Fill Length",
                                juce::StringArray{ "1/4 beat", "1/2 beat", "1 beat", "2 beats", "3 beats", "4 beats", "2 bars", "4 bars", "8 bars" },
                                static_cast<int>(tg::FillLength::Beat4)));
    l.add(std::make_unique<APB>(id(pid::trigger), "Fill Trigger", false));
    l.add(std::make_unique<APC>(id(pid::quantize), "Trigger Quantize", juce::StringArray{ "Off", "1/16", "1/8", "1/4", "1/2", "Bar" }, 3));
    l.add(std::make_unique<APC>(id(pid::style), "Style", styles.names, styles.defaultIndex));
    l.add(percent(pid::intensity, "Intensity", 70.0f));
    l.add(percent(pid::density, "Density", 50.0f));
    l.add(percent(pid::pitchAmt, "Pitch", 50.0f));
    l.add(percent(pid::crushAmt, "Crush", 50.0f));
    l.add(percent(pid::filterAmt, "Filter", 50.0f));
    l.add(std::make_unique<APC>(id(pid::ending), "Ending",
                                juce::StringArray{ "Style", "None", "Silence", "Tape Stop", "Reverse Swell", "Roll" }, 0));
    l.add(std::make_unique<APC>(id(pid::endingLen), "Ending Length", juce::StringArray{ "1/4 beat", "1/2 beat", "1 beat", "2 beats" }, 1));
    l.add(std::make_unique<API>(id(pid::seed), "Seed", kMinSeed, kMaxSeed, 1));
    l.add(std::make_unique<APC>(id(pid::variation), "Variation", juce::StringArray{ "Fixed", "Per Phrase" }, 1));
    l.add(percent(pid::mix, "Mix", 100.0f));
    l.add(std::make_unique<APF>(id(pid::outGain), "Output", juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
                                juce::AudioParameterFloatAttributes().withLabel("dB")));
    return l;
}

void ParameterSnapshot::bind(juce::AudioProcessorValueTreeState& apvts, const StyleChoices& styles)
{
    auto get = [&apvts](const char* p) {
        std::atomic<float>* v = apvts.getRawParameterValue(p);
        jassert(v != nullptr);
        return v;
    };
    styles_ = &styles;
    mode_ = get(pid::mode);
    phraseBars_ = get(pid::phraseBars);
    phraseOff_ = get(pid::phraseOff);
    fillLen_ = get(pid::fillLen);
    trigger_ = get(pid::trigger);
    quantize_ = get(pid::quantize);
    style_ = get(pid::style);
    intensity_ = get(pid::intensity);
    density_ = get(pid::density);
    pitch_ = get(pid::pitchAmt);
    crush_ = get(pid::crushAmt);
    filter_ = get(pid::filterAmt);
    ending_ = get(pid::ending);
    endingLen_ = get(pid::endingLen);
    seed_ = get(pid::seed);
    variation_ = get(pid::variation);
    mix_ = get(pid::mix);
    outGain_ = get(pid::outGain);
}

void ParameterSnapshot::read(tg::EngineSettings& s) const noexcept
{
    auto pct = [](const std::atomic<float>* p) { return std::clamp(p->load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f); };
    s.mode = static_cast<tg::TriggerMode>(indexParam(mode_, 3));
    s.phraseBars = kPhraseBars[indexParam(phraseBars_, 4)];
    s.phraseOffset = indexParam(phraseOff_, 32);
    s.fillLen = static_cast<tg::FillLength>(indexParam(fillLen_, 9));
    s.trigger = trigger_->load(std::memory_order_relaxed) >= 0.5f;
    s.quantize = static_cast<tg::Quantize>(indexParam(quantize_, 6));
    s.styleId = styles_->idAt(indexParam(style_, styles_->numStyles));
    s.intensity = pct(intensity_);
    s.density = pct(density_);
    s.pitchAmount = pct(pitch_);
    s.crushAmount = pct(crush_);
    s.filterAmount = pct(filter_);
    s.endingOverride = static_cast<uint8_t>(indexParam(ending_, 6));   // 0 = style, else EndingType + 1
    s.endingBeats = kEndingBeats[indexParam(endingLen_, 4)];
    s.seed = static_cast<uint32_t>(std::clamp(indexParam(seed_, kMaxSeed + 1), kMinSeed, kMaxSeed));
    s.variation = static_cast<tg::Variation>(indexParam(variation_, 2));
    s.mix = pct(mix_);
    s.outGainDb = std::clamp(outGain_->load(std::memory_order_relaxed), -12.0f, 12.0f);
}

} // namespace tgp
