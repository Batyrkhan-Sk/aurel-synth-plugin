#pragma once

// Sub-oscillator locked to a master oscillator (OSC1), one or two octaves
// below it.
//
// The sub phase is derived from the master: ((wraps mod div) + masterPhase)
// / div, with div = 2 or 4. The sub can therefore never drift against
// OSC1, and its square edges coincide with the master's wrap times, which
// are known to sub-sample precision and band-limited with the same kernel.
// Output is delayed by the same kernel latency as the master, so the two
// stay sample-aligned.

#include "DSP/Oscillators/BlepBuffer.h"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace aurel::dsp
{

enum class SubShape : std::uint8_t
{
    Square,
    Sine
};

template <class Kernel>
class SubOscillator
{
public:
    static constexpr int kLatency = Kernel::kHalfSpan;

    void prepare()
    {
        Kernel::initialise();
        reset();
    }

    /** Call together with the master's reset (note start). */
    void reset() noexcept
    {
        wraps = 0;
        buffer.reset();
        shape = pendingShape;
        division = pendingDivision;
    }

    void setShape (SubShape s) noexcept { pendingShape = s; }

    /** octavesBelow: 1 or 2. */
    void setOctave (int octavesBelow) noexcept { pendingDivision = octavesBelow >= 2 ? 4u : 2u; }

    /** One sample, after the master's process() for the same sample.
        masterInc must be positive (the master is never FM-modulated). */
    float process (double masterPhase, double masterWrapFraction, double masterInc) noexcept
    {
        if (masterWrapFraction > 0.0)
        {
            wraps = (wraps + 1) % 4;
            if (shape == SubShape::Square)
            {
                const auto pos = wraps % division;
                if (pos == 0)
                    buffer.addStep (2.0f, masterWrapFraction);
                else if (pos == division / 2)
                    buffer.addStep (-2.0f, masterWrapFraction);
            }
        }

        if (pendingShape != shape || pendingDivision != division)
        {
            const double pOld = subPhase (masterPhase, division);
            const double pNew = subPhase (masterPhase, pendingDivision);
            const float jump = value (pendingShape, pNew) - value (shape, pOld);
            if (jump != 0.0f)
                buffer.addStep (jump, 1.0);
            const double slopeJump = slope (pendingShape, pNew) * masterInc / pendingDivision
                                   - slope (shape, pOld) * masterInc / division;
            if (slopeJump != 0.0)
                buffer.addCorner (static_cast<float> (slopeJump), 1.0);
            shape = pendingShape;
            division = pendingDivision;
        }

        buffer.push (value (shape, subPhase (masterPhase, division)));
        return buffer.pop();
    }

private:
    [[nodiscard]] double subPhase (double masterPhase, std::uint32_t div) const noexcept
    {
        return (static_cast<double> (wraps % div) + masterPhase) / static_cast<double> (div);
    }

    [[nodiscard]] static float value (SubShape s, double p) noexcept
    {
        if (s == SubShape::Square)
            return p < 0.5 ? 1.0f : -1.0f;
        return static_cast<float> (std::sin (2.0 * std::numbers::pi * p));
    }

    /** d(value)/d(sub phase). */
    [[nodiscard]] static double slope (SubShape s, double p) noexcept
    {
        if (s == SubShape::Square)
            return 0.0;
        return 2.0 * std::numbers::pi * std::cos (2.0 * std::numbers::pi * p);
    }

    BlepBuffer<Kernel> buffer;
    std::uint32_t wraps = 0; // master wraps mod 4 (covers both divisions)
    std::uint32_t division = 2;
    std::uint32_t pendingDivision = 2;
    SubShape shape = SubShape::Square;
    SubShape pendingShape = SubShape::Square;
};

} // namespace aurel::dsp
