#include "GenTestUtil.h"

#include "transitgen/Rng.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace tg;
using namespace tgt;

namespace {

std::unique_ptr<FillPlan> gen(const GenSettings& s)
{
    auto p = std::make_unique<FillPlan>();
    generate(s, *p);
    return p;
}

bool onGrid(double b, double L, double g)
{
    if (b == 0.0 || b == L) return true;
    const double k = (L - b) / g;
    return std::fabs(k - std::round(k)) < 1e-6;
}

/// Checks validity, grid alignment of every boundary and the exact end of the source lane.
void checkMusicalGrid(const FillPlan& p, double L, double g)
{
    const ValidationResult v = validate(p, L);
    INFO(v.reason << " @" << v.index);
    REQUIRE(v.ok);
    REQUIRE(p.lengthBeats == L);
    double end = 0.0;
    for (int i = 0; i < p.numEvents; ++i) {
        const FillEvent& e = p.events[i];
        INFO("event " << i << " start " << e.startBeat << " len " << e.lengthBeats << " g " << g);
        CHECK(onGrid(e.startBeat, L, g));
        CHECK(onGrid(e.startBeat + e.lengthBeats, L, g));
        if (e.lane == Lane::Source) end = e.startBeat + e.lengthBeats;
    }
    CHECK(std::fabs(end - L) <= 1e-12 * L);
    for (const AutomationLane& l : p.lanes)
        if (l.numPoints > 0) {
            CHECK(l.points[0].beat == 0.0);
            CHECK(l.points[l.numPoints - 1].beat == L);
        }
}

const char* kGoldenNames[] = {"dubstep", "drum-and-bass", "trap", "hyperpop", "techno", "idm"};

} // namespace

TEST_CASE("golden plans are byte-identical", "[generator][golden]")
{
    const bool update = std::getenv("TG_UPDATE_GOLDEN") != nullptr && std::string(std::getenv("TG_UPDATE_GOLDEN")) == "1";
    const double lengths[] = {1.0, 4.0, 16.0};                  // 1 beat, 1 bar, 4 bars (4/4)
    const uint32_t seeds[] = {1u, 4242u, 99999u};
    const CurvePreset curves[] = {CurvePreset::RampUp, CurvePreset::Chaos};

    for (int si = 0; si < 6; ++si) {
        const uint16_t id = static_cast<uint16_t>(si + 1);
        const Style& st = *factoryStyles().find(id);
        std::string doc = "{\"style\":\"" + std::string(st.name) + "\",\"version\":" + std::to_string(st.version) + ",\"plans\":[";
        bool first = true;
        for (CurvePreset c : curves)
            for (double L : lengths)
                for (uint32_t seed : seeds) {
                    const auto p = gen(genDefaults(id, L, seed, &curvePreset(c)));
                    const ValidationResult v = validate(*p, L);
                    INFO(st.name << " L=" << L << " seed=" << seed << ": " << v.reason);
                    REQUIRE(v.ok);
                    char head[160];
                    std::snprintf(head, sizeof head, "{\"curve\":\"%s\",\"length\":%.17g,\"seed\":%u,\"plan\":",
                                  kCurvePresetNames[static_cast<int>(c)], L, seed);
                    doc += first ? "\n" : ",\n";
                    doc += head + dumpPlan(*p) + "}";
                    first = false;
                }
        doc += "\n]}\n";

        const std::string path = std::string(TG_GOLDEN_DIR) + "/" + kGoldenNames[si] + ".json";
        if (update) {
            std::ofstream f(path, std::ios::binary);
            f << doc;
            REQUIRE(f.good());
            WARN("updated " << path);
            continue;
        }
        std::ifstream f(path, std::ios::binary);
        INFO("missing " << path << " (run the tests with TG_UPDATE_GOLDEN=1 to create it)");
        REQUIRE(f.good());
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string expected = ss.str();
        INFO("golden mismatch for " << path << " (if intended, regenerate with TG_UPDATE_GOLDEN=1 and bump the style version)");
        CHECK(expected == doc);
    }
}

TEST_CASE("fuzz: random settings always give a valid plan", "[generator][fuzz]")
{
    const int kCases = 100000;      // ~5 s under ASan/UBSan, so no reduced sanitizer count
    Rng r(2024u, 0x46555A5Au);
    auto unitish = [&](float u) -> float {
        if (u < 0.02f) return 0.0f;
        if (u < 0.04f) return 1.0f;
        if (u < 0.045f) return std::numeric_limits<float>::quiet_NaN();
        if (u < 0.05f) return u < 0.0475f ? -3.0f : 7.0f;
        return r.nextFloat();
    };
    const double fills[] = {0.25, 0.5, 1.0, 2.0, 3.0, 4.0};
    const int dens[] = {1, 2, 4, 8, 16};
    auto plan = std::make_unique<FillPlan>();
    EnergyCurve randomCurve;
    int maxEvents = 0;
    for (int n = 0; n < kCases; ++n) {
        const int tsNum = 1 + static_cast<int>(r.nextInt(16));
        const int tsDen = dens[r.nextInt(5)];
        const double bar = tsNum * 4.0 / tsDen;
        GenSettings s = genDefaults(static_cast<uint16_t>(r.nextInt(9)), 4.0, r.next());
        s.beatsPerBar = bar;
        const uint32_t lenKind = r.nextInt(10);
        if (lenKind < 6) s.lengthBeats = fills[r.nextInt(6)];
        else if (lenKind < 9) s.lengthBeats = bar * static_cast<double>(1u << r.nextInt(4));   // 1, 2, 4, 8 bars
        else s.lengthBeats = kMinGenLength + static_cast<double>(r.nextFloat()) * 64.0;
        s.intensity = unitish(r.nextFloat());
        s.density = unitish(r.nextFloat());
        s.pitchAmount = unitish(r.nextFloat());
        s.crushAmount = unitish(r.nextFloat());
        s.filterAmount = unitish(r.nextFloat());
        s.endingOverride = static_cast<uint8_t>(r.nextInt(7));
        s.endingBeats = static_cast<float>(r.nextInt(9)) * 0.25f;
        const uint32_t ck = r.nextInt(20);
        if (ck == 0) s.curve = nullptr;
        else if (ck < 10) s.curve = &kCurvePresets[r.nextInt(static_cast<uint32_t>(CurvePreset::kCount))];
        else {
            randomCurve.numPoints = 2 + static_cast<int>(r.nextInt(15));
            for (int i = 0; i < randomCurve.numPoints; ++i) {
                const float t = i == 0 ? 0.0f : (i == randomCurve.numPoints - 1 ? 1.0f : r.nextFloat());
                randomCurve.points[i] = CurvePoint{t, r.nextFloat(), r.nextFloat() * 2.0f - 1.0f};
            }
            for (int i = 1; i < randomCurve.numPoints - 1; ++i)          // insertion sort by t
                for (int k = i; k > 1 && randomCurve.points[k].t < randomCurve.points[k - 1].t; --k)
                    std::swap(randomCurve.points[k], randomCurve.points[k - 1]);
            s.curve = &randomCurve;
        }
        if (r.nextInt(100) == 0) s.styles = nullptr;

        generate(s, *plan);
        const ValidationResult v = validate(*plan, s.lengthBeats);
        if (!v.ok) {
            INFO("case " << n << ": " << v.reason << " @" << v.index << " style " << s.styleId << " L " << s.lengthBeats
                         << " bar " << bar << " seed " << s.seed);
            REQUIRE(v.ok);
        }
        if (plan->numEvents > maxEvents) maxEvents = plan->numEvents;
    }
    CHECK(maxEvents <= kMaxEvents);
    std::printf("[fuzz] %d cases valid, max %d events\n", kCases, maxEvents);
}

TEST_CASE("extreme lengths fall back without breaking validity", "[generator]")
{
    for (uint16_t id = 1; id <= 6; ++id)
        for (double L : {kMinGenLength, 0.1, 64.0, 256.0, kMaxGenLength}) {
            GenSettings s = genDefaults(id, L, 7u);
            s.density = 1.0f;
            s.intensity = 1.0f;
            const auto p = gen(s);
            INFO("style " << id << " L " << L);
            CHECK(validate(*p, L).ok);
        }
}

TEST_CASE("generator fallbacks: missing style table, unknown style, bad curve, tiny fill", "[generator]")
{
    GenSettings s = genDefaults(1, 4.0, 3u);
    s.styles = nullptr;
    auto p = gen(s);
    REQUIRE(p->numEvents == 1);
    CHECK(p->events[0].type == EventType::Pass);
    CHECK(validate(*p, 4.0).ok);

    s = genDefaults(77, 4.0, 3u);                          // unknown id -> first style
    p = gen(s);
    CHECK(p->styleId == 1);
    CHECK(dumpPlan(*p) == dumpPlan(*gen(genDefaults(1, 4.0, 3u))));

    EnergyCurve bad{};
    bad.numPoints = 1;
    s = genDefaults(2, 4.0, 3u, &bad);                     // invalid curve -> Ramp Up
    CHECK(dumpPlan(*gen(s)) == dumpPlan(*gen(genDefaults(2, 4.0, 3u))));

    s = genDefaults(5, 0.125, 3u);                         // Techno grid 1/4 > L: one source event
    p = gen(s);
    REQUIRE(p->numEvents == 1);
    CHECK(p->events[0].lengthBeats == 0.125);
    CHECK(p->filterType == FilterType::Off);
    CHECK(validate(*p, 0.125).ok);
}

TEST_CASE("musical sanity: boundaries on the grid, exact end, bars respected", "[generator]")
{
    const double lengths[] = {0.25, 0.5, 1.0, 2.0, 3.0, 4.0, 8.0, 16.0, 32.0};
    for (uint16_t id = 1; id <= 6; ++id) {
        const Style& st = *factoryStyles().find(id);
        for (double L : lengths)
            for (uint32_t seed = 1; seed <= 40; ++seed) {
                GenSettings s = genDefaults(id, L, seed, &curvePreset(seed % 2 ? CurvePreset::Chaos : CurvePreset::RampUp));
                s.density = static_cast<float>(seed % 5) * 0.25f;
                const auto p = gen(s);
                INFO(st.name << " L=" << L << " seed=" << seed);
                checkMusicalGrid(*p, L, st.grid);
                if (st.respectBars) {
                    // No effect event crosses a bar line measured from the fill end (4/4); merged
                    // Pass runs may (they are dry audio).
                    for (int i = 0; i < p->numEvents; ++i) {
                        const FillEvent& e = p->events[i];
                        if (e.lane != Lane::Source || e.type == EventType::Pass) continue;
                        const double a = (L - e.startBeat) / 4.0, b = (L - e.startBeat - e.lengthBeats) / 4.0;
                        CHECK(std::floor(a - 1e-9) <= b + 1e-9);
                    }
                }
            }
    }
    // Ending: Dubstep ends with a half-beat Silence on the grid.
    const auto p = gen(genDefaults(1, 4.0, 11u));
    const FillEvent* last = nullptr;
    for (int i = 0; i < p->numEvents; ++i) if (p->events[i].lane == Lane::Source) last = &p->events[i];
    REQUIRE(last != nullptr);
    CHECK(last->type == EventType::Silence);
    CHECK(last->startBeat == 3.5);
    CHECK(last->lengthBeats == 0.5);
}

TEST_CASE("musical sanity: more intensity gives more non-Pass events", "[generator]")
{
    for (uint16_t id = 1; id <= 6; ++id) {
        double lo = 0.0, hi = 0.0;
        for (uint32_t seed = 1; seed <= 64; ++seed) {
            GenSettings s = genDefaults(id, 16.0, seed);
            s.intensity = 0.0f;
            lo += countSource(*gen(s), true);
            s.intensity = 1.0f;
            hi += countSource(*gen(s), true);
        }
        INFO("style " << id << ": intensity 0 -> " << lo / 64 << ", intensity 1 -> " << hi / 64 << " non-Pass events");
        CHECK(lo < hi);
    }
}

// ---------------------------------------------------------------- knob locality (03 §6)
namespace {
struct Case { uint16_t id; uint32_t seed; double L; };
std::vector<Case> localityCases()
{
    std::vector<Case> v;
    for (uint16_t id = 1; id <= 6; ++id)
        for (uint32_t seed : {1u, 2u, 3u, 777u})
            for (double L : {4.0, 16.0}) v.push_back({id, seed, L});
    return v;
}
template <typename F>
int countChanged(F mutate, bool (*same)(const FillPlan&, const FillPlan&))
{
    int changed = 0;
    for (const Case& c : localityCases()) {
        const GenSettings a = genDefaults(c.id, c.L, c.seed);
        GenSettings b = a;
        mutate(b);
        if (!same(*gen(a), *gen(b))) ++changed;
    }
    return changed;
}
bool samePlan(const FillPlan& a, const FillPlan& b) { return dumpPlan(a) == dumpPlan(b); }
bool sameSource(const FillPlan& a, const FillPlan& b) { return dumpLane(a, Lane::Source) == dumpLane(b, Lane::Source); }
bool sameGate(const FillPlan& a, const FillPlan& b) { return dumpLane(a, Lane::Gate) == dumpLane(b, Lane::Gate); }
bool sameCrush(const FillPlan& a, const FillPlan& b) { return dumpLane(a, Lane::Crush) == dumpLane(b, Lane::Crush); }
bool sameFilter(const FillPlan& a, const FillPlan& b) { return dumpFilter(a) == dumpFilter(b); }
bool sameSourceTiming(const FillPlan& a, const FillPlan& b) { return dumpSourceTiming(a) == dumpSourceTiming(b); }
} // namespace

TEST_CASE("locality: seed, style, length, curve, intensity and ending change the plan", "[generator][locality]")
{
    const int n = static_cast<int>(localityCases().size());
    CHECK(countChanged([](GenSettings& s) { s.seed += 1000; }, samePlan) == n);
    CHECK(countChanged([](GenSettings& s) { s.seed += 1000; }, sameSource) > n * 3 / 4);   // re-roll changes everything
    CHECK(countChanged([](GenSettings& s) { s.styleId = static_cast<uint16_t>(s.styleId % 6 + 1); }, samePlan) == n);
    CHECK(countChanged([](GenSettings& s) { s.lengthBeats *= 2.0; }, samePlan) == n);
    CHECK(countChanged([](GenSettings& s) { s.curve = &curvePreset(CurvePreset::Chaos); }, samePlan) == n);
    CHECK(countChanged([](GenSettings& s) { s.intensity = 1.0f; }, samePlan) == n);
    CHECK(countChanged([](GenSettings& s) { s.endingOverride = 5; s.endingBeats = 1.0f; }, samePlan) == n);
}

TEST_CASE("locality: density changes source, gate and crush but never the filter", "[generator][locality]")
{
    for (float d : {0.0f, 0.25f, 0.8f, 1.0f}) {
        CHECK(countChanged([d](GenSettings& s) { s.density = d; }, sameFilter) == 0);
        CHECK(countChanged([d](GenSettings& s) { s.density = d; }, sameSource) > 0);
    }
}

TEST_CASE("locality: pitch amount changes source params only", "[generator][locality]")
{
    for (float v : {0.0f, 0.3f, 1.0f}) {
        auto m = [v](GenSettings& s) { s.pitchAmount = v; };
        CHECK(countChanged(m, sameSourceTiming) == 0);     // no re-segmentation, same types
        CHECK(countChanged(m, sameGate) == 0);
        CHECK(countChanged(m, sameCrush) == 0);
        CHECK(countChanged(m, sameFilter) == 0);
        CHECK(countChanged(m, sameSource) > 0);
    }
}

TEST_CASE("locality: crush amount changes only the crush lane", "[generator][locality]")
{
    for (float v : {0.0f, 0.2f, 1.0f}) {
        auto m = [v](GenSettings& s) { s.crushAmount = v; };
        CHECK(countChanged(m, sameSource) == 0);
        CHECK(countChanged(m, sameGate) == 0);
        CHECK(countChanged(m, sameFilter) == 0);
        CHECK(countChanged(m, sameCrush) > 0);
    }
}

TEST_CASE("locality: filter amount changes only the filter", "[generator][locality]")
{
    for (float v : {0.0f, 0.2f, 1.0f}) {
        auto m = [v](GenSettings& s) { s.filterAmount = v; };
        CHECK(countChanged(m, sameSource) == 0);
        CHECK(countChanged(m, sameGate) == 0);
        CHECK(countChanged(m, sameCrush) == 0);
        CHECK(countChanged(m, sameFilter) > 0);
    }
}

TEST_CASE("timing: worst case under 50 us (asserted in Release)", "[generator][perf]")
{
    // Per case: the minimum over 10 runs spread across the whole sweep (filters out preemption
    // and frequency bursts on shared CI machines); the worst case is the maximum over cases.
    std::vector<GenSettings> cases;
    for (uint16_t id = 1; id <= 6; ++id)
        for (uint32_t seed = 1; seed <= 100; ++seed) {
            GenSettings s = genDefaults(id, 32.0, seed, &curvePreset(CurvePreset::Chaos));   // 8 bars
            s.density = 1.0f;
            s.intensity = 1.0f;
            s.crushAmount = 1.0f;
            s.filterAmount = 1.0f;
            s.pitchAmount = 1.0f;
            cases.push_back(s);
        }
    std::vector<double> best(cases.size(), 1e9);
    auto plan = std::make_unique<FillPlan>();
    for (int rep = 0; rep < 10; ++rep)
        for (size_t i = 0; i < cases.size(); ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            generate(cases[i], *plan);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            if (us < best[i]) best[i] = us;
        }
    double worst = 0.0, sum = 0.0;
    size_t worstCase = 0;
    for (size_t i = 0; i < best.size(); ++i) {
        sum += best[i];
        if (best[i] > worst) { worst = best[i]; worstCase = i; }
    }
    std::printf("[perf] generate() 8 bars, density/intensity 100 %%: mean %.2f us, worst %.2f us (style %d; budget 50 us)\n",
                sum / static_cast<double>(best.size()), worst, cases[worstCase].styleId);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !defined(TG_SANITIZED)
    CHECK(worst < 50.0);
#endif
}
