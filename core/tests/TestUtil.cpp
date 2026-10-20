#include "TestUtil.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>

// ---------------------------------------------------------------- allocation counter
namespace {
std::atomic<int64_t> g_allocs{0};
}

void* operator new(std::size_t n)
{
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace tgt {

int64_t allocationCount() { return g_allocs.load(std::memory_order_relaxed); }

// ---------------------------------------------------------------- signals
Signal makeDC(int64_t n, float v)
{
    Signal s(n);
    for (int c = 0; c < 2; ++c) std::fill(s.ch[c].begin(), s.ch[c].end(), v);
    return s;
}

Signal makeRamp(int64_t n, float slope)
{
    Signal s(n);
    for (int64_t i = 0; i < n; ++i) { const float v = static_cast<float>(i) * slope; s(0, i) = v; s(1, i) = v; }
    return s;
}

Signal makeSine(int64_t n, double hz, float amp, double sr)
{
    Signal s(n);
    for (int64_t i = 0; i < n; ++i) {
        const float v = amp * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / sr));
        s(0, i) = v; s(1, i) = v;
    }
    return s;
}

Signal makeNoise(int64_t n, uint32_t seed, float amp)
{
    Signal s(n);
    uint32_t x = seed ? seed : 1u;
    for (int c = 0; c < 2; ++c)
        for (int64_t i = 0; i < n; ++i) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            s(c, i) = amp * (static_cast<float>(x) / 2147483648.0f - 1.0f);
        }
    return s;
}

bool identical(const Signal& a, const Signal& b)
{
    return a.frames == b.frames && a.ch[0] == b.ch[0] && a.ch[1] == b.ch[1];
}

// ---------------------------------------------------------------- host / midi
tg::TransportInfo HostSim::info() const
{
    tg::TransportInfo i;
    i.playing = playing; i.hasPpq = hasPpq; i.ppq = ppq(); i.bpm = bpm;
    i.tsNum = tsNum; i.tsDen = tsDen; i.hasBarStart = hasBarStart; i.barStartPpq = barStartPpq; i.looping = looping;
    return i;
}

tg::MidiEvent noteOn(uint8_t note, uint8_t vel) { tg::MidiEvent e; e.status = 0x90; e.data1 = note; e.data2 = vel; return e; }
tg::MidiEvent noteOff(uint8_t note) { tg::MidiEvent e; e.status = 0x80; e.data1 = note; e.data2 = 0; return e; }

BlockSizeFn fixedBlocks(int n) { return [n] { return n; }; }

BlockSizeFn randomBlocks(uint32_t seed, int maxBlock)
{
    return [x = seed ? seed : 7u, maxBlock]() mutable {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return 1 + static_cast<int>(x % static_cast<uint32_t>(maxBlock));
    };
}

// ---------------------------------------------------------------- render driver
RenderResult render(tg::Engine& engine, const Signal& in, HostSim& host, const BlockSizeFn& blocks,
                    const std::vector<MidiAt>& midi, const OnBlockFn& onBlock, int maxBlock)
{
    RenderResult r;
    r.out = Signal(in.frames);
    std::vector<float> scratch[2];
    for (auto& s : scratch) s.resize(static_cast<size_t>(maxBlock));
    std::vector<tg::MidiEvent> blockMidi;
    blockMidi.reserve(64);
    float* chans[2] = {scratch[0].data(), scratch[1].data()};
    uint32_t fills = engine.telemetry().fillCount.load();

    size_t midiIdx = 0;
    while (host.pos < in.frames) {
        const int n = static_cast<int>(std::min<int64_t>({static_cast<int64_t>(std::max(1, std::min(blocks(), maxBlock))), in.frames - host.pos}));
        if (onBlock) onBlock(host.pos, host);
        blockMidi.clear();
        while (midiIdx < midi.size() && midi[midiIdx].t < host.pos + n) {
            if (midi[midiIdx].t >= host.pos) {
                tg::MidiEvent e = midi[midiIdx].ev;
                e.sampleOffset = static_cast<int>(midi[midiIdx].t - host.pos);
                blockMidi.push_back(e);
            }
            ++midiIdx;
        }
        for (int c = 0; c < 2; ++c) std::copy_n(in.ch[c].data() + host.pos, n, scratch[c].data());
        const tg::TransportInfo info = host.info();
        engine.process(chans, 2, n, info, tg::MidiEventView{blockMidi.data(), static_cast<int>(blockMidi.size())});
        for (int c = 0; c < 2; ++c) std::copy_n(scratch[c].data(), n, r.out.ch[c].data() + host.pos);
        const uint32_t nowFills = engine.telemetry().fillCount.load();
        if (nowFills != fills) {
            fills = nowFills;
            r.fillStarts.push_back(engine.telemetry().lastFillStartT.load());
            r.fillPlayOffsets.push_back(engine.telemetry().lastFillPlayOffset.load());
            r.fillSeeds.push_back(engine.telemetry().fillSeed.load());
        }
        host.pos += n;
    }
    r.rejected = engine.telemetry().rejectedPlans.load();
    return r;
}

std::unique_ptr<tg::Engine> makeEngine(tg::IPlanProvider& provider, const tg::EngineSettings& s, int maxBlock, int channels, double sr)
{
    auto e = std::make_unique<tg::Engine>();
    e->prepare(sr, maxBlock, channels);
    e->setPlanProvider(&provider);
    e->setSettings(s);
    return e;
}

tg::EngineSettings settingsFor(tg::TriggerMode mode)
{
    tg::EngineSettings s;
    s.mode = mode;
    s.phraseBars = 4;
    s.fillLen = tg::FillLength::Beat4;
    s.quantize = tg::Quantize::N4;
    s.variation = tg::Variation::Fixed;
    s.seed = 42;
    s.mix = 1.0f;
    s.outGainDb = 0.0f;
    return s;
}

FillRun runFill(const tg::FillPlan& plan, const Signal& in, int64_t startT, tg::FillLength len,
                int block, float mix, float gainDb)
{
    tg::TestPlanProvider prov;
    prov.templatePlan = plan;
    tg::EngineSettings s = settingsFor(tg::TriggerMode::Automation);
    s.quantize = tg::Quantize::Off;
    s.fillLen = len;
    s.mix = mix;
    s.outGainDb = gainDb;
    auto e = makeEngine(prov, s, std::max(block, 512));
    HostSim host;
    FillRun run;
    run.r = render(*e, in, host, fixedBlocks(block), {}, [&](int64_t pos, HostSim&) {
        s.trigger = pos >= startT;
        e->setSettings(s);
    }, std::max(block, 512));
    run.start = startT;
    run.N = static_cast<int64_t>(tg::fillLengthBeats(len, 4.0) * 24000.0);
    return run;
}

tg::FillPlan fullPlan()
{
    using tg::EventType;
    return tg::PlanBuilder(4.0)
        .source(EventType::Pass, 0.0, 0.5)
        .source(EventType::Stutter, 0.5, 1.0, {0.25f, 0.125f, 0.0f, 0.0f, 0.0f, -3.0f})
        .source(EventType::Reverse, 1.5, 0.5, {0.5f, 0.0f, -3.0f})
        .source(EventType::TapeStop, 2.0, 0.5, {0.3f, 0.0f})
        .source(EventType::TapeStart, 2.5, 0.5, {-0.3f, 0.0f})
        .source(EventType::Silence, 3.0, 0.25, {5.0f})
        .source(EventType::Pass, 3.25, 0.75)
        .gate(0.5, 1.0, {4.0f, 0.5f, 11.0f, 5.0f, 5.0f, 1.0f})
        .crush(2.0, 1.0, {16.0f, 4.0f, 1.0f, 8.0f, 1.0f})
        .filter(tg::FilterType::LowPass)
        .lane(tg::AutoTarget::FilterCutoff, {{0.0, 1.0f, 0.0f}, {2.0, 0.3f, 0.5f}, {4.0, 1.0f, -0.5f}})
        .lane(tg::AutoTarget::FilterResonance, {{0.0, 0.1f, 0.0f}, {4.0, 0.4f, 0.0f}})
        .plan();
}

// ---------------------------------------------------------------- metrics
float maxSecondDiff(const std::vector<float>& x, int64_t from, int64_t to)
{
    const int64_t n = static_cast<int64_t>(x.size());
    from = std::max<int64_t>(from, 2);
    to = std::min<int64_t>(to, n);
    float m = 0.0f;
    for (int64_t i = from; i < to; ++i) {
        const float d = x[static_cast<size_t>(i)] - 2.0f * x[static_cast<size_t>(i - 1)] + x[static_cast<size_t>(i - 2)];
        m = std::max(m, std::fabs(d));
    }
    return m;
}

float maxAbs(const std::vector<float>& x, int64_t from, int64_t to)
{
    float m = 0.0f;
    to = std::min<int64_t>(to, static_cast<int64_t>(x.size()));
    for (int64_t i = std::max<int64_t>(from, 0); i < to; ++i) m = std::max(m, std::fabs(x[static_cast<size_t>(i)]));
    return m;
}

} // namespace tgt
