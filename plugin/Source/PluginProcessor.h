// TransitGen plugin — the AudioProcessor: parameters, state and the core engine (M3).
#pragma once

#include "Parameters.h"
#include "PluginState.h"

#include "transitgen/Engine.h"
#include "transitgen/GeneratorPlanProvider.h"
#include "transitgen/Published.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

namespace tgp {

class TransitGenProcessor final : public juce::AudioProcessor {
public:
    TransitGenProcessor();
    ~TransitGenProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TransitGen"; }
    bool   acceptsMidi() const override { return true; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- message-thread API (editor, tests)
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts_; }
    const tg::EngineTelemetry&          telemetry() const noexcept { return engine_.telemetry(); }
    void currentSettings(tg::EngineSettings& s) const noexcept { snapshot_.read(s); }

    /// Selects a factory curve preset and publishes it to the audio thread.
    void   setCurvePreset(int index);
    juce::String curvePresetName() const;

    /// 03 §7: seed = 1 + nextInt(99999); the old seed goes to the history. Ignored while locked.
    void rerollSeed();
    void setLocked(bool locked);
    bool isLocked() const noexcept { return locked_.load(std::memory_order_relaxed); }
    bool loadedNewerState() const noexcept { return loadedNewer_.load(std::memory_order_relaxed); }

    /// Frees curve/style objects the audio thread no longer holds (call on a message-thread timer).
    void collectRetired();

    /// Bar length and bar-grid phase (beats) of the last processed block, for the UI countdown.
    double lastBarLength() const noexcept { return uiBarLen_.load(std::memory_order_relaxed); }
    double lastGridOrigin() const noexcept { return uiGridOrigin_.load(std::memory_order_relaxed); }

    static constexpr int kMaxMidiEventsPerBlock = 1024;

private:
    explicit TransitGenProcessor(std::unique_ptr<tg::StyleTable> styles);
    void publishCurve(const tg::EnergyCurve& c);
    void setParamValue(const char* id, float denormalised);
    juce::ValueTree normalisedParams(const juce::ValueTree& loaded);
    void processChunked(juce::AudioBuffer<float>&, int numChannels, tg::TransportInfo ti, int numMidi) noexcept;

    StyleChoices                       styleChoices_;
    juce::AudioProcessorValueTreeState apvts_;
    ParameterSnapshot                  snapshot_;

    tg::Engine                    engine_;
    tg::GeneratorPlanProvider     provider_;
    tg::Published<tg::EnergyCurve> curve_;
    tg::Published<tg::StyleTable>  styles_;

    // Audio thread
    std::array<tg::MidiEvent, kMaxMidiEventsPerBlock> midiScratch_{};
    int                                               preparedBlock_ = 0;
    std::atomic<bool>                                 wasBypassed_{ false };

    // Shared flags
    std::atomic<bool>   locked_{ false };
    std::atomic<bool>   loadedNewer_{ false };
    std::atomic<double> uiBarLen_{ 4.0 };
    std::atomic<double> uiGridOrigin_{ 0.0 };

    // Message thread (getStateInformation may come from a host thread: guarded, never touched by audio)
    mutable juce::CriticalSection stateLock_;
    NonParamState                 state_;
    juce::Random                  rng_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransitGenProcessor)
};

} // namespace tgp
