#pragma once

// Band-limited classic-waveform oscillator.
//
// The naive waveform is generated sample by sample. Every discontinuity
// (step) or corner (slope change) is located with sub-sample precision from
// the phase, and the kernel's correction is overlap-added into a short ring
// buffer around it. Output is delayed by Kernel::kHalfSpan samples so
// corrections can also reach samples *before* the discontinuity.
//
//   saw      step -2 at phase wrap
//   pulse    step +2 at wrap, step -2 at phase == pulse width
//   triangle slope corners at phase 0 (+8 inc) and 0.5 (-8 inc)
//   sine     no correction needed
//
// M2a scope: positive phase increments (no through-zero FM yet), no sync,
// waveform changes are not yet corrected. Those arrive in M2b.

#include "DSP/Oscillators/BlepKernels.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace aurel::dsp
{

enum class Waveform : std::uint8_t
{
    Sine,
    Triangle,
    Saw,
    Pulse
};

template <class Kernel>
class BandlimitedOscillator
{
public:
    static constexpr int kLatency = Kernel::kHalfSpan;

    /** Non-real-time: builds kernel tables if needed. */
    void prepare()
    {
        Kernel::initialise();
        reset();
    }

    void reset (double startPhase = 0.0) noexcept
    {
        phase = startPhase - std::floor (startPhase);
        ring.fill (0.0f);
        writeIndex = 0;
    }

    void setWaveform (Waveform w) noexcept { waveform = w; }
    void setPulseWidth (double pw) noexcept { pulseWidth = std::clamp (pw, 0.01, 0.99); }

    [[nodiscard]] double getPhase() const noexcept { return phase; }

    /** Generates one sample. increment = frequency / sampleRate, in (0, 0.5). */
    float process (double increment) noexcept
    {
        const double prev = phase;
        double next = prev + increment;
        const bool wrapped = next >= 1.0;
        if (wrapped)
            next -= 1.0;
        phase = next;

        ring[slot (0)] += naiveValue (next);

        switch (waveform)
        {
            case Waveform::Saw:
                if (wrapped)
                    addStep (-2.0f, (1.0 - prev) / increment);
                break;

            case Waveform::Pulse:
                if (wrapped)
                    addStep (2.0f, (1.0 - prev) / increment);
                if (prev < pulseWidth && prev + increment >= pulseWidth)
                    addStep (-2.0f, (pulseWidth - prev) / increment);
                else if (wrapped && pulseWidth + 1.0 <= prev + increment)
                    addStep (-2.0f, (pulseWidth + 1.0 - prev) / increment);
                break;

            case Waveform::Triangle:
                if (wrapped)
                    addCorner (static_cast<float> (8.0 * increment), (1.0 - prev) / increment);
                if (prev < 0.5 && prev + increment >= 0.5)
                    addCorner (static_cast<float> (-8.0 * increment), (0.5 - prev) / increment);
                else if (wrapped && 1.5 <= prev + increment)
                    addCorner (static_cast<float> (-8.0 * increment), (1.5 - prev) / increment);
                break;

            case Waveform::Sine:
                break;
        }

        auto& out = ring[slot (-kLatency)];
        const float y = out;
        out = 0.0f;
        ++writeIndex;
        return y;
    }

private:
    static constexpr int kHalfSpan = Kernel::kHalfSpan;
    static constexpr std::uint32_t kRingSize = std::bit_ceil (static_cast<std::uint32_t> (2 * kHalfSpan + 1));
    static constexpr std::uint32_t kMask = kRingSize - 1;

    [[nodiscard]] std::size_t slot (int offset) const noexcept
    {
        return static_cast<std::size_t> ((writeIndex + static_cast<std::uint32_t> (offset)) & kMask);
    }

    [[nodiscard]] float naiveValue (double p) const noexcept
    {
        switch (waveform)
        {
            case Waveform::Saw:      return static_cast<float> (2.0 * p - 1.0);
            case Waveform::Pulse:    return p < pulseWidth ? 1.0f : -1.0f;
            case Waveform::Triangle: return static_cast<float> (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case Waveform::Sine:     break;
        }
        return static_cast<float> (std::sin (2.0 * std::numbers::pi * p));
    }

    /** Step of `height` at fractional time f in (0, 1] after the previous
        sample (f = 1 means exactly at the current sample). */
    void addStep (float height, double f) noexcept
    {
        for (int j = -kHalfSpan; j < kHalfSpan; ++j)
            ring[slot (j)] += height * Kernel::blep (j + 1.0 - f);
    }

    /** Slope change of `slopeDelta` (units per sample) at fractional time f. */
    void addCorner (float slopeDelta, double f) noexcept
    {
        for (int j = -kHalfSpan; j < kHalfSpan; ++j)
            ring[slot (j)] += slopeDelta * Kernel::blamp (j + 1.0 - f);
    }

    std::array<float, kRingSize> ring {};
    std::uint32_t writeIndex = 0;
    double phase = 0.0;
    double pulseWidth = 0.5;
    Waveform waveform = Waveform::Saw;
};

/** AUREL's production oscillator kernel, chosen by measurement in M2a
    (docs/dsp/oscillator-antialiasing.md): worst in-band alias <= -110 dBc at
    the 88.2/96 kHz internal rates, harmonics exact to 20 kHz, 6-sample
    latency at the internal rate. The other kernels remain available to
    the render tool so the comparison stays reproducible. */
using OscillatorKernel = blep::SincBlep<6>;
using Oscillator = BandlimitedOscillator<OscillatorKernel>;

} // namespace aurel::dsp
