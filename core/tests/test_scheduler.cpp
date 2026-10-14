#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tg;
using namespace tgt;
using Catch::Approx;

namespace {
FillPlan silencePlan() { return PlanBuilder(4.0).source(EventType::Silence, 0.0, 4.0, {0.0f}).plan(); }

bool isSilent(const Signal& s, int64_t from, int64_t to)
{
    return maxAbs(s.ch[0], from, to) == 0.0f && maxAbs(s.ch[1], from, to) == 0.0f;
}
bool equalsInput(const Signal& out, const Signal& in, int64_t from, int64_t to)
{
    for (int64_t i = from; i < to; ++i)
        if (out(0, i) != in(0, i) || out(1, i) != in(1, i)) return false;
    return true;
}
} // namespace

TEST_CASE("Auto-Phrase fills occupy the last fill_len of each phrase", "[scheduler]")
{
    TestPlanProvider prov;
    prov.templatePlan = silencePlan();
    const Signal in = makeNoise(20 * 48000, 3, 0.5f);          // 20 s = 40 beats = 10 bars

    SECTION("phrase 4 bars, fill 1 bar, offset 0") {
        EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(512));
        REQUIRE(r.fillStarts.size() == 2);
        CHECK(r.fillStarts[0] == 12 * 24000);
        CHECK(r.fillStarts[1] == 28 * 24000);
        CHECK(r.fillPlayOffsets[0] == 0);
        CHECK(isSilent(r.out, 12 * 24000 + 200, 16 * 24000 - 200));
        CHECK(equalsInput(r.out, in, 0, 12 * 24000));
        CHECK(equalsInput(r.out, in, 16 * 24000, 28 * 24000));
    }
    SECTION("phrase offset of one bar shifts the grid") {
        EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
        s.phraseOffset = 1;
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(256));
        REQUIRE(r.fillStarts.size() == 3);      // phrase -1 ends at bar 1, so a fill plays in bar 0
        CHECK(r.fillStarts[0] == 0);
        CHECK(r.fillStarts[1] == 16 * 24000);
        CHECK(r.fillStarts[2] == 32 * 24000);
    }
    SECTION("per-phrase variation derives a seed per phrase") {
        EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
        s.variation = Variation::PerPhrase;
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(512));
        REQUIRE(r.fillSeeds.size() == 2);
        CHECK(r.fillSeeds[0] == mixSeed(42, 0));
        CHECK(r.fillSeeds[1] == mixSeed(42, 1));
        CHECK(r.fillSeeds[0] != r.fillSeeds[1]);
    }
    SECTION("fill_len longer than the phrase is clamped: back-to-back fills") {
        EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
        s.fillLen = FillLength::Bars8;
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(512));
        REQUIRE(r.fillStarts.size() == 3);
        CHECK(r.fillStarts[0] == 0);
        CHECK(r.fillStarts[1] == 16 * 24000);
        CHECK(r.fillStarts[2] == 32 * 24000);
        CHECK(prov.lastRequest.gen.lengthBeats == Approx(16.0));
    }
    SECTION("a loop jump into the fill region cuts and re-enters mid-fill") {
        EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
        auto e = makeEngine(prov, s);
        HostSim host;
        bool jumped = false;
        const RenderResult r = render(*e, in, host, fixedBlocks(500), {}, [&](int64_t pos, HostSim& h) {
            if (!jumped && pos >= 14 * 24000) { h.jumpTo(13.0); jumped = true; }   // 336000 is block-aligned
        });
        REQUIRE(r.fillStarts.size() >= 2);
        CHECK(r.fillStarts[0] == 12 * 24000);
        // The re-entry happens after the 10 ms cut fade, one beat (+ fade) into the fill.
        CHECK(r.fillStarts[1] == 14 * 24000 + 480);
        CHECK(r.fillPlayOffsets[1] == 24000 + 480);
        CHECK(isSilent(r.out, 14 * 24000 + 480 + 200, 14 * 24000 + 3 * 24000 - 480 - 200));
    }
    SECTION("bar start from the host shifts the grid") {
        EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
        auto e = makeEngine(prov, s);
        HostSim host;
        host.hasBarStart = true;
        host.barStartPpq = 2.0;                 // bars at 2, 6, 10, ...
        const RenderResult r = render(*e, in, host, fixedBlocks(512));
        REQUIRE(r.fillStarts.size() == 3);      // phrase -1 ends at beat 2: mid-fill entry at t = 0
        CHECK(r.fillStarts[0] == 0);
        CHECK(r.fillPlayOffsets[0] == 2 * 24000);
        CHECK(r.fillStarts[1] == 14 * 24000);
        CHECK(r.fillStarts[2] == 30 * 24000);
    }
}

TEST_CASE("Automation mode: quantized rising edge and early release", "[scheduler]")
{
    TestPlanProvider prov;
    prov.templatePlan = silencePlan();
    const Signal in = makeNoise(10 * 48000, 5, 0.5f);
    EngineSettings s = settingsFor(TriggerMode::Automation);
    s.fillLen = FillLength::Beat4;
    auto e = makeEngine(prov, s);
    HostSim host;
    const RenderResult r = render(*e, in, host, fixedBlocks(512), {}, [&](int64_t pos, HostSim&) {
        s.trigger = pos >= 100000 && pos < 200192;     // on at block 100352 (beat 4.18), off at 200192
        e->setSettings(s);
    });
    REQUIRE(r.fillStarts.size() == 1);
    CHECK(r.fillStarts[0] == 5 * 24000);           // quantized to the next quarter note
    CHECK(isSilent(r.out, 120200, 200000));
    CHECK(equalsInput(r.out, in, 0, 120000));
    CHECK(equalsInput(r.out, in, 200192 + 480 + 1, in.frames));   // released early: dry after the 10 ms fade
    CHECK_FALSE(isSilent(r.out, 200192 + 100, 200192 + 480));     // fading, not already dry
}

TEST_CASE("MIDI mode: note length map, velocity, hold and retrigger", "[scheduler]")
{
    TestPlanProvider prov;
    prov.templatePlan = silencePlan();
    const Signal in = makeNoise(5 * 48000, 9, 0.5f);
    EngineSettings s = settingsFor(TriggerMode::Midi);
    s.quantize = Quantize::Off;

    SECTION("one-shot note D = 1 beat, velocity maps to intensity") {
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(512), {{50000, noteOn(62, 64)}, {60000, noteOff(62)}});
        REQUIRE(r.fillStarts.size() == 1);
        CHECK(r.fillStarts[0] == 50000);
        CHECK(prov.lastRequest.gen.lengthBeats == Approx(1.0));
        CHECK(prov.lastRequest.gen.intensity == Approx(64.0f / 127.0f));
        CHECK(prov.lastRequest.midiNote == 62);
        CHECK(isSilent(r.out, 50200, 74000 - 200));
        CHECK(equalsInput(r.out, in, 74000, in.frames));
    }
    SECTION("hold mode: note-off cuts the fill") {
        s.midiHold = true;
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(512), {{50000, noteOn(65)}, {60000, noteOff(65)}});
        REQUIRE(r.fillStarts.size() == 1);
        CHECK(prov.lastRequest.gen.lengthBeats == Approx(4.0));   // F = 4 beats
        CHECK(isSilent(r.out, 50200, 60000));
        CHECK(equalsInput(r.out, in, 60000 + 480, in.frames));
    }
    SECTION("retrigger cuts and restarts after the fade") {
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(512), {{50000, noteOn(62)}, {60000, noteOn(63)}});
        REQUIRE(r.fillStarts.size() == 2);
        CHECK(r.fillStarts[0] == 50000);
        CHECK(r.fillStarts[1] == 60480);
        CHECK(prov.lastRequest.gen.lengthBeats == Approx(2.0));   // D# = 2 beats
        CHECK(isSilent(r.out, 60480 + 200, 60480 + 48000 - 200));
    }
    SECTION("quantized to the bar") {
        s.quantize = Quantize::Bar;
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(512), {{50000, noteOn(60)}});
        REQUIRE(r.fillStarts.size() == 1);
        CHECK(r.fillStarts[0] == 4 * 24000);
        CHECK(prov.lastRequest.gen.lengthBeats == Approx(0.25));
    }
    SECTION("works with the transport stopped (internal clock)") {
        auto e = makeEngine(prov, s);
        HostSim host;
        host.playing = false;
        const RenderResult r = render(*e, in, host, fixedBlocks(512), {{50000, noteOn(62)}});
        REQUIRE(r.fillStarts.size() == 1);
        CHECK(r.fillStarts[0] == 50000);
        CHECK(isSilent(r.out, 50200, 74000 - 200));
    }
}

TEST_CASE("rejected plans leave the audio dry", "[scheduler]")
{
    TestPlanProvider prov;
    prov.templatePlan = silencePlan();
    prov.fail = true;
    const Signal in = makeNoise(10 * 48000, 11, 0.5f);   // the fill would start at beat 12
    auto e = makeEngine(prov, settingsFor(TriggerMode::AutoPhrase));
    HostSim host;
    const RenderResult r = render(*e, in, host, fixedBlocks(512));
    CHECK(r.fillStarts.empty());
    CHECK(r.rejected == 1);
    CHECK(identical(r.out, in));
}
