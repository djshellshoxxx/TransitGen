#include "transitgen/TestPlanProvider.h"

namespace tg {

bool TestPlanProvider::makePlan(const FillRequest& req, FillPlan& out) noexcept
{
    ++calls;
    lastRequest = req;
    if (fail) return false;
    out = templatePlan;
    out.seed = req.gen.seed;
    out.styleId = req.gen.styleId;
    if (templatePlan.lengthBeats > 0.0 && req.gen.lengthBeats > 0.0) {
        const double k = req.gen.lengthBeats / templatePlan.lengthBeats;
        if (k != 1.0) {
            for (int i = 0; i < out.numEvents; ++i) {
                out.events[i].startBeat *= k;
                out.events[i].lengthBeats *= k;
                if (out.events[i].type == EventType::Reverse) out.events[i].p[0] *= static_cast<float>(k);
            }
            for (AutomationLane& l : out.lanes)
                for (int p = 0; p < l.numPoints; ++p) l.points[p].beat *= k;
        }
        out.lengthBeats = req.gen.lengthBeats;
        // Make the ends exact despite scaling round-off.
        for (int i = 0; i < out.numEvents; ++i) {
            FillEvent& e = out.events[i];
            if (e.lane == Lane::Source && i + 1 < out.numEvents && out.events[i + 1].lane == Lane::Source)
                out.events[i + 1].startBeat = e.startBeat + e.lengthBeats;
        }
        for (int i = out.numEvents - 1; i >= 0; --i)
            if (out.events[i].lane == Lane::Source) { out.events[i].lengthBeats = out.lengthBeats - out.events[i].startBeat; break; }
        for (AutomationLane& l : out.lanes)
            if (l.numPoints > 0) l.points[l.numPoints - 1].beat = out.lengthBeats;
    }
    return true;
}

} // namespace tg
