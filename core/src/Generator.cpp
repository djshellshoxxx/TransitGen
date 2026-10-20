// The fill generator (03 §5). Determinism rules (03 §2): only + − × ÷, sqrt, comparisons and
// exact rounding (floor/ceil); no pow/exp/log/sin. Every float draw comes from tg::Rng.
#include "transitgen/Generator.h"
#include "transitgen/Rng.h"

#include <cassert>
#include <cmath>

namespace tg {

namespace {

constexpr double kEps = 1e-9;
constexpr int    kMaxTries = 3;          // style grid, ×2, ×4 (03 §5 step 7)

struct Seg {
    int       j0, j1;                    // grid indices [j0, j1)
    double    e0, eMid;                  // effective energy at the start / middle
    EventType type;
    float     p[8];
};

/// Boundary grid anchored at the fill END (the fill ends on a downbeat, 03 §5): index j in [0, M]
/// is at beat L − (M − j)·g, except j = 0 which is the fill start (a partial cell when g ∤ L).
struct Grid {
    double L = 0.0, g = 0.0;
    int    M = 0;

    Grid(double len, double grid) noexcept : L(len), g(grid)
    {
        const double n = std::floor(L / g + kEps);
        M = static_cast<int>(n) + (L - n * g > kEps ? 1 : 0);
    }
    double beat(int j) const noexcept { return j <= 0 ? 0.0 : (j >= M ? L : L - static_cast<double>(M - j) * g); }
    /// Largest index whose beat is <= b (within kEps).
    int atOrBelow(double b) const noexcept
    {
        if (b >= L - kEps) return M;
        const int j = M - static_cast<int>(std::ceil((L - b) / g - kEps));
        return j < 0 ? 0 : j;
    }
};

struct Ctx {
    const Style*       st;
    const EnergyCurve* curve;
    double L, bar, intensity, density, pitch, crush, filter, endingBeats;
    EndingType endType;
    uint32_t   seed;

    double e(double t) const noexcept { return effectiveEnergy(*curve, t, intensity); }
};

/// The largest float not above d (validate() compares float params against double lengths).
float floatAtMost(double d) noexcept
{
    float f = static_cast<float>(d);
    if (static_cast<double>(f) > d) f = std::nextafter(f, 0.0f);
    return f;
}

double roundHalfAway(double x) noexcept { return x < 0.0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5); }

void clearParams(float* p) noexcept { for (int i = 0; i < 8; ++i) p[i] = 0.0f; }

/// Step 4: source type and params from draws r[1..7] at energy e for a segment of `len` beats.
void sourceParams(const Ctx& c, double e, double len, const float* r, Seg& s) noexcept
{
    const Style& st = *c.st;
    clearParams(s.p);
    double w[kNumSourceTypes];
    for (int i = 0; i < kNumSourceTypes; ++i) w[i] = st.sourceProb[i](e);
    s.type = static_cast<EventType>(pickWeighted(w, kNumSourceTypes, r[1]));

    switch (s.type) {
        case EventType::Stutter: {
            const auto& S = st.stutter;
            double sw[kMaxSlices];
            for (int i = 0; i < S.numSlices; ++i) sw[i] = S.sliceWeight[i](e);
            double slice = S.slices[pickWeighted(sw, S.numSlices, r[2])];
            if (slice > len) slice = len;
            double sliceEnd = slice;
            if (static_cast<double>(r[3]) < S.rollProb(e)) {
                sliceEnd = slice / static_cast<double>(S.rollDiv);
                const double floor64 = slice < 1.0 / 64.0 ? slice : 1.0 / 64.0;   // never longer than the slice
                if (sliceEnd < floor64) sliceEnd = floor64;
            }
            const double amt2 = c.pitch * 2.0;
            const bool pitchOn = static_cast<double>(r[4]) < S.pitchProb(e) * amt2;
            double pitchEnd = 0.0;
            if (pitchOn) {
                const double dir = S.pitchDir == PitchDir::Up ? 1.0 : (S.pitchDir == PitchDir::Down ? -1.0 : (r[5] < 0.5f ? 1.0 : -1.0));
                pitchEnd = clampd(roundHalfAway(static_cast<double>(S.pitchSemis) * e * amt2 * dir), -24.0, 24.0);
            }
            s.p[0] = static_cast<float>(slice);
            s.p[1] = static_cast<float>(sliceEnd);
            s.p[2] = static_cast<float>(S.rampMode);
            s.p[3] = S.pitchFromZero ? 0.0f : static_cast<float>(pitchEnd);
            s.p[4] = static_cast<float>(pitchEnd);
            s.p[5] = static_cast<float>(clampd(static_cast<double>(S.decayDb) * e, -12.0, 0.0));
            break;
        }
        case EventType::Reverse:
            s.p[0] = floatAtMost(len);   // gain 0 dB -> 0 dB
            break;
        case EventType::TapeStop:
        case EventType::TapeStart:
            s.p[0] = st.tapeCurve;       // rate 0
            break;
        case EventType::Silence:
            s.p[0] = st.silenceFadeMs;
            break;
        default: break;
    }
}

int emit(FillPlan& out, Lane lane, EventType type, double start, double end, const float* p) noexcept
{
    FillEvent& ev = out.events[out.numEvents];
    ev.lane = lane;
    ev.type = type;
    ev.startBeat = start;
    ev.lengthBeats = end - start;
    for (int i = 0; i < 8; ++i) ev.p[i] = p[i];
    return out.numEvents++;
}

void resetPlan(FillPlan& out, const Ctx& c) noexcept
{
    out.numEvents = 0;
    out.filterType = FilterType::Off;
    for (AutomationLane& l : out.lanes) l.numPoints = 0;
    out.lengthBeats = c.L;
    out.seed = c.seed;
}

void singleEvent(FillPlan& out, EventType type, const float* p, double L) noexcept
{
    FillEvent& ev = out.events[0];
    ev.lane = Lane::Source;
    ev.type = type;
    ev.startBeat = 0.0;
    ev.lengthBeats = L;
    for (int i = 0; i < 8; ++i) ev.p[i] = p[i];
    out.numEvents = 1;
}

/// Step 6, filter lanes. Uses the style grid (not a capacity-doubled one) so density never
/// reaches the filter lane (03 §6).
void filterLanes(const Ctx& c, FillPlan& out) noexcept
{
    const Style& st = *c.st;
    if (!(c.filter > 0.0) || st.filter.type == FilterType::Off) return;
    out.filterType = st.filter.type;
    // Sweep depth filterAmount·2 (100 % overshoots towards `to`, clamped like pitch/crush);
    // the mix lane saturates at 50 %.
    const double depth = c.filter * 2.0;
    const double amt = depth < 1.0 ? depth : 1.0;
    const double from = st.filter.from, to = st.filter.to;
    auto value = [&](double t) { return static_cast<float>(clampd(from + (to - from) * c.e(t) * depth, 0.0, 1.0)); };

    AutomationLane& cut = out.lanes[static_cast<int>(AutoTarget::FilterCutoff)];
    const EnergyCurve& curve = *c.curve;
    const double L = c.L;
    const double wobble = st.filter.wobble;

    if (!(wobble > 0.0)) {
        for (int i = 0; i < curve.numPoints; ++i) {
            const double t = curve.points[i].t;
            cut.points[i] = LanePoint{i == curve.numPoints - 1 ? L : t * L, value(t), curve.points[i].curve};
        }
        cut.numPoints = curve.numPoints;
    } else {
        // Wobble points on every 2nd grid line (spacing doubled until they fit), measured from the
        // fill end, alternating sign, magnitude wobble·amt·draw. All segments become linear.
        const int available = kMaxLanePoints - curve.numPoints;
        double sw = 2.0 * st.grid;
        int K = static_cast<int>(std::ceil(L / sw - kEps)) - 1;
        while (K > available) { sw *= 2.0; K = static_cast<int>(std::ceil(L / sw - kEps)) - 1; }
        if (K < 0) K = 0;
        Rng rng(c.seed, RngStream::Filter);
        int n = 0, ci = 0;
        for (int k = K; k >= 0; --k) {                // wobble beats ascending; k = 0 flushes the curve points
            const double wb = k > 0 ? L - static_cast<double>(k) * sw : L + 1.0;
            while (ci < curve.numPoints) {
                const double t = curve.points[ci].t;
                const double b = ci == curve.numPoints - 1 ? L : t * L;
                if (b > wb + kEps) break;
                cut.points[n++] = LanePoint{b, value(t), 0.0f};
                ++ci;
            }
            if (k == 0) break;
            const float f = rng.nextFloat();          // one draw per position, emitted or not
            if (n > 0 && wb - cut.points[n - 1].beat <= kEps) continue;          // coincides with a curve point
            if (ci < curve.numPoints && curve.points[ci].t * L - wb <= kEps) continue;
            const double sign = (k & 1) == (K & 1) ? 1.0 : -1.0;               // + for the first position
            const double t = wb / L;
            const double v = static_cast<double>(value(t)) + sign * wobble * amt * static_cast<double>(f);
            cut.points[n++] = LanePoint{wb, static_cast<float>(clampd(v, 0.0, 1.0)), 0.0f};
        }
        cut.numPoints = n;
    }
    const float res = st.filter.res, mix = static_cast<float>(amt);
    AutomationLane& r = out.lanes[static_cast<int>(AutoTarget::FilterResonance)];
    r.points[0] = LanePoint{0.0, res, 0.0f};
    r.points[1] = LanePoint{L, res, 0.0f};
    r.numPoints = 2;
    AutomationLane& m = out.lanes[static_cast<int>(AutoTarget::FilterMix)];
    m.points[0] = LanePoint{0.0, mix, 0.0f};
    m.points[1] = LanePoint{L, mix, 0.0f};
    m.numPoints = 2;
}

/// One generation attempt with boundary grid g. Returns false when the plan would exceed kMaxEvents.
bool attempt(const Ctx& c, double g, FillPlan& out) noexcept
{
    const Style& st = *c.st;
    const double L = c.L;
    resetPlan(out, c);
    Rng src(c.seed, RngStream::Source);
    float r[kSourceDrawsPerSegment];

    // 1. Shorter than one grid cell: a single source event at e(0.5), no modifiers.
    if (L < g - kEps) {
        src.draw(r, kSourceDrawsPerSegment);
        Seg s{0, 1, 0.0, 0.0, EventType::Pass, {}};
        sourceParams(c, c.e(0.5), L, r, s);
        singleEvent(out, s.type, s.p, L);
        return true;
    }

    const Grid grid(L, g);

    // 2. Ending: whole cells taken from the end, at least one, at most L/2 (rounded down).
    int endCells = 0;
    if (c.endType != EndingType::None) {
        const double want = c.endingBeats < L * 0.5 ? c.endingBeats : L * 0.5;
        endCells = static_cast<int>(std::floor(want / g + kEps));
        if (endCells < 1) endCells = 1;
        if (endCells > grid.M) endCells = grid.M;
    }
    const int jb = grid.M - endCells;
    const double body = grid.beat(jb);

    // 3. Segmentation of [0, body).
    Seg segs[kMaxEvents];
    int numSegs = 0;
    double maxLen = 0.0;
    for (int i = 0; i < st.numSegmentLens; ++i) if (st.segmentLen[i] > maxLen) maxLen = st.segmentLen[i];
    const double biasDen = maxLen / g - 1.0 > 1.0 ? maxLen / g - 1.0 : 1.0;
    double lenBias[kMaxSegmentLens];
    for (int i = 0; i < st.numSegmentLens; ++i) {
        const double b = 1.0 + (0.5 - c.density) * 2.0 * (st.segmentLen[i] / g - 1.0) / biasDen;
        lenBias[i] = b < 0.05 ? 0.05 : b;
    }

    for (int j = 0; j < jb;) {
        if (numSegs >= kMaxEvents - 1) return false;            // keep room for the ending
        src.draw(r, kSourceDrawsPerSegment);
        const double pos = grid.beat(j);
        const double e = c.e(pos / L);
        double w[kMaxSegmentLens];
        for (int i = 0; i < st.numSegmentLens; ++i) w[i] = st.segmentWeight[i](e) * lenBias[i];
        double len = st.segmentLen[pickWeighted(w, st.numSegmentLens, r[0])];
        if (len > body - pos) len = body - pos;
        if (st.respectBars) {
            // Bar lines at L − k·bar; the next one strictly after pos (k >= 1).
            const int k = static_cast<int>(std::ceil((L - pos) / c.bar - kEps)) - 1;
            if (k >= 1) {
                const double line = L - static_cast<double>(k) * c.bar;
                if (pos + len > line + kEps) len = line - pos;
            }
        }
        int j1 = grid.atOrBelow(pos + len);
        if (j1 < j + 1) j1 = j + 1;
        if (j1 > jb) j1 = jb;
        Seg& s = segs[numSegs++];
        s.j0 = j;
        s.j1 = j1;
        s.e0 = e;
        sourceParams(c, e, grid.beat(j1) - pos, r, s);
        j = j1;
    }

    // Post-pass: no two Silences in a row (the 2nd becomes Pass), then merge adjacent Passes.
    for (int i = 1; i < numSegs; ++i)
        if (segs[i].type == EventType::Silence && segs[i - 1].type == EventType::Silence) {
            segs[i].type = EventType::Pass;
            clearParams(segs[i].p);
        }
    int m = 0;
    for (int i = 0; i < numSegs; ++i) {
        if (m > 0 && segs[i].type == EventType::Pass && segs[m - 1].type == EventType::Pass) segs[m - 1].j1 = segs[i].j1;
        else segs[m++] = segs[i];
    }
    numSegs = m;

    for (int i = 0; i < numSegs; ++i) {
        const double b0 = grid.beat(segs[i].j0), b1 = grid.beat(segs[i].j1);
        segs[i].eMid = c.e((b0 + b1) * 0.5 / L);
        emit(out, Lane::Source, segs[i].type, b0, b1, segs[i].p);
    }
    const double eBody = c.e(body / L);    // energy at the end of the last body segment

    // 5. Ending event over [body, L).
    if (endCells > 0) {
        float p[8] = {};
        EventType type = EventType::Pass;
        const double endLen = L - body;
        switch (c.endType) {
            case EndingType::Silence: type = EventType::Silence; p[0] = st.silenceFadeMs; break;
            case EndingType::TapeStop: type = EventType::TapeStop; p[0] = st.tapeCurve; break;
            case EndingType::ReverseSwell: type = EventType::Reverse; p[0] = floatAtMost(endLen); p[1] = -24.0f; break;
            case EndingType::Roll:
                type = EventType::Stutter;
                p[0] = floatAtMost(endLen < 0.125 ? endLen : 0.125);
                p[1] = 1.0f / 64.0f;
                p[4] = static_cast<float>(roundHalfAway(12.0 * c.pitch));
                break;
            default: break;
        }
        emit(out, Lane::Source, type, body, L, p);
    }

    // 6. Gate lane: 4 draws per body segment, whatever its type.
    {
        Rng rng(c.seed, RngStream::Gate);
        const auto& G = st.gate;
        for (int i = 0; i < numSegs; ++i) {
            float d[4];
            rng.draw(d, 4);
            const Seg& s = segs[i];
            if (s.type == EventType::Silence || s.type == EventType::TapeStop) continue;
            const double b0 = grid.beat(s.j0), b1 = grid.beat(s.j1);
            const double eMid = s.eMid;
            if (!(static_cast<double>(d[0]) < G.prob(eMid))) continue;
            if (out.numEvents >= kMaxEvents) return false;
            float p[8] = {};
            p[0] = static_cast<float>(G.rates[pickWeighted(G.rateWeights, G.numRates, d[1])]);
            p[1] = static_cast<float>(clampd(G.duty(eMid), 0.05, 1.0));
            int pi = static_cast<int>(d[2] * static_cast<float>(G.numPatterns));
            if (pi >= G.numPatterns) pi = G.numPatterns - 1;
            p[2] = static_cast<float>(G.patterns[pi]);
            p[3] = G.attackMs;
            p[4] = G.releaseMs;
            p[5] = G.depth;
            emit(out, Lane::Gate, EventType::Gate, b0, b1, p);
        }
    }

    // 6. Crush lane: 2 draws per body segment; contiguous events with continuous params merge.
    {
        Rng rng(c.seed, RngStream::Crush);
        const auto& C = st.crush;
        const double amt2 = c.crush * 2.0;
        int prev = -1, prevJ1 = -1;
        for (int i = 0; i < numSegs; ++i) {
            float d[2];
            rng.draw(d, 2);
            const Seg& s = segs[i];
            const double b0 = grid.beat(s.j0), b1 = grid.beat(s.j1);
            if (!(amt2 > 0.0) || !(static_cast<double>(d[0]) < C.prob(s.eMid) * amt2)) continue;
            const double e1 = i + 1 < numSegs ? segs[i + 1].e0 : eBody;
            const double a0 = clampd(s.e0 * amt2, 0.0, 1.0), a1 = clampd(e1 * amt2, 0.0, 1.0);
            const float bits0 = static_cast<float>(16.0 + (static_cast<double>(C.minBits) - 16.0) * a0);
            const float bits1 = static_cast<float>(16.0 + (static_cast<double>(C.minBits) - 16.0) * a1);
            const float down0 = static_cast<float>(1.0 + (static_cast<double>(C.maxDown) - 1.0) * a0);
            const float down1 = static_cast<float>(1.0 + (static_cast<double>(C.maxDown) - 1.0) * a1);
            if (prev >= 0 && prevJ1 == s.j0 && out.events[prev].p[1] == bits0 && out.events[prev].p[3] == down0) {
                FillEvent& pe = out.events[prev];
                pe.lengthBeats = b1 - pe.startBeat;
                pe.p[1] = bits1;
                pe.p[3] = down1;
            } else {
                if (out.numEvents >= kMaxEvents) return false;
                const float p[8] = {bits0, bits1, down0, down1, C.mix, 0.0f, 0.0f, 0.0f};
                prev = emit(out, Lane::Crush, EventType::Crush, b0, b1, p);
            }
            prevJ1 = s.j1;
        }
    }

    filterLanes(c, out);
    return true;
}

} // namespace

void generate(const GenSettings& s, FillPlan& out) noexcept
{
    auto unit = [](float v) { return v >= 0.0f ? (v <= 1.0f ? static_cast<double>(v) : 1.0) : 0.0; };   // NaN -> 0

    Ctx c{};
    c.L = s.lengthBeats >= kMinGenLength ? (s.lengthBeats <= kMaxGenLength ? s.lengthBeats : kMaxGenLength) : kMinGenLength;
    c.bar = s.beatsPerBar >= 0.25 && s.beatsPerBar <= 256.0 ? s.beatsPerBar : 4.0;
    c.intensity = unit(s.intensity);
    c.density = unit(s.density);
    c.pitch = unit(s.pitchAmount);
    c.crush = unit(s.crushAmount);
    c.filter = unit(s.filterAmount);
    c.seed = s.seed;
    c.curve = (s.curve != nullptr && isValidCurve(*s.curve)) ? s.curve : &curvePreset(CurvePreset::RampUp);

    const Style* st = nullptr;
    if (s.styles != nullptr && s.styles->numStyles > 0) {
        st = s.styles->find(s.styleId);
        if (st == nullptr) st = &s.styles->styles[0];
    }
    out.styleId = st != nullptr ? st->id : s.styleId;
    if (st == nullptr) {
        resetPlan(out, c);
        const float p[8] = {};
        singleEvent(out, EventType::Pass, p, c.L);
        return;
    }
    c.st = st;

    // 2. Ending: an override picks the type and uses the Ending Length knob; "Style" uses both
    //    the style's type and its ending.beats.
    if (s.endingOverride >= 1 && s.endingOverride <= static_cast<uint8_t>(EndingType::kCount)) {
        c.endType = static_cast<EndingType>(s.endingOverride - 1);
        c.endingBeats = s.endingBeats >= 0.0f && s.endingBeats <= 64.0f ? static_cast<double>(s.endingBeats) : 0.0;
    } else {
        c.endType = st->ending.type;
        c.endingBeats = st->ending.beats;
    }

    double g = st->grid;
    bool ok = false;
    for (int tryNo = 0; tryNo < kMaxTries && !ok; ++tryNo, g *= 2.0) ok = attempt(c, g, out);
    if (!ok) {
        resetPlan(out, c);
        const float p[8] = {};
        singleEvent(out, EventType::Pass, p, c.L);
    }
    assert(validate(out, c.L));
}

} // namespace tg
