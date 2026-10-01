// TransitGen core — host transport types and TransportTracker (02 §1).
#pragma once

#include <cstdint>

namespace tg {

struct TransportInfo {
    bool   playing = false;
    bool   hasPpq = false;
    double ppq = 0.0;
    double bpm = 0.0;
    int    tsNum = 4, tsDen = 4;
    bool   hasBarStart = false;
    double barStartPpq = 0.0;
    bool   looping = false;
};

struct MidiEvent {
    int     sampleOffset = 0;   // within the block
    uint8_t status = 0;         // 0x9n note-on, 0x8n note-off (channel ignored)
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    bool isNoteOn() const noexcept { return (status & 0xF0) == 0x90 && data2 > 0; }
    bool isNoteOff() const noexcept { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
};

struct MidiEventView {
    const MidiEvent* events = nullptr;
    int              count = 0;      // sorted by sampleOffset
};

/// Per-block timing resolved from the host (or the internal clock).
struct BlockTime {
    int64_t t0 = 0;            // engine time of the block's first sample
    int     numSamples = 0;
    double  startBeat = 0.0;   // beat of the first sample
    double  bpm = 120.0;
    double  spb = 0.0;         // samples per beat
    double  barLen = 4.0;      // beats
    double  gridOrigin = 0.0;  // beat position of a bar boundary (phase of the bar grid)
    bool    clock = false;     // true = internal clock drives startBeat
    bool    playing = false;
    bool    jumped = false;    // discontinuity vs. the previous block

    double beatAt(int offset) const noexcept { return startBeat + static_cast<double>(offset) / spb; }
    /// Sample offset (within this block, may be < 0 or >= numSamples) of a beat.
    int64_t offsetOf(double beat) const noexcept;
};

class TransportTracker {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    /// Advances engine time by numSamples and returns the resolved block timing.
    BlockTime update(const TransportInfo& info, int numSamples) noexcept;
    int64_t engineTime() const noexcept { return t_; }

private:
    double  sr_ = 48000.0;
    int64_t t_ = 0;
    int64_t clockT_ = 0;
    double  lastBpm_ = 0.0;
    bool    havePrev_ = false;
    bool    prevClock_ = false;
    double  prevStartBeat_ = 0.0;
    double  prevSpb_ = 0.0;
    int     prevN_ = 0;
};

} // namespace tg
