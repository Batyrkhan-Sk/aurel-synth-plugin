#include "FilterTools.h"

#include "DSP/Core/Conversions.h"
#include "DSP/Core/Denormals.h"
#include "DSP/Filters/LadderFilter.h"
#include "DSP/Filters/StateVariableFilter.h"
#include "DSP/Oscillators/BandlimitedOscillator.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace aurel::render
{

namespace
{
    using namespace aurel::dsp;

    int svfOutput (const std::string& mode)
    {
        if (mode == "lp") return 0;
        if (mode == "bp") return 1;
        if (mode == "hp") return 2;
        if (mode == "notch") return 3;
        return -1;
    }

    /** Band-limited input so the measurements see the filter's own
        distortion and aliasing, not the source's. */
    struct Source
    {
        Oscillator osc;
        std::string kind;
        double inc = 0.0, amp = 1.0;
        long long n = 0;

        void prepare (const FilterRenderOptions& o)
        {
            kind = o.input;
            amp = o.amplitude;
            inc = o.frequency / o.sampleRate;
            osc.prepare();
            osc.setWaveform (kind == "sine" ? Waveform::Sine : Waveform::Saw);
            osc.reset();
            for (int i = 0; i < Oscillator::kLatency; ++i)
                (void) osc.process (inc);
        }

        double next()
        {
            if (kind == "impulse")
                return n++ == 0 ? amp : 0.0;
            ++n;
            return amp * osc.process (inc);
        }
    };
} // namespace

bool isValidFilterOptions (const FilterRenderOptions& o)
{
    const bool filterOk = (o.filter == "ladder" && (o.solver == "linear" || o.solver == "newton")
                           && (o.mode == "lp24" || o.mode == "lp12"))
                       || (o.filter == "svf" && svfOutput (o.mode) >= 0);
    return filterOk && (o.input == "saw" || o.input == "sine" || o.input == "impulse")
        && o.cutoff > 0.0 && o.feedback >= 0.0 && o.resonance >= 0.0 && o.resonance <= 1.0
        && o.compensation >= 0.0 && o.compensation <= 1.0 && o.sampleRate >= 8000.0
        && o.iterations >= 1 && o.iterations <= 64
        && o.frequency > 0.0 && o.frequency < 0.49 * o.sampleRate;
}

bool renderFilter (const FilterRenderOptions& o, std::span<float> out)
{
    if (! isValidFilterOptions (o))
        return false;

    const ScopedFlushDenormals ftz;
    Source source;
    source.prepare (o);

    // Optional 4th-order Butterworth pre-filter: two SVF low-passes with
    // Q = 0.5412 and 1.3066 (k = 1/Q).
    StateVariableFilter pre1, pre2;
    const bool usePre = o.prefilterHz > 0.0;
    if (usePre)
    {
        pre1.prepare (o.sampleRate);
        pre2.prepare (o.sampleRate);
        pre1.setCutoffAndDamping (o.prefilterHz, 1.0 / 0.5412);
        pre2.setCutoffAndDamping (o.prefilterHz, 1.0 / 1.3066);
    }
    struct
    {
        Source& s;
        StateVariableFilter &a, &b;
        bool on;
        double next() { const double x = s.next(); return on ? b.process (a.process (x).lp).lp : x; }
    } src { source, pre1, pre2, usePre };

    if (o.filter == "ladder")
    {
        LadderFilter f;
        f.prepare (o.sampleRate);
        f.setSolver (o.solver == "newton" ? LadderFilter::Solver::Newton : LadderFilter::Solver::Linearised,
                     o.iterations);
        f.setMode (o.mode == "lp12" ? LadderFilter::Mode::LowPass12 : LadderFilter::Mode::LowPass24);
        f.setTuningCorrection (o.tuning != 0);
        f.setParameters (o.cutoff, o.feedback, decibelsToGain (static_cast<float> (o.driveDb)), o.compensation);
        for (auto& s : out)
            s = static_cast<float> (f.process (src.next()));
    }
    else
    {
        StateVariableFilter f;
        f.prepare (o.sampleRate);
        f.setParameters (o.cutoff, o.resonance);
        const int which = svfOutput (o.mode);
        for (auto& s : out)
        {
            const auto r = f.process (src.next());
            s = static_cast<float> (which == 0 ? r.lp : which == 1 ? r.bp : which == 2 ? r.hp : r.lp + r.hp);
        }
    }
    return true;
}

int benchmarkFilters()
{
#ifndef NDEBUG
    std::puts ("WARNING: debug build, timings are not meaningful. Use the Release build.");
#endif
    constexpr double rate = 96000.0;
    constexpr int n = 1 << 22;
    const ScopedFlushDenormals ftz;

    // Pre-render the input so only the filter is timed.
    std::vector<double> input (n);
    for (int i = 0; i < n; ++i)
        input[static_cast<std::size_t> (i)] = 2.0 * std::fmod (i * 110.0 / rate, 1.0) - 1.0;

    struct Case
    {
        const char* name;
        double k, drive;
    };

    // tanh cost (dominates the ladder): library vs exp-based.
    {
        volatile double seed = 0.37;
        auto timeIt = [&] (const char* name, auto&& fn) {
            double sink = 0.0, x = seed;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i)
            {
                sink += fn (x);
                x = x * 1.0000001 + 1e-7 - (x > 8.0 ? 16.0 : 0.0);
            }
            const auto t1 = std::chrono::steady_clock::now();
            std::printf ("%-22s %10.2f ns   (sink %g)\n", name,
                         std::chrono::duration<double, std::nano> (t1 - t0).count() / n, sink);
        };
        timeIt ("std::tanh (double)", [] (double v) { return std::tanh (v); });
        timeIt ("std::tanh (float)", [] (double v) { return static_cast<double> (std::tanh (static_cast<float> (v))); });
        timeIt ("exp-based (double)", [] (double v) {
            const double t = std::exp (-2.0 * std::abs (v));
            return std::copysign ((1.0 - t) / (1.0 + t), v);
        });
        timeIt ("baseline (x * 0.5)", [] (double v) { return v * 0.5; });
        std::puts ("");
    }

    std::printf ("%-22s %-34s %10s %10s\n", "filter", "case", "ns/sample", "mean iter");
    for (const auto solver : { LadderFilter::Solver::Linearised, LadderFilter::Solver::Newton })
    {
        for (const Case c : { Case { "k 2, drive 0 dB", 2.0, 1.0 }, Case { "k 3.8, drive +12 dB", 3.8, 4.0 },
                              Case { "k 4.5 (self-osc), drive +24 dB", 4.5, 16.0 } })
        {
            LadderFilter f;
            f.prepare (rate);
            f.setSolver (solver);
            f.setParameters (2000.0, c.k, c.drive, 0.5);
            double sink = 0.0;
            long long iterations = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i)
            {
                sink += f.process (input[static_cast<std::size_t> (i)]);
                iterations += f.getLastIterations();
            }
            const auto t1 = std::chrono::steady_clock::now();
            std::printf ("%-22s %-34s %10.2f %10.2f   (sink %g)\n",
                         solver == LadderFilter::Solver::Newton ? "ladder newton" : "ladder linearised", c.name,
                         std::chrono::duration<double, std::nano> (t1 - t0).count() / n,
                         static_cast<double> (iterations) / n, sink);
        }
    }

    {
        StateVariableFilter f;
        f.prepare (rate);
        f.setParameters (2000.0, 0.7);
        double sink = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i)
            sink += f.process (input[static_cast<std::size_t> (i)]).lp;
        const auto t1 = std::chrono::steady_clock::now();
        std::printf ("%-22s %-34s %10.2f %10s   (sink %g)\n", "svf", "fixed coefficients",
                     std::chrono::duration<double, std::nano> (t1 - t0).count() / n, "-", sink);
    }
    {
        StateVariableFilter f;
        f.prepare (rate);
        double sink = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i)
        {
            f.setParameters (1000.0 + (i & 1023), 0.7); // tan() every sample
            sink += f.process (input[static_cast<std::size_t> (i)]).lp;
        }
        const auto t1 = std::chrono::steady_clock::now();
        std::printf ("%-22s %-34s %10.2f %10s   (sink %g)\n", "svf", "coefficients every sample",
                     std::chrono::duration<double, std::nano> (t1 - t0).count() / n, "-", sink);
    }
    return 0;
}

} // namespace aurel::render
