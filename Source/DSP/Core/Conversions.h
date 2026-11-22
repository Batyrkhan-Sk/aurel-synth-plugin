#pragma once

// Unit conversions used throughout the DSP core.
//
// Pitch is handled in (fractional) MIDI note numbers / semitones wherever
// possible so that modulation is musically linear; conversion to Hz happens
// as late as possible. These reference implementations use std::exp2 /
// std::log2 in double precision. Faster approximations, if ever needed in a
// hot path, must be validated against these in the test suite.

#include <algorithm>
#include <cmath>

namespace aurel::dsp
{

inline constexpr double kA4Frequency = 440.0;
inline constexpr double kA4Note = 69.0;

/** Fractional MIDI note number -> frequency in Hz (12-TET). */
[[nodiscard]] inline double noteToFrequency (double note, double a4Hz = kA4Frequency) noexcept
{
    return a4Hz * std::exp2 ((note - kA4Note) / 12.0);
}

/** Frequency in Hz -> fractional MIDI note number. Requires hz > 0. */
[[nodiscard]] inline double frequencyToNote (double hz, double a4Hz = kA4Frequency) noexcept
{
    return kA4Note + 12.0 * std::log2 (hz / a4Hz);
}

/** Decibels -> linear gain. Anything at or below minusInfinityDb maps to 0. */
[[nodiscard]] inline float decibelsToGain (float db, float minusInfinityDb = -120.0f) noexcept
{
    return db > minusInfinityDb ? std::pow (10.0f, db * 0.05f) : 0.0f;
}

/** Linear gain -> decibels, clamped below at minusInfinityDb. */
[[nodiscard]] inline float gainToDecibels (float gain, float minusInfinityDb = -120.0f) noexcept
{
    return gain > 0.0f ? std::max (minusInfinityDb, 20.0f * std::log10 (gain))
                       : minusInfinityDb;
}

} // namespace aurel::dsp
