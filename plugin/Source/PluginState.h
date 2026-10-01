// TransitGen plugin — host project state (06 §1): ValueTree layout, tolerant loading, migrations.
#pragma once

#include "transitgen/EnergyCurve.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <vector>

namespace tgp {

inline constexpr int kStateVersion    = 1;
inline constexpr int kSeedHistorySize = 32;   // 03 §7

namespace ids {
inline const juce::Identifier root{ "TransitGen" }, stateVersion{ "stateVersion" }, pluginVersion{ "pluginVersion" },
    params{ "PARAMS" }, param{ "PARAM" }, id{ "id" }, value{ "value" },
    curve{ "CURVE" }, preset{ "preset" }, point{ "P" }, t{ "t" }, v{ "v" }, c{ "c" },
    seeds{ "SEEDS" }, history{ "history" }, lock{ "LOCK" }, on{ "on" },
    style{ "STYLE" }, version{ "version" }, frozen{ "FROZEN" }, ui{ "UI" };
} // namespace ids

/// Everything in the state that is not an APVTS parameter. Message thread only.
struct NonParamState {
    tg::EnergyCurve  curve = tg::curvePreset(tg::CurvePreset::RampUp);
    juce::String     curvePreset = tg::kCurvePresetNames[0];   // empty = custom curve
    std::vector<int> seedHistory;                              // newest first, no duplicates
    bool             locked = false;
    uint16_t         styleId = 1;
    int              styleVersion = 1;
    juce::ValueTree  frozen{ ids::frozen };   // placeholder until 06 §3; kept verbatim across load/save
    juce::ValueTree  ui;                      // kept verbatim when present (M4 writes it)

    NonParamState() { frozen.setProperty(ids::on, 0, nullptr); }

    /// Pushes `seed` to the front of the history (03 §7: 32 entries, newest first, no duplicates).
    void pushSeed(int seed);
};

/// Builds the <TransitGen> tree from the APVTS <PARAMS> tree and the non-parameter state.
juce::ValueTree writeState(const juce::ValueTree& params, const NonParamState& s);

struct LoadedState {
    juce::ValueTree params;          // the <PARAMS> child, or invalid when missing
    NonParamState   state;
    bool            hasStyle = false;   // <STYLE> present: its id is authoritative over the choice index
    int             stateVersion = 0;
    bool            newerVersion = false;   // saved by a newer TransitGen: loaded what we could
};

/// Tolerant parse: unknown elements are ignored, missing or invalid ones get defaults.
/// Returns false only if `root` is not a TransitGen state at all.
bool readState(juce::ValueTree root, LoadedState& out);

/// Migration chain migrate_vN_to_vN+1 (06 §1). Version 1 is the first format: nothing to do yet.
void migrate(juce::ValueTree& root, int fromVersion);

} // namespace tgp
