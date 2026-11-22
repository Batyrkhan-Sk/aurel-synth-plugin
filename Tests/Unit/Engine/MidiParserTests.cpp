#include "Engine/MidiParser.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace aurel::engine;

namespace
{
    bool parse (std::array<std::uint8_t, 3> bytes, Event& e, int offset = 7)
    {
        return parseMidi (bytes.data(), 3, offset, e);
    }
} // namespace

TEST_CASE ("MIDI parser: notes", "[engine][midi]")
{
    Event e;
    REQUIRE (parse ({ 0x93, 60, 127 }, e));
    CHECK (e.type == EventType::NoteOn);
    CHECK (e.channel == 3);
    CHECK (e.note == 60);
    CHECK (e.value == 1.0f);
    CHECK (e.sampleOffset == 7);

    REQUIRE (parse ({ 0x90, 61, 64 }, e));
    CHECK (e.value == 64.0f / 127.0f);

    REQUIRE (parse ({ 0x85, 62, 40 }, e));
    CHECK (e.type == EventType::NoteOff);
    CHECK (e.channel == 5);
    CHECK (e.note == 62);

    // Note On with velocity 0 is a Note Off.
    REQUIRE (parse ({ 0x90, 63, 0 }, e));
    CHECK (e.type == EventType::NoteOff);
    CHECK (e.note == 63);
}

TEST_CASE ("MIDI parser: controllers", "[engine][midi]")
{
    Event e;
    REQUIRE (parse ({ 0xB0, 64, 127 }, e));
    CHECK (e.type == EventType::SustainPedal);
    CHECK (e.value == 1.0f);

    REQUIRE (parse ({ 0xB0, 64, 64 }, e));
    CHECK (e.value == 1.0f);

    REQUIRE (parse ({ 0xB0, 64, 63 }, e));
    CHECK (e.value == 0.0f);

    REQUIRE (parse ({ 0xB0, 121, 0 }, e)); // reset all controllers
    CHECK (e.type == EventType::SustainPedal);
    CHECK (e.value == 0.0f);

    REQUIRE (parse ({ 0xB0, 120, 0 }, e));
    CHECK (e.type == EventType::AllSoundOff);

    for (std::uint8_t cc = 123; cc <= 127; ++cc)
    {
        REQUIRE (parse ({ 0xB0, cc, 0 }, e));
        CHECK (e.type == EventType::AllNotesOff);
    }
}

TEST_CASE ("MIDI parser: ignores unsupported and malformed messages", "[engine][midi]")
{
    Event e;
    CHECK_FALSE (parse ({ 0xE0, 0, 64 }, e));  // pitch bend (M4)
    CHECK_FALSE (parse ({ 0xB0, 1, 64 }, e));  // mod wheel (M4)
    CHECK_FALSE (parse ({ 0xF8, 0, 0 }, e));   // clock

    const std::array<std::uint8_t, 2> shortMsg { 0x90, 60 };
    CHECK_FALSE (parseMidi (shortMsg.data(), 2, 0, e));
    CHECK_FALSE (parseMidi (nullptr, 3, 0, e));

    const std::array<std::uint8_t, 6> sysex { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    CHECK_FALSE (parseMidi (sysex.data(), 6, 0, e));
}
