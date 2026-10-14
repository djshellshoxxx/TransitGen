#include "transitgen/Transport.h"
#include "transitgen/Constants.h"
#include "transitgen/math.h"

#include <cmath>

namespace tg {

int64_t BlockTime::offsetOf(double beat) const noexcept
{
    return beatsToSamples(beat - startBeat, spb, kBeatEpsSamples);
}

void TransportTracker::prepare(double sampleRate) noexcept
{
    sr_ = sampleRate;
    reset();
}

void TransportTracker::reset() noexcept
{
    t_ = 0;
    clockT_ = 0;
    lastBpm_ = 0.0;
    havePrev_ = false;
    prevClock_ = false;
    prevStartBeat_ = 0.0;
    prevSpb_ = 0.0;
    prevN_ = 0;
}

BlockTime TransportTracker::update(const TransportInfo& info, int numSamples) noexcept
{
    BlockTime bt;
    bt.t0 = t_;
    bt.numSamples = numSamples;
    if (numSamples <= 0) {
        // Zero-length block: report the current state without advancing anything.
        bt.bpm = lastBpm_ > 0.0 ? lastBpm_ : kDefaultBpm;
        bt.spb = sr_ * 60.0 / bt.bpm;
        bt.startBeat = havePrev_ ? prevStartBeat_ + prevN_ / prevSpb_ : 0.0;
        return bt;
    }

    const bool hostClock = info.playing && info.hasPpq;
    bt.clock = !hostClock;
    bt.playing = info.playing;

    // Tempo: host if valid, else last valid, else default.
    double bpm = (hostClock && info.bpm > 0.0 && std::isfinite(info.bpm)) ? info.bpm
               : (info.bpm > 0.0 && std::isfinite(info.bpm)) ? info.bpm
               : (lastBpm_ > 0.0 ? lastBpm_ : kDefaultBpm);
    bpm = clampd(bpm, kMinBpm, kMaxBpm);
    lastBpm_ = bpm;
    bt.bpm = bpm;
    bt.spb = sr_ * 60.0 / bpm;

    // Time signature with 4/4 fallback.
    const int num = info.tsNum >= 1 ? info.tsNum : 4;
    const int den = info.tsDen >= 1 ? info.tsDen : 4;
    bt.barLen = num * 4.0 / den;
    bt.gridOrigin = info.hasBarStart
        ? info.barStartPpq - bt.barLen * std::floor((info.barStartPpq + 1e-9) / bt.barLen)
        : 0.0;

    const double expected = havePrev_ ? prevStartBeat_ + static_cast<double>(prevN_) / prevSpb_ : 0.0;

    if (hostClock) {
        bt.startBeat = info.ppq;
        bt.jumped = !havePrev_ || prevClock_ || std::fabs(info.ppq - expected) > kJumpToleranceBeats;
    } else {
        if (havePrev_ && !prevClock_) {
            // play -> stop: continue the internal clock from where the host left off.
            clockT_ = static_cast<int64_t>(std::llround(expected * bt.spb));
        }
        bt.startBeat = static_cast<double>(clockT_) / bt.spb;
        bt.jumped = !havePrev_ || !prevClock_;
        clockT_ += numSamples;
    }

    havePrev_ = true;
    prevClock_ = bt.clock;
    prevStartBeat_ = bt.startBeat;
    prevSpb_ = bt.spb;
    prevN_ = numSamples;
    t_ += numSamples;
    return bt;
}

} // namespace tg
