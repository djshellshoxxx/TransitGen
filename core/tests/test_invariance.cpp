#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

using namespace tg;
using namespace tgt;

namespace {
constexpr int64_t kLen = 10 * 48000;

Signal testInput()
{
    Signal s = makeNoise(kLen, 77, 0.3f);
    const Signal sine = makeSine(kLen, 220.0, 0.4f);
    for (int c = 0; c < 2; ++c)
        for (int64_t i = 0; i < kLen; ++i) s(c, i) += sine(c, i);
    return s;
}

EngineSettings autoSettings()
{
    EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
    s.fillLen = FillLength::Bars2;        // fill region = beats [8, 16) of each 4-bar phrase
    s.variation = Variation::PerPhrase;
    return s;
}
} // namespace

TEST_CASE("idle output is bit-exact", "[invariance]")
{
    const Signal in = testInput();
    TestPlanProvider prov;
    prov.templatePlan = fullPlan();
    EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
    s.phraseBars = 32;                     // no fill within 10 s
    auto e = makeEngine(prov, s);
    HostSim host;
    const RenderResult r = render(*e, in, host, randomBlocks(3, 512));
    CHECK(r.fillStarts.empty());
    CHECK(prov.calls == 0);
    CHECK(identical(r.out, in));
}

TEST_CASE("block-size invariance: Auto-Phrase with every effect", "[invariance]")
{
    const Signal in = testInput();
    TestPlanProvider prov;
    prov.templatePlan = fullPlan();
    const int sizes[] = {1, 7, 64, 512};
    std::vector<Signal> outs;
    for (int n : sizes) {
        auto e = makeEngine(prov, autoSettings());
        HostSim host;
        RenderResult r = render(*e, in, host, fixedBlocks(n));
        REQUIRE(r.fillStarts.size() == 1);
        CHECK(r.fillStarts[0] == 8 * 24000);
        outs.push_back(std::move(r.out));
    }
    {
        auto e = makeEngine(prov, autoSettings());
        HostSim host;
        RenderResult r = render(*e, in, host, randomBlocks(12345, 512));
        REQUIRE(r.fillStarts.size() == 1);
        outs.push_back(std::move(r.out));
    }
    CHECK_FALSE(identical(outs[0], in));      // the fill actually did something
    for (size_t i = 1; i < outs.size(); ++i) {
        INFO("partition " << i);
        CHECK(identical(outs[0], outs[i]));
    }
}

TEST_CASE("block-size invariance: MIDI triggers with retrigger", "[invariance]")
{
    const Signal in = testInput();
    TestPlanProvider prov;
    prov.templatePlan = fullPlan();
    EngineSettings s = settingsFor(TriggerMode::Midi);
    s.quantize = Quantize::Off;
    const std::vector<MidiAt> midi = {
        {10000, noteOn(62, 100)}, {100000, noteOn(65, 90)}, {130000, noteOn(63, 127)}, {300001, noteOn(60, 50)}};
    std::vector<Signal> outs;
    const int sizes[] = {1, 7, 64, 512};
    for (int n : sizes) {
        auto e = makeEngine(prov, s);
        HostSim host;
        RenderResult r = render(*e, in, host, fixedBlocks(n), midi);
        REQUIRE(r.fillStarts.size() == 4);
        outs.push_back(std::move(r.out));
    }
    {
        auto e = makeEngine(prov, s);
        HostSim host;
        RenderResult r = render(*e, in, host, randomBlocks(999, 512), midi);
        REQUIRE(r.fillStarts.size() == 4);
        outs.push_back(std::move(r.out));
    }
    for (size_t i = 1; i < outs.size(); ++i) {
        INFO("partition " << i);
        CHECK(identical(outs[0], outs[i]));
    }
}

TEST_CASE("no allocation on the audio path", "[realtime]")
{
    const Signal in = testInput();
    TestPlanProvider prov;
    prov.templatePlan = fullPlan();
    auto e = makeEngine(prov, autoSettings());
    std::vector<float> l(512), r(512);
    float* chans[2] = {l.data(), r.data()};
    HostSim host;
    const MidiEventView noMidi{};

    const int64_t before = allocationCount();
    for (int64_t pos = 0; pos + 512 <= kLen; pos += 512) {
        for (int i = 0; i < 512; ++i) { l[static_cast<size_t>(i)] = in(0, pos + i); r[static_cast<size_t>(i)] = in(1, pos + i); }
        host.pos = pos;
        const TransportInfo info = host.info();
        e->process(chans, 2, 512, info, noMidi);
    }
    const int64_t after = allocationCount();
    CHECK(after == before);
    CHECK(e->telemetry().fillCount.load() == 1);
}

TEST_CASE("performance budget (informational, asserted in Release)", "[perf]")
{
    const Signal in = testInput();
    TestPlanProvider prov;
    prov.templatePlan = fullPlan();
    EngineSettings s = autoSettings();
    s.fillLen = FillLength::Bars4;          // the whole 10 s is one fill (every effect runs)
    auto e = makeEngine(prov, s);
    std::vector<float> l(512), r(512);
    float* chans[2] = {l.data(), r.data()};
    HostSim host;
    const MidiEventView noMidi{};
    const auto t0 = std::chrono::steady_clock::now();
    int64_t frames = 0;
    for (int64_t pos = 0; pos + 512 <= kLen; pos += 512) {
        for (int i = 0; i < 512; ++i) { l[static_cast<size_t>(i)] = in(0, pos + i); r[static_cast<size_t>(i)] = in(1, pos + i); }
        host.pos = pos;
        const TransportInfo info = host.info();
        e->process(chans, 2, 512, info, noMidi);
        frames += 512;
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / static_cast<double>(frames);
    std::printf("[perf] %.1f ns per stereo frame inside a fill (budget 400 ns = 2%% of a core at 48 kHz)\n", ns);
    CHECK(e->telemetry().fillCount.load() >= 1);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !defined(TG_SANITIZED)
    CHECK(ns < 400.0);
#endif
}
