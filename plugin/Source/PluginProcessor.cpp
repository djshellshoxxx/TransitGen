#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "transitgen/StyleLoader.h"

#include <algorithm>
#include <cmath>

namespace tgp {

namespace {
std::unique_ptr<tg::StyleTable> loadFactoryStyles()
{
    std::string err;
    std::unique_ptr<tg::StyleTable> t = tg::makeFactoryStyleTable(&err);
    jassert(t != nullptr);   // factory styles are validated by the core tests
    if (t == nullptr) {
        DBG("TransitGen: factory styles failed to load: " << err);
    }
    return t;
}

/// Host playhead -> core TransportInfo. Every PositionInfo field is optional (JUCE 7+).
tg::TransportInfo readTransport(juce::AudioPlayHead* ph) noexcept
{
    tg::TransportInfo ti;
    if (ph == nullptr) return ti;
    const juce::Optional<juce::AudioPlayHead::PositionInfo> pos = ph->getPosition();
    if (!pos.hasValue()) return ti;
    ti.playing = pos->getIsPlaying();
    ti.looping = pos->getIsLooping();
    if (const auto ppq = pos->getPpqPosition()) {
        ti.hasPpq = std::isfinite(*ppq);
        ti.ppq = ti.hasPpq ? *ppq : 0.0;
    }
    if (const auto bpm = pos->getBpm(); bpm && std::isfinite(*bpm) && *bpm > 0.0) ti.bpm = *bpm;
    if (const auto ts = pos->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0) {
        ti.tsNum = ts->numerator;
        ti.tsDen = ts->denominator;
    }
    if (const auto bar = pos->getPpqPositionOfLastBarStart(); bar && std::isfinite(*bar)) {
        ti.hasBarStart = true;
        ti.barStartPpq = *bar;
    }
    return ti;
}
} // namespace

// ------------------------------------------------------------------------------------ construction

TransitGenProcessor::TransitGenProcessor() : TransitGenProcessor(loadFactoryStyles()) {}

TransitGenProcessor::TransitGenProcessor(std::unique_ptr<tg::StyleTable> styles)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      styleChoices_(StyleChoices::fromTable(styles.get())),
      apvts_(*this, nullptr, ids::params, createParameterLayout(styleChoices_))
{
    snapshot_.bind(apvts_, styleChoices_);
    engine_.setPlanProvider(&provider_);
    if (styles != nullptr) styles_.publish(std::move(styles));
    publishCurve(state_.curve);
}

TransitGenProcessor::~TransitGenProcessor() = default;

bool TransitGenProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const juce::AudioChannelSet out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

// ------------------------------------------------------------------------------------ audio thread

void TransitGenProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    preparedBlock_ = std::max(1, samplesPerBlock);
    engine_.prepare(sampleRate, preparedBlock_, tg::kMaxChannels);   // fixed channel count: layout changes need no realloc
    wasBypassed_.store(false, std::memory_order_relaxed);
    setLatencySamples(0);
}

void TransitGenProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    for (int c = numIn; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, numSamples);
    if (numSamples <= 0 || preparedBlock_ <= 0) {
        midi.clear();
        return;
    }
    if (wasBypassed_.exchange(false, std::memory_order_relaxed)) engine_.reset();   // no stale fill after bypass

    // Parameters and published objects: once per block (01 §7). The curve/style pointers feed GenSettings.
    tg::EngineSettings s;
    snapshot_.read(s);
    if (locked_.load(std::memory_order_relaxed)) s.variation = tg::Variation::Fixed;   // 03 §7
    s.curve = curve_.acquire();
    s.styles = styles_.acquire();
    engine_.setSettings(s);

    const tg::TransportInfo ti = readTransport(getPlayHead());
    const double barLen = ti.tsNum * 4.0 / ti.tsDen;
    uiBarLen_.store(barLen, std::memory_order_relaxed);
    uiGridOrigin_.store(ti.hasBarStart ? ti.barStartPpq - barLen * std::floor((ti.barStartPpq + 1e-9) / barLen) : 0.0,
                        std::memory_order_relaxed);

    int numMidi = 0;   // note on/off only; the core ignores everything else
    for (const juce::MidiMessageMetadata m : midi) {
        if (numMidi >= kMaxMidiEventsPerBlock) break;
        if (m.numBytes < 3) continue;
        const auto st = static_cast<uint8_t>(m.data[0] & 0xF0);
        if (st != 0x80 && st != 0x90) continue;
        midiScratch_[static_cast<size_t>(numMidi++)] = { std::clamp(m.samplePosition, 0, numSamples - 1), m.data[0], m.data[1], m.data[2] };
    }
    midi.clear();

    processChunked(buffer, std::min(buffer.getNumChannels(), tg::kMaxChannels), ti, numMidi);
}

void TransitGenProcessor::processChunked(juce::AudioBuffer<float>& buffer, int numChannels, tg::TransportInfo ti,
                                         int numMidi) noexcept
{
    // Hosts may exceed the prepared block size: split, advancing the position between chunks.
    const int total = buffer.getNumSamples();
    float* const* data = buffer.getArrayOfWritePointers();
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    int m0 = 0;
    for (int start = 0; start < total;) {
        const int n = std::min(preparedBlock_, total - start);
        float* ch[tg::kMaxChannels] = {};
        for (int c = 0; c < numChannels; ++c) ch[c] = data[c] + start;
        int m1 = m0;
        while (m1 < numMidi && midiScratch_[static_cast<size_t>(m1)].sampleOffset < start + n) {
            midiScratch_[static_cast<size_t>(m1)].sampleOffset -= start;
            ++m1;
        }
        const tg::MidiEventView view{ m1 > m0 ? &midiScratch_[static_cast<size_t>(m0)] : nullptr, m1 - m0 };
        engine_.process(ch, numChannels, n, ti, view);
        if (ti.hasPpq && ti.bpm > 0.0 && ti.playing) ti.ppq += n * ti.bpm / (60.0 * sr);
        m0 = m1;
        start += n;
    }
}

void TransitGenProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Pass-through: inputs stay as they are, extra outputs are silenced. Zero latency, nothing to delay.
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
    midi.clear();
    wasBypassed_.store(true, std::memory_order_relaxed);
}

// ------------------------------------------------------------------------------------ message thread

juce::AudioProcessorEditor* TransitGenProcessor::createEditor() { return new TransitGenEditor(*this); }

void TransitGenProcessor::publishCurve(const tg::EnergyCurve& c)
{
    const juce::ScopedLock sl(stateLock_);
    curve_.publish(std::make_unique<tg::EnergyCurve>(c));
}

void TransitGenProcessor::collectRetired()
{
    const juce::ScopedLock sl(stateLock_);
    curve_.collect();
    styles_.collect();
}

void TransitGenProcessor::setCurvePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(tg::CurvePreset::kCount)) return;
    {
        const juce::ScopedLock sl(stateLock_);
        state_.curve = tg::kCurvePresets[index];
        state_.curvePreset = tg::kCurvePresetNames[index];
    }
    publishCurve(tg::kCurvePresets[index]);
}

juce::String TransitGenProcessor::curvePresetName() const
{
    const juce::ScopedLock sl(stateLock_);
    return state_.curvePreset;
}

void TransitGenProcessor::setParamValue(const char* id, float denormalised)
{
    if (juce::RangedAudioParameter* p = apvts_.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(denormalised));
        p->endChangeGesture();
    }
}

void TransitGenProcessor::rerollSeed()
{
    if (isLocked()) return;
    const int old = static_cast<int>(apvts_.getRawParameterValue(pid::seed)->load());
    const int seed = kMinSeed + rng_.nextInt(kMaxSeed);   // 03 §7: 1 + nextInt(99999)
    {
        const juce::ScopedLock sl(stateLock_);
        state_.pushSeed(old);
    }
    setParamValue(pid::seed, static_cast<float>(seed));
}

void TransitGenProcessor::setLocked(bool locked)
{
    locked_.store(locked, std::memory_order_relaxed);
    const juce::ScopedLock sl(stateLock_);
    state_.locked = locked;
}

juce::ValueTree TransitGenProcessor::normalisedParams(const juce::ValueTree& loaded)
{
    // Every known parameter, in the APVTS's own child order (so save -> load -> save is
    // byte-identical); missing or unreadable values get their default, unknown ids are dropped.
    juce::ValueTree out = apvts_.copyState();
    for (juce::ValueTree child : out) {
        juce::RangedAudioParameter* p = apvts_.getParameter(child[ids::id].toString());
        if (p == nullptr) continue;
        float v = p->convertFrom0to1(p->getDefaultValue());
        const juce::ValueTree src = loaded.isValid() ? loaded.getChildWithProperty(ids::id, p->paramID) : juce::ValueTree{};
        if (src.isValid() && src.hasProperty(ids::value)) {
            const auto raw = static_cast<float>(src[ids::value]);
            if (std::isfinite(raw)) v = p->convertFrom0to1(p->convertTo0to1(raw));
        }
        child.setProperty(ids::value, v, nullptr);
    }
    return out;
}

void TransitGenProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ValueTree params = apvts_.copyState();
    juce::ValueTree tree;
    {
        const juce::ScopedLock sl(stateLock_);
        NonParamState s = state_;
        s.styleId = styleChoices_.idAt(static_cast<int>(std::lround(apvts_.getRawParameterValue(pid::style)->load())));
        if (const tg::StyleTable* t = styles_.current())
            if (const tg::Style* st = t->find(s.styleId)) s.styleVersion = st->version;
        tree = writeState(params, s);
    }
    if (const std::unique_ptr<juce::XmlElement> xml = tree.createXml()) copyXmlToBinary(*xml, destData);
}

void TransitGenProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;
    LoadedState ls;
    if (!readState(juce::ValueTree::fromXml(*xml), ls)) return;

    apvts_.replaceState(normalisedParams(ls.params));
    if (ls.hasStyle)   // the style id is stable; the choice index may shift once user styles exist
        if (const int idx = styleChoices_.indexOf(ls.state.styleId); idx >= 0) setParamValue(pid::style, static_cast<float>(idx));

    locked_.store(ls.state.locked, std::memory_order_relaxed);
    loadedNewer_.store(ls.newerVersion, std::memory_order_relaxed);
    {
        const juce::ScopedLock sl(stateLock_);
        state_ = ls.state;
    }
    publishCurve(ls.state.curve);
}

} // namespace tgp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tgp::TransitGenProcessor(); }
