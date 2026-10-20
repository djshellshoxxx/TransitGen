#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tg;
using namespace tgt;
using Catch::Approx;

namespace {
constexpr int64_t kStart = 96000;
constexpr int64_t kLen = 10 * 48000;
} // namespace

TEST_CASE("Gate pattern, duty and depth on DC", "[modifier]")
{
    const Signal in = makeDC(kLen, 1.0f);
    // 4 steps per beat (6000 samples), duty 0.5, pattern 0b0101, instant attack/release, depth 1.
    const FillPlan plan = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0)
        .gate(0.0, 2.0, {4.0f, 0.5f, 5.0f, 0.0f, 0.0f, 1.0f}).plan();
    const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
    REQUIRE(run.r.fillStarts.size() == 1);
    CHECK(run.r.out(0, run.start + 1000) == 1.0f);    // step 0, phase 0.17: open
    CHECK(run.r.out(0, run.start + 2999) == 1.0f);    // phase just under 0.5
    CHECK(run.r.out(0, run.start + 3000) == 0.0f);    // phase 0.5: closed
    CHECK(run.r.out(0, run.start + 7000) == 0.0f);    // step 1: bit clear
    CHECK(run.r.out(0, run.start + 13000) == 1.0f);   // step 2: open
    CHECK(run.r.out(0, run.start + 19000) == 0.0f);   // step 3: bit clear
    CHECK(run.r.out(0, run.start + 30000) == 0.0f);   // step 5: clear (pattern has only 2 bits)

    SECTION("attack and release are linear slews") {
        const FillPlan slow = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0)
            .gate(0.0, 2.0, {4.0f, 0.5f, 5.0f, 0.0f, 10.0f, 0.5f}).plan();
        const FillRun r2 = runFill(slow, in, kStart, FillLength::Beat2);
        // Closing at 3000 with a 10 ms (480 samples) release to depth 0.5.
        CHECK(r2.r.out(0, r2.start + 3000 + 240) == Approx(1.0f - 0.5f * 241.0f / 480.0f).margin(2e-3));
        CHECK(r2.r.out(0, r2.start + 3000 + 600) == Approx(0.5f));
    }
    SECTION("pattern 0 means all steps open") {
        const FillPlan open = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0)
            .gate(0.0, 2.0, {4.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}).plan();
        const FillRun r3 = runFill(open, in, kStart, FillLength::Beat2);
        CHECK(identical(r3.r.out, in));
    }
}

TEST_CASE("Filter: TPT SVF follows the lanes", "[modifier]")
{
    SECTION("low-pass passes DC, high-pass removes it") {
        const Signal in = makeDC(kLen, 0.5f);
        const FillPlan lp = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).filter(FilterType::LowPass)
            .lane(AutoTarget::FilterCutoff, {{0.0, 0.5f, 0.0f}, {2.0, 0.5f, 0.0f}}).plan();
        const FillRun r = runFill(lp, in, kStart, FillLength::Beat2);
        CHECK(r.r.out(0, r.start + 20000) == Approx(0.5f).margin(1e-3));
        const FillPlan hp = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).filter(FilterType::HighPass)
            .lane(AutoTarget::FilterCutoff, {{0.0, 0.5f, 0.0f}, {2.0, 0.5f, 0.0f}}).plan();
        const FillRun h = runFill(hp, in, kStart, FillLength::Beat2);
        CHECK(std::fabs(h.r.out(0, h.start + 20000)) < 1e-3f);
    }
    SECTION("low-pass at 224 Hz attenuates 10 kHz by more than 40 dB") {
        const Signal in = makeSine(kLen, 10000.0, 0.5f);
        const FillPlan lp = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).filter(FilterType::LowPass)
            .lane(AutoTarget::FilterCutoff, {{0.0, 0.35f, 0.0f}, {2.0, 0.35f, 0.0f}}).plan();
        const FillRun r = runFill(lp, in, kStart, FillLength::Beat2);
        CHECK(maxAbs(r.r.out.ch[0], r.start + 24000, r.start + 47000) < 0.5f * 0.01f);
    }
    SECTION("filter mix lane blends dry and filtered") {
        const Signal in = makeDC(kLen, 0.5f);
        const FillPlan hp = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).filter(FilterType::HighPass)
            .lane(AutoTarget::FilterCutoff, {{0.0, 0.5f, 0.0f}, {2.0, 0.5f, 0.0f}})
            .lane(AutoTarget::FilterMix, {{0.0, 0.5f, 0.0f}, {2.0, 0.5f, 0.0f}}).plan();
        const FillRun h = runFill(hp, in, kStart, FillLength::Beat2);
        CHECK(h.r.out(0, h.start + 20000) == Approx(0.25f).margin(1e-3));
    }
    SECTION("cutoff sweep is applied: the response changes along the fill") {
        const Signal in = makeSine(kLen, 2000.0, 0.5f);
        const FillPlan lp = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).filter(FilterType::LowPass)
            .lane(AutoTarget::FilterCutoff, {{0.0, 0.2f, 0.0f}, {2.0, 1.0f, 0.0f}}).plan();   // 79 Hz -> 20 kHz
        const FillRun r = runFill(lp, in, kStart, FillLength::Beat2);
        CHECK(maxAbs(r.r.out.ch[0], r.start + 4000, r.start + 8000) < 0.05f);
        CHECK(maxAbs(r.r.out.ch[0], r.start + 40000, r.start + 44000) > 0.4f);
    }
}

TEST_CASE("Crush: continuous bit depth and sample-and-hold downsampling", "[modifier]")
{
    SECTION("2 bits quantizes 0.3 to 0.5") {
        const Signal in = makeDC(kLen, 0.3f);
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0)
            .crush(0.0, 2.0, {2.0f, 2.0f, 1.0f, 1.0f, 1.0f}).plan();
        const FillRun r = runFill(plan, in, kStart, FillLength::Beat2);
        CHECK(r.r.out(0, r.start + 20000) == Approx(0.5f).margin(1e-6));
        CHECK(r.r.out(0, r.start + 10) == Approx(0.3f).margin(0.05f));     // edge ramp still mostly dry
    }
    SECTION("downsample 4 holds each value for 4 samples") {
        const Signal in = makeRamp(kLen, 1e-3f);
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0)
            .crush(0.0, 2.0, {16.0f, 16.0f, 4.0f, 4.0f, 1.0f}).plan();
        const FillRun r = runFill(plan, in, kStart, FillLength::Beat2);
        for (int64_t g = 1000; g < 1040; g += 4) {
            const float v = r.r.out(0, r.start + g);
            CHECK(r.r.out(0, r.start + g + 1) == v);
            CHECK(r.r.out(0, r.start + g + 2) == v);
            CHECK(r.r.out(0, r.start + g + 3) == v);
            CHECK(r.r.out(0, r.start + g + 4) != v);
        }
    }
    SECTION("mix 0.5 halves the crush error") {
        const Signal in = makeDC(kLen, 0.3f);
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0)
            .crush(0.0, 2.0, {2.0f, 2.0f, 1.0f, 1.0f, 0.5f}).plan();
        const FillRun r = runFill(plan, in, kStart, FillLength::Beat2);
        CHECK(r.r.out(0, r.start + 20000) == Approx(0.4f).margin(1e-6));
    }
}
