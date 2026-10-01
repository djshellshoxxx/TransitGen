// TransitGen core — the real-time fill engine (01 §5, 02 §10).
#pragma once

#include "transitgen/CaptureBuffer.h"
#include "transitgen/FillPlan.h"
#include "transitgen/FillScheduler.h"
#include "transitgen/PlanPlayer.h"
#include "transitgen/Settings.h"
#include "transitgen/Telemetry.h"
#include "transitgen/Transport.h"

namespace tg {

class Engine {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels);   // allocates
    void reset() noexcept;                                                // clears state, no alloc

    void setSettings(const EngineSettings& s) noexcept { settings_ = s; }   // audio thread, per block
    void setPlanProvider(IPlanProvider* p) noexcept { provider_ = p; }      // before processing

    /// In-place processing. Channels beyond kMaxChannels are left untouched.
    void process(float* const* channels, int numChannels, int numSamples,
                 const TransportInfo& transport, const MidiEventView& midi) noexcept;

    const EngineTelemetry& telemetry() const noexcept { return telemetry_; }
    const CaptureBuffer&   capture() const noexcept { return capture_; }
    bool                   fillActive() const noexcept { return player_.active(); }
    int64_t                engineTime() const noexcept { return tracker_.engineTime(); }

private:
    void startFill(const SchedAction& act, const BlockTime& bt) noexcept;
    void publishPlan() noexcept;

    double           sr_ = 48000.0;
    int              maxBlock_ = 0;
    TransportTracker tracker_;
    CaptureBuffer    capture_;
    FillScheduler    scheduler_;
    PlanPlayer       player_;
    EngineSettings   settings_{};
    IPlanProvider*   provider_ = nullptr;
    FillPlan         plan_{};
    EngineTelemetry  telemetry_;
};

} // namespace tg
