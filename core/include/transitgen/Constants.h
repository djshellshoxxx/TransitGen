// TransitGen core — engine-wide constants (see docs/spec/02-fill-engine.md §0).
#pragma once

#include <cstdint>

namespace tg {

constexpr int    kMaxChannels          = 2;
constexpr double kSourceFadeMs         = 3.0;   // source-boundary crossfade, fill entry/exit
constexpr double kCutFadeMs            = 10.0;  // early release / abort fade
constexpr double kRepeatFadeMs         = 2.0;   // stutter repeat crossfade (clamped to L/4)
constexpr int    kMinFadeSamples       = 32;    // "hard" silence fade floor
constexpr int    kMinRepeatSamples     = 32;
constexpr int    kFilterUpdateInterval = 16;    // SVF coefficient anchor spacing
constexpr double kMaxLookbackBeats     = 8.0;   // capture capacity in beats at kMinBpm
constexpr double kMinBpm               = 30.0;
constexpr double kMaxBpm               = 999.0;
constexpr double kDefaultBpm           = 120.0;
constexpr int    kTapeInitialLag       = 3;     // Hermite needs two future taps
constexpr double kTapeMuteRate         = 0.02;  // tape gain = min(1, rate / this)
constexpr double kJumpToleranceBeats   = 0.005;
constexpr double kMinEntryMs           = 10.0;  // shortest mid-fill entry remainder
constexpr double kBeatEpsSamples       = 1e-6;  // see 02 §12 A1
constexpr int    kMaxLoopIterations    = 64;    // safety guard for the block loop

} // namespace tg
