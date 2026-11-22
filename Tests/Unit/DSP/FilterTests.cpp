#include "Support/AllocationCounter.h"

#include "DSP/Core/Denormals.h"
#include "DSP/Filters/LadderFilter.h"
#include "DSP/Filters/StateVariableFilter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>
#include <functional>
#include <numbers>
#include <vector>

using namespace aurel::dsp;
using cd = std::complex<double>;

namespace
{
    constexpr double kRate = 96000.0;

    /** Frequency response from the impulse response (scaled impulse for the
        nonlinear ladder, so it stays in its linear region). The response is
        recorded until its tail has decayed below 1e-14 of the peak (up to
        maxLength), so high-Q resonances are not truncated. */
    std::vector<cd> measureResponse (const std::function<double (double)>& process, double impulse,
                                     const std::vector<double>& freqs, int maxLength)
    {
        std::vector<double> h;
        h.reserve (static_cast<std::size_t> (maxLength));
        double peak = 0.0;
        int quiet = 0;
        for (int n = 0; n < maxLength && quiet < 4096; ++n)
        {
            const double v = process (n == 0 ? impulse : 0.0) / impulse;
            h.push_back (v);
            peak = std::max (peak, std::abs (v));
            quiet = std::abs (v) < 1e-14 * peak ? quiet + 1 : 0;
        }
        const auto length = static_cast<int> (h.size());

        std::vector<cd> H;
        for (const double f : freqs)
        {
            cd acc {};
            const double w = -2.0 * std::numbers::pi * f / kRate;
            for (int n = 0; n < length; ++n)
                acc += h[static_cast<std::size_t> (n)] * std::polar (1.0, w * n);
            H.push_back (acc);
        }
        return H;
    }

    /** Analog prototype variable at digital frequency f (bilinear, pre-warped). */
    cd sOf (double f, double g) { return cd (0.0, std::tan (std::numbers::pi * f / kRate) / g); }

    double db (cd v) { return 20.0 * std::log10 (std::abs (v)); }

    std::vector<double> testFrequencies()
    {
        std::vector<double> f;
        for (double x = 20.0; x < 40000.0; x *= 1.31)
            f.push_back (x);
        return f;
    }

    void requireMatch (const std::vector<cd>& measured, const std::vector<cd>& expected,
                       const std::vector<double>& freqs)
    {
        for (std::size_t i = 0; i < freqs.size(); ++i)
        {
            if (db (expected[i]) < -100.0)
                continue; // deep stopband: limited by impulse length / float precision
            INFO ("f = " << freqs[i] << " Hz: measured " << db (measured[i]) << " dB, expected " << db (expected[i]) << " dB");
            REQUIRE (std::abs (db (measured[i]) - db (expected[i])) < 0.01);
        }
    }
} // namespace

// --- State-variable filter ----------------------------------------------------

TEST_CASE ("SVF matches the analytic LP/BP/HP/notch response", "[dsp][filter][svf]")
{
    const double fc = GENERATE (100.0, 1000.0, 10000.0, 30000.0);
    const double res = GENERATE (0.0, 0.5, 0.9);
    const auto freqs = testFrequencies();

    StateVariableFilter f;
    f.prepare (kRate);
    f.setParameters (fc, res);
    const double g = f.getG(), k = f.getDamping();

    for (const int output : { 0, 1, 2, 3 })
    {
        f.reset();
        const auto measured = measureResponse ([&] (double x) {
            const auto o = f.process (x);
            switch (output)
            {
                case 0: return o.lp;
                case 1: return o.bp;
                case 2: return o.hp;
                default: return o.lp + o.hp; // notch
            }
        }, 1.0, freqs, static_cast<int> (kRate));

        std::vector<cd> expected;
        for (const double fr : freqs)
        {
            const cd s = sOf (fr, g);
            const cd den = s * s + k * s + 1.0;
            const cd num = output == 0 ? cd (1.0) : output == 1 ? s : output == 2 ? s * s : s * s + 1.0;
            expected.push_back (num / den);
        }
        INFO ("fc " << fc << ", res " << res << ", output " << output);
        requireMatch (measured, expected, freqs);
    }
}

TEST_CASE ("SVF morph endpoints are LP, unity-peak BP, HP and notch", "[dsp][filter][svf]")
{
    StateVariableFilter a, b;
    a.prepare (kRate);
    b.prepare (kRate);
    a.setParameters (2000.0, 0.7);
    b.setParameters (2000.0, 0.7);
    for (int n = 0; n < 1000; ++n)
    {
        const double x = std::sin (0.05 * n) + (n % 37 == 0 ? 1.0 : 0.0);
        const auto o = a.process (x);
        (void) b.process (x);
        REQUIRE (a.morph (o, 0.0) == o.lp);
        REQUIRE (std::abs (a.morph (o, 1.0) - a.getDamping() * o.bp) < 1e-15);
        REQUIRE (a.morph (o, 2.0) == o.hp);
        REQUIRE (std::abs (a.morph (o, 3.0) - (o.lp + o.hp)) < 1e-15);
    }
}

// --- Ladder ---------------------------------------------------------------------

TEST_CASE ("Ladder small-signal response matches the analytic ladder", "[dsp][filter][ladder]")
{
    const auto solver = GENERATE (LadderFilter::Solver::Linearised, LadderFilter::Solver::Newton);
    const auto mode = GENERATE (LadderFilter::Mode::LowPass24, LadderFilter::Mode::LowPass12);
    const double fc = GENERATE (100.0, 1000.0, 10000.0, 30000.0);
    const double k = GENERATE (0.0, 2.0, 3.9);
    const double comp = GENERATE (0.0, 1.0);
    const auto freqs = testFrequencies();

    LadderFilter f;
    f.prepare (kRate);
    f.setSolver (solver);
    f.setMode (mode);
    f.setParameters (fc, k, 1.0, comp);
    const double g = f.getG();

    // 1e-7 impulse: tanh(v) = v to ~1e-15 relative, i.e. linear.
    const auto measured = measureResponse ([&] (double x) { return f.process (x); }, 1e-7, freqs,
                                           static_cast<int> (kRate * 30));
    std::vector<cd> expected;
    for (const double fr : freqs)
    {
        const cd s = sOf (fr, g);
        const cd p = (1.0 + s) * (1.0 + s);
        const cd den = p * p + k;
        expected.push_back ((1.0 + comp * k) * (mode == LadderFilter::Mode::LowPass24 ? cd (1.0) : p) / den);
    }
    INFO ("solver " << static_cast<int> (solver) << ", mode " << static_cast<int> (mode)
                    << ", fc " << fc << ", k " << k << ", comp " << comp);
    requireMatch (measured, expected, freqs);
}

TEST_CASE ("Ladder stability matrix: bounded, finite output everywhere", "[dsp][filter][ladder][stability]")
{
    const ScopedFlushDenormals ftz;
    const auto solver = GENERATE (LadderFilter::Solver::Linearised, LadderFilter::Solver::Newton);
    const double k = GENERATE (0.0, 3.0, 4.0, 4.5, 6.0);
    const double drive = GENERATE (0.25, 1.0, 4.0, 16.0);

    for (const double fc : { 20.0, 200.0, 2000.0, 20000.0, 43000.0 })
    {
        LadderFilter f;
        f.prepare (kRate);
        f.setSolver (solver);
        f.setParameters (fc, k, drive, 0.5);

        double peak = 0.0;
        for (int n = 0; n < 24000; ++n)
        {
            // Full-scale saw, then a +12 dB burst, then silence.
            const double phase = std::fmod (n * 110.0 / kRate, 1.0);
            const double x = n < 12000 ? 2.0 * phase - 1.0 : (n < 18000 ? 4.0 * (2.0 * phase - 1.0) : 0.0);
            const double y = f.process (x);
            REQUIRE (std::isfinite (y));
            peak = std::max (peak, std::abs (y));
        }
        INFO ("solver " << static_cast<int> (solver) << ", k " << k << ", drive " << drive << ", fc " << fc
                        << ": peak " << peak);
        // The tanh stages bound the model's internal state (|y| < ~3); the
        // output gain is at most (1 + 0.5 * 4) / 0.5 = 6 here.
        CHECK (peak < 20.0);
    }
}

TEST_CASE ("Ladder survives audio-rate cutoff modulation at full resonance", "[dsp][filter][ladder][stability]")
{
    const ScopedFlushDenormals ftz;
    const auto solver = GENERATE (LadderFilter::Solver::Linearised, LadderFilter::Solver::Newton);
    LadderFilter f;
    f.prepare (kRate);
    f.setSolver (solver);
    double peak = 0.0;
    for (int n = 0; n < 96000; ++n)
    {
        // Cutoff sweeps 20 Hz <-> 20 kHz (10 octaves) at 1 kHz.
        const double fc = 20.0 * std::pow (1000.0, 0.5 + 0.5 * std::sin (2.0 * std::numbers::pi * 1000.0 * n / kRate));
        f.setParameters (fc, 5.0, 4.0, 1.0);
        const double x = 2.0 * std::fmod (n * 55.0 / kRate, 1.0) - 1.0;
        const double y = f.process (x);
        REQUIRE (std::isfinite (y));
        peak = std::max (peak, std::abs (y));
    }
    INFO ("peak " << peak);
    CHECK (peak < 20.0);
}

TEST_CASE ("SVF survives audio-rate cutoff modulation at maximum resonance", "[dsp][filter][svf][stability]")
{
    const ScopedFlushDenormals ftz;
    StateVariableFilter f;
    f.prepare (kRate);
    double peak = 0.0;
    for (int n = 0; n < 96000; ++n)
    {
        const double fc = 20.0 * std::pow (1000.0, 0.5 + 0.5 * std::sin (2.0 * std::numbers::pi * 1000.0 * n / kRate));
        f.setParameters (fc, 1.0);
        const auto o = f.process (2.0 * std::fmod (n * 55.0 / kRate, 1.0) - 1.0);
        REQUIRE (std::isfinite (o.lp + o.bp + o.hp));
        peak = std::max ({ peak, std::abs (o.lp), std::abs (o.hp) });
    }
    INFO ("peak " << peak);
    CHECK (peak < 1000.0); // Q = 200 resonance under 10-octave sweeps; TPT keeps it bounded
}

TEST_CASE ("DIAG ladder near Nyquist", "[.diag]")
{
    for (const auto solver : { LadderFilter::Solver::Linearised, LadderFilter::Solver::Newton })
    {
        for (const double fc : { 20000.0, 30000.0, 36000.0, 40000.0, 43000.0 })
        {
            LadderFilter f;
            f.prepare (kRate);
            f.setSolver (solver, 50);
            f.setParameters (fc, 6.0, 1.0, 0.5);
            double peak = 0.0;
            int maxIt = 0, peakAt = 0;
            for (int n = 0; n < 24000; ++n)
            {
                const double phase = std::fmod (n * 110.0 / kRate, 1.0);
                const double x = n < 12000 ? 2.0 * phase - 1.0 : (n < 18000 ? 4.0 * (2.0 * phase - 1.0) : 0.0);
                const double y = f.process (x);
                if (std::abs (y) > peak) { peak = std::abs (y); peakAt = n; }
                maxIt = std::max (maxIt, f.getLastIterations());
            }
            std::printf ("solver %d fc %6.0f g %6.3f: peak %8.3f at %5d, max iterations %d, non-converged %lld\n",
                         static_cast<int> (solver), fc, f.getG(), peak, peakAt, maxIt, f.getNonConvergedCount());
        }
    }
}

TEST_CASE ("Ladder Newton solver converges within its iteration limit", "[dsp][filter][ladder]")
{
    const ScopedFlushDenormals ftz;
    LadderFilter f;
    f.prepare (kRate);
    f.setSolver (LadderFilter::Solver::Newton, 16);
    int worst = 0;
    long long total = 0;
    constexpr int n = 96000;
    for (int i = 0; i < n; ++i)
    {
        // 50 Hz .. 43 kHz ramps (up to the cutoff limit, g ~ 6), hard drive and feedback.
        const double fc = 50.0 * std::pow (860.0, std::fmod (i / 24000.0, 1.0));
        f.setParameters (fc, 6.0, 16.0, 1.0);
        (void) f.process (2.0 * std::fmod (i * 98.0 / kRate, 1.0) - 1.0);
        worst = std::max (worst, f.getLastIterations());
        total += f.getLastIterations();
    }
    INFO ("worst " << worst << " iterations, mean " << static_cast<double> (total) / n);
    CHECK (f.getNonConvergedCount() == 0); // residual < 1e-10 every sample
    CHECK (worst < 16);
}

namespace
{
    /** Self-oscillation frequency from rising zero crossings of the tail. */
    double selfOscillationHz (double rate, double fc, double k, double& amplitude)
    {
        LadderFilter f;
        f.prepare (rate);
        f.setParameters (fc, k, 1.0, 0.0);
        // Build-up rate scales with the cutoff: low cutoffs need longer to settle.
        const double seconds = fc < 200.0 ? 16.0 : 4.0;
        const auto total = static_cast<int> (rate * seconds);
        const auto start = total / 2;
        double prev = 0.0, first = -1.0, last = -1.0, sq = 0.0;
        int crossings = 0;
        for (int n = 0; n < total; ++n)
        {
            const double y = f.process (n == 0 ? 0.01 : 0.0);
            if (n > start)
            {
                sq += y * y;
                if (prev < 0.0 && y >= 0.0)
                {
                    const double t = (n - 1) + prev / (prev - y);
                    if (first < 0.0)
                        first = t;
                    last = t;
                    ++crossings;
                }
            }
            prev = y;
        }
        amplitude = std::sqrt (2.0 * sq / (total - start));
        return (crossings - 1) * rate / (last - first);
    }
} // namespace

TEST_CASE ("Self-oscillating ladder tracks the requested pitch", "[dsp][filter][ladder][tuning]")
{
    // With the measured correction table, the self-oscillation pitch tracks
    // the cutoff across the musical range, resonance amounts and sample rates.
    const double rate = GENERATE (88200.0, 96000.0, 192000.0);
    const double k = GENERATE (4.15, 4.5, 5.5);
    for (const double fc : { 55.0, 220.0, 880.0, 3520.0, 7040.0 })
    {
        double amplitude = 0.0;
        const double cents = 1200.0 * std::log2 (selfOscillationHz (rate, fc, k, amplitude) / fc);
        INFO ("rate " << rate << ", k " << k << ", cutoff " << fc << " Hz: " << cents << " cents, amplitude " << amplitude);
        CHECK (std::abs (cents) < 4.0);
        CHECK (amplitude > 0.05); // really oscillating
    }
}

TEST_CASE ("Ladder with full bass compensation has unity DC gain at any resonance", "[dsp][filter][ladder]")
{
    for (const double k : { 0.0, 1.0, 3.0, 3.9 })
    {
        LadderFilter f;
        f.prepare (kRate);
        f.setParameters (1000.0, k, 1.0, 1.0);
        double y = 0.0;
        for (int i = 0; i < 96000; ++i)
            y = f.process (1e-6);
        INFO ("k " << k);
        CHECK (std::abs (y / 1e-6 - 1.0) < 1e-6);
    }
}

TEST_CASE ("Filters do not allocate while processing", "[dsp][filter][rt-safety]")
{
    LadderFilter ladder;
    StateVariableFilter svf;
    ladder.prepare (kRate);
    svf.prepare (kRate);
    ladder.setSolver (LadderFilter::Solver::Newton);

    std::size_t allocs = 0;
    double sink = 0.0;
    {
        const aurel::test::ScopedAllocationCounter counter;
        for (int i = 0; i < 10000; ++i)
        {
            ladder.setParameters (300.0 + i, 4.2, 2.0, 0.5);
            svf.setParameters (300.0 + i, 0.8);
            const double x = std::sin (0.01 * i);
            sink += ladder.process (x) + svf.process (x).lp;
        }
        allocs = counter.allocations();
    }
    CHECK (allocs == 0);
    CHECK (std::isfinite (sink));
}
