#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

using namespace tg;

namespace {
FillPlan goodPlan()
{
    return PlanBuilder(4.0)
        .source(EventType::Pass, 0.0, 1.0)
        .source(EventType::Stutter, 1.0, 1.0, {0.25f, 0.125f, 0.0f, 0.0f, 12.0f, -3.0f})
        .source(EventType::Reverse, 2.0, 1.0, {0.5f, 0.0f, -6.0f})
        .source(EventType::Silence, 3.0, 1.0, {5.0f})
        .gate(1.0, 1.0, {4.0f, 0.5f, 5.0f, 1.0f, 5.0f, 1.0f})
        .crush(2.0, 1.5, {16.0f, 4.0f, 1.0f, 8.0f, 1.0f})
        .filter(FilterType::LowPass)
        .lane(AutoTarget::FilterCutoff, {{0.0, 1.0f, 0.0f}, {2.0, 0.3f, 0.5f}, {4.0, 1.0f, 0.0f}})
        .plan();
}
} // namespace

TEST_CASE("validate accepts a well-formed plan", "[plan]")
{
    const FillPlan p = goodPlan();
    const ValidationResult r = validate(p, 4.0);
    INFO(r.reason);
    CHECK(r.ok);
}

TEST_CASE("validate rejects structural errors", "[plan]")
{
    SECTION("source gap") {
        FillPlan p = goodPlan();
        p.events[1].startBeat = 1.1;
        CHECK_FALSE(validate(p));
    }
    SECTION("source overlap") {
        FillPlan p = goodPlan();
        p.events[0].lengthBeats = 1.2;
        CHECK_FALSE(validate(p));
    }
    SECTION("source lane shorter than the fill") {
        FillPlan p = goodPlan();
        p.events[3].lengthBeats = 0.5;
        CHECK_FALSE(validate(p));
    }
    SECTION("length mismatch with the request") {
        CHECK_FALSE(validate(goodPlan(), 2.0));
    }
    SECTION("gate overlap") {
        FillPlan p = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0)
            .gate(0.0, 1.0, {4.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f})
            .gate(0.5, 1.0, {4.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f}).plan();
        CHECK_FALSE(validate(p));
    }
    SECTION("modifier beyond fill end") {
        FillPlan p = PlanBuilder(2.0).source(EventType::Pass, 0.0, 2.0).crush(1.5, 1.0, {8.0f, 8.0f, 1.0f, 1.0f, 1.0f}).plan();
        CHECK_FALSE(validate(p));
    }
    SECTION("lane must start at 0 and end at length") {
        FillPlan p = goodPlan();
        p.lanes[0].points[0].beat = 0.1;
        CHECK_FALSE(validate(p));
        p = goodPlan();
        p.lanes[0].points[2].beat = 3.9;
        CHECK_FALSE(validate(p));
    }
    SECTION("lane value out of range") {
        FillPlan p = goodPlan();
        p.lanes[0].points[1].value = 1.5f;
        CHECK_FALSE(validate(p));
    }
    SECTION("too many events") {
        FillPlan p = goodPlan();
        p.numEvents = kMaxEvents + 1;
        CHECK_FALSE(validate(p));
    }
    SECTION("param range") {
        FillPlan p = goodPlan();
        p.events[1].p[3] = 30.0f;
        CHECK_FALSE(validate(p));
        p = goodPlan();
        p.events[2].p[0] = 2.0f;   // reverse longer than its event
        CHECK_FALSE(validate(p));
    }
    SECTION("empty plan") {
        FillPlan p;
        p.lengthBeats = 1.0;
        CHECK_FALSE(validate(p));
    }
}

TEST_CASE("mixSeed and bend behave as specified", "[math]")
{
    CHECK(mixSeed(42, 0) != mixSeed(42, 1));
    CHECK(mixSeed(42, 3) == mixSeed(42, 3));
    CHECK(mixSeed(42, -1) != 0);
    CHECK(bend(0.0, 0.7) == 0.0);
    CHECK(bend(1.0, -0.7) == 1.0);
    CHECK(bend(0.5, 0.0) == 0.5);
    CHECK(bend(0.5, 0.5) > 0.5);
    CHECK(bend(0.5, -0.5) < 0.5);
    CHECK(hermite(0.0f, 1.0f, 2.0f, 3.0f, 0.0f) == 1.0f);
    CHECK(hermite(0.0f, 1.0f, 2.0f, 3.0f, 0.5f) == 1.5f);   // exact on a linear ramp
}

TEST_CASE("TestPlanProvider rescales to the requested length", "[plan]")
{
    TestPlanProvider prov;
    prov.templatePlan = goodPlan();
    FillRequest req;
    req.gen.lengthBeats = 2.0;
    req.gen.seed = 7;
    FillPlan out;
    REQUIRE(prov.makePlan(req, out));
    CHECK(out.seed == 7);
    CHECK(validate(out, 2.0).ok);
    CHECK(out.events[1].startBeat == 0.5);
}
