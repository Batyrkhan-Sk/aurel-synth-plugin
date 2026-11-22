#pragma once

// Band-limited classic-waveform oscillator.
//
// The naive waveform is generated per sample. Every event inside the sample
// interval is located with sub-sample precision and corrected with the
// kernel through a BlepBuffer:
//
//   phase crossing a breakpoint   saw: 0 (step)  pulse: 0, pw (steps)
//                                 triangle: 0, 0.5 (corners)
//   hard sync                     phase reset at the master's wrap time:
//                                 value step + slope change
//   waveform switch               step + slope change at the sample
//   pulse-width modulation        pw moves linearly across the interval;
//                                 the edge is where phase - pw(t) crosses 0
//
// Increments may be negative (through-zero FM): the phase then runs
// backwards. Steps change sign with the direction, while corners scale
// with |increment|, because the slope change of a corner does not depend
// on direction.

#include "DSP/Oscillators/BlepBuffer.h"

#include <algorithm>
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
    static constexpr double kMinPulseWidth = 0.01;
    static constexpr double kMaxIncrement = 0.49; // |frequency| < 0.49 * sample rate

    /** Non-real-time: builds kernel tables if needed. */
    void prepare()
    {
        Kernel::initialise();
        reset();
    }

    /** Clears history. A waveform / pulse width set before reset() applies
        immediately, without a switch transient. */
    void reset (double startPhase = 0.0) noexcept
    {
        phase = wrap (startPhase);
        buffer.reset();
        waveform = pendingWaveform;
        pulseWidth = targetPulseWidth;
        wrapFraction = -1.0;
    }

    /** Takes effect at the next sample, band-limited (no click). */
    void setWaveform (Waveform w) noexcept { pendingWaveform = w; }

    /** The edge moves linearly to the new width over the next sample. */
    void setPulseWidth (double pw) noexcept
    {
        targetPulseWidth = std::clamp (pw, kMinPulseWidth, 1.0 - kMinPulseWidth);
    }

    /** Hard-resets the phase without band-limiting (e.g. note start of an
        idle voice). For sync use process (inc, syncAt). */
    void setPhase (double p) noexcept { phase = wrap (p); }

    [[nodiscard]] double getPhase() const noexcept { return phase; }
    [[nodiscard]] Waveform getWaveform() const noexcept { return pendingWaveform; }

    /** Time (0, 1] at which the phase wrapped forward during the last
        process() call, or -1. Used as the sync / sub-oscillator source. */
    [[nodiscard]] double getWrapFraction() const noexcept { return wrapFraction; }

    /** One sample. increment = frequency / sampleRate (may be negative).
        syncAt in (0, 1]: reset the phase to 0 at that time within the
        interval (hard sync); <= 0 for no sync. */
    float process (double increment, double syncAt = -1.0) noexcept
    {
        const double inc = std::clamp (increment, -kMaxIncrement, kMaxIncrement);
        const double pw0 = pulseWidth;
        const double pw1 = targetPulseWidth;
        wrapFraction = -1.0;

        double endPhase = 0.0;
        if (syncAt > 0.0 && syncAt <= 1.0)
        {
            processSegment (phase, 0.0, syncAt, inc, pw0, pw1);

            const double before = wrap (phase + inc * syncAt);
            const double pwAt = pw0 + (pw1 - pw0) * syncAt;
            const float jump = naiveValue (waveform, 0.0, pwAt) - naiveValue (waveform, before, pwAt);
            if (jump != 0.0f)
                buffer.addStep (jump, syncAt);
            const double slopeJump = (naiveSlope (waveform, 0.0) - naiveSlope (waveform, before)) * inc;
            if (slopeJump != 0.0)
                buffer.addCorner (static_cast<float> (slopeJump), syncAt);

            processSegment (0.0, syncAt, 1.0, inc, pw0, pw1);
            endPhase = wrap (inc * (1.0 - syncAt));
        }
        else
        {
            processSegment (phase, 0.0, 1.0, inc, pw0, pw1);
            endPhase = wrap (phase + inc);
        }

        if (pendingWaveform != waveform)
        {
            const float jump = naiveValue (pendingWaveform, endPhase, pw1) - naiveValue (waveform, endPhase, pw1);
            if (jump != 0.0f)
                buffer.addStep (jump, 1.0);
            const double slopeJump = (naiveSlope (pendingWaveform, endPhase) - naiveSlope (waveform, endPhase)) * inc;
            if (slopeJump != 0.0)
                buffer.addCorner (static_cast<float> (slopeJump), 1.0);
            waveform = pendingWaveform;
        }

        phase = endPhase;
        pulseWidth = pw1;
        buffer.push (naiveValue (waveform, endPhase, pw1));
        return buffer.pop();
    }

    // --- naive waveform definitions (public for tests) ------------------------

    [[nodiscard]] static float naiveValue (Waveform w, double p, double pw) noexcept
    {
        switch (w)
        {
            case Waveform::Saw:      return static_cast<float> (2.0 * p - 1.0);
            case Waveform::Pulse:    return p < pw ? 1.0f : -1.0f;
            case Waveform::Triangle: return static_cast<float> (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case Waveform::Sine:     break;
        }
        return static_cast<float> (std::sin (2.0 * std::numbers::pi * p));
    }

    /** d(naive)/d(phase). */
    [[nodiscard]] static double naiveSlope (Waveform w, double p) noexcept
    {
        switch (w)
        {
            case Waveform::Saw:      return 2.0;
            case Waveform::Pulse:    return 0.0;
            case Waveform::Triangle: return p < 0.5 ? 4.0 : -4.0;
            case Waveform::Sine:     break;
        }
        return 2.0 * std::numbers::pi * std::cos (2.0 * std::numbers::pi * p);
    }

private:
    static double wrap (double p) noexcept { return p - std::floor (p); }

    /** Calls onCross(localTime) for every crossing of value b (mod 1) by a
        line starting at `start` with `slope` over `duration`. Forward:
        start < b' <= end. Backward: end < b' <= start. */
    template <class F>
    static void forEachCrossing (double start, double slope, double duration, double b, F&& onCross) noexcept
    {
        const double end = start + slope * duration;
        for (const double bb : { b - 1.0, b, b + 1.0 })
        {
            if ((slope > 0.0 && start < bb && bb <= end) || (slope < 0.0 && end < bb && bb <= start))
                onCross ((bb - start) / slope);
        }
    }

    /** Events while the phase runs from `start` (at time t0) to time t1. */
    void processSegment (double start, double t0, double t1, double inc, double pw0, double pw1) noexcept
    {
        const double duration = t1 - t0;
        if (duration <= 0.0 || inc == 0.0)
            return;
        const float dir = inc > 0.0 ? 1.0f : -1.0f;

        switch (waveform)
        {
            case Waveform::Saw:
                forEachCrossing (start, inc, duration, 0.0, [&] (double t) {
                    buffer.addStep (-2.0f * dir, t0 + t);
                    if (inc > 0.0)
                        wrapFraction = t0 + t;
                });
                break;

            case Waveform::Pulse:
            {
                forEachCrossing (start, inc, duration, 0.0, [&] (double t) {
                    buffer.addStep (2.0f * dir, t0 + t);
                    if (inc > 0.0)
                        wrapFraction = t0 + t;
                });
                // Falling edge: where phase - pw(t) crosses 0 (mod 1).
                const double pwA = pw0 + (pw1 - pw0) * t0;
                const double pwB = pw0 + (pw1 - pw0) * t1;
                const double relSlope = inc - (pwB - pwA) / duration;
                forEachCrossing (start - pwA, relSlope, duration, 0.0, [&] (double t) {
                    buffer.addStep (relSlope > 0.0 ? -2.0f : 2.0f, t0 + t);
                });
                break;
            }

            case Waveform::Triangle:
            {
                const auto corner = static_cast<float> (8.0 * std::abs (inc));
                forEachCrossing (start, inc, duration, 0.0, [&] (double t) {
                    buffer.addCorner (corner, t0 + t);
                    if (inc > 0.0)
                        wrapFraction = t0 + t;
                });
                forEachCrossing (start, inc, duration, 0.5, [&] (double t) {
                    buffer.addCorner (-corner, t0 + t);
                });
                break;
            }

            case Waveform::Sine:
                forEachCrossing (start, inc, duration, 0.0, [&] (double t) {
                    if (inc > 0.0)
                        wrapFraction = t0 + t;
                });
                break;
        }
    }

    BlepBuffer<Kernel> buffer;
    double phase = 0.0;
    double pulseWidth = 0.5;
    double targetPulseWidth = 0.5;
    double wrapFraction = -1.0;
    Waveform waveform = Waveform::Saw;
    Waveform pendingWaveform = Waveform::Saw;
};

/** AUREL's production oscillator kernel, chosen by measurement in M2a
    (docs/dsp/oscillator-antialiasing.md): worst in-band alias <= -110 dBc at
    the 88.2/96 kHz internal rates, harmonics exact to 20 kHz, 6-sample
    latency at the internal rate. The other kernels remain available to
    the render tool so the comparison stays reproducible. */
using OscillatorKernel = blep::SincBlep<6>;
using Oscillator = BandlimitedOscillator<OscillatorKernel>;

} // namespace aurel::dsp
