#include "DSP/Oscillators/BandlimitedOscillator.h"
#include "DSP/Oscillators/Noise.h"
#include "DSP/Oscillators/SubOscillator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace aurel::dsp;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    std::vector<float> renderSub (SubShape shape, int octaves, double inc, int n)
    {
        Oscillator master;
        SubOscillator<OscillatorKernel> sub;
        master.prepare();
        sub.prepare();
        sub.setShape (shape);
        sub.setOctave (octaves);
        master.reset();
        sub.reset();

        std::vector<float> out;
        for (int k = 0; k < n + SubOscillator<OscillatorKernel>::kLatency; ++k)
        {
            (void) master.process (inc);
            out.push_back (sub.process (master.getPhase(), master.getWrapFraction(), inc));
        }
        out.erase (out.begin(), out.begin() + SubOscillator<OscillatorKernel>::kLatency);
        return out;
    }
} // namespace

TEST_CASE ("Sub square is locked to the master, one or two octaves down", "[dsp][sub]")
{
    const int octaves = GENERATE (1, 2);
    const double div = octaves == 1 ? 2.0 : 4.0;
    constexpr double inc = 329.6275569 / 96000.0;
    constexpr int n = 20000;
    const int H = SubOscillator<OscillatorKernel>::kLatency;
    const auto x = renderSub (SubShape::Square, octaves, inc, n);

    int compared = 0;
    for (int k = 0; k < n; ++k)
    {
        const double sp = (k + 1) * inc / div; // sub phase, unwrapped
        const double frac = sp - std::floor (sp);
        const double distEdge = std::min ({ frac, std::abs (frac - 0.5), 1.0 - frac }) * div / inc;
        if (distEdge <= H + 0.5)
            continue;
        REQUIRE (x[static_cast<std::size_t> (k)] == (frac < 0.5 ? 1.0f : -1.0f));
        ++compared;
    }
    CHECK (compared > n * 3 / 4);
}

TEST_CASE ("Sub sine is an exact sine one or two octaves down", "[dsp][sub]")
{
    const int octaves = GENERATE (1, 2);
    const double div = octaves == 1 ? 2.0 : 4.0;
    constexpr double inc = 440.0 / 96000.0;
    const auto x = renderSub (SubShape::Sine, octaves, inc, 10000);
    for (std::size_t k = 0; k < x.size(); ++k)
        REQUIRE_THAT (x[k], WithinAbs (std::sin (2.0 * std::numbers::pi * (k + 1.0) * inc / div), 1e-5));
}

TEST_CASE ("White noise: zero mean, saw-equal RMS, uncorrelated, deterministic", "[dsp][noise]")
{
    NoiseGenerator a, b, c;
    a.prepare (96000.0, 1234, 1.0);
    b.prepare (96000.0, 1234, 1.0);
    c.prepare (96000.0, 99, 1.0);

    constexpr int n = 1 << 20;
    double sum = 0.0, sq = 0.0, lag = 0.0, prev = 0.0;
    bool sameAsB = true, sameAsC = true;
    for (int i = 0; i < n; ++i)
    {
        const double x = a.process();
        sameAsB = sameAsB && (static_cast<float> (x) == b.process());
        sameAsC = sameAsC && (static_cast<float> (x) == c.process());
        sum += x;
        sq += x * x;
        lag += x * prev;
        prev = x;
    }
    CHECK (std::abs (sum / n) < 0.005);
    CHECK_THAT (std::sqrt (sq / n), WithinRel (1.0 / std::sqrt (3.0), 0.01));
    CHECK (std::abs (lag / sq) < 0.01);
    CHECK (sameAsB);
    CHECK_FALSE (sameAsC);
}

TEST_CASE ("Pink filter slope is -3.01 dB/octave from 20 Hz to 20 kHz", "[dsp][noise]")
{
    const double rate = GENERATE (88200.0, 96000.0, 192000.0);
    PinkFilter f;
    f.prepare (rate);

    const auto n = static_cast<int> (rate * 2.0);
    std::vector<double> h (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        h[static_cast<std::size_t> (i)] = f.process (i == 0 ? 1.0 : 0.0);

    auto levelDb = [&] (double hz) {
        std::complex<double> acc {};
        const double w = -2.0 * std::numbers::pi * hz / rate;
        for (int i = 0; i < n; ++i)
            acc += h[static_cast<std::size_t> (i)] * std::polar (1.0, w * i);
        return 20.0 * std::log10 (std::abs (acc));
    };

    // Compare to the ideal -10*log10(f) line through the band's mean.
    std::vector<double> freqs, dev;
    for (double hz = 20.0; hz <= 20000.0; hz *= std::sqrt (2.0))
        freqs.push_back (hz);
    double meanOffset = 0.0;
    for (double hz : freqs)
    {
        dev.push_back (levelDb (hz) + 10.0 * std::log10 (hz));
        meanOffset += dev.back();
    }
    meanOffset /= static_cast<double> (dev.size());

    for (std::size_t i = 0; i < freqs.size(); ++i)
    {
        INFO ("rate " << rate << ", " << freqs[i] << " Hz: deviation " << dev[i] - meanOffset << " dB");
        CHECK (std::abs (dev[i] - meanOffset) < 0.5);
    }
}

TEST_CASE ("Pink noise RMS matches white noise RMS", "[dsp][noise]")
{
    constexpr double rate = 96000.0;
    NoiseGenerator g;
    g.prepare (rate, 777, NoiseGenerator::computePinkGain (rate));
    g.setColor (NoiseColor::Pink);
    constexpr int n = 1 << 21;
    double sq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = g.process();
        if (i >= 96000)
            sq += x * x;
    }
    CHECK_THAT (std::sqrt (sq / (n - 96000)), WithinRel (1.0 / std::sqrt (3.0), 0.05));
}
