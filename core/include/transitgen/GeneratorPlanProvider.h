// TransitGen core — the real IPlanProvider: wraps generate() (02 §3, 03).
#pragma once

#include "transitgen/Generator.h"
#include "transitgen/Settings.h"

namespace tg {

/// Stateless: everything comes from the frozen FillRequest. Its GenSettings carry the curve and
/// style-table pointers the host wrapper acquired from Published<> for the current block
/// (EngineSettings::curve/styles), so the provider never touches shared state itself.
class GeneratorPlanProvider : public IPlanProvider {
public:
    bool makePlan(const FillRequest& req, FillPlan& out) noexcept override;
};

} // namespace tg
