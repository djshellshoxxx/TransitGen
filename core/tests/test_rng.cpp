#include "GenTestUtil.h"

#include "transitgen/Rng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tg;
using Catch::Approx;

TEST_CASE("PCG32 matches an independent reference implementation", "[rng]")
{
    // Reference values from a Python PCG32 with the seeding of 03 §3.
    Rng a(1u, RngStream::Source);
    CHECK(a.next() == 0x4d700850u);
    CHECK(a.next() == 0xfa9b681fu);
    CHECK(a.next() == 0x1c00672fu);
    CHECK(a.next() == 0x75b01035u);
    Rng b(42u, RngStream::Gate);
    CHECK(b.next() == 0xc754f975u);
    CHECK(b.next() == 0x7a27aa66u);
    Rng c(7u, RngStream::Filter);
    const uint32_t expect[] = {1, 4, 2, 3, 3, 4, 5, 0};
    for (uint32_t e : expect) CHECK(c.nextInt(6) == e);
}

TEST_CASE("RNG streams are independent and draws are in range", "[rng]")
{
    Rng s(5u, RngStream::Source), g(5u, RngStream::Gate);
    int same = 0;
    for (int i = 0; i < 100; ++i) same += s.next() == g.next();
    CHECK(same < 3);

    Rng r(99u, RngStream::Crush);
    int buckets[10] = {};
    for (int i = 0; i < 100000; ++i) {
        const float f = r.nextFloat();
        REQUIRE(f >= 0.0f);
        REQUIRE(f < 1.0f);
        ++buckets[static_cast<int>(f * 10.0f)];
        const uint32_t k = r.nextInt(7);
        REQUIRE(k < 7u);
    }
    for (int b : buckets) CHECK(b > 9500);
    CHECK(r.nextInt(0) == 0u);
    CHECK(r.nextInt(1) == 0u);
}

TEST_CASE("pickWeighted follows 03 §3", "[rng]")
{
    const double w[] = {1.0, 0.0, -2.0, 3.0};
    CHECK(pickWeighted(w, 4, 0.0f) == 0);
    CHECK(pickWeighted(w, 4, 0.24f) == 0);
    CHECK(pickWeighted(w, 4, 0.26f) == 3);     // zero and negative weights are skipped
    CHECK(pickWeighted(w, 4, 0.999f) == 3);
    const double zeros[] = {0.0, 0.0, 0.0};
    CHECK(pickWeighted(zeros, 3, 0.5f) == 2);  // all zero -> last index
}

TEST_CASE("energy curve evaluation and effective energy", "[curve]")
{
    const EnergyCurve& ramp = curvePreset(CurvePreset::RampUp);
    CHECK(ramp.evaluate(0.0) == Approx(0.1));
    CHECK(ramp.evaluate(1.0) == Approx(1.0));
    CHECK(ramp.evaluate(0.5) == Approx(0.1 + 0.9 * bend(0.5, 0.3)));
    CHECK(ramp.evaluate(0.5) > 0.55);                     // bend() with k > 0 rises early
    CHECK(curvePreset(CurvePreset::Flat).evaluate(0.37) == Approx(0.6));
    const EnergyCurve& bc = curvePreset(CurvePreset::BuildAndCut);
    CHECK(bc.evaluate(0.85) == Approx(1.0));
    CHECK(bc.evaluate(0.9) == Approx(0.0));
    CHECK(effectiveEnergy(ramp, 1.0, 0.7) == Approx(1.0));
    CHECK(effectiveEnergy(ramp, 0.0, 0.7) == Approx(0.1));
    CHECK(effectiveEnergy(ramp, 0.0, 1.0) == Approx(0.1 / 0.7));
    CHECK(effectiveEnergy(ramp, 0.9, 1.0) == 1.0);       // clamped
    for (int i = 0; i < static_cast<int>(CurvePreset::kCount); ++i) CHECK(isValidCurve(kCurvePresets[i]));
}

TEST_CASE("energy tables are piecewise linear and clamped", "[styles]")
{
    EnergyTable t;
    t.n = 3;
    t.e[0] = 0.2f; t.v[0] = 1.0f;
    t.e[1] = 0.6f; t.v[1] = 0.0f;
    t.e[2] = 1.0f; t.v[2] = 0.5f;
    CHECK(t(0.0) == Approx(1.0));
    CHECK(t(0.4) == Approx(0.5));
    CHECK(t(0.8) == Approx(0.25));
    CHECK(t(1.0) == Approx(0.5));
    CHECK(EnergyTable{}(0.5) == 0.0);
}
