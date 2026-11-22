#pragma once

// Band-limiting correction kernels for oscillator discontinuities.
//
// Convention: d = (sample time - discontinuity time), in samples.
//   blep(d)  : correction added to the naive signal for a unit step (+1) at
//              d = 0. Non-zero only for d in [-kHalfSpan, kHalfSpan).
//   blamp(d) : correction for a unit increase of slope (+1 per sample) at
//              d = 0 (a corner, e.g. the peaks of a triangle).
// A kernel with half-span H needs H samples of look-ahead, i.e. the
// oscillator output is delayed by H samples (linear-phase kernels).
//
// Kernels:
//   Naive       no correction (reference / measurement baseline only).
//   PolyBlep2   2-point polynomial BLEP/BLAMP: the step convolved with a
//               linear B-spline (Välimäki & Huovilainen 2007; Esqueda et
//               al. 2016 for polyBLAMP). Very cheap, weak stopband.
//   SincBlep<H> tabulated integral of a Kaiser-windowed sinc spanning 2H
//               samples (cutoff at Nyquist). Stopband set by H and beta.
//
// Which kernel AUREL ships with is decided by measurement (ADR 0001, D3).

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace aurel::dsp::blep
{

struct Naive
{
    static constexpr int kHalfSpan = 0;
    static constexpr const char* kName = "naive";
    static void initialise() noexcept {}
    static float blep (double) noexcept { return 0.0f; }
    static float blamp (double) noexcept { return 0.0f; }
};

struct PolyBlep2
{
    static constexpr int kHalfSpan = 1;
    static constexpr const char* kName = "polyblep2";
    static void initialise() noexcept {}

    static float blep (double d) noexcept
    {
        if (d < 0.0)
        {
            const double t = d + 1.0;
            return static_cast<float> (0.5 * t * t);
        }
        const double t = 1.0 - d;
        return static_cast<float> (-0.5 * t * t);
    }

    static float blamp (double d) noexcept
    {
        const double t = 1.0 - std::abs (d);
        return static_cast<float> (t * t * t / 6.0);
    }
};

namespace detail
{
    /** Zeroth-order modified Bessel function of the first kind (series). */
    inline double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double q = 0.25 * x * x;
        for (int k = 1; k < 64; ++k)
        {
            term *= q / (static_cast<double> (k) * k);
            sum += term;
            if (term < sum * 1e-17)
                break;
        }
        return sum;
    }
} // namespace detail

template <int HalfSpan, int PhasesPerSample = 1024>
struct SincBlep
{
    static_assert (HalfSpan >= 2 && HalfSpan <= 32);

    static constexpr int kHalfSpan = HalfSpan;
    static constexpr int kPhases = PhasesPerSample;
    static constexpr double kCutoff = 0.5;     // fraction of the sample rate (= Nyquist)
    static constexpr double kKaiserBeta = 9.0;
    static constexpr const char* kName = "sinc";

    /** Builds the tables. Call from a non-real-time context (prepare); the
        first call allocates temporary memory and takes a static-init lock. */
    static void initialise() { (void) tables(); }

    static float blep (double d) noexcept
    {
        // The table holds the continuous step integral S(d); subtracting the
        // ideal step analytically avoids interpolating across the jump at 0.
        return lookup (tables().stepIntegral, d) - (d >= 0.0 ? 1.0f : 0.0f);
    }

    static float blamp (double d) noexcept
    {
        // Ramp residual R(d) = Q(d) - max(0, d). It is continuous, and its kink
        // at d = 0 lies exactly on a table point, so linear interpolation never
        // straddles it.
        return lookup (tables().rampResidual, d);
    }

    static constexpr int kTableSize = 2 * HalfSpan * PhasesPerSample + 2; // +1 point, +1 guard

    struct Tables
    {
        std::array<float, kTableSize> stepIntegral {};
        std::array<float, kTableSize> rampResidual {};

        Tables()
        {
            // Integrate the kernel on a grid 8x finer than the table.
            constexpr int sub = 8;
            constexpr int fineCount = 2 * HalfSpan * PhasesPerSample * sub;
            constexpr double h = 1.0 / (PhasesPerSample * sub);
            const double i0Beta = detail::besselI0 (kKaiserBeta);

            auto kernel = [i0Beta] (double t) {
                const double x = 2.0 * kCutoff * t;
                const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
                const double r = t / HalfSpan;
                const double w = std::abs (r) >= 1.0 ? 0.0 : detail::besselI0 (kKaiserBeta * std::sqrt (1.0 - r * r)) / i0Beta;
                return 2.0 * kCutoff * sinc * w;
            };

            std::vector<double> S (fineCount + 1, 0.0), Q (fineCount + 1, 0.0);
            double prevK = kernel (-HalfSpan);
            for (int i = 1; i <= fineCount; ++i)
            {
                const double k = kernel (-HalfSpan + i * h);
                S[static_cast<std::size_t> (i)] = S[static_cast<std::size_t> (i - 1)] + 0.5 * h * (prevK + k);
                prevK = k;
            }
            const double total = S.back();
            for (auto& s : S)
                s /= total; // exact unit step: S(-H) = 0, S(H) = 1

            for (int i = 1; i <= fineCount; ++i)
                Q[static_cast<std::size_t> (i)] = Q[static_cast<std::size_t> (i - 1)]
                                                + 0.5 * h * (S[static_cast<std::size_t> (i - 1)] + S[static_cast<std::size_t> (i)]);

            for (int i = 0; i < kTableSize - 1; ++i)
            {
                const auto fi = static_cast<std::size_t> (i * sub);
                const double d = -HalfSpan + static_cast<double> (i) / PhasesPerSample;
                stepIntegral[static_cast<std::size_t> (i)] = static_cast<float> (S[fi]);
                rampResidual[static_cast<std::size_t> (i)] = static_cast<float> (Q[fi] - std::max (0.0, d));
            }
            stepIntegral[kTableSize - 1] = stepIntegral[kTableSize - 2];
            rampResidual[kTableSize - 1] = rampResidual[kTableSize - 2];
        }
    };

    static const Tables& tables()
    {
        static const Tables t;
        return t;
    }

private:
    static float lookup (const std::array<float, kTableSize>& table, double d) noexcept
    {
        const double x = std::clamp ((d + HalfSpan) * PhasesPerSample, 0.0,
                                     static_cast<double> (kTableSize - 2));
        const auto i = static_cast<std::size_t> (x);
        const auto frac = static_cast<float> (x - static_cast<double> (i));
        return table[i] + frac * (table[i + 1] - table[i]);
    }
};

} // namespace aurel::dsp::blep
