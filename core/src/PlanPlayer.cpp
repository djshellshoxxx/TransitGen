#include "transitgen/PlanPlayer.h"
#include "transitgen/math.h"

#include <algorithm>
#include <cmath>

namespace tg {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int    kFadeTableLen = 4096;
} // namespace

int PlanPlayer::msToSamples(double ms) const noexcept
{
    const int n = static_cast<int>(std::lround(ms * sr_ / 1000.0));
    return n < 1 ? 1 : n;
}

void PlanPlayer::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    sr_ = sampleRate;
    numCh_ = std::min(numChannels, kMaxChannels);
    tableLen_ = kFadeTableLen;
    rc_.assign(static_cast<size_t>(tableLen_) + 1, 0.0f);
    epIn_.assign(static_cast<size_t>(tableLen_) + 1, 0.0f);
    epOut_.assign(static_cast<size_t>(tableLen_) + 1, 0.0f);
    for (int i = 0; i <= tableLen_; ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(tableLen_);
        const double s = 0.5 - 0.5 * std::cos(kPi * x);          // raised cosine, C1 at both ends
        rc_[static_cast<size_t>(i)] = static_cast<float>(s);
        epIn_[static_cast<size_t>(i)] = static_cast<float>(std::sin(0.5 * kPi * s));
        epOut_[static_cast<size_t>(i)] = static_cast<float>(std::cos(0.5 * kPi * s));
    }
    sourceFade_ = msToSamples(kSourceFadeMs);
    cutFade_ = msToSamples(kCutFadeMs);
    repeatFade_ = msToSamples(kRepeatFadeMs);
    // Any read at t - lagLimit_ is inside the capture's valid range for every block partition.
    lagLimit_ = CaptureBuffer::capacityFor(sampleRate, maxBlockSize) - maxBlockSize - 8;
    reset();
}

void PlanPlayer::reset() noexcept
{
    active_ = false;
    plan_ = nullptr;
    cutF_ = -1;
    cutPending_ = false;
    xfadeActive_ = false;
    gateOpen_ = 1.0f;
    gateRel_ = 1.0f;
    gateDepth_ = 0.0f;
    for (int c = 0; c < kMaxChannels; ++c) { ic1_[c] = ic2_[c] = 0.0f; held_[c] = 0.0f; }
    crushPhase_ = 1.0;
    crushEv_ = -1;
}

void PlanPlayer::start(const FillPlan& plan, double spb, int64_t fillStartT, int64_t playOffset,
                       float mix, float outGainDb) noexcept
{
    reset();
    plan_ = &plan;
    spb_ = spb;
    fillStartT_ = fillStartT;
    playOffset_ = playOffset;
    f_ = playOffset;
    N_ = std::max<int64_t>(1, beatsToSamples(plan.lengthBeats, spb, kBeatEpsSamples));
    mix_ = clampf(mix, 0.0f, 1.0f);
    gain_ = dbToGain(outGainDb);
    entryFade_ = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(sourceFade_, N_ / 2)));

    srcFirst_ = srcCount_ = gateFirst_ = gateCount_ = crushFirst_ = crushCount_ = 0;
    for (int i = 0; i < plan.numEvents; ++i) {
        const FillEvent& e = plan.events[i];
        const int64_t s = beatsToSamples(e.startBeat, spb, kBeatEpsSamples);
        const int64_t en = std::max(s + 1, beatsToSamples(e.startBeat + e.lengthBeats, spb, kBeatEpsSamples));
        bounds_[i] = {s, en};
        switch (e.lane) {
            case Lane::Source: if (srcCount_++ == 0) srcFirst_ = i; break;
            case Lane::Gate:   if (gateCount_++ == 0) gateFirst_ = i; break;
            case Lane::Crush:  if (crushCount_++ == 0) crushFirst_ = i; break;
        }
    }
    // Source event containing the entry sample.
    srcIdx_ = srcFirst_;
    while (srcIdx_ + 1 < srcFirst_ + srcCount_ && playOffset >= bounds_[srcIdx_ + 1].start) ++srcIdx_;
    gateIdx_ = gateFirst_;
    crushIdx_ = crushFirst_;
    cur_ = SourceState{};
    initSource(cur_, srcIdx_, playOffset);

    filterOn_ = plan.filterType != FilterType::Off
             && plan.lanes[static_cast<int>(AutoTarget::FilterCutoff)].numPoints > 0;
    for (int& c : laneCursor_) c = 0;
    if (filterOn_) {
        float g, k, m;
        filterTargets(playOffset, g, k, m);
        fg_ = g; fk_ = k; fmix_ = m;
        dg_ = dk_ = dmix_ = 0.0f;
    }
    active_ = true;
}

// ---------------------------------------------------------------- sources

void PlanPlayer::startRepeat(SourceState& s, int64_t Rk, int k) noexcept
{
    const FillEvent& e = plan_->events[s.index];
    s.prev = s.head;
    s.prevValid = true;
    s.k = k;
    const double u = static_cast<double>(Rk - s.E) / static_cast<double>(s.Nev);
    const double l0 = e.p[0];
    const double l1 = e.p[1] > 0.0f ? e.p[1] : l0;
    double len = l0 * std::pow(l1 / l0, u);
    if (e.p[2] < 0.5f) len = l0 * std::exp2(std::round(std::log2(len / l0)));   // stepped: octave snaps
    s.Lk = std::min<int64_t>(lagLimit_, std::max<int64_t>(kMinRepeatSamples, beatsToSamples(len, spb_, kBeatEpsSamples)));
    Head& h = s.head;
    h.Rk = Rk;
    h.W = std::min<int64_t>(s.Lk, std::min<int64_t>(lagLimit_, Rk - s.E));   // fully recorded before Rk
    const double semis = e.p[3] + (e.p[4] - e.p[3]) * u;
    h.rate = std::exp2(semis / 12.0);
    h.gain = dbToGain(e.p[5] * static_cast<float>(k));
    h.seam = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(repeatFade_, h.W / 4)));
    s.repFade = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(repeatFade_, s.Lk / 4)));
}

void PlanPlayer::initSource(SourceState& s, int eventIndex, int64_t f) noexcept
{
    const FillEvent& e = plan_->events[eventIndex];
    s = SourceState{};
    s.type = e.type;
    s.index = eventIndex;
    s.E = bounds_[eventIndex].start;
    s.Eend = bounds_[eventIndex].end;
    s.Nev = std::max<int64_t>(1, s.Eend - s.E);
    switch (e.type) {
        case EventType::Stutter: {
            s.k = 0;
            s.head = Head{};
            s.head.Rk = s.E;              // repeat 0 is live (W = 0)
            s.Lk = std::min<int64_t>(lagLimit_, std::max<int64_t>(kMinRepeatSamples, beatsToSamples(e.p[0], spb_, kBeatEpsSamples)));
            s.repFade = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(repeatFade_, s.Lk / 4)));
            // Mid-event entry: walk forward to the repeat containing f (bounded: Lk >= 32).
            while (f >= s.head.Rk + s.Lk && s.head.Rk + s.Lk < s.Eend) startRepeat(s, s.head.Rk + s.Lk, s.k + 1);
            s.prevValid = false;
            break;
        }
        case EventType::Reverse: {
            const int64_t absE = fillStartT_ + s.E;
            s.R = std::max<int64_t>(0, std::min<int64_t>({beatsToSamples(e.p[0], spb_, kBeatEpsSamples), lagLimit_, absE}));
            s.revFade = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(sourceFade_, s.R / 2)));
            break;
        }
        case EventType::TapeStop:
        case EventType::TapeStart:
            s.stop = e.type == EventType::TapeStop;
            s.curve = e.p[0];
            s.endRate = e.p[1];
            s.rp = static_cast<double>(f - kTapeInitialLag);
            break;
        default: break;
    }
}

void PlanPlayer::readHermite(double pos, float* out, CaptureBuffer& cap) const noexcept
{
    const double i = std::floor(pos);
    const float phi = static_cast<float>(pos - i);
    const int64_t p = fillStartT_ + static_cast<int64_t>(i);
    for (int c = 0; c < numCh_; ++c)
        out[c] = hermite(cap.read(c, p - 1), cap.read(c, p), cap.read(c, p + 1), cap.read(c, p + 2), phi);
}

/// Looped window read with an equal-power seam: the last `seam` samples of the window are
/// crossfaded into the audio just before the window, which then runs on into the window start.
void PlanPlayer::readHead(const SourceState& s, const Head& h, int64_t f, float* out, CaptureBuffer& cap) const noexcept
{
    const int64_t t = fillStartT_ + f;
    if (h.W == 0) {                                   // live first slice
        for (int c = 0; c < numCh_; ++c) out[c] = cap.at(c, t);
        return;
    }
    const double W = static_cast<double>(h.W);
    const double q = std::fmod(static_cast<double>(f - h.Rk) * h.rate, W);
    const double pos = static_cast<double>(s.E) + q;
    readHermite(pos, out, cap);
    const double seamStart = W - static_cast<double>(h.seam);
    if (q >= seamStart) {
        float pre[kMaxChannels];
        readHermite(pos - W, pre, cap);
        const float x = static_cast<float>((q - seamStart) / static_cast<double>(h.seam));
        const float a = epIn(x), b = epOut(x);
        for (int c = 0; c < numCh_; ++c) out[c] = b * out[c] + a * pre[c];
    }
}

void PlanPlayer::renderSource(SourceState& s, int64_t f, float* out, CaptureBuffer& cap) noexcept
{
    const int64_t t = fillStartT_ + f;
    switch (s.type) {
        case EventType::Pass:
            for (int c = 0; c < numCh_; ++c) out[c] = cap.at(c, t);
            break;
        case EventType::Silence:
            for (int c = 0; c < numCh_; ++c) out[c] = 0.0f;
            break;
        case EventType::Stutter: {
            if (!s.frozen && f >= s.head.Rk + s.Lk && s.head.Rk + s.Lk < s.Eend) startRepeat(s, s.head.Rk + s.Lk, s.k + 1);
            readHead(s, s.head, f, out, cap);
            for (int c = 0; c < numCh_; ++c) out[c] *= s.head.gain;
            if (s.prevValid) {
                const int64_t j = f - s.head.Rk;
                if (j < s.repFade) {
                    // Sum-preserving crossfade: old and new are often phase-aligned copies.
                    float o[kMaxChannels];
                    readHead(s, s.prev, f, o, cap);
                    const float a = rc(static_cast<float>(j) / static_cast<float>(s.repFade));
                    for (int c = 0; c < numCh_; ++c) out[c] = (1.0f - a) * (o[c] * s.prev.gain) + a * out[c];
                } else {
                    s.prevValid = false;
                }
            }
            break;
        }
        case EventType::Reverse: {
            const FillEvent& e = plan_->events[s.index];
            const int64_t j = f - s.E;
            if (j >= s.R) { for (int c = 0; c < numCh_; ++c) out[c] = 0.0f; break; }
            const float u = static_cast<float>(static_cast<double>(j) / static_cast<double>(s.Nev));
            float g = dbToGain(e.p[1] + (e.p[2] - e.p[1]) * u);
            if (j >= s.R - s.revFade) g *= rc(static_cast<float>(s.R - j) / static_cast<float>(s.revFade));
            const int64_t pos = fillStartT_ + s.E - 1 - j;
            for (int c = 0; c < numCh_; ++c) out[c] = cap.read(c, pos) * g;
            break;
        }
        case EventType::TapeStop:
        case EventType::TapeStart: {
            const double u = clampd(static_cast<double>(f - s.E) / static_cast<double>(s.Nev), 0.0, 1.0);
            const double sh = bend(u, s.curve);
            const double v = s.stop ? 1.0 - (1.0 - s.endRate) * sh : s.endRate + (1.0 - s.endRate) * sh;
            const float g = rc(static_cast<float>(std::min(1.0, v / kTapeMuteRate)));
            readHermite(s.rp, out, cap);
            for (int c = 0; c < numCh_; ++c) out[c] *= g;
            s.rp += v;
            const double minRp = static_cast<double>(f - lagLimit_);
            if (s.rp < minRp) s.rp = minRp;
            break;
        }
        default:
            for (int c = 0; c < numCh_; ++c) out[c] = 0.0f;
            break;
    }
}

// ---------------------------------------------------------------- modifiers

float PlanPlayer::laneValue(int lane, double beat) noexcept
{
    const AutomationLane& l = plan_->lanes[lane];
    int& cur = laneCursor_[lane];
    while (cur + 2 < l.numPoints && beat >= l.points[cur + 1].beat) ++cur;
    const LanePoint& a = l.points[cur];
    const LanePoint& b = l.points[cur + 1];
    const double span = b.beat - a.beat;
    const double x = span > 0.0 ? clampd((beat - a.beat) / span, 0.0, 1.0) : 1.0;
    return static_cast<float>(bendLerp(a.value, b.value, x, a.curve));
}

void PlanPlayer::filterTargets(int64_t f, float& g, float& k, float& mix) noexcept
{
    const double beat = static_cast<double>(f) / spb_;
    const double vc = laneValue(static_cast<int>(AutoTarget::FilterCutoff), beat);
    const double fc = std::min(20.0 * std::pow(1000.0, vc), 0.45 * sr_);
    g = static_cast<float>(std::tan(kPi * fc / sr_));
    const AutomationLane& resLane = plan_->lanes[static_cast<int>(AutoTarget::FilterResonance)];
    const double q = resLane.numPoints > 0 ? 0.5 + 11.5 * laneValue(static_cast<int>(AutoTarget::FilterResonance), beat) : 0.5;
    k = static_cast<float>(1.0 / q);
    const AutomationLane& mixLane = plan_->lanes[static_cast<int>(AutoTarget::FilterMix)];
    mix = mixLane.numPoints > 0 ? laneValue(static_cast<int>(AutoTarget::FilterMix), beat) : 1.0f;
}

// ---------------------------------------------------------------- render

int PlanPlayer::render(float* const* channels, int numChannels, int from, int to, int64_t blockT0,
                       CaptureBuffer& cap) noexcept
{
    const int nCh = std::min(numChannels, numCh_);
    const int srcEnd = srcFirst_ + srcCount_, gateEnd = gateFirst_ + gateCount_, crushEnd = crushFirst_ + crushCount_;
    const float invEntry = 1.0f / static_cast<float>(entryFade_);
    const float invCut = 1.0f / static_cast<float>(cutFade_);
    const float invSrcFade = 1.0f / static_cast<float>(sourceFade_);

    for (int sIdx = from; sIdx < to; ++sIdx) {
        const int64_t t = blockT0 + sIdx;
        const int64_t f = t - fillStartT_;
        if (cutPending_) { cutF_ = f; cutPending_ = false; }
        if (f >= N_ || (cutF_ >= 0 && f - cutF_ >= cutFade_)) { active_ = false; return sIdx; }
        f_ = f;

        // Fill envelope: raised-cosine entry, pre-rolled exit, cut (02 §7.2).
        float env = std::min(1.0f, static_cast<float>(f - playOffset_ + 1) * invEntry);
        env = std::min(env, static_cast<float>(N_ - f) * invEntry);
        if (cutF_ >= 0) env = std::min(env, 1.0f - static_cast<float>(f - cutF_) * invCut);
        const float m = mix_ * rc(env);

        // Source boundaries (sum-preserving crossfade from the outgoing source).
        while (srcIdx_ + 1 < srcEnd && f >= bounds_[srcIdx_ + 1].start) {
            out_ = cur_;
            out_.frozen = true;
            ++srcIdx_;
            initSource(cur_, srcIdx_, f);
            int len = sourceFade_;
            if (cur_.type == EventType::Silence)
                len = std::max(kMinFadeSamples, msToSamples(plan_->events[srcIdx_].p[0]));
            xfadeLen_ = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(len, cur_.Nev / 2)));
            xfadePos_ = 0;
            xfadeActive_ = true;
        }
        float w[kMaxChannels];
        renderSource(cur_, f, w, cap);
        if (xfadeActive_) {
            float o[kMaxChannels];
            renderSource(out_, f, o, cap);
            const float a = rc(static_cast<float>(xfadePos_) / static_cast<float>(xfadeLen_));
            for (int c = 0; c < nCh; ++c) w[c] = (1.0f - a) * o[c] + a * w[c];
            if (++xfadePos_ >= xfadeLen_) xfadeActive_ = false;
        }

        // Gate: slewed openness, shaped by a raised cosine so ramps have no corners.
        while (gateIdx_ < gateEnd && f >= bounds_[gateIdx_].end) ++gateIdx_;
        float target = 1.0f, up = 1.0f, down = gateRel_, depth = 0.0f;
        if (gateIdx_ < gateEnd && f >= bounds_[gateIdx_].start) {
            const FillEvent& e = plan_->events[gateIdx_];
            const double stepLen = spb_ / static_cast<double>(e.p[0]);
            const double rel = static_cast<double>(f - bounds_[gateIdx_].start);
            const int64_t step = static_cast<int64_t>(std::floor(rel / stepLen));
            const double phase = (rel - static_cast<double>(step) * stepLen) / stepLen;
            uint32_t pattern = static_cast<uint32_t>(static_cast<int>(e.p[2])) & 0xFFFFu;
            if (pattern == 0) pattern = 0xFFFFu;
            const bool open = ((pattern >> (step & 15)) & 1u) != 0 && phase < static_cast<double>(e.p[1]);
            target = open ? 1.0f : 0.0f;
            depth = e.p[5];
            up = e.p[3] <= 0.0f ? 1.0f : 1.0f / static_cast<float>(msToSamples(e.p[3]));
            down = e.p[4] <= 0.0f ? 1.0f : 1.0f / static_cast<float>(msToSamples(e.p[4]));
            gateRel_ = down;
            gateDepth_ = depth;
        } else {
            depth = gateDepth_;
        }
        if (gateOpen_ < target) gateOpen_ = std::min(target, gateOpen_ + up);
        else if (gateOpen_ > target) gateOpen_ = std::max(target, gateOpen_ - down);
        const float gateGain = 1.0f - depth * (1.0f - rc(gateOpen_));
        for (int c = 0; c < nCh; ++c) w[c] *= gateGain;

        // Filter (TPT SVF, coefficients ramped between 16-sample anchors).
        if (filterOn_) {
            if ((f % kFilterUpdateInterval) == 0) {
                float g, k, mx;
                filterTargets(f, g, k, mx);
                const float inv = 1.0f / static_cast<float>(kFilterUpdateInterval);
                dg_ = (g - fg_) * inv; dk_ = (k - fk_) * inv; dmix_ = (mx - fmix_) * inv;
            }
            fg_ += dg_; fk_ += dk_; fmix_ += dmix_;
            const float g = fg_, k = fk_;
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            for (int c = 0; c < nCh; ++c) {
                const float x = w[c];
                const float v3 = x - ic2_[c];
                const float v1 = a1 * ic1_[c] + a2 * v3;
                const float v2 = ic2_[c] + a2 * ic1_[c] + a3 * v3;
                ic1_[c] = 2.0f * v1 - ic1_[c];
                ic2_[c] = 2.0f * v2 - ic2_[c];
                float y;
                switch (plan_->filterType) {
                    case FilterType::HighPass: y = x - k * v1 - v2; break;
                    case FilterType::BandPass: y = v1; break;
                    default:                   y = v2; break;
                }
                w[c] = x + fmix_ * (y - x);
            }
        }

        // Crush (continuous bits + sample-and-hold, edge-ramped mix).
        while (crushIdx_ < crushEnd && f >= bounds_[crushIdx_].end) ++crushIdx_;
        if (crushIdx_ < crushEnd && f >= bounds_[crushIdx_].start) {
            const FillEvent& e = plan_->events[crushIdx_];
            const Bounds& b = bounds_[crushIdx_];
            if (crushEv_ != crushIdx_) { crushEv_ = crushIdx_; crushPhase_ = 1.0; }
            const double u = static_cast<double>(f - b.start) / static_cast<double>(b.end - b.start);
            const float bits = static_cast<float>(e.p[0] + (e.p[1] - e.p[0]) * u);
            const double D = e.p[2] + (e.p[3] - e.p[2]) * u;
            const float q = std::exp2(bits - 1.0f);
            if (crushPhase_ >= 1.0) {
                crushPhase_ -= 1.0;
                for (int c = 0; c < nCh; ++c) held_[c] = std::round(w[c] * q) / q;
            }
            crushPhase_ += 1.0 / D;
            const float edge = rc(std::min(1.0f, static_cast<float>(std::min<int64_t>(f - b.start + 1, b.end - f)) * invSrcFade));
            const float cm = e.p[4] * edge;
            for (int c = 0; c < nCh; ++c) w[c] = w[c] + cm * (held_[c] - w[c]);
        }

        // Mix (02 §7.2): bit-exact wet at m = 1, bit-exact dry at m = 0 and whenever w == x.
        if (m >= 1.0f) {
            for (int c = 0; c < nCh; ++c) channels[c][sIdx] = gain_ * w[c];
        } else {
            for (int c = 0; c < nCh; ++c) {
                const float x = cap.at(c, t);
                channels[c][sIdx] = x + m * (gain_ * w[c] - x);
            }
        }
    }
    return to;
}

} // namespace tg
