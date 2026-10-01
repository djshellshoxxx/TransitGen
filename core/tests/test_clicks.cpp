#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

using namespace tg;
using namespace tgt;

namespace {
constexpr int64_t kStart = 96000;
constexpr int64_t kLen = 10 * 48000;
constexpr int     kF = 144;             // 3 ms
constexpr float   kRatio = 4.0f;        // +12 dB, 02 §7.3
constexpr float   kFloor = 1e-4f;
} // namespace

TEST_CASE("no click at any boundary of a plan with every source and modifier", "[click]")
{
    const Signal in = makeSine(kLen, 220.0, 0.5f);
    const FillRun run = runFill(fullPlan(), in, kStart, FillLength::Beat4);
    REQUIRE(run.r.fillStarts.size() == 1);
    const float ref = maxSecondDiff(in.ch[0], 0, kLen);
    const float limit = kRatio * ref + kFloor;
    const int64_t s = run.start;
    const int64_t crushStart = s + 2 * 24000, crushEnd = s + 3 * 24000;

    for (int c = 0; c < 2; ++c) {
        // Everything except the crush region (inherently stepped) is measured against the input.
        const float before = maxSecondDiff(run.r.out.ch[c], s - 2 * kF, crushStart - 2 * kF);
        const float after = maxSecondDiff(run.r.out.ch[c], crushEnd + 2 * kF, s + run.N + 2 * kF);
        INFO("channel " << c << " ref " << ref << " limit " << limit << " before " << before << " after " << after);
        CHECK(before <= limit);
        CHECK(after <= limit);
        // Crush edges: the boundary windows must not exceed the event's interior.
        const float interior = maxSecondDiff(run.r.out.ch[c], crushStart + 2 * kF, crushEnd - 2 * kF);
        const float edgeIn = maxSecondDiff(run.r.out.ch[c], crushStart - 2 * kF, crushStart + 2 * kF);
        const float edgeOut = maxSecondDiff(run.r.out.ch[c], crushEnd - 2 * kF, crushEnd + 2 * kF);
        INFO("crush interior " << interior << " edges " << edgeIn << " " << edgeOut);
        CHECK(edgeIn <= 1.5f * interior + kFloor);
        CHECK(edgeOut <= 1.5f * interior + kFloor);
    }
}

TEST_CASE("no click on fill entry, exit and early-release cut", "[click]")
{
    const Signal in = makeSine(kLen, 220.0, 0.5f);
    const float ref = maxSecondDiff(in.ch[0], 0, kLen);
    const float limit = kRatio * ref + kFloor;

    SECTION("entry into a stutter, exit from silence") {
        const FillPlan plan = PlanBuilder(2.0)
            .source(EventType::Stutter, 0.0, 1.0, {0.125f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f})
            .source(EventType::Silence, 1.0, 1.0, {3.0f}).plan();
        const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
        const float m = maxSecondDiff(run.r.out.ch[0], run.start - 2 * kF, run.start + run.N + 2 * kF);
        INFO("max second diff " << m << " limit " << limit);
        CHECK(m <= limit);
    }
    SECTION("cut while reversing") {
        TestPlanProvider prov;
        prov.templatePlan = PlanBuilder(4.0).source(EventType::Reverse, 0.0, 4.0, {4.0f, 0.0f, 0.0f}).plan();
        EngineSettings s = settingsFor(TriggerMode::Automation);
        s.quantize = Quantize::Off;
        auto e = makeEngine(prov, s);
        HostSim host;
        const RenderResult r = render(*e, in, host, fixedBlocks(500), {}, [&](int64_t pos, HostSim&) {
            s.trigger = pos >= kStart && pos < kStart + 30000;
            e->setSettings(s);
        });
        REQUIRE(r.fillStarts.size() == 1);
        const float m = maxSecondDiff(r.out.ch[1], kStart + 30000 - 2 * kF, kStart + 30000 + 480 + 2 * kF);
        INFO("max second diff around the cut " << m << " limit " << limit);
        CHECK(m <= limit);
    }
    SECTION("mid-fill entry after a loop jump") {
        TestPlanProvider prov;
        prov.templatePlan = fullPlan();
        auto e = makeEngine(prov, settingsFor(TriggerMode::AutoPhrase));
        HostSim host;
        bool jumped = false;
        const RenderResult r = render(*e, in, host, fixedBlocks(500), {}, [&](int64_t pos, HostSim& h) {
            if (!jumped && pos >= 14 * 24000) { h.jumpTo(13.0); jumped = true; }
        });
        REQUIRE(r.fillStarts.size() >= 2);
        const int64_t t = r.fillStarts[1];
        // The window starts at the re-entry: just before it the cut fill was inside its crush event.
        const float m = maxSecondDiff(r.out.ch[0], t, t + 4 * kF);
        INFO("max second diff at re-entry " << m << " limit " << limit);
        CHECK(m <= limit);
    }
}
