// TransitGen core — the fill generator (03 §5).
#pragma once

#include "transitgen/EnergyCurve.h"
#include "transitgen/FillPlan.h"
#include "transitgen/StyleTable.h"

namespace tg {

/// Turns settings into a plan. Pure, deterministic across platforms, allocation-free, lock-free,
/// bounded (< 50 µs). Always produces a plan that passes validate(out, s.lengthBeats) for any
/// lengthBeats in [kMinGenLength, kMaxGenLength]; other lengths are clamped into that range.
/// A null or invalid curve falls back to Ramp Up; an unknown styleId to the table's first style;
/// a null or empty style table to a single Pass event.
void generate(const GenSettings& s, FillPlan& out) noexcept;

constexpr double kMinGenLength = 1.0 / 64.0;
constexpr double kMaxGenLength = 1024.0;

} // namespace tg
