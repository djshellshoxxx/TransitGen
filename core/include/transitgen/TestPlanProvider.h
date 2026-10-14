// TransitGen core — a stand-in IPlanProvider plus a small plan builder for tests and demos.
#pragma once

#include "transitgen/Settings.h"

#include <initializer_list>

namespace tg {

/// Fluent builder for hand-written plans. Beats are relative to fill start.
class PlanBuilder {
public:
    explicit PlanBuilder(double lengthBeats, uint32_t seed = 1) { plan_.lengthBeats = lengthBeats; plan_.seed = seed; }

    PlanBuilder& source(EventType type, double startBeat, double lengthBeats, std::initializer_list<float> p = {})
    { return add(Lane::Source, type, startBeat, lengthBeats, p); }
    PlanBuilder& gate(double startBeat, double lengthBeats, std::initializer_list<float> p)
    { return add(Lane::Gate, EventType::Gate, startBeat, lengthBeats, p); }
    PlanBuilder& crush(double startBeat, double lengthBeats, std::initializer_list<float> p)
    { return add(Lane::Crush, EventType::Crush, startBeat, lengthBeats, p); }
    PlanBuilder& filter(FilterType t) { plan_.filterType = t; return *this; }
    PlanBuilder& lane(AutoTarget target, std::initializer_list<LanePoint> points)
    {
        AutomationLane& l = plan_.lanes[static_cast<int>(target)];
        l.numPoints = 0;
        for (const LanePoint& pt : points) if (l.numPoints < kMaxLanePoints) l.points[l.numPoints++] = pt;
        return *this;
    }
    const FillPlan& plan() const noexcept { return plan_; }

private:
    PlanBuilder& add(Lane lane, EventType type, double start, double len, std::initializer_list<float> p)
    {
        if (plan_.numEvents >= kMaxEvents) return *this;
        FillEvent& e = plan_.events[plan_.numEvents++];
        e.lane = lane; e.type = type; e.startBeat = start; e.lengthBeats = len;
        for (float& v : e.p) v = 0.0f;
        int i = 0;
        for (float v : p) if (i < 8) e.p[i++] = v;
        return *this;
    }
    FillPlan plan_{};
};

/// Returns a stored template plan, rescaled to the requested length, seed = req.seed.
class TestPlanProvider : public IPlanProvider {
public:
    FillPlan    templatePlan{};
    bool        fail = false;         // return false from makePlan
    int         calls = 0;
    FillRequest lastRequest{};

    bool makePlan(const FillRequest& req, FillPlan& out) noexcept override;
};

} // namespace tg
