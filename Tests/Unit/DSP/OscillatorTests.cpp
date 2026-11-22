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
        osc.reset();
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

namespace
{
    using Osc6 = BandlimitedOscillator<blep::SincBlep<6>>;
    constexpr int kH = Osc6::kLatency;

    /** Distance in samples from time `t` (in samples) to the nearest event time. */
    double distanceTo (const std::vector<double>& eventTimes, double t)
    {
        double best = 1e9;
        for (double e : eventTimes)
            best = std::min (best, std::abs (e - t));
        return best;
    }
} // namespace

TEMPLATE_TEST_CASE ("Backward phase (through-zero FM) equals naive away from discontinuities",
                    "[dsp][osc][fm]", blep::PolyBlep2, blep::SincBlep<6>)
{
    const auto wave = GENERATE (Waveform::Saw, Waveform::Pulse, Waveform::Triangle);
    constexpr double inc = -523.2511306011972 / 96000.0;
    constexpr double pw = 0.3;
    constexpr int n = 4000;
    const int H = TestType::kHalfSpan;

    const auto bl = render<TestType> (wave, inc, n, pw);

    int compared = 0;
    for (int k = 0; k < n; ++k)
    {
        const double t = (k + 1) * inc;
        double dist = 1e9;
        for (double b : { 0.0, pw, 0.5 })
            dist = std::min (dist, std::abs ((t - b) - std::round (t - b)) / std::abs (inc));
        if (dist <= H + 0.5)
            continue;
        const double p = t - std::floor (t);
        INFO ("sample " << k);
        REQUIRE_THAT (bl[static_cast<std::size_t> (k)],
                      WithinAbs (Osc6::naiveValue (wave, p, pw), 1e-5));
        ++compared;
    }
    CHECK (compared > n / 2);
}

TEST_CASE ("Hard sync equals the analytic synced waveform away from events", "[dsp][osc][sync]")
{
    const auto wave = GENERATE (Waveform::Saw, Waveform::Pulse, Waveform::Triangle, Waveform::Sine);
    constexpr double incMaster = 440.0 / 96000.0;
    constexpr double incSlave = 1013.7 / 96000.0; // non-integer ratio, ~2.3 slave cycles per sync
    constexpr double pw = 0.4;
    constexpr int n = 6000;

    Osc6 master, slave;
    master.prepare();
    slave.prepare();
    master.setWaveform (Waveform::Saw);
    slave.setWaveform (wave);
    slave.setPulseWidth (pw);
    master.reset();
    slave.reset();

    // Time convention: phase 0 at time 0; call k covers time (k, k+1]; the
    // undelayed output of call k is the value at time k + 1.
    std::vector<float> out;
    std::vector<double> syncs;
    for (int k = 0; k < n + kH; ++k)
    {
        (void) master.process (incMaster);
        const double f = master.getWrapFraction();
        if (f > 0.0)
            syncs.push_back (k + f);
        out.push_back (slave.process (incSlave, f));
    }
    REQUIRE (syncs.size() > 10);

    // Every event of the undelayed slave: sync resets plus its own breakpoints
    // (phase (t - lastSync) * incSlave crossing 0, pw, 0.5) between resets.
    std::vector<double> events = syncs;
    std::vector<double> segmentStarts { 0.0 };
    segmentStarts.insert (segmentStarts.end(), syncs.begin(), syncs.end());
    for (std::size_t i = 0; i < segmentStarts.size(); ++i)
    {
        const double start = segmentStarts[i];
        const double end = i + 1 < segmentStarts.size() ? segmentStarts[i + 1] : n + kH + 1.0;
        for (double cycles = 0.0; start + cycles / incSlave < end; cycles += 1.0)
            for (double b : { 0.0, pw, 0.5 })
                events.push_back (start + (cycles + b) / incSlave);
    }

    int compared = 0;
    for (int k = 0; k < n; ++k)
    {
        const double t = k + 1.0;
        if (distanceTo (events, t) <= kH + 0.5)
            continue;
        const double lastWrap = std::floor (t * incMaster) / incMaster; // last master wrap <= t
        const double since = t - lastWrap;
        const double p = since * incSlave - std::floor (since * incSlave);
        INFO ("sample " << k << " waveform " << static_cast<int> (wave));
        REQUIRE_THAT (out[static_cast<std::size_t> (k + kH)], WithinAbs (Osc6::naiveValue (wave, p, pw), 2e-5));
        ++compared;
    }
    CHECK (compared > n / 3);
}

TEST_CASE ("Waveform switch is band-limited and lands on the new waveform", "[dsp][osc]")
{
    constexpr double inc = 233.08 / 96000.0;
    constexpr int switchAt = 1000;
    Osc6 osc;
    osc.prepare();
    osc.setWaveform (Waveform::Saw);
    osc.reset();

    std::vector<float> out;
    for (int k = 0; k < 3000 + kH; ++k)
    {
        if (k == switchAt)
            osc.setWaveform (Waveform::Triangle);
        out.push_back (osc.process (inc));
    }

    // The switch is applied at the end of call `switchAt`, i.e. at time switchAt + 1.
    int before = 0, after = 0;
    for (int k = 0; k < 3000; ++k)
    {
        const double t = (k + 1) * inc;
        const double p = t - std::floor (t);
        const auto y = out[static_cast<std::size_t> (k + kH)];
        const bool nearCorner = std::min ({ std::abs (p - 0.5), p, 1.0 - p }) / inc <= kH + 1;
        if (k < switchAt - kH - 1 && std::min (p, 1.0 - p) / inc > kH + 1)
        {
            REQUIRE_THAT (y, WithinAbs (Osc6::naiveValue (Waveform::Saw, p, 0.5), 1e-5));
            ++before;
        }
        else if (k > switchAt + kH + 1 && ! nearCorner)
        {
            REQUIRE_THAT (y, WithinAbs (Osc6::naiveValue (Waveform::Triangle, p, 0.5), 1e-5));
            ++after;
        }
    }
    // The correction itself uses the kernel validated above; its effect is
    // confined to +/-H samples around the switch, which this checks.
    CHECK (before > 500);
    CHECK (after > 1000);
}

TEST_CASE ("Pulse-width modulation stays exact away from edges", "[dsp][osc][pwm]")
{
    constexpr double inc = 196.0 / 96000.0;
    constexpr int n = 20000;
    Osc6 osc;
    osc.prepare();
    osc.setWaveform (Waveform::Pulse);
    auto pwAt = [] (int k) { return 0.5 + 0.4 * std::sin (2.0 * std::numbers::pi * 3.0 * k / 96000.0); };
    osc.setPulseWidth (pwAt (-1));
    osc.reset();

    std::vector<float> out;
    for (int k = 0; k < n + kH; ++k)
    {
        osc.setPulseWidth (pwAt (k));
        out.push_back (osc.process (inc));
    }

    int compared = 0;
    for (int k = 0; k < n; ++k)
    {
        const double t = (k + 1) * inc;
        const double p = t - std::floor (t);
        const double pw = pwAt (k);
        const double distEdge = std::min ({ std::abs (p - pw), p, 1.0 - p }) / inc;
        if (distEdge <= kH + 1.5)
            continue;
        REQUIRE_THAT (out[static_cast<std::size_t> (k + kH)], WithinAbs (Osc6::naiveValue (Waveform::Pulse, p, pw), 1e-5));
        ++compared;
    }
    CHECK (compared > n / 2);
}

TEST_CASE ("Wrap fraction reports the exact forward wrap time", "[dsp][osc]")
{
    constexpr double inc = 0.0123456;
    Osc6 osc;
    osc.prepare();
    osc.reset();
    int wraps = 0;
    for (int k = 0; k < 10000; ++k)
    {
        (void) osc.process (inc);
        const double f = osc.getWrapFraction();
        if (f > 0.0)
        {
            const double t = k + f; // phase 0 at time 0; call k spans (k, k+1]
            CHECK_THAT (t * inc - std::round (t * inc), WithinAbs (0.0, 1e-9));
            ++wraps;
        }
    }
    CHECK (wraps == static_cast<int> (std::floor (10000 * inc)));
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
