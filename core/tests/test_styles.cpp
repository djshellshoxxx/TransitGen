#include "GenTestUtil.h"

#include "transitgen/Published.h"
#include "transitgen/StyleLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

using namespace tg;

namespace {
std::string factoryJson(int index)
{
    int n = 0;
    const EmbeddedStyle* src = factoryStyleSources(n);
    REQUIRE(index < n);
    return src[index].json;
}

/// Replaces the first occurrence of `from` in the Dubstep JSON and returns the parse error.
std::string errorFor(const std::string& from, const std::string& to, StyleOrigin origin = StyleOrigin::Factory)
{
    std::string j = factoryJson(0);
    const size_t at = j.find(from);
    REQUIRE(at != std::string::npos);
    j.replace(at, from.size(), to);
    Style s;
    std::string err;
    CHECK_FALSE(parseStyle(j, s, err, origin));
    return err;
}
} // namespace

TEST_CASE("every factory style is embedded, parses and validates", "[styles]")
{
    int n = 0;
    const EmbeddedStyle* src = factoryStyleSources(n);
    REQUIRE(n == 6);
    for (int i = 0; i < n; ++i) {
        Style s;
        std::string err;
        INFO(src[i].fileName << ": " << err);
        CHECK(parseStyle(src[i].json, s, err, StyleOrigin::Factory));
        CHECK(s.id == i + 1);
    }
    const StyleTable& t = tgt::factoryStyles();
    REQUIRE(t.numStyles == 6);
    const char* names[] = {"Dubstep", "Drum & Bass", "Trap", "Hyperpop", "Techno", "IDM"};
    for (int i = 0; i < 6; ++i) {
        const Style* s = t.find(static_cast<uint16_t>(i + 1));
        REQUIRE(s != nullptr);
        CHECK(std::strcmp(s->name, names[i]) == 0);
    }
}

TEST_CASE("factory style fields are loaded exactly", "[styles]")
{
    const StyleTable& t = tgt::factoryStyles();
    const Style& trap = *t.find(3);
    CHECK(trap.grid == 0.16666666666666666);
    CHECK(trap.segmentLen[0] == 0.3333333333333333);
    CHECK(trap.stutter.pitchDir == PitchDir::Down);
    CHECK(trap.ending.type == EndingType::TapeStop);
    CHECK(trap.filter.type == FilterType::LowPass);
    CHECK(trap.gate.patterns[1] == 0xB6DB);            // "1101101101101101", bit i = character i
    CHECK(trap.sourceProb[static_cast<int>(EventType::Reverse)].n == 0);   // missing = 0
    const Style& idm = *t.find(6);
    CHECK(idm.numSegmentLens == 5);
    CHECK(idm.stutter.rollDiv == 8);
    CHECK(idm.stutter.rampMode == 1);
    CHECK(idm.filter.wobble == 0.15f);
    CHECK(idm.gate.numRates == 4);
    const Style& techno = *t.find(5);
    CHECK(techno.ending.type == EndingType::None);
    CHECK(techno.gate.depth == 0.85f);
}

TEST_CASE("style validation rejects bad input with a clear message", "[styles]")
{
    CHECK(errorFor("\"rollDiv\":4", "\"rollDiv\":3") == "stutter.rollDiv: must be 2, 4 or 8");
    CHECK(errorFor("\"grid\":0.25", "\"grid\":0") == "grid: out of range [0.015625, 4]");
    CHECK(errorFor("\"pitchDir\":\"up\"", "\"pitchDir\":\"sideways\"") == "stutter.pitchDir: must be one of \"up\", \"down\", \"both\"");
    CHECK(errorFor("\"1011101110111011\"", "\"10111011101110\"") == "gate.patterns[1]: must be a string of 16 '0'/'1' characters");
    CHECK(errorFor("\"rates\":[2,4]", "\"rates\":[2,5]") == "gate.rates[1]: must be 1, 2, 3, 4, 6 or 8");
    CHECK(errorFor("\"len\":[0.5,1,2]", "\"len\":[0.5,1]") == "segment.weight: needs one table per segment.len entry");
    CHECK(errorFor("[[0,0.2],[1,1]]", "[[0.5,0.2],[0.5,1]]") == "segment.weight[0][1]: energies must be strictly increasing");
    CHECK(errorFor("\"tape\":{\"curve\":0.3}", "\"tape\":{\"curve\":0.3,\"speed\":1}") == "tape.speed: unknown key");
    CHECK(errorFor("\"silence\":{\"fadeMs\":3},", "") == "silence: missing");
    CHECK(errorFor("\"decayDb\":-1", "\"decayDb\":1") == "stutter.decayDb: out of range [-12, 0]");
    CHECK(errorFor("\"Pass\":[[0,0.6],[1,0.05]],\"Stutter\":[[0,0.35],[1,0.8]],\"Reverse\":[[0,0.05],[1,0.1]],\"Silence\":[[0,0],[1,0.05]]",
                   "\"Silence\":[[0,0],[1,0.05]]") == "source.prob: weights must not all be 0 at any energy");
    CHECK(errorFor("\"id\":1", "\"id\":1.5") == "id: must be an integer");
    CHECK(errorFor("\"id\":1", "\"id\":1", StyleOrigin::User) == "id: out of range [100, 65535]");
    CHECK(errorFor("\"respectBars\":true", "\"respectBars\":true,") .find("line 1, column") == 0);
}

TEST_CASE("JSON reader edge cases", "[styles]")
{
    Style s;
    std::string err;
    CHECK_FALSE(parseStyle("", s, err));
    CHECK(err == "line 1, column 1: unexpected end of input");
    CHECK_FALSE(parseStyle("{\"id\":1,\"id\":2}", s, err));
    CHECK(err == "line 1, column 13: duplicate key \"id\"");
    CHECK_FALSE(parseStyle("{\n \"id\": 01}", s, err));
    CHECK(err == "line 2, column 9: leading zeros are not allowed");
    CHECK_FALSE(parseStyle("[1, 2]", s, err));
    CHECK(err == "<root>: must be an object");
    // A user style with an escaped, non-ASCII name loads (and user ids start at 100).
    std::string j = factoryJson(0);
    j.replace(j.find("\"id\":1"), 6, "\"id\":100");
    j.replace(j.find("\"Dubstep\""), 9, "\"Dub\\u00e9 \\\"x\\\"\"");
    CHECK(parseStyle(j, s, err, StyleOrigin::User));
    CHECK(std::string(s.name) == "Dub\xc3\xa9 \"x\"");
    StyleTable t;
    CHECK(addStyle(t, s, err));
    CHECK_FALSE(addStyle(t, s, err));
    CHECK(err == "duplicate style id 100");
}

TEST_CASE("Published swaps immutable objects and frees them only after acknowledgement", "[styles]")
{
    Published<StyleTable> pub;
    CHECK(pub.acquire() == nullptr);
    pub.publish(std::make_unique<StyleTable>());
    const StyleTable* a = pub.acquire();
    REQUIRE(a != nullptr);
    pub.publish(std::make_unique<StyleTable>());
    CHECK(pub.retiredCount() == 1);              // the audio thread may still hold `a`
    pub.collect();
    CHECK(pub.retiredCount() == 1);
    const StyleTable* b = pub.acquire();
    CHECK(b != a);
    pub.collect();
    CHECK(pub.retiredCount() == 0);
    CHECK(pub.current() == b);
}
