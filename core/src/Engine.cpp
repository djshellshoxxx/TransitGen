#include "transitgen/Engine.h"
#include "transitgen/Constants.h"

#include <algorithm>

namespace tg {

void Engine::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    sr_ = sampleRate;
    maxBlock_ = maxBlockSize;
    tracker_.prepare(sampleRate);
    capture_.prepare(sampleRate, maxBlockSize, numChannels);
    scheduler_.prepare(sampleRate);
    player_.prepare(sampleRate, maxBlockSize, numChannels);
    reset();
}

void Engine::reset() noexcept
{
    tracker_.reset();
    capture_.reset();
    scheduler_.reset();
    player_.reset();
    telemetry_.fillActive.store(false, std::memory_order_relaxed);
    telemetry_.fillProgress.store(0.0f, std::memory_order_relaxed);
    telemetry_.fillCount.store(0, std::memory_order_relaxed);
    telemetry_.lastFillStartT.store(-1, std::memory_order_relaxed);
    telemetry_.rejectedPlans.store(0, std::memory_order_relaxed);
    telemetry_.captureUnderruns.store(0, std::memory_order_relaxed);
}

void Engine::process(float* const* channels, int numChannels, int numSamples,
                     const TransportInfo& transport, const MidiEventView& midi) noexcept
{
    if (numSamples <= 0) return;
    const BlockTime bt = tracker_.update(transport, numSamples);
    capture_.write(channels, numChannels, numSamples);           // dry is always captured first
    scheduler_.beginBlock(bt, settings_, midi, player_.active());

    // Position-driven modes abort a running fill on a transport discontinuity.
    if (bt.jumped && player_.active() && settings_.mode != TriggerMode::Midi) player_.cut();

    int s = 0;
    for (int guard = 0; s < numSamples && guard < kMaxLoopIterations; ++guard) {
        SchedAction act;
        scheduler_.nextAction(s, player_.active(), act);
        const int a = act.kind == SchedAction::Kind::None ? numSamples : act.offset;
        const int e = player_.active() ? player_.render(channels, numChannels, s, a, bt.t0, capture_) : a;
        if (e < a) {                       // the fill ended inside [s, a): re-query from there
            s = e;
            continue;
        }
        if (act.kind == SchedAction::Kind::Start) startFill(act, bt);
        else if (act.kind == SchedAction::Kind::Cut) player_.cut();
        s = a;
    }

    telemetry_.playing.store(bt.playing, std::memory_order_relaxed);
    telemetry_.usingInternalClock.store(bt.clock, std::memory_order_relaxed);
    telemetry_.bpm.store(bt.bpm, std::memory_order_relaxed);
    telemetry_.beat.store(bt.startBeat, std::memory_order_relaxed);
    telemetry_.fillActive.store(player_.active(), std::memory_order_relaxed);
    telemetry_.fillProgress.store(player_.progress(), std::memory_order_relaxed);
    telemetry_.currentSourceType.store(static_cast<uint8_t>(player_.currentSource()), std::memory_order_relaxed);
    telemetry_.captureUnderruns.store(capture_.underruns(), std::memory_order_relaxed);
}

void Engine::startFill(const SchedAction& act, const BlockTime& bt) noexcept
{
    if (provider_ == nullptr || !provider_->makePlan(act.req, plan_) || !validate(plan_, act.req.gen.lengthBeats)) {
        telemetry_.rejectedPlans.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const int64_t startT = bt.t0 + act.offset;
    const int64_t fillStartT = startT - act.playOffset;
    player_.start(plan_, bt.spb, fillStartT, act.playOffset, settings_.mix, settings_.outGainDb);
    if (act.playOffset >= player_.lengthSamples()) {   // nothing left to play
        player_.reset();
        return;
    }
    telemetry_.fillCount.fetch_add(1, std::memory_order_relaxed);
    telemetry_.lastFillStartT.store(startT, std::memory_order_relaxed);
    telemetry_.lastFillPlayOffset.store(act.playOffset, std::memory_order_relaxed);
    telemetry_.fillSeed.store(plan_.seed, std::memory_order_relaxed);
    telemetry_.phraseIndex.store(act.req.phraseIndex, std::memory_order_relaxed);
    publishPlan();
}

void Engine::publishPlan() noexcept
{
    const uint32_t e = telemetry_.planEpoch.load(std::memory_order_relaxed);
    telemetry_.plans[e & 1] = plan_;
    telemetry_.planEpoch.store(e + 1, std::memory_order_release);
}

} // namespace tg
