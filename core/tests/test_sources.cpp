#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tg;
using namespace tgt;
using Catch::Approx;

namespace {
constexpr int64_t kStart = 96000;     // fill start (block-aligned for 500-sample blocks)
constexpr int     kFade = 144;        // 3 ms at 48 kHz
constexpr int64_t kLen = 10 * 48000;

/// The engine's raised-cosine table entry for x (same formula and quantization as prepare()).
double rcq(double x)
{
    const float xf = static_cast<float>(std::min(1.0, std::max(0.0, x)));
    const int i = static_cast<int>(xf * 4096.0f);
    return static_cast<float>(0.5 - 0.5 * std::cos(3.14159265358979323846 * static_cast<double>(i) / 4096.0));
}

bool bitExactOutside(const FillRun& run, const Signal& in)
{
    for (int64_t i = 0; i < in.frames; ++i) {
        if (i >= run.start && i < run.start + run.N) continue;
        if (run.r.out(0, i) != in(0, i) || run.r.out(1, i) != in(1, i)) return false;
    }
    return true;
}
} // namespace

TEST_CASE("Pass is bit-exact at mix 100% / 0 dB", "[source]")
{
    const Signal in = makeNoise(kLen, 21, 0.8f);
    const FillPlan plan = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).plan();
    const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
    REQUIRE(run.r.fillStarts.size() == 1);
    CHECK(identical(run.r.out, in));
}

TEST_CASE("Silence is exactly zero inside the fill and transparent outside", "[source]")
{
    const Signal in = makeNoise(kLen, 22, 0.8f);
    const FillPlan plan = PlanBuilder(2.0).source(EventType::Silence, 0.0, 2.0, {0.0f}).plan();
    const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
    REQUIRE(run.r.fillStarts.size() == 1);
    CHECK(maxAbs(run.r.out.ch[0], run.start + kFade, run.start + run.N - kFade) == 0.0f);
    CHECK(maxAbs(run.r.out.ch[1], run.start + kFade, run.start + run.N - kFade) == 0.0f);
    CHECK(bitExactOutside(run, in));
    // Entry ramp is monotonic in |y| / |x|.
    float prevRatio = 2.0f;
    for (int64_t i = run.start; i < run.start + kFade; ++i) {
        const float ratio = std::fabs(run.r.out(0, i)) / std::fabs(in(0, i));
        CHECK(ratio <= prevRatio + 1e-6f);
        prevRatio = ratio;
    }
}

TEST_CASE("Reverse reads past audio backwards from the event start", "[source]")
{
    const Signal in = makeRamp(kLen, 1e-6f);
    const FillPlan plan = PlanBuilder(2.0).source(EventType::Reverse, 0.0, 2.0, {1.0f, 0.0f, 0.0f}).plan();
    const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
    REQUIRE(run.r.fillStarts.size() == 1);
    const int64_t R = 24000;
    bool ok = true;
    for (int64_t j = kFade; j < R - kFade && ok; ++j)
        ok = run.r.out(0, run.start + j) == in(0, run.start - 1 - j) && run.r.out(1, run.start + j) == in(1, run.start - 1 - j);
    CHECK(ok);
    CHECK(maxAbs(run.r.out.ch[0], run.start + R, run.start + run.N - kFade) == 0.0f);   // exhausted -> silence
    CHECK(bitExactOutside(run, in));
}

TEST_CASE("Reverse applies a dB gain ramp", "[source]")
{
    const Signal in = makeDC(kLen, 0.5f);
    const FillPlan plan = PlanBuilder(2.0).source(EventType::Reverse, 0.0, 2.0, {2.0f, 0.0f, -20.0f}).plan();
    const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
    const int64_t half = run.N / 2;
    CHECK(run.r.out(0, run.start + half) == Approx(0.5f * std::pow(10.0f, -10.0f / 20.0f)).margin(1e-4));
    CHECK(run.r.out(0, run.start + 1000) == Approx(0.5f * std::pow(10.0f, -20.0f * 1000.0f / 48000.0f / 20.0f)).margin(1e-4));
}

TEST_CASE("Stutter: live first slice, then repeats of the window", "[source]")
{
    const Signal in = makeRamp(kLen, 1e-6f);
    const int64_t L = 12000;      // 0.5 beat

    SECTION("constant slice, no pitch, no decay: repeats equal the window exactly") {
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Stutter, 0.0, 2.0, {0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f}).plan();
        const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
        REQUIRE(run.r.fillStarts.size() == 1);
        bool live = true, rep1 = true, rep3 = true;
        for (int64_t j = kFade; j < L && live; ++j) live = run.r.out(0, run.start + j) == in(0, run.start + j);
        for (int64_t j = 96; j < L - 96 && rep1; ++j) rep1 = run.r.out(0, run.start + L + j) == in(0, run.start + j);   // skip crossfade and seam
        for (int64_t j = 96; j < L - kFade && rep3; ++j) rep3 = run.r.out(1, run.start + 3 * L + j) == in(1, run.start + j);
        CHECK(live);
        CHECK(rep1);
        CHECK(rep3);
    }
    SECTION("+12 semitones reads every second sample of the window") {
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Stutter, 0.0, 2.0, {0.5f, 0.5f, 0.0f, 12.0f, 12.0f, 0.0f}).plan();
        const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
        bool ok = true;
        for (int64_t j = 96; j < L && ok; ++j) {
            const int64_t q = (2 * j) % L;
            if (q >= L - 96) continue;                                   // seam region
            ok = run.r.out(0, run.start + L + j) == in(0, run.start + q);
        }
        CHECK(ok);
    }
    SECTION("-12 semitones reads at half speed with exact midpoints on a ramp") {
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Stutter, 0.0, 2.0, {0.5f, 0.5f, 0.0f, -12.0f, -12.0f, 0.0f}).plan();
        const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
        bool ok = true;
        for (int64_t j = 96; j < L && ok; ++j) {
            if (0.5 * static_cast<double>(j) >= static_cast<double>(L - 96)) continue;   // seam region
            const double pos = static_cast<double>(run.start) + 0.5 * static_cast<double>(j);
            ok = run.r.out(0, run.start + L + j) == Approx(pos * 1e-6).epsilon(1e-5);
        }
        CHECK(ok);
    }
    SECTION("decay per repeat") {
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Stutter, 0.0, 2.0, {0.5f, 0.5f, 0.0f, 0.0f, 0.0f, -6.0f}).plan();
        const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
        const float g2 = std::pow(10.0f, -12.0f / 20.0f);
        CHECK(run.r.out(0, run.start + 2 * L + 6000) == Approx(in(0, run.start + 6000) * g2).epsilon(1e-5));
        CHECK(run.r.out(0, run.start + 6000) == in(0, run.start + 6000));   // first slice undecayed
    }
    SECTION("stepped roll snaps lengths to octaves of the start slice") {
        // 0.5 -> 0.125 beat over 2 beats on DC with -6 dB per repeat: each repeat is a level
        // step, so the level transitions locate the repeat boundaries.
        const Signal dc = makeDC(kLen, 1.0f);
        const FillPlan plan = PlanBuilder(2.0).source(EventType::Stutter, 0.0, 2.0, {0.5f, 0.125f, 0.0f, 0.0f, 0.0f, -6.0f}).plan();
        const FillRun run = runFill(plan, dc, kStart, FillLength::Beat2);
        std::vector<int64_t> restarts;
        int64_t j = kFade;
        for (int k = 0; k < 12 && j < run.N - kFade; ++k) {
            const float lo = std::pow(10.0f, -6.0f * static_cast<float>(k + 1) / 20.0f);
            const float hi = std::pow(10.0f, -6.0f * static_cast<float>(k) / 20.0f);
            while (j < run.N - kFade && run.r.out(0, run.start + j) > 0.5f * (lo + hi)) ++j;
            if (j < run.N - kFade) restarts.push_back(j);
        }
        REQUIRE(restarts.size() >= 4);
        for (size_t i = 0; i + 1 < restarts.size(); ++i) {
            const int64_t len = restarts[i + 1] - restarts[i];
            const bool octave = len == 12000 || len == 6000 || len == 3000;
            INFO("repeat length " << len);
            CHECK(octave);
        }
    }
}

TEST_CASE("TapeStop follows the rate curve with a lagging Hermite read head", "[source]")
{
    const float slope = 1e-6f;
    const Signal in = makeRamp(kLen, slope);
    const FillPlan plan = PlanBuilder(2.0).source(EventType::TapeStop, 0.0, 2.0, {0.0f, 0.0f}).plan();
    const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
    REQUIRE(run.r.fillStarts.size() == 1);
    double rp = -3.0;
    bool ok = true;
    int64_t firstBad = -1;
    for (int64_t j = 0; j < run.N - kFade; ++j) {
        const double u = static_cast<double>(j) / static_cast<double>(run.N);
        const double v = 1.0 - u;
        const double g = rcq(v / kTapeMuteRate);
        const double expected = g * (static_cast<double>(run.start) + rp) * slope;
        if (j >= kFade && std::fabs(run.r.out(0, run.start + j) - expected) > 2e-5) { ok = false; firstBad = j; break; }
        rp += v;
    }
    INFO("first mismatch at " << firstBad);
    CHECK(ok);
    CHECK(std::fabs(run.r.out(0, run.start + run.N - kFade - 1)) < 1e-2f);   // nearly stopped = nearly silent
}

TEST_CASE("TapeStart ramps the rate from startRate to 1 and rejoins live audio", "[source]")
{
    const float slope = 1e-6f;
    const Signal in = makeRamp(kLen, slope);
    const FillPlan plan = PlanBuilder(2.0)
        .source(EventType::TapeStart, 0.0, 1.0, {0.0f, 0.0f})
        .source(EventType::Pass, 1.0, 1.0).plan();
    const FillRun run = runFill(plan, in, kStart, FillLength::Beat2);
    const int64_t Nev = 24000;
    double rp = -3.0;
    bool ok = true;
    for (int64_t j = 0; j < Nev - kFade; ++j) {
        const double v = static_cast<double>(j) / static_cast<double>(Nev);
        const double g = rcq(v / kTapeMuteRate);
        const double expected = g * (static_cast<double>(run.start) + rp) * slope;
        if (j >= kFade && std::fabs(run.r.out(0, run.start + j) - expected) > 2e-5) { ok = false; break; }
        rp += v;
    }
    CHECK(ok);
    CHECK(run.r.out(0, run.start + kFade) < 0.5f * in(0, run.start + kFade));   // slow start = muted
    CHECK(run.r.out(0, run.start + Nev + kFade + 10) == in(0, run.start + Nev + kFade + 10));   // back to live
}

TEST_CASE("mix and output gain scale the wet path only", "[source]")
{
    const Signal in = makeDC(kLen, 0.5f);
    const FillPlan plan = PlanBuilder(2.0).source(EventType::Silence, 0.0, 2.0, {0.0f}).plan();
    const FillRun half = runFill(plan, in, kStart, FillLength::Beat2, 500, 0.5f, 0.0f);
    CHECK(half.r.out(0, half.start + half.N / 2) == Approx(0.25f));
    const FillPlan pass = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).plan();
    const FillRun loud = runFill(pass, in, kStart, FillLength::Beat2, 500, 1.0f, 6.0f);
    CHECK(loud.r.out(0, loud.start + loud.N / 2) == Approx(0.5f * std::pow(10.0f, 6.0f / 20.0f)));
    CHECK(loud.r.out(0, loud.start - 1) == 0.5f);                 // gain does not apply when idle
    CHECK(loud.r.out(0, loud.start + loud.N) == 0.5f);
}
