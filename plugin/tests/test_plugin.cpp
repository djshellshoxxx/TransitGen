// Headless tests of the plugin wrapper: the real processor, a fake playhead, an Auto-Phrase fill
// and the state round trip (M3).
#include "PluginProcessor.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace tgp;

namespace {
constexpr double kSr = 48000.0, kBpm = 120.0;
constexpr int    kBlock = 512;
constexpr int64_t kSpb = 24000;                 // samples per beat at 120 BPM
constexpr int64_t kLen = 20 * kSpb;             // 4-bar phrase: one 1-bar fill over beats [12, 16)

/// Playing, 120 BPM, 4/4, from ppq 0; the test advances it per block.
struct FakePlayHead final : juce::AudioPlayHead {
    int64_t sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        const double ppq = static_cast<double>(sample) / static_cast<double>(kSpb);
        p.setIsPlaying(true);
        p.setBpm(kBpm);
        p.setTimeSignature(TimeSignature{ 4, 4 });
        p.setPpqPosition(ppq);
        p.setPpqPositionOfLastBarStart(4.0 * std::floor(ppq / 4.0));
        p.setTimeInSamples(sample);
        return p;
    }
};

void setParam(TransitGenProcessor& p, const char* id, float denormalised)
{
    juce::RangedAudioParameter* param = p.parameters().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(denormalised));
}

juce::MemoryBlock saveState(TransitGenProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation(mb);
    return mb;
}

std::string stateText(const juce::MemoryBlock& mb)   // readable failure output
{
    const std::unique_ptr<juce::XmlElement> xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), static_cast<int>(mb.getSize()));
    return xml != nullptr ? xml->toString(juce::XmlElement::TextFormat().singleLine()).toStdString() : std::string("<invalid>");
}

float input(int c, int64_t i)
{
    const double t = static_cast<double>(i) / kSr;
    const double noise = static_cast<double>((static_cast<uint32_t>(i) * 2654435761u + static_cast<uint32_t>(c) * 97u) >> 8 & 0xFFFF) / 65535.0 - 0.5;
    return static_cast<float>(0.4 * std::sin(2.0 * juce::MathConstants<double>::pi * 330.0 * t) + 0.3 * noise);
}
} // namespace

TEST_CASE("plugin: Auto-Phrase fill alters only the fill region", "[plugin]")
{
    TransitGenProcessor proc;
    FakePlayHead head;
    proc.setPlayHead(&head);
    REQUIRE(proc.setBusesLayout({ { juce::AudioChannelSet::stereo() }, { juce::AudioChannelSet::stereo() } }));
    setParam(proc, "phrase_bars", 0.0f);   // 4 bars
    setParam(proc, "intensity", 100.0f);
    setParam(proc, "density", 80.0f);
    proc.setCurvePreset(static_cast<int>(tg::CurvePreset::Chaos));
    proc.prepareToPlay(kSr, kBlock);
    CHECK(proc.getLatencySamples() == 0);

    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out[2] = { std::vector<float>(static_cast<size_t>(kLen)), std::vector<float>(static_cast<size_t>(kLen)) };
    for (int64_t pos = 0; pos < kLen; pos += kBlock) {
        const int n = static_cast<int>(std::min<int64_t>(kBlock, kLen - pos));
        buf.setSize(2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) buf.setSample(c, i, input(c, pos + i));
        head.sample = pos;
        proc.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) out[c][static_cast<size_t>(pos + i)] = buf.getSample(c, i);
    }

    const tg::EngineTelemetry& t = proc.telemetry();
    REQUIRE(t.fillCount.load() == 1);
    CHECK(t.rejectedPlans.load() == 0);
    CHECK(t.lastFillStartT.load() == 12 * kSpb);

    const int64_t fillStart = 12 * kSpb, fillEnd = 16 * kSpb;
    int64_t diffOutside = 0, diffInside = 0;
    for (int c = 0; c < 2; ++c)
        for (int64_t i = 0; i < kLen; ++i) {
            const bool same = out[c][static_cast<size_t>(i)] == input(c, i);
            if (i >= fillStart && i < fillEnd) diffInside += same ? 0 : 1;
            else diffOutside += same ? 0 : 1;
        }
    CHECK(diffOutside == 0);                              // bit-exact outside the fill
    CHECK(diffInside > (fillEnd - fillStart) / 4);        // the fill audibly changes the signal

    proc.releaseResources();
    proc.setPlayHead(nullptr);
}

TEST_CASE("plugin: bypass passes audio through", "[plugin]")
{
    TransitGenProcessor proc;
    proc.prepareToPlay(kSr, kBlock);
    juce::AudioBuffer<float> buf(2, kBlock);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < kBlock; ++i) buf.setSample(c, i, input(c, i));
    juce::MidiBuffer midi;
    proc.processBlockBypassed(buf, midi);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < kBlock; ++i) REQUIRE(buf.getSample(c, i) == input(c, i));
}

TEST_CASE("plugin: output peak meter tracks processed and bypassed audio", "[plugin][meter]")
{
    TransitGenProcessor proc;
    proc.prepareToPlay(kSr, kBlock);
    juce::AudioBuffer<float> buf(2, kBlock);
    buf.clear();
    buf.setSample(0, 17, -0.75f);
    buf.setSample(1, 31, 0.5f);
    juce::MidiBuffer midi;

    proc.processBlock(buf, midi);
    CHECK(proc.consumeOutputPeak() == 0.75f);
    CHECK(proc.consumeOutputPeak() == 0.0f);

    buf.clear();
    buf.setSample(1, 23, 0.9f);
    proc.processBlockBypassed(buf, midi);
    CHECK(proc.consumeOutputPeak() == 0.9f);
}

TEST_CASE("plugin: preview is a queued one-shot and restores the selected trigger mode", "[plugin][preview]")
{
    TransitGenProcessor proc;
    FakePlayHead head;
    proc.setPlayHead(&head);
    setParam(proc, "fill_len", 0.0f); // 1/4 beat keeps the test short
    setParam(proc, "quantize", 0.0f); // next block
    proc.prepareToPlay(kSr, kBlock);

    proc.requestPreview();
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    int block = 0;
    for (; block < 40 && proc.telemetry().fillCount.load() == 0; ++block) {
        buf.clear();
        head.sample = static_cast<int64_t>(block) * kBlock;
        proc.processBlock(buf, midi);
    }
    REQUIRE(proc.telemetry().fillCount.load() == 1);

    for (; block < 70; ++block) {
        buf.clear();
        head.sample = static_cast<int64_t>(block) * kBlock;
        proc.processBlock(buf, midi);
    }
    CHECK(proc.telemetry().fillCount.load() == 1);
    tg::EngineSettings settings;
    proc.currentSettings(settings);
    CHECK(settings.mode == tg::TriggerMode::AutoPhrase);
}

TEST_CASE("plugin: state save -> load -> save is byte-identical", "[plugin][state]")
{
    juce::MemoryBlock first;
    {
        TransitGenProcessor a;
        setParam(a, "seed", 4821.0f);
        setParam(a, "intensity", 33.3f);
        setParam(a, "out_gain", -4.5f);
        setParam(a, "style", 3.0f);
        a.rerollSeed();
        a.rerollSeed();
        a.setCurvePreset(static_cast<int>(tg::CurvePreset::BuildAndCut));
        a.setLocked(true);
        first = saveState(a);
    }
    TransitGenProcessor b;
    b.setStateInformation(first.getData(), static_cast<int>(first.getSize()));
    const juce::MemoryBlock second = saveState(b);
    INFO("first:  " << stateText(first));
    INFO("second: " << stateText(second));
    CHECK(first == second);
    CHECK(b.isLocked());
    CHECK(b.curvePresetName() == "Build & Cut");

    // And once more through a fresh instance.
    TransitGenProcessor c;
    c.setStateInformation(second.getData(), static_cast<int>(second.getSize()));
    CHECK(saveState(c) == second);
}

TEST_CASE("plugin: custom energy curve is validated, displayed as custom and saved", "[plugin][curve]")
{
    TransitGenProcessor proc;
    tg::EnergyCurve curve = tg::curvePreset(tg::CurvePreset::RampUp);
    curve.points[0].value = 0.25f;
    REQUIRE(proc.setEnergyCurve(curve));
    CHECK(proc.curvePresetName().isEmpty());
    CHECK(proc.energyCurve().points[0].value == 0.25f);

    tg::EnergyCurve invalid = curve;
    invalid.points[1].t = 0.5f;
    CHECK_FALSE(proc.setEnergyCurve(invalid));
    CHECK(proc.energyCurve().points[1].t == 1.0f);

    const juce::MemoryBlock saved = saveState(proc);
    TransitGenProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(restored.energyCurve().points[0].value == 0.25f);
    CHECK(restored.curvePresetName().isEmpty());
}

TEST_CASE("plugin: generated preview plan matches current controls", "[plugin][preview]")
{
    TransitGenProcessor proc;
    setParam(proc, "seed", 4242.0f);
    setParam(proc, "fill_len", 5.0f);
    tg::FillPlan first{};
    tg::FillPlan second{};
    REQUIRE(proc.previewPlan(first));
    REQUIRE(proc.previewPlan(second));
    CHECK(tg::validate(first, first.lengthBeats).ok);
    CHECK(first.seed == 4242);
    CHECK(first.lengthBeats == 4.0);
    CHECK(first.numEvents == second.numEvents);
    CHECK(std::memcmp(first.events, second.events, sizeof(first.events)) == 0);
}

TEST_CASE("plugin: loading tolerates unknown and missing elements", "[plugin][state]")
{

    juce::XmlElement xml("TransitGen");
    xml.setAttribute("stateVersion", 99);   // newer than supported: load what we can
    auto* params = xml.createNewChildElement("PARAMS");
    auto* seed = params->createNewChildElement("PARAM");
    seed->setAttribute("id", "seed");
    seed->setAttribute("value", 777);
    auto* bogus = params->createNewChildElement("PARAM");
    bogus->setAttribute("id", "no_such_param");
    bogus->setAttribute("value", 1);
    xml.createNewChildElement("SOMETHING_NEW")->setAttribute("x", 1);
    auto* curve = xml.createNewChildElement("CURVE");   // invalid points, unknown preset: keep default
    curve->setAttribute("preset", "Nope");
    curve->createNewChildElement("P")->setAttribute("t", 0.5);

    TransitGenProcessor p;
    setParam(p, "mix", 10.0f);   // must return to its default: missing params get defaults
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary(xml, mb);
    p.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));

    CHECK(p.loadedNewerState());
    CHECK(static_cast<int>(p.parameters().getRawParameterValue("seed")->load()) == 777);
    CHECK(p.parameters().getRawParameterValue("mix")->load() == 100.0f);
    CHECK(p.curvePresetName() == "Ramp Up");
    CHECK(!p.isLocked());

    // Garbage must not crash or change anything.
    const char junk[] = "not a state";
    p.setStateInformation(junk, sizeof(junk));
    CHECK(static_cast<int>(p.parameters().getRawParameterValue("seed")->load()) == 777);
}

int main(int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juce;   // APVTS and parameters expect a message manager
    return Catch::Session().run(argc, argv);
}
