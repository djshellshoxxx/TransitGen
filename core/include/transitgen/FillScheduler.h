// TransitGen core — decides when fills start and stop (02 §2).
#pragma once

#include "transitgen/Settings.h"
#include "transitgen/Transport.h"

#include <cstdint>

namespace tg {

struct SchedAction {
    enum class Kind : uint8_t { None, Start, Cut };
    Kind        kind = Kind::None;
    int         offset = 0;        // block offset of the action
    int64_t     playOffset = 0;    // Start: fill-local sample to enter at (> 0 = mid-fill entry)
    FillRequest req{};             // Start only
};

class FillScheduler {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// Call once per block before nextAction(): edge detection, jump handling, MIDI cursor reset.
    void beginBlock(const BlockTime& bt, const EngineSettings& s, const MidiEventView& midi, bool playerActive) noexcept;

    /// Next action at offset >= from (Kind::None when nothing happens before the block end).
    /// Consumes whatever it returns, so repeated calls with increasing `from` make progress.
    void nextAction(int from, bool playerActive, SchedAction& out) noexcept;

    /// Fill-length (beats) for the current settings, per mode rules; `midiNote` < 0 for non-MIDI.
    double fillBeats(int midiNote = -1) const noexcept;

    bool    hasPending() const noexcept { return pending_; }
    int64_t lastStartedPhrase() const noexcept { return lastPhrase_; }

private:
    struct Pending {
        bool    bySample = false;  // true: fire at absolute sample pendingT; false: at pendingBeat
        int64_t t = 0;
        double  beat = 0.0;
        double  lengthBeats = 0.0;
        int     midiNote = -1;
        float   velocity = 0.0f;
    };

    double  quantize(double beat) const noexcept;
    void    setPending(double beat, int absOffset, bool immediate, double lengthBeats, int midiNote, float velocity) noexcept;
    bool    pendingOffset(int from, int& offset) noexcept;   // false = dropped or not in this block
    void    buildRequest(SchedAction& out, uint32_t seed, int64_t phraseIndex, double lengthBeats,
                         int midiNote, float velocity) const noexcept;
    bool    autoPhrase(int from, SchedAction& out) noexcept;
    bool    midiScan(int from, bool playerActive, SchedAction& out) noexcept;

    double         sr_ = 48000.0;
    BlockTime      bt_{};
    EngineSettings s_{};
    MidiEventView  midi_{};
    int            midiCursor_ = 0;
    bool           pending_ = false;
    Pending        pend_{};
    int64_t        notBeforeT_ = 0;
    int64_t        lastPhrase_ = INT64_MIN;
    bool           prevTrigger_ = false;
    bool           rearm_ = true;
    bool           cutRequested_ = false;
    int            activeNote_ = -1;
    int            cutFadeSamples_ = 480;
};

} // namespace tg
