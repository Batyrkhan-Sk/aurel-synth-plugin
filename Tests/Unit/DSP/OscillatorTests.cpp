#include "Support/AllocationCounter.h"

#include "DSP/Oscillators/BandlimitedOscillator.h"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numeric>
#include <vector>

using namespace aurel::dsp;
using Catch::Matchers::WithinAbs;

namespace
{
    template <class K>
    std::vector<float> render (Waveform w, double inc, int n, double pw = 0.5)
    {
        BandlimitedOscillator<K> osc;
        osc.prepare();
        osc.setWaveform (w);
        osc.setPulseWidth (pw);
        for (int i = 0; i < BandlimitedOscillator<K>::kLatency; ++i)
            (void) osc.process (inc);
        std::vector<float> out (static_cast<std::size_t> (n));
        for (auto& s : out)
            s = osc.process (inc);
        return out;
    }

    double mean (const std::vector<float>& x, std::size_t from, std::size_t count)
    {
        return std::accumulate (x.begin() + static_cast<std::ptrdiff_t> (from),
                                x.begin() + static_cast<std::ptrdiff_t> (from + count), 0.0)
             / static_cast<double> (count);
    }
} // namespace

// --- Kernel properties ------------------------------------------------------

TEMPLATE_TEST_CASE ("BLEP kernels: shape and symmetry", "[dsp][osc][kernel]",
                    blep::PolyBlep2, blep::SincBlep<4>, blep::SincBlep<8>, blep::SincBlep<12>)
{
    TestType::initialise();
    constexpr double H = TestType::kHalfSpan;
    constexpr double tol = 2e-5;

    // Correction vanishes at both ends of the support.
    CHECK_THAT (TestType::blep (-H), WithinAbs (0.0, tol));
    CHECK_THAT (TestType::blep (H - 1e-9), WithinAbs (0.0, tol));
    CHECK_THAT (TestType::blamp (-H), WithinAbs (0.0, tol));
    CHECK_THAT (TestType::blamp (H - 1e-9), WithinAbs (0.0, tol));

    // At the discontinuity the band-limited step sits at the midpoint.
    CHECK_THAT (TestType::blep (-1e-9), WithinAbs (0.5, tol));
    CHECK_THAT (TestType::blep (0.0), WithinAbs (-0.5, tol));

    // Linear-phase kernels: BLEP residual is odd, BLAMP residual is even.
    for (double d = 0.013; d < H; d += 0.137)
    {
        INFO ("d = " << d);
        REQUIRE_THAT (TestType::blep (-d), WithinAbs (-TestType::blep (d), tol));
        REQUIRE_THAT (TestType::blamp (-d), WithinAbs (TestType::blamp (d), tol));
    }
}

TEST_CASE ("Naive kernel applies no correction", "[dsp][osc][kernel]")
{
    CHECK (blep::Naive::kHalfSpan == 0);
    CHECK (blep::Naive::blep (0.3) == 0.0f);
    CHECK (blep::Naive::blamp (-0.3) == 0.0f);
}

// --- Oscillator ---------------------------------------------------------------

TEMPLATE_TEST_CASE ("Band-limited output equals the naive waveform away from discontinuities",
                    "[dsp][osc]", blep::PolyBlep2, blep::SincBlep<4>, blep::SincBlep<8>)
{
    const auto wave = GENERATE (Waveform::Saw, Waveform::Pulse, Waveform::Triangle);
    constexpr double inc = 261.6255653005986 / 96000.0; // non-integer period
    constexpr double pw = 0.3;
    constexpr int n = 4000;
    const int H = TestType::kHalfSpan;

    const auto naive = render<blep::Naive> (wave, inc, n, pw);
    const auto bl = render<TestType> (wave, inc, n, pw);

    // Sample k has phase (k+1)*inc. Breakpoints: 0 (all), pw (pulse), 0.5 (triangle).
    auto distanceToBreakpoint = [&] (int k) {
        const double t = (k + 1) * inc; // phase in cycles, unwrapped
        double best = 1e9;
        for (double b : { 0.0, pw, 0.5 })
        {
            const double cycles = t - b;
            const double nearest = std::round (cycles);
            best = std::min (best, std::abs (cycles - nearest) / inc); // in samples
        }
        return best;
    };

    int compared = 0;
    for (int k = 0; k < n; ++k)
    {
        if (distanceToBreakpoint (k) <= H + 0.5)
            continue;
        INFO ("sample " << k);
        REQUIRE_THAT (bl[static_cast<std::size_t> (k)],
                      WithinAbs (naive[static_cast<std::size_t> (k)], 1e-5));
        ++compared;
    }
    CHECK (compared > n / 2); // the comparison really covered most samples
}

TEMPLATE_TEST_CASE ("Oscillator DC, bounds and pulse width", "[dsp][osc]",
                    blep::PolyBlep2, blep::SincBlep<8>)
{
    // 96 kHz / 480 = 200 Hz: exactly 480 samples per cycle, so whole periods.
    constexpr double inc = 1.0 / 480.0;
    constexpr std::size_t period = 480;

    for (const auto wave : { Waveform::Saw, Waveform::Pulse, Waveform::Triangle, Waveform::Sine })
    {
        const auto x = render<TestType> (wave, inc, static_cast<int> (period * 20));
        INFO ("waveform " << static_cast<int> (wave));
        CHECK_THAT (mean (x, period, period * 10), WithinAbs (0.0, 1e-4));
        // A band-limited step overshoots by ~8.95% of the *jump* (Gibbs). The
        // saw/pulse jump is 2, so peaks near 1.18 are correct, not a defect.
        // Gain staging must budget for this headroom.
        for (const float s : x)
            REQUIRE (std::abs (s) <= 1.2f);
    }

    for (const double pw : { 0.1, 0.25, 0.75 })
    {
        const auto x = render<TestType> (Waveform::Pulse, inc, static_cast<int> (period * 20), pw);
        INFO ("pulse width " << pw);
        CHECK_THAT (mean (x, period, period * 10), WithinAbs (2.0 * pw - 1.0, 1e-4));
    }
}

TEST_CASE ("Sine oscillator is an exact sine", "[dsp][osc]")
{
    constexpr double inc = 440.0 / 48000.0;
    const auto x = render<blep::SincBlep<8>> (Waveform::Sine, inc, 1000);
    for (std::size_t k = 0; k < x.size(); ++k)
        REQUIRE_THAT (x[k], WithinAbs (std::sin (2.0 * std::numbers::pi * inc * static_cast<double> (k + 1)), 1e-6));
}

TEST_CASE ("Oscillator processing does not allocate", "[dsp][osc][rt-safety]")
{
    BandlimitedOscillator<blep::SincBlep<8>> osc;
    osc.prepare(); // builds tables (allowed to allocate here)

    std::size_t allocs = 0;
    float sink = 0.0f;
    {
        const aurel::test::ScopedAllocationCounter counter;
        for (const auto wave : { Waveform::Saw, Waveform::Pulse, Waveform::Triangle })
        {
            osc.setWaveform (wave);
            for (int i = 0; i < 10000; ++i)
                sink += osc.process (0.0371);
        }
        allocs = counter.allocations();
    }
    CHECK (allocs == 0);
    CHECK (std::isfinite (sink));
}
