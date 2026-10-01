#include "transitgen/FillScheduler.h"
#include "transitgen/Constants.h"
#include "transitgen/math.h"

#include <algorithm>
#include <cmath>

namespace tg {

namespace {
// Pitch class -> beats (negative = use fill_len). 02 §2.3.
double midiLengthBeats(int note, double barLen, double fillLenBeats) noexcept
{
    switch (((note % 12) + 12) % 12) {
        case 0:  return 0.25;
        case 1:  return 0.5;
        case 2:  return 1.0;
        case 3:  return 2.0;
        case 4:  return 3.0;
        case 5:  return 4.0;
        case 6:  return 2.0 * barLen;
        case 7:  return 4.0 * barLen;
        case 8:  return 8.0 * barLen;
        default: return fillLenBeats;
    }
}
} // namespace

void FillScheduler::prepare(double sampleRate) noexcept
{
    sr_ = sampleRate;
    cutFadeSamples_ = std::max(1, static_cast<int>(std::lround(kCutFadeMs * sr_ / 1000.0)));
    reset();
}

void FillScheduler::reset() noexcept
{
    midiCursor_ = 0;
    pending_ = false;
    notBeforeT_ = 0;
    lastPhrase_ = INT64_MIN;
    prevTrigger_ = false;
    rearm_ = true;
    cutRequested_ = false;
    activeNote_ = -1;
}

double FillScheduler::fillBeats(int midiNote) const noexcept
{
    const double base = fillLengthBeats(s_.fillLen, bt_.barLen);
    if (s_.mode == TriggerMode::AutoPhrase) return std::min(base, s_.phraseBars * bt_.barLen);
    if (s_.mode == TriggerMode::Midi && midiNote >= 0) return midiLengthBeats(midiNote, bt_.barLen, base);
    return base;
}

double FillScheduler::quantize(double beat) const noexcept
{
    const double step = quantizeStepBeats(s_.quantize, bt_.barLen);
    if (step <= 0.0) return beat;
    return bt_.gridOrigin + step * std::ceil((beat - bt_.gridOrigin) / step - 1e-9);
}

void FillScheduler::beginBlock(const BlockTime& bt, const EngineSettings& s, const MidiEventView& midi, bool playerActive) noexcept
{
    bt_ = bt;
    s_ = s;
    midi_ = midi;
    midiCursor_ = 0;

    if (bt.jumped) {
        lastPhrase_ = INT64_MIN;      // a loop replays the phrase's fill
        rearm_ = true;                // a high trigger after a jump counts as an edge
        if (pending_ && !pend_.bySample) pending_ = false;
    }

    if (s.mode == TriggerMode::Automation) {
        const bool rising = s.trigger && (!prevTrigger_ || rearm_);
        const bool falling = !s.trigger && prevTrigger_;
        if (rising && !playerActive && !pending_) {
            const double q = quantize(bt.startBeat);
            setPending(q, 0, s.quantize == Quantize::Off, fillBeats(), -1, 0.0f);
        }
        if (falling && playerActive) cutRequested_ = true;
        if (falling) pending_ = false;
        prevTrigger_ = s.trigger;
        rearm_ = false;
    } else {
        prevTrigger_ = false;
    }
}

void FillScheduler::setPending(double beat, int absOffset, bool immediate, double lengthBeats, int midiNote, float velocity) noexcept
{
    pending_ = true;
    pend_.bySample = immediate;
    pend_.t = bt_.t0 + absOffset;
    pend_.beat = beat;
    pend_.lengthBeats = lengthBeats;
    pend_.midiNote = midiNote;
    pend_.velocity = velocity;
}

bool FillScheduler::pendingOffset(int from, int& offset) noexcept
{
    int64_t off;
    if (pend_.bySample) {
        off = pend_.t - bt_.t0;
    } else {
        off = bt_.offsetOf(pend_.beat);
        // Missed by more than the fill length (jump): drop it.
        if (bt_.startBeat - pend_.beat > pend_.lengthBeats) { pending_ = false; return false; }
    }
    off = std::max(off, notBeforeT_ - bt_.t0);
    if (off < from) off = from;
    if (off >= bt_.numSamples) return false;
    offset = static_cast<int>(off);
    return true;
}

void FillScheduler::buildRequest(SchedAction& out, uint32_t seed, int64_t phraseIndex, double lengthBeats,
                                 int midiNote, float velocity) const noexcept
{
    FillRequest& r = out.req;
    r.gen.seed = seed;
    r.gen.styleId = s_.styleId;
    r.gen.lengthBeats = lengthBeats;
    r.gen.intensity = (s_.mode == TriggerMode::Midi && midiNote >= 0 && s_.midiVelocityToIntensity) ? velocity : s_.intensity;
    r.gen.density = s_.density;
    r.gen.pitchAmount = s_.pitchAmount;
    r.gen.crushAmount = s_.crushAmount;
    r.gen.filterAmount = s_.filterAmount;
    r.gen.endingOverride = s_.endingOverride;
    r.gen.endingBeats = s_.endingBeats;
    r.gen.curve = s_.curve;
    r.gen.styles = s_.styles;
    r.bpm = bt_.bpm;
    r.sampleRate = sr_;
    r.phraseIndex = phraseIndex;
    r.tsNum = static_cast<int>(std::lround(bt_.barLen));   // informational; bar length is what matters
    r.tsDen = 4;
    r.mode = s_.mode;
    r.midiNote = midiNote;
    r.velocity = velocity;
}

bool FillScheduler::autoPhrase(int from, SchedAction& out) noexcept
{
    const double P = s_.phraseBars * bt_.barLen;
    const double o = bt_.gridOrigin + s_.phraseOffset * bt_.barLen;
    const double Lf = fillBeats();
    const double b = bt_.beatAt(from);
    const int64_t i = static_cast<int64_t>(std::floor((b - o) / P));
    const double regionStart = o + static_cast<double>(i + 1) * P - Lf;
    const double regionEnd = o + static_cast<double>(i + 1) * P;

    int64_t phrase;
    double start;
    int64_t playOffset = 0;
    int offset;
    if (b >= regionStart - 1e-9) {
        // Inside phrase i's fill region (mid-fill entry) unless it already played.
        if (lastPhrase_ == i) {
            phrase = i + 1;
            start = regionEnd + P - Lf;
            const int64_t off = std::max<int64_t>(bt_.offsetOf(start), notBeforeT_ - bt_.t0);
            if (off >= bt_.numSamples) return false;
            offset = static_cast<int>(std::max<int64_t>(off, from));
        } else {
            phrase = i;
            start = regionStart;
            const double remainingBeats = regionEnd - b;
            if (remainingBeats * bt_.spb < kMinEntryMs * sr_ / 1000.0) {
                lastPhrase_ = i;                 // too little left: skip, look at the next phrase
                return autoPhrase(from, out);
            }
            const int64_t off = std::max<int64_t>(from, notBeforeT_ - bt_.t0);
            if (off >= bt_.numSamples) return false;
            offset = static_cast<int>(off);
            playOffset = std::max<int64_t>(0, std::llround((bt_.beatAt(offset) - regionStart) * bt_.spb));
        }
    } else {
        phrase = i;
        start = regionStart;
        const int64_t off = std::max<int64_t>(bt_.offsetOf(start), notBeforeT_ - bt_.t0);
        if (off >= bt_.numSamples) return false;
        offset = static_cast<int>(std::max<int64_t>(off, from));
    }
    const uint32_t seed = s_.variation == Variation::PerPhrase ? mixSeed(s_.seed, phrase) : s_.seed;
    out.kind = SchedAction::Kind::Start;
    out.offset = offset;
    out.playOffset = playOffset;
    buildRequest(out, seed, phrase, Lf, -1, 0.0f);
    lastPhrase_ = phrase;
    return true;
}

bool FillScheduler::midiScan(int from, bool playerActive, SchedAction& out) noexcept
{
    for (;;) {
        int pendOff = 0;
        const bool havePend = !playerActive && pending_ && pendingOffset(from, pendOff);
        const MidiEvent* ev = midiCursor_ < midi_.count ? &midi_.events[midiCursor_] : nullptr;
        const int evOff = ev ? std::max(from, ev->sampleOffset) : bt_.numSamples;

        if (havePend && (!ev || pendOff <= evOff)) {
            pending_ = false;
            out.kind = SchedAction::Kind::Start;
            out.offset = pendOff;
            out.playOffset = 0;
            buildRequest(out, s_.seed, 0, pend_.lengthBeats, pend_.midiNote, pend_.velocity);
            activeNote_ = pend_.midiNote;
            return true;
        }
        if (!ev) return false;
        ++midiCursor_;
        if (ev->isNoteOn()) {
            const double beat = bt_.beatAt(evOff);
            const bool immediate = s_.quantize == Quantize::Off;
            setPending(quantize(beat), evOff, immediate, fillBeats(ev->data1), ev->data1, ev->data2 / 127.0f);
            if (playerActive) {
                // Retrigger: cut now; the pending start waits for the cut fade.
                notBeforeT_ = bt_.t0 + evOff + cutFadeSamples_;
                out.kind = SchedAction::Kind::Cut;
                out.offset = evOff;
                return true;
            }
        } else if (ev->isNoteOff() && s_.midiHold && playerActive && ev->data1 == activeNote_) {
            notBeforeT_ = bt_.t0 + evOff + cutFadeSamples_;
            out.kind = SchedAction::Kind::Cut;
            out.offset = evOff;
            return true;
        }
    }
}

void FillScheduler::nextAction(int from, bool playerActive, SchedAction& out) noexcept
{
    out.kind = SchedAction::Kind::None;
    out.offset = bt_.numSamples;
    if (from >= bt_.numSamples) return;

    if (playerActive && cutRequested_) {
        cutRequested_ = false;
        notBeforeT_ = bt_.t0 + from + cutFadeSamples_;
        out.kind = SchedAction::Kind::Cut;
        out.offset = from;
        return;
    }

    switch (s_.mode) {
        case TriggerMode::AutoPhrase:
            if (!playerActive) autoPhrase(from, out);
            break;
        case TriggerMode::Automation: {
            int off;
            if (!playerActive && pending_ && pendingOffset(from, off)) {
                pending_ = false;
                out.kind = SchedAction::Kind::Start;
                out.offset = off;
                out.playOffset = 0;
                buildRequest(out, s_.seed, 0, pend_.lengthBeats, -1, 0.0f);
            }
            break;
        }
        case TriggerMode::Midi:
            midiScan(from, playerActive, out);
            break;
    }
}

} // namespace tg
