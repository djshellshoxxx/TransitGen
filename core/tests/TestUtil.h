// Shared helpers for the TransitGen core tests: signals, a host transport simulator,
// a block-wise render driver and an allocation counter.
#pragma once

#include "transitgen/Engine.h"
#include "transitgen/TestPlanProvider.h"
#include "transitgen/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace tgt {

constexpr double kSr = 48000.0;

struct Signal {
    std::vector<float> ch[2];
    int64_t frames = 0;
    explicit Signal(int64_t n = 0) : frames(n) { ch[0].assign(static_cast<size_t>(n), 0.0f); ch[1].assign(static_cast<size_t>(n), 0.0f); }
    float& operator()(int c, int64_t i) { return ch[c][static_cast<size_t>(i)]; }
    float  operator()(int c, int64_t i) const { return ch[c][static_cast<size_t>(i)]; }
};

Signal makeDC(int64_t n, float v);
Signal makeRamp(int64_t n, float slope);               // x[i] = i * slope (same on both channels)
Signal makeSine(int64_t n, double hz, float amp, double sr = kSr);
Signal makeNoise(int64_t n, uint32_t seed, float amp); // deterministic, channels differ
bool   identical(const Signal& a, const Signal& b);

/// Simulated host transport: ppq = ppqBase + (pos - posBase) / spb.
struct HostSim {
    double  sr = kSr, bpm = 120.0;
    int     tsNum = 4, tsDen = 4;
    bool    playing = true, hasPpq = true, hasBarStart = false, looping = false;
    double  barStartPpq = 0.0;
    int64_t pos = 0, posBase = 0;
    double  ppqBase = 0.0;

    double spb() const { return sr * 60.0 / bpm; }
    double ppq() const { return ppqBase + static_cast<double>(pos - posBase) / spb(); }
    void   jumpTo(double newPpq) { ppqBase = newPpq; posBase = pos; }
    void   setBpm(double b) { ppqBase = ppq(); posBase = pos; bpm = b; }
    tg::TransportInfo info() const;
};

struct MidiAt { int64_t t; tg::MidiEvent ev; };
tg::MidiEvent noteOn(uint8_t note, uint8_t vel = 100);
tg::MidiEvent noteOff(uint8_t note);

struct RenderResult {
    Signal               out;
    std::vector<int64_t> fillStarts;        // engine time of each fill start
    std::vector<int64_t> fillPlayOffsets;
    std::vector<uint32_t> fillSeeds;
    uint32_t             rejected = 0;
};

using BlockSizeFn = std::function<int()>;
using OnBlockFn   = std::function<void(int64_t pos, HostSim&)>;

BlockSizeFn fixedBlocks(int n);
BlockSizeFn randomBlocks(uint32_t seed, int maxBlock);

/// Renders `in` through `engine` block by block. `onBlock` runs before each block (tempo, loops).
RenderResult render(tg::Engine& engine, const Signal& in, HostSim& host, const BlockSizeFn& blocks,
                    const std::vector<MidiAt>& midi = {}, const OnBlockFn& onBlock = nullptr, int maxBlock = 512);

std::unique_ptr<tg::Engine> makeEngine(tg::IPlanProvider& provider, const tg::EngineSettings& s,
                                       int maxBlock = 512, int channels = 2, double sr = kSr);
tg::EngineSettings settingsFor(tg::TriggerMode mode);

struct FillRun {
    RenderResult r;
    int64_t      start = 0;   // engine time of fill-local sample 0
    int64_t      N = 0;       // fill length in samples
};
/// Plays `plan` once at 120 BPM, starting exactly at the block-aligned `startT`
/// (Automation mode, quantize off). The fill length is `len`.
FillRun runFill(const tg::FillPlan& plan, const Signal& in, int64_t startT, tg::FillLength len,
                int block = 500, float mix = 1.0f, float gainDb = 0.0f);

/// A 4-beat plan that exercises every source type and modifier (shared by the click and
/// invariance tests). Crush covers beats [2, 3); the stutter is unpitched.
tg::FillPlan fullPlan();

/// max |x[i] - 2x[i-1] + x[i-2]| over [from, to) (indices clamped to the signal).
float maxSecondDiff(const std::vector<float>& x, int64_t from, int64_t to);
float maxAbs(const std::vector<float>& x, int64_t from, int64_t to);

/// Global operator new counter (defined in TestUtil.cpp).
int64_t allocationCount();

} // namespace tgt
