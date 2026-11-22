#include "OscillatorTools.h"

#include "DSP/Core/Denormals.h"

#include <chrono>
#include <cstdio>
#include <vector>

namespace aurel::render
{

namespace
{
    using namespace aurel::dsp;

    template <class F>
    bool withKernel (std::string_view name, F&& f)
    {
        if (name == "naive")     { f.template operator()<blep::Naive>(); return true; }
        if (name == "polyblep2") { f.template operator()<blep::PolyBlep2>(); return true; }
        if (name == "sinc4")     { f.template operator()<blep::SincBlep<4>>(); return true; }
        if (name == "sinc6")     { f.template operator()<blep::SincBlep<6>>(); return true; }
        if (name == "sinc8")     { f.template operator()<blep::SincBlep<8>>(); return true; }
        if (name == "sinc12")    { f.template operator()<blep::SincBlep<12>>(); return true; }
        return false;
    }

    const char* waveformName (Waveform w)
    {
        switch (w)
        {
            case Waveform::Sine:     return "sine";
            case Waveform::Triangle: return "triangle";
            case Waveform::Saw:      return "saw";
            case Waveform::Pulse:    return "pulse";
        }
        return "?";
    }
} // namespace

bool isKnownKernel (std::string_view name)
{
    return withKernel (name, []<class K>() {});
}

bool parseWaveform (std::string_view name, dsp::Waveform& out)
{
    if (name == "sine")     { out = Waveform::Sine; return true; }
    if (name == "triangle") { out = Waveform::Triangle; return true; }
    if (name == "saw")      { out = Waveform::Saw; return true; }
    if (name == "pulse")    { out = Waveform::Pulse; return true; }
    return false;
}

bool renderOscillator (std::string_view kernel, dsp::Waveform waveform, double frequency,
                       double pulseWidth, double sampleRate, std::span<float> out)
{
    return withKernel (kernel, [&]<class K>() {
        BandlimitedOscillator<K> osc;
        osc.prepare();
        osc.setWaveform (waveform);
        osc.setPulseWidth (pulseWidth);
        const double inc = frequency / sampleRate;

        for (int i = 0; i < BandlimitedOscillator<K>::kLatency; ++i) // latency compensation
            (void) osc.process (inc);
        for (auto& s : out)
            s = osc.process (inc);
    });
}

int benchmarkOscillators()
{
#ifndef NDEBUG
    std::puts ("WARNING: debug build, timings are not meaningful. Use the Release build.");
#endif
    constexpr int numSamples = 1 << 23;
    constexpr double rate = 96000.0;
    const ScopedFlushDenormals noDenormals;

    std::printf ("%-10s %-9s %8s %12s\n", "kernel", "waveform", "freq", "ns/sample");
    for (const char* kernel : { "naive", "polyblep2", "sinc4", "sinc6", "sinc8", "sinc12" })
    {
        for (const auto wave : { Waveform::Saw, Waveform::Pulse, Waveform::Triangle })
        {
            for (const double freq : { 440.0, 3520.0 })
            {
                double nsPerSample = 0.0;
                float sink = 0.0f;
                (void) withKernel (kernel, [&]<class K>() {
                    BandlimitedOscillator<K> osc;
                    osc.prepare();
                    osc.setWaveform (wave);
                    osc.setPulseWidth (0.3);
                    const double inc = freq / rate;
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int i = 0; i < numSamples; ++i)
                        sink += osc.process (inc);
                    const auto t1 = std::chrono::steady_clock::now();
                    nsPerSample = std::chrono::duration<double, std::nano> (t1 - t0).count() / numSamples;
                });
                std::printf ("%-10s %-9s %8.0f %12.2f   (sink %g)\n", kernel, waveformName (wave), freq,
                             nsPerSample, static_cast<double> (sink));
            }
        }
    }
    return 0;
}

} // namespace aurel::render
