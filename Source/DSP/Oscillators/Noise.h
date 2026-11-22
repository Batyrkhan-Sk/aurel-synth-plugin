#pragma once

// Noise source: white or pink, deterministic per seed.
//
// White: uniform in [-1, 1) from a xorshift32 generator. Its RMS is 1/sqrt(3),
//        the same as a full-scale saw, so mixer levels are comparable.
// Pink:  white noise through a cascade of first-order shelving sections whose
//        poles and zeros are interleaved on a geometric grid (poles at 5 Hz *
//        3^i, zeros at pole * sqrt(3)). This gives -3.01 dB/octave with small
//        ripple. Corners are defined in Hz and discretised per sample rate
//        (matched-z), so the slope is correct at any internal rate. A
//        fixed-coefficient design such as Kellet's assumes 44.1 kHz. Gain is
//        normalised so pink has the same RMS as white at the current rate.

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace aurel::dsp
{

enum class NoiseColor : std::uint8_t
{
    White,
    Pink
};

/** -3 dB/octave shaping filter (exposed separately for testing). */
class PinkFilter
{
public:
    static constexpr int kMaxSections = 10;
    static constexpr double kFirstPoleHz = 5.0;
    static constexpr double kSpacing = 3.0;

    void prepare (double sampleRate) noexcept
    {
        sections = 0;
        const double rootSpacing = std::sqrt (kSpacing);
        for (double pole = kFirstPoleHz; sections < kMaxSections; pole *= kSpacing)
        {
            // The section is needed as long as its pole is in band; its zero
            // may lie above Nyquist (still a valid real matched-z zero).
            const double zero = pole * rootSpacing;
            if (pole >= 0.45 * sampleRate)
                break;
            auto& s = section[static_cast<std::size_t> (sections++)];
            s.p = std::exp (-2.0 * std::numbers::pi * pole / sampleRate);
            s.z = std::exp (-2.0 * std::numbers::pi * zero / sampleRate);
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : section)
            s.x1 = s.y1 = 0.0;
    }

    /** Unnormalised: each section is (1 - z q^-1) / (1 - p q^-1). */
    double process (double x) noexcept
    {
        for (int i = 0; i < sections; ++i)
        {
            auto& s = section[static_cast<std::size_t> (i)];
            const double y = x - s.z * s.x1 + s.p * s.y1;
            s.x1 = x;
            s.y1 = y;
            x = y;
        }
        return x;
    }

    [[nodiscard]] int numSections() const noexcept { return sections; }

private:
    struct Section
    {
        double p = 0.0, z = 0.0, x1 = 0.0, y1 = 0.0;
    };
    std::array<Section, kMaxSections> section {};
    int sections = 0;
};

class NoiseGenerator
{
public:
    /** Gain that gives pink noise the same RMS as white at this rate:
        1 / sqrt(energy of the filter's impulse response). Non-real-time;
        compute once per sample rate and share between voices. */
    [[nodiscard]] static double computePinkGain (double sampleRate) noexcept
    {
        PinkFilter probe;
        probe.prepare (sampleRate);
        double energy = 0.0;
        const auto n = static_cast<int> (sampleRate); // lowest pole (5 Hz) decays in ~32 ms
        for (int i = 0; i < n; ++i)
        {
            const double h = probe.process (i == 0 ? 1.0 : 0.0);
            energy += h * h;
        }
        return energy > 0.0 ? 1.0 / std::sqrt (energy) : 0.0;
    }

    void prepare (double sampleRate, std::uint32_t seed, double pinkNormalisation) noexcept
    {
        pink.prepare (sampleRate);
        pinkGain = pinkNormalisation;
        initialSeed = seed == 0 ? 0x9E3779B9u : seed;
        reset();
    }

    void reset() noexcept
    {
        state = initialSeed;
        pink.reset();
    }

    void setColor (NoiseColor c) noexcept { color = c; }

    float process() noexcept
    {
        const float white = nextWhite();
        if (color == NoiseColor::White)
            return white;
        return static_cast<float> (pink.process (white) * pinkGain);
    }

private:
    float nextWhite() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // Top 24 bits -> [-1, 1) exactly representable in float.
        return static_cast<float> (static_cast<std::int32_t> (state >> 8) - (1 << 23)) * (1.0f / (1 << 23));
    }

    PinkFilter pink;
    double pinkGain = 1.0;
    std::uint32_t state = 0x9E3779B9u;
    std::uint32_t initialSeed = 0x9E3779B9u;
    NoiseColor color = NoiseColor::White;
};

} // namespace aurel::dsp
