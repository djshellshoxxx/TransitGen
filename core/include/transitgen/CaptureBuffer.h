// TransitGen core — always-on ring buffer of the dry input (02 §4).
#pragma once

#include "transitgen/Constants.h"

#include <cstdint>
#include <vector>

namespace tg {

class CaptureBuffer {
public:
    /// Allocates. Capacity = nextPow2(kMaxLookbackBeats * 60 / kMinBpm * sr + maxBlock + 8).
    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;                 // zero, rewind to t = 0 (no allocation)

    static int64_t capacityFor(double sampleRate, int maxBlockSize) noexcept;
    int64_t capacity() const noexcept { return capacity_; }
    int     numChannels() const noexcept { return numCh_; }
    int64_t writeEnd() const noexcept { return writeEnd_; }  // engine time of the next sample to be written
    int64_t validStart() const noexcept { const int64_t s = writeEnd_ - capacity_; return s > 0 ? s : 0; }

    /// Appends numSamples frames at engine time writeEnd().
    void write(const float* const* channels, int numChannels, int numSamples) noexcept;

    /// Unchecked read; pos must be in [validStart, writeEnd).
    float at(int ch, int64_t pos) const noexcept { return data_[ch][static_cast<size_t>(pos & mask_)]; }

    /// Checked read; returns 0 outside the valid range and counts the underrun.
    float read(int ch, int64_t pos) noexcept
    {
        if (pos < validStart() || pos >= writeEnd_) { ++underruns_; return 0.0f; }
        return at(ch, pos);
    }

    uint32_t underruns() const noexcept { return underruns_; }
    void     clearUnderruns() noexcept { underruns_ = 0; }

private:
    std::vector<float> data_[kMaxChannels];
    int64_t  capacity_ = 0;
    int64_t  mask_ = 0;
    int      numCh_ = 0;
    int64_t  writeEnd_ = 0;
    uint32_t underruns_ = 0;
};

} // namespace tg
