#include "GenTestUtil.h"

#include "transitgen/StyleLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <memory>

namespace tgt {

namespace {
void appendf(std::string& s, const char* fmt, double v)
{
    char buf[40];
    std::snprintf(buf, sizeof buf, fmt, v);
    s += buf;
}
void appendEvent(std::string& s, const tg::FillEvent& e, bool params)
{
    s += "{\"lane\":" + std::to_string(static_cast<int>(e.lane)) + ",\"type\":" + std::to_string(static_cast<int>(e.type));
    s += ",\"start\":";
    appendf(s, "%.17g", e.startBeat);
    s += ",\"len\":";
    appendf(s, "%.17g", e.lengthBeats);
    if (params) {
        s += ",\"p\":[";
        for (int i = 0; i < 8; ++i) { if (i) s += ","; appendf(s, "%.9g", static_cast<double>(e.p[i])); }
        s += "]";
    }
    s += "}";
}
} // namespace

const tg::StyleTable& factoryStyles()
{
    static const std::unique_ptr<tg::StyleTable> table = [] {
        std::string err;
        auto t = tg::makeFactoryStyleTable(&err);
        INFO(err);
        REQUIRE(t != nullptr);
        return t;
    }();
    return *table;
}

tg::GenSettings genDefaults(uint16_t styleId, double lengthBeats, uint32_t seed, const tg::EnergyCurve* curve)
{
    tg::GenSettings g{};
    g.seed = seed;
    g.styleId = styleId;
    g.lengthBeats = lengthBeats;
    g.beatsPerBar = 4.0;
    g.intensity = 0.7f;
    g.density = 0.5f;
    g.pitchAmount = 0.5f;
    g.crushAmount = 0.5f;
    g.filterAmount = 0.5f;
    g.endingOverride = 0;
    g.endingBeats = 0.5f;
    g.curve = curve;
    g.styles = &factoryStyles();
    return g;
}

std::string dumpLane(const tg::FillPlan& p, tg::Lane lane)
{
    std::string s;
    for (int i = 0; i < p.numEvents; ++i)
        if (p.events[i].lane == lane) { appendEvent(s, p.events[i], true); s += "\n"; }
    return s;
}

std::string dumpSourceTiming(const tg::FillPlan& p)
{
    std::string s;
    for (int i = 0; i < p.numEvents; ++i)
        if (p.events[i].lane == tg::Lane::Source) { appendEvent(s, p.events[i], false); s += "\n"; }
    return s;
}

std::string dumpFilter(const tg::FillPlan& p)
{
    std::string s = "\"filterType\":" + std::to_string(static_cast<int>(p.filterType)) + ",\"lanes\":[";
    for (int l = 0; l < static_cast<int>(tg::AutoTarget::kCount); ++l) {
        s += l ? ",\n  [" : "\n  [";
        const tg::AutomationLane& lane = p.lanes[l];
        for (int i = 0; i < lane.numPoints; ++i) {
            s += i ? ",[" : "[";
            appendf(s, "%.17g", lane.points[i].beat);
            s += ",";
            appendf(s, "%.9g", static_cast<double>(lane.points[i].value));
            s += ",";
            appendf(s, "%.9g", static_cast<double>(lane.points[i].curve));
            s += "]";
        }
        s += "]";
    }
    return s + "]";
}

std::string dumpPlan(const tg::FillPlan& p)
{
    std::string s = "{\"seed\":" + std::to_string(p.seed) + ",\"styleId\":" + std::to_string(p.styleId) + ",\"lengthBeats\":";
    appendf(s, "%.17g", p.lengthBeats);
    s += ",\"events\":[";
    for (int i = 0; i < p.numEvents; ++i) {
        s += i ? ",\n  " : "\n  ";
        appendEvent(s, p.events[i], true);
    }
    s += "],\n " + dumpFilter(p) + "}";
    return s;
}

int countSource(const tg::FillPlan& p, bool nonPassOnly)
{
    int n = 0;
    for (int i = 0; i < p.numEvents; ++i)
        if (p.events[i].lane == tg::Lane::Source && (!nonPassOnly || p.events[i].type != tg::EventType::Pass)) ++n;
    return n;
}

} // namespace tgt
