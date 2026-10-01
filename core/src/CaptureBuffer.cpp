#include "transitgen/CaptureBuffer.h"
#include "transitgen/math.h"

#include <algorithm>
#include <cmath>

namespace tg {

int64_t CaptureBuffer::capacityFor(double sampleRate, int maxBlockSize) noexcept
{
    const double lookback = kMaxLookbackBeats * 60.0 / kMinBpm * sampleRate;
    return nextPow2(static_cast<int64_t>(std::ceil(lookback)) + maxBlockSize + 8);
}

void CaptureBuffer::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    numCh_ = std::min(numChannels, kMaxChannels);
    capacity_ = capacityFor(sampleRate, maxBlockSize);
    mask_ = capacity_ - 1;
    for (int c = 0; c < kMaxChannels; ++c) {
        data_[c].assign(c < numCh_ ? static_cast<size_t>(capacity_) : 0, 0.0f);
    }
    reset();
}

void CaptureBuffer::reset() noexcept
{
    for (int c = 0; c < numCh_; ++c) std::fill(data_[c].begin(), data_[c].end(), 0.0f);
    writeEnd_ = 0;
    underruns_ = 0;
}

void CaptureBuffer::write(const float* const* channels, int numChannels, int numSamples) noexcept
{
    const int ch = std::min(numChannels, numCh_);
    for (int c = 0; c < ch; ++c) {
        float* dst = data_[c].data();
        const float* src = channels[c];
        const int64_t start = writeEnd_ & mask_;
        const int64_t first = std::min<int64_t>(numSamples, capacity_ - start);
        std::copy(src, src + first, dst + start);
        if (first < numSamples) std::copy(src + first, src + numSamples, dst);
    }
    writeEnd_ += numSamples;
}

} // namespace tg
