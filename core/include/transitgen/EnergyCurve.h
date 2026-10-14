// TransitGen core — the energy curve (03 §4). POD, published immutable to the audio thread (01 §7).
#pragma once

#include "transitgen/math.h"

namespace tg {

constexpr int kMaxCurvePoints = 16;

struct CurvePoint {
    float t;       // 0..1, sorted; first 0, last 1
    float value;   // 0..1
    float curve;   // -1..1 bend of the segment to the next point
};

struct EnergyCurve {
    int        numPoints = 0;
    CurvePoint points[kMaxCurvePoints]{};

    /// E(t), t clamped to [0,1]. No points -> 0, one point -> constant.
    double evaluate(double t) const noexcept
    {
        if (numPoints <= 0) return 0.0;
        const int n = numPoints < kMaxCurvePoints ? numPoints : kMaxCurvePoints;
        t = clampd(t, 0.0, 1.0);
        if (n == 1 || t <= static_cast<double>(points[0].t)) return points[0].value;
        for (int i = 0; i + 1 < n; ++i) {
            const double t0 = points[i].t, t1 = points[i + 1].t;
            if (t > t1) continue;
            const double span = t1 - t0;
            const double x = span > 0.0 ? (t - t0) / span : 1.0;
            return bendLerp(points[i].value, points[i + 1].value, x, points[i].curve);
        }
        return points[n - 1].value;
    }
};

/// e(t) = clamp(E(t) · intensity / 0.7, 0, 1): intensity 0.7 reproduces the curve as drawn.
inline double effectiveEnergy(const EnergyCurve& c, double t, double intensity) noexcept
{
    return clampd(c.evaluate(t) * intensity / 0.7, 0.0, 1.0);
}

/// True when the curve satisfies 03 §4 (2..16 points, sorted t from 0 to 1, values in range).
constexpr bool isValidCurve(const EnergyCurve& c) noexcept
{
    if (c.numPoints < 2 || c.numPoints > kMaxCurvePoints) return false;
    if (c.points[0].t != 0.0f || c.points[c.numPoints - 1].t != 1.0f) return false;
    for (int i = 0; i < c.numPoints; ++i) {
        const CurvePoint& p = c.points[i];
        if (!(p.t >= 0.0f && p.t <= 1.0f) || !(p.value >= 0.0f && p.value <= 1.0f) || !(p.curve >= -1.0f && p.curve <= 1.0f))
            return false;
        if (i > 0 && p.t < c.points[i - 1].t) return false;
    }
    return true;
}

// ---------------------------------------------------------------- factory presets (03 §4)
enum class CurvePreset : int { RampUp = 0, RampDown, BuildAndCut, Swell, Pulse, Plateau, Chaos, Flat, kCount };

constexpr EnergyCurve kCurvePresets[static_cast<int>(CurvePreset::kCount)] = {
    {2, {{0.0f, 0.1f, 0.3f}, {1.0f, 1.0f, 0.0f}}},
    {2, {{0.0f, 1.0f, -0.3f}, {1.0f, 0.1f, 0.0f}}},
    {4, {{0.0f, 0.2f, 0.4f}, {0.85f, 1.0f, 0.0f}, {0.86f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}},
    {3, {{0.0f, 0.0f, 0.5f}, {0.5f, 1.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}},
    {5, {{0.0f, 0.3f, 0.0f}, {0.25f, 0.9f, 0.0f}, {0.5f, 0.3f, 0.0f}, {0.75f, 0.9f, 0.0f}, {1.0f, 0.3f, 0.0f}}},
    {3, {{0.0f, 0.2f, 0.6f}, {0.3f, 0.85f, 0.0f}, {1.0f, 0.85f, 0.0f}}},
    {7, {{0.0f, 0.5f, 0.0f}, {0.15f, 0.9f, 0.0f}, {0.3f, 0.2f, 0.0f}, {0.45f, 1.0f, 0.0f}, {0.6f, 0.35f, 0.0f},
         {0.8f, 0.95f, 0.0f}, {1.0f, 0.6f, 0.0f}}},
    {2, {{0.0f, 0.6f, 0.0f}, {1.0f, 0.6f, 0.0f}}},
};

constexpr const char* kCurvePresetNames[static_cast<int>(CurvePreset::kCount)] = {
    "Ramp Up", "Ramp Down", "Build & Cut", "Swell", "Pulse", "Plateau", "Chaos", "Flat",
};

constexpr const EnergyCurve& curvePreset(CurvePreset p) noexcept { return kCurvePresets[static_cast<int>(p)]; }

static_assert(isValidCurve(kCurvePresets[0]) && isValidCurve(kCurvePresets[1]) && isValidCurve(kCurvePresets[2])
              && isValidCurve(kCurvePresets[3]) && isValidCurve(kCurvePresets[4]) && isValidCurve(kCurvePresets[5])
              && isValidCurve(kCurvePresets[6]) && isValidCurve(kCurvePresets[7]), "factory curve presets must be valid");

} // namespace tg
