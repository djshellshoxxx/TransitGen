#include "PluginState.h"

#include <algorithm>

namespace tgp {

void NonParamState::pushSeed(int seed)
{
    seedHistory.erase(std::remove(seedHistory.begin(), seedHistory.end(), seed), seedHistory.end());
    seedHistory.insert(seedHistory.begin(), seed);
    if (seedHistory.size() > static_cast<size_t>(kSeedHistorySize)) seedHistory.resize(kSeedHistorySize);
}

juce::ValueTree writeState(const juce::ValueTree& params, const NonParamState& s)
{
    juce::ValueTree root{ ids::root };
    root.setProperty(ids::stateVersion, kStateVersion, nullptr);
    root.setProperty(ids::pluginVersion, TG_VERSION_STRING, nullptr);
    root.appendChild(params.createCopy(), nullptr);

    juce::ValueTree curve{ ids::curve };
    curve.setProperty(ids::preset, s.curvePreset, nullptr);
    for (int i = 0; i < s.curve.numPoints && i < tg::kMaxCurvePoints; ++i) {
        const tg::CurvePoint& p = s.curve.points[i];
        juce::ValueTree pt{ ids::point };
        pt.setProperty(ids::t, p.t, nullptr);
        pt.setProperty(ids::v, p.value, nullptr);
        pt.setProperty(ids::c, p.curve, nullptr);
        curve.appendChild(pt, nullptr);
    }
    root.appendChild(curve, nullptr);

    juce::StringArray hist;
    for (int seed : s.seedHistory) hist.add(juce::String(seed));
    root.appendChild(juce::ValueTree{ ids::seeds, { { ids::history, hist.joinIntoString(",") } } }, nullptr);
    root.appendChild(juce::ValueTree{ ids::lock, { { ids::on, s.locked ? 1 : 0 } } }, nullptr);
    root.appendChild(juce::ValueTree{ ids::style, { { ids::id, static_cast<int>(s.styleId) }, { ids::version, s.styleVersion } } },
                     nullptr);
    root.appendChild(s.frozen.createCopy(), nullptr);
    if (s.ui.isValid()) root.appendChild(s.ui.createCopy(), nullptr);
    return root;
}

namespace {
bool readCurve(const juce::ValueTree& node, NonParamState& s)
{
    if (!node.isValid()) return false;
    tg::EnergyCurve c{};
    for (const juce::ValueTree& pt : node) {
        if (!pt.hasType(ids::point)) continue;
        if (c.numPoints >= tg::kMaxCurvePoints) return false;
        c.points[c.numPoints++] = { static_cast<float>(pt[ids::t]), static_cast<float>(pt[ids::v]),
                                    static_cast<float>(pt[ids::c]) };
    }
    const juce::String preset = node[ids::preset].toString();
    if (tg::isValidCurve(c)) {
        s.curve = c;
        s.curvePreset = preset;
        return true;
    }
    for (int i = 0; i < static_cast<int>(tg::CurvePreset::kCount); ++i)   // invalid points: fall back to the named preset
        if (preset == tg::kCurvePresetNames[i]) {
            s.curve = tg::kCurvePresets[i];
            s.curvePreset = preset;
            return true;
        }
    return false;
}
} // namespace

bool readState(juce::ValueTree root, LoadedState& out)
{
    out = LoadedState{};
    if (!root.hasType(ids::root)) return false;
    out.stateVersion = root.getProperty(ids::stateVersion, 1);
    out.newerVersion = out.stateVersion > kStateVersion;
    if (out.stateVersion < kStateVersion) migrate(root, out.stateVersion);

    out.params = root.getChildWithName(ids::params);
    readCurve(root.getChildWithName(ids::curve), out.state);

    const juce::ValueTree seeds = root.getChildWithName(ids::seeds);
    if (seeds.isValid()) {
        juce::StringArray tok;
        tok.addTokens(seeds[ids::history].toString(), ",", "");
        for (const juce::String& t : tok) {
            const int seed = t.trim().getIntValue();
            const bool dup = std::find(out.state.seedHistory.begin(), out.state.seedHistory.end(), seed) != out.state.seedHistory.end();
            if (seed >= 1 && seed <= 99999 && !dup && out.state.seedHistory.size() < static_cast<size_t>(kSeedHistorySize))
                out.state.seedHistory.push_back(seed);
        }
    }
    const juce::ValueTree lock = root.getChildWithName(ids::lock);
    if (lock.isValid()) out.state.locked = static_cast<int>(lock[ids::on]) != 0;

    const juce::ValueTree style = root.getChildWithName(ids::style);
    if (style.isValid() && style.hasProperty(ids::id)) {
        out.hasStyle = true;
        out.state.styleId = static_cast<uint16_t>(std::clamp(static_cast<int>(style[ids::id]), 0, 65535));
        out.state.styleVersion = style.getProperty(ids::version, 1);
    }
    const juce::ValueTree frozen = root.getChildWithName(ids::frozen);
    if (frozen.isValid()) out.state.frozen = frozen.createCopy();
    const juce::ValueTree ui = root.getChildWithName(ids::ui);
    if (ui.isValid()) out.state.ui = ui.createCopy();
    return true;
}

void migrate(juce::ValueTree& root, int fromVersion)
{
    for (int v = std::max(fromVersion, 1); v < kStateVersion; ++v) {
        // switch (v) { case 1: migrate_v1_to_v2(root); break; }   // future format changes go here
    }
    root.setProperty(ids::stateVersion, kStateVersion, nullptr);
}

} // namespace tgp
