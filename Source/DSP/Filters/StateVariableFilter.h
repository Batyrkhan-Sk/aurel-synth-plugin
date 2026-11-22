#pragma once

// 2-pole state-variable filter, topology-preserving transform (trapezoidal
// integrators, zero-delay feedback). Zavalishin, "The Art of VA Filter
// Design" ch. 4; this arrangement of the equations follows A. Simper
// (Cytomic), "Linear Trapezoidal Integrated SVF".
//
//   H_LP = 1 / (s^2 + k s + 1)      H_BP = s / (...)     H_HP = s^2 / (...)
//   with s = j * tan(w/2) / g,  g = tan(pi * fc / fs),  k = 1/Q
//
// The bilinear map is pre-warped at the cutoff, so the digital response is
// the analog response at warped frequency: exact cutoff and Q at any
// cutoff below Nyquist. All outputs are available every sample; morph()
// blends them continuously LP -> BP -> HP -> notch.
//
// Why custom rather than juce::dsp::StateVariableTPTFilter (same
// algorithm): the DSP core is JUCE-free (ADR D8), all outputs are needed at
// once (morph, notch), and coefficients are set per sample/sub-block for
// modulation. Numerical equivalence to JUCE's implementation is verified
// in the M3 measurements.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aurel::dsp
{

class StateVariableFilter
{
public:
    static constexpr double kMinCutoffHz = 5.0;
    static constexpr double kMaxCutoffRatio = 0.48; // of the sample rate
    static constexpr double kMinDamping = 0.005;    // k = 1/Q, Q <= 200

    struct Outputs
    {
        double lp = 0.0, bp = 0.0, hp = 0.0;
    };

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
        setParameters (1000.0, 0.0);
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }

    /** resonance 0..1 maps to damping k = 2(1 - resonance): 0 -> Q 0.5,
        1 -> Q 200 (clamped; the linear SVF does not self-oscillate). */
    void setParameters (double cutoffHz, double resonance) noexcept
    {
        setCutoffAndDamping (cutoffHz, 2.0 * (1.0 - std::clamp (resonance, 0.0, 1.0)));
    }

    void setCutoffAndDamping (double cutoffHz, double damping) noexcept
    {
        const double fc = std::clamp (cutoffHz, kMinCutoffHz, kMaxCutoffRatio * fs);
        g = std::tan (std::numbers::pi * fc / fs);
        k = std::max (damping, kMinDamping);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    Outputs process (double x) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }

    /** Continuous mode: 0 = LP, 1 = BP (unity peak), 2 = HP, 3 = notch;
        linear crossfade between neighbours. */
    [[nodiscard]] double morph (const Outputs& o, double mode) const noexcept
    {
        const double m = std::clamp (mode, 0.0, 3.0);
        const double bpNorm = k * o.bp;
        const double notch = o.lp + o.hp;
        // (1 - t) a + t b is exact at both endpoints.
        auto blend = [] (double a, double b, double t) { return (1.0 - t) * a + t * b; };
        if (m <= 1.0)
            return blend (o.lp, bpNorm, m);
        if (m <= 2.0)
            return blend (bpNorm, o.hp, m - 1.0);
        return blend (o.hp, notch, m - 2.0);
    }

    [[nodiscard]] double getG() const noexcept { return g; }
    [[nodiscard]] double getDamping() const noexcept { return k; }

private:
    double fs = 48000.0;
    double g = 0.0, k = 2.0;
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
};

} // namespace aurel::dsp
