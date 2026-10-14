// TransitGen core — audio -> UI telemetry (02 §9). Single writer (audio), lock-free readers.
#pragma once

#include "transitgen/FillPlan.h"

#include <atomic>
#include <cstdint>

namespace tg {

struct EngineTelemetry {
    std::atomic<bool>     playing{false};
    std::atomic<bool>     usingInternalClock{false};
    std::atomic<double>   bpm{120.0};
    std::atomic<double>   beat{0.0};              // beat of the last block start
    std::atomic<bool>     fillActive{false};
    std::atomic<float>    fillProgress{0.0f};     // 0..1
    std::atomic<uint32_t> fillSeed{0};
    std::atomic<int64_t>  phraseIndex{0};
    std::atomic<uint8_t>  currentSourceType{0};   // EventType of the active source
    std::atomic<uint32_t> fillCount{0};           // fills started since prepare
    std::atomic<int64_t>  lastFillStartT{-1};     // engine time of the last fill start
    std::atomic<int64_t>  lastFillPlayOffset{0};  // > 0 for mid-fill entries
    std::atomic<uint32_t> rejectedPlans{0};
    std::atomic<uint32_t> captureUnderruns{0};
    std::atomic<uint32_t> planEpoch{0};           // incremented after each plan publish

    /// Seqlock-style double buffer of the last played plan (POD copy).
    FillPlan plans[2];

    /// UI side: copies the last published plan. Returns false if none yet.
    bool readLastPlan(FillPlan& out) const noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t e = planEpoch.load(std::memory_order_acquire);
            if (e == 0) return false;
            out = plans[(e - 1) & 1];
            if (planEpoch.load(std::memory_order_acquire) == e) return true;
        }
        return false;
    }
};

} // namespace tg
