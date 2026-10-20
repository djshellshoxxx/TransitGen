#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tg;
using Catch::Approx;

namespace {
TransportInfo playingAt(double ppq, double bpm = 120.0)
{
    TransportInfo i;
    i.playing = true; i.hasPpq = true; i.ppq = ppq; i.bpm = bpm; i.tsNum = 4; i.tsDen = 4;
    return i;
}
} // namespace

TEST_CASE("beat to sample offset conversion", "[transport]")
{
    TransportTracker tr;
    tr.prepare(48000.0);
    const BlockTime bt = tr.update(playingAt(10.0), 512);
    CHECK(bt.spb == Approx(24000.0));
    CHECK(bt.offsetOf(10.5) == 12000);
    CHECK(bt.offsetOf(10.0) == 0);
    CHECK(bt.beatAt(24000) == Approx(11.0));
    CHECK(bt.barLen == 4.0);
    CHECK(bt.jumped);                       // first block always counts as a discontinuity
    CHECK(tr.engineTime() == 512);
}

TEST_CASE("tempo change between blocks is not a jump", "[transport]")
{
    TransportTracker tr;
    tr.prepare(48000.0);
    tr.update(playingAt(0.0, 120.0), 512);
    const BlockTime b2 = tr.update(playingAt(512.0 / 24000.0, 60.0), 512);
    CHECK_FALSE(b2.jumped);
    CHECK(b2.spb == Approx(48000.0));
    CHECK(b2.offsetOf(b2.startBeat + 0.5) == 24000);
}

TEST_CASE("loop jump is detected", "[transport]")
{
    TransportTracker tr;
    tr.prepare(48000.0);
    tr.update(playingAt(16.0), 512);
    const BlockTime b = tr.update(playingAt(8.0), 512);
    CHECK(b.jumped);
    const BlockTime c = tr.update(playingAt(8.0 + 512.0 / 24000.0), 512);
    CHECK_FALSE(c.jumped);
}

TEST_CASE("stopped transport runs the internal clock continuously", "[transport]")
{
    TransportTracker tr;
    tr.prepare(48000.0);
    tr.update(playingAt(5.0), 480);
    TransportInfo stopped = playingAt(5.02);
    stopped.playing = false;
    const BlockTime s1 = tr.update(stopped, 480);
    CHECK(s1.clock);
    CHECK(s1.startBeat == Approx(5.0 + 480.0 / 24000.0));
    CHECK(s1.jumped);                       // clock source changed
    const BlockTime s2 = tr.update(stopped, 480);
    CHECK_FALSE(s2.jumped);
    CHECK(s2.startBeat == Approx(5.0 + 960.0 / 24000.0));
    const BlockTime p = tr.update(playingAt(1.0), 480);
    CHECK(p.jumped);
    CHECK_FALSE(p.clock);
}

TEST_CASE("missing host info falls back sensibly", "[transport]")
{
    TransportTracker tr;
    tr.prepare(48000.0);
    TransportInfo none;                      // not playing, no ppq, bpm 0, ts 4/4
    const BlockTime a = tr.update(none, 100);
    CHECK(a.bpm == 120.0);
    CHECK(a.clock);
    CHECK(a.startBeat == 0.0);
    tr.update(playingAt(3.0, 100.0), 100);
    none.bpm = 0.0;
    const BlockTime c = tr.update(none, 100);
    CHECK(c.bpm == 100.0);                   // last valid host tempo is kept
    TransportInfo weird = playingAt(0.0);
    weird.tsNum = 0; weird.tsDen = 0;
    CHECK(tr.update(weird, 100).barLen == 4.0);
    TransportInfo seven = playingAt(0.0);
    seven.tsNum = 7; seven.tsDen = 8; seven.hasBarStart = true; seven.barStartPpq = 10.5;
    const BlockTime b = tr.update(seven, 100);
    CHECK(b.barLen == Approx(3.5));
    CHECK(b.gridOrigin == Approx(0.0));
    seven.barStartPpq = 11.0;
    CHECK(tr.update(seven, 100).gridOrigin == Approx(0.5));
}

TEST_CASE("capture capacity covers 8 beats at 30 BPM", "[capture]")
{
    CHECK(CaptureBuffer::capacityFor(48000.0, 512) == (1 << 20));
    CHECK(CaptureBuffer::capacityFor(192000.0, 1024) == (1 << 22));
    CHECK(CaptureBuffer::capacityFor(48000.0, 512) >= static_cast<int64_t>(8.0 * 2.0 * 48000.0) + 512);

    CaptureBuffer cb;
    cb.prepare(48000.0, 64, 2);
    std::vector<float> l(64), r(64);
    for (int i = 0; i < 64; ++i) { l[static_cast<size_t>(i)] = static_cast<float>(i); r[static_cast<size_t>(i)] = -static_cast<float>(i); }
    const float* chans[2] = {l.data(), r.data()};
    cb.write(chans, 2, 64);
    CHECK(cb.at(0, 10) == 10.0f);
    CHECK(cb.at(1, 10) == -10.0f);
    CHECK(cb.read(0, 64) == 0.0f);          // not yet written
    CHECK(cb.read(0, -1) == 0.0f);
    CHECK(cb.underruns() == 2);
}
