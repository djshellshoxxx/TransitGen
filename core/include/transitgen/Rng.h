// TransitGen core — the generator's portable RNG (03 §3). Never use std::*_distribution.
#pragma once

#include "transitgen/math.h"

#include <cstdint>

namespace tg {

/// Stream tags: one independent stream per lane so knob locality holds (03 §3, §6).
enum class RngStream : uint32_t {
    Source = 0x534F5552u,   // 'SOUR'
    Gate   = 0x47415445u,   // 'GATE'
    Crush  = 0x43525553u,   // 'CRUS'
    Filter = 0x46494C54u,   // 'FILT'
};

/// Draws per source segment, whatever branch the segment takes (03 §3).
constexpr int kSourceDrawsPerSegment = 8;

/// PCG32 (XSH-RR, 64-bit state, odd increment selects the stream).
class Rng {
public:
    constexpr Rng() noexcept = default;
    constexpr Rng(uint64_t seed, uint64_t streamTag) noexcept { seedWith(seed, streamTag); }
    constexpr Rng(uint32_t seed, RngStream stream) noexcept { seedWith(seed, static_cast<uint64_t>(stream)); }

    constexpr void seedWith(uint64_t seed, uint64_t streamTag) noexcept
    {
        state_ = splitmix64(seed);
        inc_ = (splitmix64(seed ^ streamTag) << 1) | 1u;
    }

    constexpr uint32_t next() noexcept
    {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18) ^ old) >> 27);
        const uint32_t rot = static_cast<uint32_t>(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    /// Uniform in [0, 1) with 24 bits of resolution (exact in float).
    constexpr float nextFloat() noexcept { return static_cast<float>(next() >> 8) * 0x1p-24f; }

    /// Uniform in [0, n) by Lemire's multiply-shift with rejection (unbiased). n == 0 returns 0.
    constexpr uint32_t nextInt(uint32_t n) noexcept
    {
        if (n == 0) return 0;
        uint64_t m = static_cast<uint64_t>(next()) * n;
        uint32_t l = static_cast<uint32_t>(m);
        if (l < n) {
            const uint32_t t = (0u - n) % n;
            while (l < t) {
                m = static_cast<uint64_t>(next()) * n;
                l = static_cast<uint32_t>(m);
            }
        }
        return static_cast<uint32_t>(m >> 32);
    }

    /// Fills `out` with `n` nextFloat() draws (used for the fixed per-segment budget).
    constexpr void draw(float* out, int n) noexcept { for (int i = 0; i < n; ++i) out[i] = nextFloat(); }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

/// Weighted pick for a pre-drawn u in [0,1): linear scan of the cumulative sum against u·sum.
/// Weights <= 0 (and NaN) are skipped; if the sum is 0 the last index is returned.
template <typename W>
constexpr int pickWeighted(const W* w, int n, float u) noexcept
{
    if (n <= 0) return 0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) if (w[i] > 0) sum += static_cast<double>(w[i]);
    if (!(sum > 0.0)) return n - 1;
    const double target = static_cast<double>(u) * sum;
    double acc = 0.0;
    int last = n - 1;
    for (int i = 0; i < n; ++i) {
        if (!(w[i] > 0)) continue;
        acc += static_cast<double>(w[i]);
        last = i;
        if (target < acc) return i;
    }
    return last;   // only reachable through round-off: the last positive weight
}

} // namespace tg
