// TransitGen core — sample-accurate FillPlan playback: sources, modifiers, mix (02 §5-§7).
#pragma once

#include "transitgen/CaptureBuffer.h"
#include "transitgen/Constants.h"
#include "transitgen/FillPlan.h"

#include <cstdint>
#include <vector>

namespace tg {

class PlanPlayer {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels);   // allocates fade tables
    void reset() noexcept;

    /// Starts playing `plan` (must stay valid and unchanged while active) with fill-local
    /// sample 0 at engine time fillStartT. playOffset > 0 enters the fill mid-way.
    void start(const FillPlan& plan, double spb, int64_t fillStartT, int64_t playOffset,
               float mix, float outGainDb) noexcept;
    /// Ends the fill with a kCutFadeMs fade starting at the next rendered sample.
    void cut() noexcept { if (active_ && cutF_ < 0) cutPending_ = true; }

    bool      active() const noexcept { return active_; }
    int64_t   fillStartT() const noexcept { return fillStartT_; }
    int64_t   lengthSamples() const noexcept { return N_; }
    float     progress() const noexcept { return active_ && N_ > 0 ? static_cast<float>(static_cast<double>(f_) / static_cast<double>(N_)) : 0.0f; }
    EventType currentSource() const noexcept { return cur_.type; }

    /// Renders block offsets [from, to) in place; dry audio comes from `cap` (already written
    /// for this block). Returns the offset where rendering stopped: `to`, or earlier if the fill ended.
    int render(float* const* channels, int numChannels, int from, int to, int64_t blockT0,
               CaptureBuffer& cap) noexcept;

private:
    /// One stutter read head: a looped window [E, E+W) with a crossfaded seam.
    struct Head {
        int64_t Rk = 0, W = 0;      // repeat start, window length (0 = live)
        double  rate = 1.0;
        float   gain = 1.0f;
        int     seam = 1;           // seam crossfade length
    };

    struct SourceState {
        EventType type = EventType::Pass;
        int       index = -1;
        int64_t   E = 0, Eend = 0, Nev = 1;
        bool      frozen = false;   // outgoing copy: no new stutter repeats
        // Stutter
        int     k = 0;
        int64_t Lk = 0;
        Head    head, prev;
        bool    prevValid = false;
        int     repFade = 1;
        // Reverse
        int64_t R = 0;
        int     revFade = 1;
        // Tape
        double  rp = 0.0;
        bool    stop = false;
        double  curve = 0.0, endRate = 0.0;
    };

    struct Bounds { int64_t start, end; };

    void    initSource(SourceState& s, int eventIndex, int64_t f) noexcept;
    void    startRepeat(SourceState& s, int64_t Rk, int k) noexcept;
    void    renderSource(SourceState& s, int64_t f, float* out, CaptureBuffer& cap) noexcept;
    void    readHead(const SourceState& s, const Head& h, int64_t f, float* out, CaptureBuffer& cap) const noexcept;
    void    readHermite(double pos, float* out, CaptureBuffer& cap) const noexcept;
    float   laneValue(int lane, double beat) noexcept;
    void    filterTargets(int64_t f, float& g, float& k, float& mix) noexcept;
    int     msToSamples(double ms) const noexcept;

    // Fade shapes (02 §7.1): rc = raised cosine 0->1, epIn/epOut = equal-power pair of rc.
    float   rc(float x) const noexcept { return rc_[tableIndex(x)]; }
    float   epIn(float x) const noexcept { return epIn_[tableIndex(x)]; }
    float   epOut(float x) const noexcept { return epOut_[tableIndex(x)]; }
    size_t  tableIndex(float x) const noexcept
    {
        const int i = static_cast<int>(x * static_cast<float>(tableLen_));
        return static_cast<size_t>(i < 0 ? 0 : (i > tableLen_ ? tableLen_ : i));
    }

    double sr_ = 48000.0;
    int    numCh_ = 2;
    std::vector<float> rc_, epIn_, epOut_;
    int    tableLen_ = 0;
    int    sourceFade_ = 0, cutFade_ = 0, repeatFade_ = 0;
    int64_t lagLimit_ = 0;                  // max samples any read head may lag engine time

    const FillPlan* plan_ = nullptr;
    double  spb_ = 0.0;
    int64_t fillStartT_ = 0, playOffset_ = 0, N_ = 0, f_ = 0;
    int64_t cutF_ = -1;
    bool    cutPending_ = false;
    bool    active_ = false;
    float   mix_ = 1.0f, gain_ = 1.0f;
    int     entryFade_ = 1;

    // Event bounds in fill-local samples, grouped per lane.
    Bounds  bounds_[kMaxEvents];
    int     srcFirst_ = 0, srcCount_ = 0, gateFirst_ = 0, gateCount_ = 0, crushFirst_ = 0, crushCount_ = 0;
    int     srcIdx_ = 0, gateIdx_ = 0, crushIdx_ = 0;

    SourceState cur_, out_;
    bool    xfadeActive_ = false;
    int     xfadeLen_ = 0, xfadePos_ = 0;

    // Gate
    float   gateOpen_ = 1.0f;   // slewed openness 0..1, shaped by rc() into a gain
    float   gateRel_ = 1.0f;    // last release step, used after an event ends
    float   gateDepth_ = 0.0f;  // last depth, so a closed gate releases to 1 after its event
    // Filter
    bool    filterOn_ = false;
    float   ic1_[kMaxChannels]{}, ic2_[kMaxChannels]{};
    float   fg_ = 0.0f, fk_ = 2.0f, fmix_ = 1.0f;
    float   dg_ = 0.0f, dk_ = 0.0f, dmix_ = 0.0f;
    int     laneCursor_[static_cast<int>(AutoTarget::kCount)]{};
    // Crush
    double  crushPhase_ = 1.0;
    int     crushEv_ = -1;
    float   held_[kMaxChannels]{};
};

} // namespace tg
