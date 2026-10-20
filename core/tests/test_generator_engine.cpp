// The engine driven by the real generator (02 §3 + 03): block-size invariance, no allocation,
// and the played plan equals generate() for the frozen request.
#include "GenTestUtil.h"
#include "TestUtil.h"

#include "transitgen/GeneratorPlanProvider.h"
#include "transitgen/Published.h"
#include "transitgen/StyleLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace tg;
using namespace tgt;

namespace {
constexpr int64_t kLen = 10 * 48000;   // 20 beats at 120 BPM: one fill over beats [8, 16)

Signal testInput()
{
    Signal s = makeNoise(kLen, 31, 0.3f);
    const Signal sine = makeSine(kLen, 330.0, 0.4f);
    for (int c = 0; c < 2; ++c)
        for (int64_t i = 0; i < kLen; ++i) s(c, i) += sine(c, i);
    return s;
}

/// Message-thread side of the host wrapper: the published curve and style table.
struct Shared {
    Published<EnergyCurve> curve;
    Published<StyleTable>  styles;
    Shared()
    {
        curve.publish(std::make_unique<EnergyCurve>(curvePreset(CurvePreset::Chaos)));
        std::string err;
        std::unique_ptr<const StyleTable> t = makeFactoryStyleTable(&err);
        REQUIRE(t != nullptr);
        styles.publish(std::move(t));
    }
};

EngineSettings genSettings(uint16_t styleId)
{
    EngineSettings s = settingsFor(TriggerMode::AutoPhrase);
    s.fillLen = FillLength::Bars2;
    s.variation = Variation::PerPhrase;
    s.styleId = styleId;
    s.intensity = 1.0f;
    s.density = 0.8f;
    return s;
}
} // namespace

TEST_CASE("generator in the engine: Auto-Phrase render is block-size invariant", "[generator][invariance]")
{
    const Signal in = testInput();
    Shared shared;
    GeneratorPlanProvider prov;
    for (uint16_t styleId : {uint16_t{1}, uint16_t{4}, uint16_t{6}}) {
        INFO("style " << styleId);
        std::vector<Signal> outs;
        std::vector<BlockSizeFn> partitions = {fixedBlocks(1), fixedBlocks(7), fixedBlocks(64), fixedBlocks(512),
                                               randomBlocks(4242, 512)};
        for (const BlockSizeFn& blocks : partitions) {
            EngineSettings s = genSettings(styleId);
            auto e = makeEngine(prov, s);
            HostSim host;
            RenderResult r = render(*e, in, host, blocks, {}, [&](int64_t, HostSim&) {
                s.curve = shared.curve.acquire();      // audio thread, once per block (01 §7)
                s.styles = shared.styles.acquire();
                e->setSettings(s);
            });
            REQUIRE(r.fillStarts.size() == 1);
            CHECK(r.fillStarts[0] == 8 * 24000);
            CHECK(r.rejected == 0);
            outs.push_back(std::move(r.out));

            // The plan that played is exactly generate() of the frozen request.
            const auto played = std::make_unique<FillPlan>();
            REQUIRE(e->telemetry().readLastPlan(*played));
            GenSettings g = genDefaults(styleId, 8.0, mixSeed(42, 0), &curvePreset(CurvePreset::Chaos));
            g.intensity = 1.0f;
            g.density = 0.8f;
            const auto expect = std::make_unique<FillPlan>();
            generate(g, *expect);
            CHECK(dumpPlan(*played) == dumpPlan(*expect));
        }
        CHECK_FALSE(identical(outs[0], in));
        for (size_t i = 1; i < outs.size(); ++i) {
            INFO("partition " << i);
            CHECK(identical(outs[0], outs[i]));
        }
    }
}

TEST_CASE("generator in the engine: no allocation on the audio path", "[generator][realtime]")
{
    const Signal in = testInput();
    Shared shared;
    GeneratorPlanProvider prov;
    EngineSettings s = genSettings(6);
    auto e = makeEngine(prov, s);
    std::vector<float> l(512), r(512);
    float* chans[2] = {l.data(), r.data()};
    HostSim host;
    const MidiEventView noMidi{};

    const int64_t before = allocationCount();
    for (int64_t pos = 0; pos + 512 <= kLen; pos += 512) {
        for (int i = 0; i < 512; ++i) { l[static_cast<size_t>(i)] = in(0, pos + i); r[static_cast<size_t>(i)] = in(1, pos + i); }
        s.curve = shared.curve.acquire();
        s.styles = shared.styles.acquire();
        e->setSettings(s);
        host.pos = pos;
        const TransportInfo info = host.info();
        e->process(chans, 2, 512, info, noMidi);
    }
    const int64_t after = allocationCount();
    CHECK(after == before);
    CHECK(e->telemetry().fillCount.load() == 1);
    CHECK(e->telemetry().rejectedPlans.load() == 0);
}
