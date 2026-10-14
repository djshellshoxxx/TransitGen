#include "transitgen/FillPlan.h"

#include <cmath>

namespace tg {

namespace {

constexpr double kEps = 1e-9;

ValidationResult fail(const char* why, int idx) noexcept { return ValidationResult{false, why, idx}; }

bool inRange(float v, float lo, float hi) noexcept { return v >= lo && v <= hi; }

ValidationResult checkParams(const FillEvent& e, int i) noexcept
{
    switch (e.type) {
        case EventType::Stutter:
            if (e.p[0] <= 0.0f) return fail("stutter sliceStart must be > 0", i);
            if (e.p[1] < 0.0f) return fail("stutter sliceEnd must be >= 0", i);
            if (!inRange(e.p[3], -24.0f, 24.0f) || !inRange(e.p[4], -24.0f, 24.0f))
                return fail("stutter pitch out of range", i);
            if (!inRange(e.p[5], -12.0f, 0.0f)) return fail("stutter decay out of range", i);
            break;
        case EventType::Reverse:
            if (e.p[0] <= 0.0 || e.p[0] > e.lengthBeats + kEps) return fail("reverse length out of range", i);
            break;
        case EventType::TapeStop:
        case EventType::TapeStart:
            if (!inRange(e.p[0], -1.0f, 1.0f) || !inRange(e.p[1], 0.0f, 1.0f)) return fail("tape param out of range", i);
            break;
        case EventType::Silence:
            if (e.p[0] < 0.0f) return fail("silence fade must be >= 0", i);
            break;
        case EventType::Gate:
            if (!(e.p[0] == 1 || e.p[0] == 2 || e.p[0] == 3 || e.p[0] == 4 || e.p[0] == 6 || e.p[0] == 8))
                return fail("gate stepsPerBeat invalid", i);
            if (!inRange(e.p[1], 0.05f, 1.0f)) return fail("gate duty out of range", i);
            if (e.p[3] < 0.0f || e.p[4] < 0.0f) return fail("gate attack/release negative", i);
            if (!inRange(e.p[5], 0.0f, 1.0f)) return fail("gate depth out of range", i);
            break;
        case EventType::Crush:
            if (!inRange(e.p[0], 1.0f, 16.0f) || !inRange(e.p[1], 1.0f, 16.0f)) return fail("crush bits out of range", i);
            if (!inRange(e.p[2], 1.0f, 64.0f) || !inRange(e.p[3], 1.0f, 64.0f)) return fail("crush downsample out of range", i);
            if (!inRange(e.p[4], 0.0f, 1.0f)) return fail("crush mix out of range", i);
            break;
        default: break;
    }
    return {};
}

} // namespace

ValidationResult validate(const FillPlan& plan, double expectedLengthBeats) noexcept
{
    if (plan.numEvents < 0 || plan.numEvents > kMaxEvents) return fail("numEvents out of range", -1);
    if (!(plan.lengthBeats > 0.0) || !std::isfinite(plan.lengthBeats)) return fail("lengthBeats must be > 0", -1);
    if (expectedLengthBeats >= 0.0 && std::fabs(plan.lengthBeats - expectedLengthBeats) > kEps)
        return fail("lengthBeats differs from request", -1);

    const double L = plan.lengthBeats;
    double sourceCursor = 0.0;
    int    numSource = 0;
    double laneEnd[3] = {0.0, 0.0, 0.0};
    int    prevLane = -1;

    for (int i = 0; i < plan.numEvents; ++i) {
        const FillEvent& e = plan.events[i];
        const int lane = static_cast<int>(e.lane);
        if (lane < 0 || lane > 2) return fail("bad lane", i);
        if (lane < prevLane) return fail("events not sorted by lane", i);
        prevLane = lane;
        if (!std::isfinite(e.startBeat) || !std::isfinite(e.lengthBeats)) return fail("non-finite time", i);
        if (e.startBeat < -kEps || e.lengthBeats <= 0.0) return fail("bad start/length", i);
        for (float p : e.p) if (!std::isfinite(p)) return fail("non-finite param", i);

        const bool typeOk = (e.lane == Lane::Source && isSourceType(e.type))
                         || (e.lane == Lane::Gate && e.type == EventType::Gate)
                         || (e.lane == Lane::Crush && e.type == EventType::Crush);
        if (!typeOk) return fail("event type does not match lane", i);

        if (e.lane == Lane::Source) {
            if (std::fabs(e.startBeat - sourceCursor) > kEps) return fail("source lane gap or overlap", i);
            sourceCursor = e.startBeat + e.lengthBeats;
            ++numSource;
        } else {
            if (e.startBeat + kEps < laneEnd[lane]) return fail("lane events overlap or unsorted", i);
            if (e.startBeat + e.lengthBeats > L + kEps) return fail("event exceeds fill length", i);
            laneEnd[lane] = e.startBeat + e.lengthBeats;
        }
        if (auto r = checkParams(e, i); !r) return r;
    }
    if (numSource == 0) return fail("no source events", -1);
    if (std::fabs(sourceCursor - L) > kEps) return fail("source lane does not cover fill length", -1);

    for (int l = 0; l < static_cast<int>(AutoTarget::kCount); ++l) {
        const AutomationLane& lane = plan.lanes[l];
        if (lane.numPoints < 0 || lane.numPoints > kMaxLanePoints) return fail("lane numPoints out of range", l);
        if (lane.numPoints == 0) continue;
        if (lane.numPoints < 2) return fail("lane needs >= 2 points", l);
        if (std::fabs(lane.points[0].beat) > kEps) return fail("lane must start at 0", l);
        if (std::fabs(lane.points[lane.numPoints - 1].beat - L) > kEps) return fail("lane must end at length", l);
        for (int p = 0; p < lane.numPoints; ++p) {
            const LanePoint& pt = lane.points[p];
            if (!std::isfinite(pt.beat) || !std::isfinite(pt.value) || !std::isfinite(pt.curve)) return fail("non-finite lane point", p);
            if (p > 0 && pt.beat + kEps < lane.points[p - 1].beat) return fail("lane points unsorted", p);
            if (!inRange(pt.value, 0.0f, 1.0f) || !inRange(pt.curve, -1.0f, 1.0f)) return fail("lane value out of range", p);
        }
    }
    return {};
}

} // namespace tg
