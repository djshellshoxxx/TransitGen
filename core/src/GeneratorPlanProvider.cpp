#include "transitgen/GeneratorPlanProvider.h"

namespace tg {

bool GeneratorPlanProvider::makePlan(const FillRequest& req, FillPlan& out) noexcept
{
    GenSettings g = req.gen;
    if (!(g.beatsPerBar > 0.0) && req.tsNum >= 1 && req.tsDen >= 1)
        g.beatsPerBar = static_cast<double>(req.tsNum) * 4.0 / static_cast<double>(req.tsDen);
    generate(g, out);
    return true;
}

} // namespace tg
