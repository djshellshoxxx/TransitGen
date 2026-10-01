// TransitGen core — shared math used by the engine (02) and the generator (03).
#pragma once

#include <cmath>
#include <cstdint>

namespace tg {

/// splitmix64 finaliser.
constexpr uint64_t splitmix64(uint64_t z) noexcept
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// Per-phrase seed: low 32 bits of splitmix64((seed << 32) ^ phraseIndex); never 0.
constexpr uint32_t mixSeed(uint32_t seed, int64_t phraseIndex) noexcept
{
    const uint64_t h = splitmix64((static_cast<uint64_t>(seed) << 32) ^ static_cast<uint64_t>(phraseIndex));
    const uint32_t r = static_cast<uint32_t>(h & 0xFFFFFFFFull);
    return r == 0 ? 1u : r;
}

/// Segment bend: k in [-1,1] (clamped to +-0.99), x in [0,1]. k = 0 is linear,
/// k > 0 pushes the shape late ("exp"), k < 0 early ("log").
inline double bend(double x, double k) noexcept
{
    if (k > 0.99) k = 0.99;
    if (k < -0.99) k = -0.99;
    const double s = (1.0 + k) / (1.0 - k);
    return x * s / (1.0 + (s - 1.0) * x);
}

/// Interpolate a -> b with bend k at normalized position x.
inline double bendLerp(double a, double b, double x, double k) noexcept
{
    return a + (b - a) * bend(x, k);
}

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * (1.0f / 20.0f)); }

inline double clampd(double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float  clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

/// 4-point cubic Hermite (Catmull-Rom). At phi == 0 returns x1 bit-exactly.
inline float hermite(float xm1, float x0, float x1, float x2, float phi) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return x0 + phi * (c1 + phi * (c2 + phi * c3));
}

inline int64_t nextPow2(int64_t v) noexcept
{
    int64_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

/// Beats -> samples with the contract rounding: ceil(b*spb - eps).
inline int64_t beatsToSamples(double beats, double spb, double epsSamples) noexcept
{
    return static_cast<int64_t>(std::ceil(beats * spb - epsSamples));
}

} // namespace tg
