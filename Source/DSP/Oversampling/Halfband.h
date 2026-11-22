#pragma once

// Polyphase IIR half-band filter for 2x decimation.
//
//   H(z) = 0.5 * (A0(z^2) + z^-1 A1(z^2))
//
// A0 and A1 are cascades of first-order all-pass sections. Each section
// costs one multiply per *output* sample, so a 12-coefficient design costs
// 12 multiplies per output sample: far cheaper than a linear-phase FIR of
// similar stopband. Phase is non-linear, which is inaudible for a
// synthesizer's internal decimation, and latency is low.
//
// Coefficient design: elliptic-function method for power-complementary
// half-band IIR filters (Valenti; R. Ansari & B. Liu; as popularised by L.
// de Soras' HIIR). Given a stopband attenuation and a transition width,
// it returns the minimum number of coefficients. Implemented from the
// published equations; verified by measurement in the test suite.

#include <array>
#include <cmath>
#include <numbers>

namespace aurel::dsp
{

namespace halfband
{
    inline constexpr int kMaxCoefficients = 24;

    struct Design
    {
        std::array<double, kMaxCoefficients> coefficients {};
        int count = 0;
    };

    namespace detail
    {
        inline double accNum (double q, int order, int c) noexcept
        {
            double acc = 0.0, term = 0.0;
            int i = 0, j = 1;
            do
            {
                term = std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * std::numbers::pi / order) * j;
                acc += term;
                j = -j;
                ++i;
            } while (std::abs (term) > 1e-100 && i < 100);
            return acc;
        }

        inline double accDen (double q, int order, int c) noexcept
        {
            double acc = 0.0, term = 0.0;
            int i = 1, j = -1;
            do
            {
                term = std::pow (q, i * i) * std::cos (i * 2 * c * std::numbers::pi / order) * j;
                acc += term;
                j = -j;
                ++i;
            } while (std::abs (term) > 1e-100 && i < 100);
            return acc;
        }
    } // namespace detail

    /** attenuationDb: stopband attenuation. transition: half-width of the
        transition band as a fraction of the input sample rate, i.e.
        (fs/4 - passbandEdge) / fs, in (0, 0.25). */
    inline Design design (double attenuationDb, double transition) noexcept
    {
        double k = std::tan ((1.0 - transition * 2.0) * std::numbers::pi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e4 = e * e * e * e;
        const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

        const double attn = std::pow (10.0, -attenuationDb / 10.0);
        const double a = attn / (1.0 - attn);
        int order = static_cast<int> (std::ceil (std::log (a * a / 16.0) / std::log (q)));
        if ((order & 1) == 0)
            ++order;
        if (order < 3)
            order = 3;

        Design d;
        d.count = std::min ((order - 1) / 2, kMaxCoefficients);
        for (int index = 0; index < d.count; ++index)
        {
            const int c = index + 1;
            const double num = detail::accNum (q, order, c) * std::pow (q, 0.25);
            const double den = detail::accDen (q, order, c) + 0.5;
            const double ww = num / den;
            const double wwsq = ww * ww;
            const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            d.coefficients[static_cast<std::size_t> (index)] = (1.0 - x) / (1.0 + x);
        }
        return d;
    }
} // namespace halfband

/** One channel of 2:1 decimation. */
class HalfbandDecimator
{
public:
    void setDesign (const halfband::Design& d) noexcept
    {
        design = d;
        reset();
    }

    void reset() noexcept
    {
        xm.fill (0.0);
        ym.fill (0.0);
    }

    /** Consumes two input samples (earlier, later), returns one output sample. */
    float process (float earlier, float later) noexcept
    {
        double path0 = later;
        double path1 = earlier;
        for (int i = 0; i < design.count; ++i)
        {
            const auto s = static_cast<std::size_t> (i);
            double& x = (i & 1) == 0 ? path0 : path1;
            const double y = (x - ym[s]) * design.coefficients[s] + xm[s];
            xm[s] = x;
            ym[s] = y;
            x = y;
        }
        return static_cast<float> (0.5 * (path0 + path1));
    }

    [[nodiscard]] int numCoefficients() const noexcept { return design.count; }

private:
    halfband::Design design;
    std::array<double, halfband::kMaxCoefficients> xm {}, ym {};
};

} // namespace aurel::dsp
