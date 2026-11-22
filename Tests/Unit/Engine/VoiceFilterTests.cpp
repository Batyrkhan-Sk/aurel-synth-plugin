// The filter section in context: engine renders with the voice filter.

#include "Support/EngineHarness.h"

#include "DSP/Core/Conversions.h"
#include "Engine/Engine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace aurel;
using namespace aurel::test;
using engine::Engine;
using engine::EngineParameters;
using engine::FilterType;

namespace
{
    constexpr double kRate = 48000.0;

    Engine makeEngine()
    {
        Engine e;
        e.prepare (kRate, 512);
        return e;
    }

    double rmsFrom (const std::vector<float>& x, std::size_t from)
    {
        double s = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            s += static_cast<double> (x[i]) * x[i];
        return std::sqrt (s / static_cast<double> (x.size() - from));
    }

    double frequencyOf (const std::vector<float>& x, std::size_t from)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (std::size_t i = from + 1; i < x.size(); ++i)
        {
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0.0)
                    first = t;
                last = t;
                ++crossings;
            }
        }
        return crossings > 1 ? (crossings - 1) * kRate / (last - first) : 0.0;
    }

    float maxStep (const std::vector<float>& x, std::size_t from)
    {
        float m = 0.0f;
        for (std::size_t i = from + 1; i < x.size(); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }
} // namespace

TEST_CASE ("Ladder in the voice attenuates a saw at low cutoff", "[engine][filter]")
{
    EngineParameters open, closed;
    open.voice.filter.cutoffHz = 20000.0f;
    closed.voice.filter.cutoffHz = 400.0f;

    auto a = makeEngine();
    auto b = makeEngine();
    const auto x = renderBlocks (a, { { 0, noteOn (57) } }, 24000, 256, open);
    const auto y = renderBlocks (b, { { 0, noteOn (57) } }, 24000, 256, closed);
    // 220 Hz saw through a 400 Hz 4-pole low-pass keeps mostly the fundamental.
    CHECK (rmsFrom (y.left, 4800) < 0.75 * rmsFrom (x.left, 4800));
    CHECK (rmsFrom (y.left, 4800) > 0.2 * rmsFrom (x.left, 4800));
}

TEST_CASE ("Self-oscillating ladder plays in tune from the keyboard (key tracking)", "[engine][filter]")
{
    // Oscillators off: only the -120 dBFS noise floor excites the filter.
    EngineParameters p;
    p.masterGainDb = 0.0f;
    p.voice.osc[0].level = 0.0f;
    p.voice.filter.cutoffHz = static_cast<float> (dsp::noteToFrequency (60.0));
    p.voice.filter.keyTracking = 1.0f;
    p.voice.filter.resonance = 1.0f; // k = 4.5

    const int note = GENERATE (48, 60, 69, 81);
    auto eng = makeEngine();
    const auto out = renderBlocks (eng, { { 0, noteOn (note) } }, 96000, 256, p);
    const double hz = frequencyOf (out.left, 48000);
    const double cents = 1200.0 * std::log2 (hz / dsp::noteToFrequency (note));
    INFO ("note " << note << ": " << hz << " Hz, " << cents << " cents, rms " << rmsFrom (out.left, 48000));
    CHECK (rmsFrom (out.left, 48000) > 0.01); // it really oscillates
    CHECK (std::abs (cents) < 3.0);
}

TEST_CASE ("Changing the filter type crossfades without clicks", "[engine][filter]")
{
    // Pure 220 Hz sine through a 2 kHz filter: types differ in phase and
    // gain, so a hard switch would jump by ~0.1; the crossfade spreads it.
    EngineParameters p;
    p.masterGainDb = 0.0f;
    p.voice.osc[0].waveform = dsp::Waveform::Sine;
    p.voice.filter.cutoffHz = 2000.0f;
    p.voice.filter.resonance = 0.6f;

    auto reference = makeEngine();
    const auto ref = renderBlocks (reference, { { 0, noteOn (57) } }, 24000, 256, p);
    const float baseline = maxStep (ref.left, 4800);

    auto eng = makeEngine();
    constexpr int blocks = 93; // whole blocks only: every sample is rendered
    std::vector<float> out (blocks * 256);
    std::vector<float> r (blocks * 256);
    const auto on = noteOn (57);
    const FilterType sequence[] = { FilterType::Ladder24, FilterType::StateVariable, FilterType::Ladder12,
                                    FilterType::Off, FilterType::Ladder24 };
    for (int block = 0; block < blocks; ++block)
    {
        p.voice.filter.type = sequence[(block / 15) % 5]; // a switch every 80 ms
        const std::span<const engine::Event> ev = block == 0 ? std::span<const engine::Event> (&on, 1)
                                                             : std::span<const engine::Event>();
        eng.process (ev, out.data() + block * 256, r.data() + block * 256, 256, p);
    }
    std::size_t worstAt = 0;
    for (std::size_t i = 4801; i < out.size(); ++i)
        if (std::abs (out[i] - out[i - 1]) > std::abs (out[worstAt] - out[worstAt > 0 ? worstAt - 1 : 0]))
            worstAt = i;
    INFO ("baseline max step " << baseline << ", with switching " << maxStep (out, 4800) << " at sample "
                               << worstAt << " (block " << worstAt / 256 << ", type index " << (worstAt / 256 / 15) % 5 << ")");
    CHECK (maxStep (out, 4800) < 1.7f * baseline);
}

TEST_CASE ("Series high-pass removes low frequencies; at its minimum it is bypassed", "[engine][filter]")
{
    EngineParameters p;
    p.masterGainDb = 0.0f;
    p.voice.osc[0].waveform = dsp::Waveform::Sine;
    p.voice.filter.type = FilterType::Off;

    auto a = makeEngine();
    const auto dry = renderBlocks (a, { { 0, noteOn (43) } }, 24000, 256, p); // ~98 Hz

    p.voice.filter.hpfCutoffHz = 1000.0f;
    auto b = makeEngine();
    const auto wet = renderBlocks (b, { { 0, noteOn (43) } }, 24000, 256, p);
    CHECK (rmsFrom (wet.left, 4800) < 0.02 * rmsFrom (dry.left, 4800)); // 2-pole HP: ~ -40 dB at 0.1 fc

    p.voice.filter.hpfCutoffHz = engine::FilterParameters::kHpfOffHz;
    auto c = makeEngine();
    const auto off = renderBlocks (c, { { 0, noteOn (43) } }, 24000, 256, p);
    CHECK (std::memcmp (off.left.data(), dry.left.data(), sizeof (float) * off.left.size()) == 0);
}

TEST_CASE ("Abrupt cutoff and resonance jumps stay finite and bounded", "[engine][filter][stability]")
{
    EngineParameters p;
    p.voice.osc[1].level = 1.0f;
    p.voice.subLevel = 1.0f;
    p.voice.filter.driveDb = 24.0f;
    auto eng = makeEngine();
    std::vector<float> l (256), r (256);
    const auto on = noteOn (36);
    float peak = 0.0f;
    for (int block = 0; block < 400; ++block)
    {
        p.voice.filter.type = block % 50 < 25 ? FilterType::Ladder24 : FilterType::StateVariable;
        p.voice.filter.cutoffHz = block % 2 ? 20.0f : 20000.0f;
        p.voice.filter.resonance = (block % 3) / 2.0f;
        eng.process (block == 0 ? std::span<const engine::Event> (&on, 1) : std::span<const engine::Event>(),
                     l.data(), r.data(), 256, p);
        for (const float s : l)
        {
            REQUIRE (std::isfinite (s));
            peak = std::max (peak, std::abs (s));
        }
    }
    INFO ("peak " << peak);
    CHECK (peak < 4.0f);
}
