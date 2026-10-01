#include "transitgen/StyleLoader.h"

#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace tg {

namespace {

using json::Value;

constexpr const char* kSourceTypeNames[kNumSourceTypes] = {"Pass", "Stutter", "Reverse", "TapeStop", "TapeStart", "Silence"};

/// Reads typed fields with "path: message" errors. Every accessor returns false on error.
class Reader {
public:
    explicit Reader(std::string& err) : err_(err) {}

    bool fail(const std::string& path, const std::string& what)
    {
        err_ = path + ": " + what;
        return false;
    }

    /// Only `allowed` keys may appear in `obj`, and all of `required` must.
    bool keys(const Value& obj, const std::string& path, std::initializer_list<const char*> allowed,
              std::initializer_list<const char*> required)
    {
        if (obj.type != Value::Type::Object) return fail(path.empty() ? "<root>" : path, "must be an object");
        for (const auto& kv : obj.obj) {
            bool ok = false;
            for (const char* a : allowed) if (kv.first == a) ok = true;
            if (!ok) return fail(join(path, kv.first), "unknown key");
        }
        for (const char* r : required)
            if (obj.get(r) == nullptr) return fail(join(path, r), "missing");
        return true;
    }

    static std::string join(const std::string& path, const std::string& key) { return path.empty() ? key : path + "." + key; }

    bool number(const Value& v, const std::string& path, double lo, double hi, double& out)
    {
        if (v.type != Value::Type::Number) return fail(path, "must be a number");
        if (!(v.num >= lo && v.num <= hi)) return fail(path, "out of range [" + fmt(lo) + ", " + fmt(hi) + "]");
        out = v.num;
        return true;
    }
    bool number(const Value& v, const std::string& path, double lo, double hi, float& out)
    {
        double d;
        if (!number(v, path, lo, hi, d)) return false;
        out = static_cast<float>(d);
        return true;
    }
    bool integer(const Value& v, const std::string& path, long lo, long hi, long& out)
    {
        if (v.type != Value::Type::Number || !v.isInteger) return fail(path, "must be an integer");
        if (!(v.num >= static_cast<double>(lo) && v.num <= static_cast<double>(hi)))
            return fail(path, "out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
        out = static_cast<long>(v.num);
        return true;
    }
    bool boolean(const Value& v, const std::string& path, bool& out)
    {
        if (v.type != Value::Type::Bool) return fail(path, "must be true or false");
        out = v.b;
        return true;
    }
    bool array(const Value& v, const std::string& path, size_t minN, size_t maxN)
    {
        if (v.type != Value::Type::Array) return fail(path, "must be an array");
        if (v.arr.size() < minN || v.arr.size() > maxN)
            return fail(path, "must have " + std::to_string(minN) + ".." + std::to_string(maxN) + " entries");
        return true;
    }
    bool choice(const Value& v, const std::string& path, std::initializer_list<const char*> names, int& out)
    {
        std::string list;
        int i = 0;
        for (const char* n : names) {
            if (v.type == Value::Type::String && v.str == n) { out = i; return true; }
            list += (i ? ", \"" : "\"") + std::string(n) + "\"";
            ++i;
        }
        return fail(path, "must be one of " + list);
    }

    /// T = [[e, v], ...]: 1..5 points, e in [0,1] strictly increasing, v in [vlo, vhi].
    bool table(const Value& v, const std::string& path, double vlo, double vhi, EnergyTable& out)
    {
        if (!array(v, path, 1, kMaxTablePoints)) return false;
        out = EnergyTable{};
        for (size_t i = 0; i < v.arr.size(); ++i) {
            const std::string p = path + "[" + std::to_string(i) + "]";
            const Value& pt = v.arr[i];
            if (pt.type != Value::Type::Array || pt.arr.size() != 2) return fail(p, "must be a pair [e, v]");
            double e, val;
            if (!number(pt.arr[0], p + "[0]", 0.0, 1.0, e) || !number(pt.arr[1], p + "[1]", vlo, vhi, val)) return false;
            if (i > 0 && !(static_cast<float>(e) > out.e[i - 1])) return fail(p, "energies must be strictly increasing");
            out.e[i] = static_cast<float>(e);
            out.v[i] = static_cast<float>(val);
        }
        out.n = static_cast<int>(v.arr.size());
        return true;
    }

private:
    static std::string fmt(double d)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", d);
        return buf;
    }
    std::string& err_;
};

/// A sum of piecewise-linear tables is piecewise linear between the union of their breakpoints,
/// so it is positive for every e iff it is positive at every breakpoint and at 0 and 1.
bool sumPositiveEverywhere(const EnergyTable* t, int n)
{
    auto sumAt = [&](double e) { double s = 0.0; for (int i = 0; i < n; ++i) s += t[i](e); return s; };
    if (!(sumAt(0.0) > 0.0) || !(sumAt(1.0) > 0.0)) return false;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < t[i].n; ++k)
            if (!(sumAt(t[i].e[k]) > 0.0)) return false;
    return true;
}

bool validRate(long r) { return r == 1 || r == 2 || r == 3 || r == 4 || r == 6 || r == 8; }

bool parseInto(const Value& root, Style& s, Reader& rd, StyleOrigin origin)
{
    if (!rd.keys(root, "", {"id", "name", "version", "grid", "respectBars", "segment", "source", "stutter", "tape", "silence",
                            "gate", "crush", "filter", "ending"},
                 {"id", "name", "version", "grid", "respectBars", "segment", "source", "stutter", "tape", "silence", "gate",
                  "crush", "filter", "ending"}))
        return false;

    long l;
    if (origin == StyleOrigin::Factory) { if (!rd.integer(*root.get("id"), "id", 1, 99, l)) return false; }
    else if (!rd.integer(*root.get("id"), "id", 100, 65535, l)) return false;
    s.id = static_cast<uint16_t>(l);

    const Value& name = *root.get("name");
    if (name.type != Value::Type::String || name.str.empty()) return rd.fail("name", "must be a non-empty string");
    if (name.str.size() >= static_cast<size_t>(kStyleNameLen))
        return rd.fail("name", "longer than " + std::to_string(kStyleNameLen - 1) + " bytes");
    std::memcpy(s.name, name.str.c_str(), name.str.size() + 1);

    if (!rd.integer(*root.get("version"), "version", 1, 1000000, l)) return false;
    s.version = static_cast<int>(l);
    if (!rd.number(*root.get("grid"), "grid", 1.0 / 64.0, 4.0, s.grid)) return false;
    if (!rd.boolean(*root.get("respectBars"), "respectBars", s.respectBars)) return false;

    // segment
    {
        const Value& seg = *root.get("segment");
        if (!rd.keys(seg, "segment", {"len", "weight"}, {"len", "weight"})) return false;
        const Value& len = *seg.get("len");
        const Value& w = *seg.get("weight");
        if (!rd.array(len, "segment.len", 1, kMaxSegmentLens) || !rd.array(w, "segment.weight", 1, kMaxSegmentLens)) return false;
        if (w.arr.size() != len.arr.size()) return rd.fail("segment.weight", "needs one table per segment.len entry");
        s.numSegmentLens = static_cast<int>(len.arr.size());
        for (int i = 0; i < s.numSegmentLens; ++i) {
            const std::string idx = "[" + std::to_string(i) + "]";
            if (!rd.number(len.arr[static_cast<size_t>(i)], "segment.len" + idx, 1.0 / 64.0, 32.0, s.segmentLen[i])) return false;
            if (!rd.table(w.arr[static_cast<size_t>(i)], "segment.weight" + idx, 0.0, 1000.0, s.segmentWeight[i])) return false;
        }
        if (!sumPositiveEverywhere(s.segmentWeight, s.numSegmentLens))
            return rd.fail("segment.weight", "weights must not all be 0 at any energy");
    }

    // source
    {
        const Value& src = *root.get("source");
        if (!rd.keys(src, "source", {"prob"}, {"prob"})) return false;
        const Value& prob = *src.get("prob");
        if (!rd.keys(prob, "source.prob", {"Pass", "Stutter", "Reverse", "TapeStop", "TapeStart", "Silence"}, {})) return false;
        for (int i = 0; i < kNumSourceTypes; ++i) {
            s.sourceProb[i] = EnergyTable{};
            if (const Value* t = prob.get(kSourceTypeNames[i]))
                if (!rd.table(*t, std::string("source.prob.") + kSourceTypeNames[i], 0.0, 1000.0, s.sourceProb[i])) return false;
        }
        if (!sumPositiveEverywhere(s.sourceProb, kNumSourceTypes))
            return rd.fail("source.prob", "weights must not all be 0 at any energy");
    }

    // stutter
    {
        const Value& st = *root.get("stutter");
        if (!rd.keys(st, "stutter", {"slices", "sliceWeight", "rollProb", "rollDiv", "rampMode", "pitchProb", "pitchSemis",
                                     "pitchDir", "pitchFromZero", "decayDb"},
                     {"slices", "sliceWeight", "rollProb", "rollDiv", "rampMode", "pitchProb", "pitchSemis", "pitchDir",
                      "pitchFromZero", "decayDb"}))
            return false;
        auto& S = s.stutter;
        const Value& sl = *st.get("slices");
        const Value& sw = *st.get("sliceWeight");
        if (!rd.array(sl, "stutter.slices", 1, kMaxSlices) || !rd.array(sw, "stutter.sliceWeight", 1, kMaxSlices)) return false;
        if (sw.arr.size() != sl.arr.size()) return rd.fail("stutter.sliceWeight", "needs one table per stutter.slices entry");
        S.numSlices = static_cast<int>(sl.arr.size());
        for (int i = 0; i < S.numSlices; ++i) {
            const std::string idx = "[" + std::to_string(i) + "]";
            if (!rd.number(sl.arr[static_cast<size_t>(i)], "stutter.slices" + idx, 1.0 / 256.0, 8.0, S.slices[i])) return false;
            if (!rd.table(sw.arr[static_cast<size_t>(i)], "stutter.sliceWeight" + idx, 0.0, 1000.0, S.sliceWeight[i])) return false;
        }
        if (!sumPositiveEverywhere(S.sliceWeight, S.numSlices))
            return rd.fail("stutter.sliceWeight", "weights must not all be 0 at any energy");
        if (!rd.table(*st.get("rollProb"), "stutter.rollProb", 0.0, 1.0, S.rollProb)) return false;
        if (!rd.integer(*st.get("rollDiv"), "stutter.rollDiv", 2, 8, l) || !(l == 2 || l == 4 || l == 8))
            return rd.fail("stutter.rollDiv", "must be 2, 4 or 8");
        S.rollDiv = static_cast<int>(l);
        if (!rd.integer(*st.get("rampMode"), "stutter.rampMode", 0, 1, l)) return false;
        S.rampMode = static_cast<uint8_t>(l);
        if (!rd.table(*st.get("pitchProb"), "stutter.pitchProb", 0.0, 1.0, S.pitchProb)) return false;
        if (!rd.number(*st.get("pitchSemis"), "stutter.pitchSemis", 0.0, 24.0, S.pitchSemis)) return false;
        int c;
        if (!rd.choice(*st.get("pitchDir"), "stutter.pitchDir", {"up", "down", "both"}, c)) return false;
        S.pitchDir = static_cast<PitchDir>(c);
        if (!rd.boolean(*st.get("pitchFromZero"), "stutter.pitchFromZero", S.pitchFromZero)) return false;
        if (!rd.number(*st.get("decayDb"), "stutter.decayDb", -12.0, 0.0, S.decayDb)) return false;
    }

    // tape, silence
    {
        const Value& tape = *root.get("tape");
        if (!rd.keys(tape, "tape", {"curve"}, {"curve"}) || !rd.number(*tape.get("curve"), "tape.curve", -1.0, 1.0, s.tapeCurve))
            return false;
        const Value& sil = *root.get("silence");
        if (!rd.keys(sil, "silence", {"fadeMs"}, {"fadeMs"})
            || !rd.number(*sil.get("fadeMs"), "silence.fadeMs", 0.0, 50.0, s.silenceFadeMs))
            return false;
    }

    // gate
    {
        const Value& g = *root.get("gate");
        if (!rd.keys(g, "gate", {"prob", "rates", "rateWeights", "duty", "patterns", "attackMs", "releaseMs", "depth"},
                     {"prob", "rates", "rateWeights", "duty", "patterns", "attackMs", "releaseMs", "depth"}))
            return false;
        auto& G = s.gate;
        if (!rd.table(*g.get("prob"), "gate.prob", 0.0, 1.0, G.prob)) return false;
        if (!rd.table(*g.get("duty"), "gate.duty", 0.05, 1.0, G.duty)) return false;
        const Value& rates = *g.get("rates");
        const Value& rw = *g.get("rateWeights");
        if (!rd.array(rates, "gate.rates", 1, kMaxGateRates) || !rd.array(rw, "gate.rateWeights", 1, kMaxGateRates)) return false;
        if (rw.arr.size() != rates.arr.size()) return rd.fail("gate.rateWeights", "needs one weight per gate.rates entry");
        G.numRates = static_cast<int>(rates.arr.size());
        double wsum = 0.0;
        for (int i = 0; i < G.numRates; ++i) {
            const std::string idx = "[" + std::to_string(i) + "]";
            if (!rd.integer(rates.arr[static_cast<size_t>(i)], "gate.rates" + idx, 1, 8, l) || !validRate(l))
                return rd.fail("gate.rates" + idx, "must be 1, 2, 3, 4, 6 or 8");
            G.rates[i] = static_cast<int>(l);
            if (!rd.number(rw.arr[static_cast<size_t>(i)], "gate.rateWeights" + idx, 0.0, 1000.0, G.rateWeights[i])) return false;
            wsum += G.rateWeights[i];
        }
        if (!(wsum > 0.0)) return rd.fail("gate.rateWeights", "must not all be 0");
        const Value& pats = *g.get("patterns");
        if (!rd.array(pats, "gate.patterns", 1, kMaxGatePatterns)) return false;
        G.numPatterns = static_cast<int>(pats.arr.size());
        for (int i = 0; i < G.numPatterns; ++i) {
            const Value& p = pats.arr[static_cast<size_t>(i)];
            const std::string path = "gate.patterns[" + std::to_string(i) + "]";
            if (p.type != Value::Type::String || p.str.size() != 16 || p.str.find_first_not_of("01") != std::string::npos)
                return rd.fail(path, "must be a string of 16 '0'/'1' characters");
            uint16_t bits = 0;
            for (int k = 0; k < 16; ++k) if (p.str[static_cast<size_t>(k)] == '1') bits = static_cast<uint16_t>(bits | (1u << k));
            if (bits == 0) return rd.fail(path, "must open at least one step");
            G.patterns[i] = bits;
        }
        if (!rd.number(*g.get("attackMs"), "gate.attackMs", 0.0, 1000.0, G.attackMs)) return false;
        if (!rd.number(*g.get("releaseMs"), "gate.releaseMs", 0.0, 1000.0, G.releaseMs)) return false;
        if (!rd.number(*g.get("depth"), "gate.depth", 0.0, 1.0, G.depth)) return false;
    }

    // crush
    {
        const Value& c = *root.get("crush");
        if (!rd.keys(c, "crush", {"prob", "minBits", "maxDown", "mix"}, {"prob", "minBits", "maxDown", "mix"})) return false;
        if (!rd.table(*c.get("prob"), "crush.prob", 0.0, 1.0, s.crush.prob)) return false;
        if (!rd.number(*c.get("minBits"), "crush.minBits", 1.0, 16.0, s.crush.minBits)) return false;
        if (!rd.number(*c.get("maxDown"), "crush.maxDown", 1.0, 64.0, s.crush.maxDown)) return false;
        if (!rd.number(*c.get("mix"), "crush.mix", 0.0, 1.0, s.crush.mix)) return false;
    }

    // filter
    {
        const Value& f = *root.get("filter");
        if (!rd.keys(f, "filter", {"type", "from", "to", "res", "wobble"}, {"type", "from", "to", "res", "wobble"})) return false;
        int c;
        if (!rd.choice(*f.get("type"), "filter.type", {"Off", "LowPass", "HighPass", "BandPass"}, c)) return false;
        s.filter.type = static_cast<FilterType>(c);
        if (!rd.number(*f.get("from"), "filter.from", 0.0, 1.0, s.filter.from)) return false;
        if (!rd.number(*f.get("to"), "filter.to", 0.0, 1.0, s.filter.to)) return false;
        if (!rd.number(*f.get("res"), "filter.res", 0.0, 1.0, s.filter.res)) return false;
        if (!rd.number(*f.get("wobble"), "filter.wobble", 0.0, 1.0, s.filter.wobble)) return false;
    }

    // ending
    {
        const Value& e = *root.get("ending");
        if (!rd.keys(e, "ending", {"type", "beats"}, {"type", "beats"})) return false;
        int c;
        if (!rd.choice(*e.get("type"), "ending.type", {"None", "Silence", "TapeStop", "ReverseSwell", "Roll"}, c)) return false;
        s.ending.type = static_cast<EndingType>(c);
        if (!rd.number(*e.get("beats"), "ending.beats", 1.0 / 64.0, 16.0, s.ending.beats)) return false;
    }
    return true;
}

} // namespace

bool parseStyle(std::string_view text, Style& out, std::string& error, StyleOrigin origin)
{
    json::Value root;
    if (!json::parse(text, root, error)) return false;
    out = Style{};
    Reader rd(error);
    return parseInto(root, out, rd, origin);
}

bool addStyle(StyleTable& table, const Style& s, std::string& error)
{
    if (table.find(s.id) != nullptr) { error = "duplicate style id " + std::to_string(s.id); return false; }
    if (table.numStyles >= kMaxStyles) { error = "style table full (" + std::to_string(kMaxStyles) + " styles)"; return false; }
    table.styles[table.numStyles++] = s;
    return true;
}

bool loadFactoryStyles(StyleTable& table, std::string& error)
{
    int n = 0;
    const EmbeddedStyle* src = factoryStyleSources(n);
    if (n == 0) { error = "no factory styles embedded"; return false; }
    for (int i = 0; i < n; ++i) {
        Style s;
        std::string err;
        if (!parseStyle(src[i].json, s, err, StyleOrigin::Factory) || !addStyle(table, s, err)) {
            error = std::string(src[i].fileName) + ": " + err;
            return false;
        }
    }
    return true;
}

std::unique_ptr<StyleTable> makeFactoryStyleTable(std::string* error)
{
    auto t = std::make_unique<StyleTable>();
    std::string err;
    if (!loadFactoryStyles(*t, err)) {
        if (error) *error = err;
        return nullptr;
    }
    return t;
}

} // namespace tg
