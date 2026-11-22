#include "DSP/Oversampling/Halfband.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace aurel::dsp;

namespace
{
    constexpr double kAttenuationDb = 110.0;
    constexpr double kPassbandEdge = 20000.0;

    /** Amplitude of the decimated output at `fOut` for an input sine at
        `fIn` (both integer Hz -> exact DFT over a 1 s window, no leakage). */
    double decimatedAmplitude (const halfband::Design& d, int inRate, int fIn, int fOut)
    {
        HalfbandDecimator dec;
        dec.setDesign (d);
        const int outRate = inRate / 2;
        const int settle = outRate / 2;
        double re = 0.0, im = 0.0;
        for (int n = 0; n < settle + outRate; ++n)
        {
            const auto x0 = static_cast<float> (std::sin (2.0 * std::numbers::pi * fIn * (2.0 * n) / inRate));
            const auto x1 = static_cast<float> (std::sin (2.0 * std::numbers::pi * fIn * (2.0 * n + 1.0) / inRate));
            const double y = dec.process (x0, x1);
            if (n >= settle)
            {
                const double w = 2.0 * std::numbers::pi * fOut * (n - settle) / outRate;
                re += y * std::cos (w);
                im += y * std::sin (w);
            }
        }
        return 2.0 * std::sqrt (re * re + im * im) / outRate;
    }

    double db (double a) { return 20.0 * std::log10 (std::max (a, 1e-300)); }
} // namespace

TEST_CASE ("Half-band design: coefficients are valid all-pass coefficients", "[dsp][halfband]")
{
    for (const int outRate : { 44100, 48000 })
    {
        const double transition = (outRate / 2.0 - kPassbandEdge) / (2.0 * outRate);
        const auto d = halfband::design (kAttenuationDb, transition);
        INFO ("output rate " << outRate << ": " << d.count << " coefficients");
        CHECK (d.count >= 4);
        CHECK (d.count < halfband::kMaxCoefficients);
        for (int i = 0; i < d.count; ++i)
        {
            const double c = d.coefficients[static_cast<std::size_t> (i)];
            CHECK (c > 0.0);
            CHECK (c < 1.0); // stable first-order all-pass
            if (i > 0)
                CHECK (c > d.coefficients[static_cast<std::size_t> (i - 1)]); // ascending
        }
    }
}

TEST_CASE ("Half-band decimator: passband flat to 20 kHz, >= 100 dB stopband", "[dsp][halfband]")
{
    const int outRate = GENERATE (44100, 48000);
    const int inRate = outRate * 2;
    const double transition = (outRate / 2.0 - kPassbandEdge) / inRate;
    const auto d = halfband::design (kAttenuationDb, transition);

    for (const int f : { 50, 1000, 5000, 10000, 15000, 19000, 20000 })
    {
        const double g = db (decimatedAmplitude (d, inRate, f, f));
        INFO ("rate " << outRate << ", passband " << f << " Hz: " << g << " dB");
        CHECK (std::abs (g) < 0.01);
    }

    // Input components that would fold into 0..20 kHz after decimation.
    for (const int fIn : { outRate - 20000, outRate - 15000, outRate - 5000, outRate - 1000, outRate - 1 })
    {
        const int fOut = outRate - fIn;
        const double g = db (decimatedAmplitude (d, inRate, fIn, fOut));
        INFO ("rate " << outRate << ", input " << fIn << " Hz -> alias at " << fOut << " Hz: " << g << " dB");
        CHECK (g < -100.0);
    }
}

TEST_CASE ("Half-band decimator: DC gain is exactly 1 and the filter is stable", "[dsp][halfband]")
{
    const auto d = halfband::design (kAttenuationDb, (24000.0 - kPassbandEdge) / 96000.0);
    HalfbandDecimator dec;
    dec.setDesign (d);
    float y = 0.0f;
    for (int i = 0; i < 20000; ++i)
        y = dec.process (1.0f, 1.0f);
    CHECK (std::abs (y - 1.0f) < 1e-6f);

    // Impulse response decays to (numerically) zero.
    dec.reset();
    (void) dec.process (0.0f, 1.0f);
    float tail = 0.0f;
    for (int i = 0; i < 20000; ++i)
        tail = std::max (tail, i > 10000 ? std::abs (dec.process (0.0f, 0.0f)) : (dec.process (0.0f, 0.0f), 0.0f));
    CHECK (tail < 1e-12f);
}
