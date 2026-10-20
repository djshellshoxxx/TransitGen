// Helpers for the generator tests: factory style table, default settings, stable plan dumps.
#pragma once

#include "transitgen/EnergyCurve.h"
#include "transitgen/Generator.h"
#include "transitgen/StyleTable.h"

#include <string>

namespace tgt {

/// The factory styles, parsed once (REQUIREs success).
const tg::StyleTable& factoryStyles();

/// Default knob values (01 §6) for style `styleId`, 4/4, Ramp Up.
tg::GenSettings genDefaults(uint16_t styleId, double lengthBeats, uint32_t seed,
                            const tg::EnergyCurve* curve = &tg::curvePreset(tg::CurvePreset::RampUp));

/// Stable text dumps: floats as %.9g, doubles as %.17g, one event / lane per line.
std::string dumpPlan(const tg::FillPlan& p);
std::string dumpLane(const tg::FillPlan& p, tg::Lane lane);       // events of one lane
std::string dumpFilter(const tg::FillPlan& p);                     // filter type + automation lanes
std::string dumpSourceTiming(const tg::FillPlan& p);              // source starts/lengths/types, no params

int countSource(const tg::FillPlan& p, bool nonPassOnly);

} // namespace tgt
