#include "DSP/Core/Conversions.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace aurel::dsp;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE ("noteToFrequency matches 12-TET reference points", "[dsp][conversions]")
{
    CHECK_THAT (noteToFrequency (69.0), WithinRel (440.0, 1e-12));
    CHECK_THAT (noteToFrequency (81.0), WithinRel (880.0, 1e-12));
    CHECK_THAT (noteToFrequency (57.0), WithinRel (220.0, 1e-12));
    CHECK_THAT (noteToFrequency (60.0), WithinRel (261.6255653005986, 1e-12));
    CHECK_THAT (noteToFrequency (0.0), WithinRel (8.175798915643707, 1e-12));
    CHECK_THAT (noteToFrequency (127.0), WithinRel (12543.853951415975, 1e-12));
    CHECK_THAT (noteToFrequency (69.0, 432.0), WithinRel (432.0, 1e-12));
}

TEST_CASE ("note <-> frequency round trip is exact to 1e-9 semitone", "[dsp][conversions]")
{
    for (double note = -24.0; note <= 151.0; note += 0.37)
        REQUIRE_THAT (frequencyToNote (noteToFrequency (note)), WithinAbs (note, 1e-9));
}

TEST_CASE ("decibel conversions", "[dsp][conversions]")
{
    CHECK_THAT (decibelsToGain (0.0f), WithinRel (1.0f, 1e-6f));
    CHECK_THAT (decibelsToGain (-20.0f), WithinRel (0.1f, 1e-6f));
    CHECK_THAT (decibelsToGain (20.0f * std::log10 (0.5f)), WithinRel (0.5f, 1e-6f));
    CHECK (decibelsToGain (-120.0f) == 0.0f);
    CHECK (decibelsToGain (-500.0f) == 0.0f);

    CHECK_THAT (gainToDecibels (1.0f), WithinAbs (0.0f, 1e-6f));
    CHECK_THAT (gainToDecibels (0.1f), WithinAbs (-20.0f, 1e-5f));
    CHECK (gainToDecibels (0.0f) == -120.0f);
    CHECK (gainToDecibels (-1.0f) == -120.0f);
    CHECK (gainToDecibels (1e-9f) == -120.0f);
}
