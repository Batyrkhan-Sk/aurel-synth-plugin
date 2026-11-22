#include "Support/AllocationCounter.h"
#include "Support/EngineHarness.h"

#include "DSP/Core/Conversions.h"
#include "Engine/Engine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <random>

using namespace aurel;
using namespace aurel::test;
using engine::Engine;
using engine::EngineParameters;
using engine::EventType;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kMaxBlock = 512;
    const long long kReleaseTail = static_cast<long long> (kRate * engine::Voice::kReleaseSeconds) + 64;

    Engine makeEngine()
    {
        Engine e;
        e.prepare (kRate, kMaxBlock);
        return e;
    }

    /** Frequency from interpolated rising zero crossings (skips the first 10 ms). */
    double measureFrequency (const std::vector<float>& x, double rate)
    {
        const auto skip = static_cast<std::size_t> (rate * 0.01);
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (std::size_t i = skip + 1; i < x.size(); ++i)
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
        return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
    }

    float peakOf (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        float p = 0.0f;
        for (std::size_t i = from; i < std::min (to, x.size()); ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }

    /** OSC1 sine only, 0 dB master: the voice is a pure, measurable tone. */
    EngineParameters unityGain()
    {
        EngineParameters p;
        p.masterGainDb = 0.0f;
        p.voice.osc[0].waveform = dsp::Waveform::Sine;
        return p;
    }

    /** Below this an IIR decimator tail counts as silence (-160 dBFS). */
    constexpr float kSilence = 1e-8f;
} // namespace

TEST_CASE ("Engine is silent with no notes", "[engine]")
{
    auto eng = makeEngine();
    const auto out = renderBlocks (eng, {}, 4800, 256);
    CHECK (peakOf (out.left) == 0.0f);
    CHECK (peakOf (out.right) == 0.0f);
}

TEST_CASE ("Engine plays the correct pitch", "[engine]")
{
    const int note = GENERATE (33, 57, 69, 93, 105);
    auto eng = makeEngine();
    const auto out = renderBlocks (eng, { { 0, noteOn (note) } }, 48000, 256, unityGain());

    const double expected = dsp::noteToFrequency (note);
    const double measured = measureFrequency (out.left, kRate);
    const double cents = 1200.0 * std::log2 (measured / expected);
    INFO ("note " << note << ": expected " << expected << " Hz, measured " << measured << " Hz");
    CHECK (std::abs (cents) < 0.01);
}

TEST_CASE ("Engine output level follows velocity and master gain", "[engine]")
{
    auto eng = makeEngine();
    const auto full = renderBlocks (eng, { { 0, noteOn (69, 1.0f) } }, 9600, 256, unityGain());
    CHECK_THAT (peakOf (full.left, 4800), Catch::Matchers::WithinRel (engine::Voice::kVelocityGain, 1e-3f));

    auto eng2 = makeEngine();
    EngineParameters minus6 = unityGain();
    minus6.masterGainDb = -6.0206f;
    const auto half = renderBlocks (eng2, { { 0, noteOn (69, 0.5f) } }, 9600, 256, minus6);
    CHECK_THAT (peakOf (half.left, 4800),
                Catch::Matchers::WithinRel (engine::Voice::kVelocityGain * 0.25f, 1e-3f));
}

TEST_CASE ("Events are sample-accurate", "[engine]")
{
    // Delaying the note by k host samples must delay the output by exactly
    // k samples, bit for bit. Latency (kernel + decimator) is constant.
    EngineParameters saw;
    auto a = makeEngine();
    const auto ref = renderBlocks (a, { { 1000, noteOn (57) } }, 4096, 512, saw);
    CHECK (peakOf (ref.left, 0, 1000) == 0.0f); // nothing before the note
    CHECK (peakOf (ref.left, 1000, 1016) > 0.0f); // starts within the latency

    const int shift = GENERATE (1, 7, 64);
    auto b = makeEngine();
    const auto late = renderBlocks (b, { { 1000 + shift, noteOn (57) } }, 4096, 512, saw);
    INFO ("shift " << shift);
    CHECK (std::memcmp (ref.left.data(), late.left.data() + shift, sizeof (float) * (4096 - shift)) == 0);
}

TEST_CASE ("Engine runs voices at >= 88.2 kHz internally", "[engine][oversampling]")
{
    CHECK (Engine::oversamplingFactorFor (44100.0) == 2);
    CHECK (Engine::oversamplingFactorFor (48000.0) == 2);
    CHECK (Engine::oversamplingFactorFor (88200.0) == 1);
    CHECK (Engine::oversamplingFactorFor (96000.0) == 1);
    CHECK (Engine::oversamplingFactorFor (192000.0) == 1);
    CHECK (Engine::oversamplingFactorFor (32000.0) == 4);
    CHECK (Engine::oversamplingFactorFor (22050.0) == 4);

    for (const double rate : { 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        Engine e;
        e.prepare (rate, 256);
        INFO ("host rate " << rate);
        CHECK (e.getInternalSampleRate() >= 88200.0 - 1.0);
    }
}

TEST_CASE ("Engine pitch is exact at every host sample rate", "[engine][oversampling]")
{
    const double rate = GENERATE (32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0);
    Engine eng;
    eng.prepare (rate, 512);
    const auto total = static_cast<long long> (rate);
    const auto out = renderBlocks (eng, { { 0, noteOn (69) } }, total, 512, unityGain());
    const double cents = 1200.0 * std::log2 (measureFrequency (out.left, rate) / 440.0);
    INFO ("rate " << rate << ": " << cents << " cents");
    CHECK (std::abs (cents) < 0.01);
}

TEST_CASE ("Output is identical for any block segmentation", "[engine]")
{
    const std::vector<TimedEvent> events {
        { 0, noteOn (60, 0.8f) },  { 100, noteOn (64, 0.6f) }, { 101, noteOn (67, 0.7f) },
        { 3000, noteOff (64) },    { 3000, sustain (true) },   { 4000, noteOff (60) },
        { 6000, sustain (false) }, { 7000, noteOn (72, 1.0f) }, { 9000, noteOff (67) },
        { 9001, noteOff (72) },
    };
    constexpr long long total = 12000;

    auto reference = makeEngine();
    const auto ref = renderBlocks (reference, events, total, 64);

    // 1 and 7: tiny/odd blocks. 4096: larger than maxBlock -> internal chunking.
    const int blockSize = GENERATE (1, 7, 64, 333, 512, 4096);
    auto eng = makeEngine();
    const auto out = renderBlocks (eng, events, total, blockSize);
    INFO ("block size " << blockSize);
    CHECK (std::memcmp (ref.left.data(), out.left.data(), sizeof (float) * total) == 0);
    CHECK (std::memcmp (ref.right.data(), out.right.data(), sizeof (float) * total) == 0);
}

TEST_CASE ("Zero-length blocks still apply events", "[engine]")
{
    auto eng = makeEngine();
    const auto on = noteOn (60);
    eng.process ({ &on, 1 }, nullptr, nullptr, 0, {});
    CHECK (eng.activeVoiceCount() == 1);

    const auto off = noteOff (60);
    eng.process ({ &off, 1 }, nullptr, nullptr, 0, {});
    std::vector<float> l (static_cast<std::size_t> (kReleaseTail)), r (l.size());
    eng.process ({}, l.data(), r.data(), static_cast<int> (l.size()), {});
    CHECK (eng.activeVoiceCount() == 0);
}

TEST_CASE ("Mono output is the L/R average", "[engine]")
{
    auto a = makeEngine();
    auto b = makeEngine();
    const auto on = noteOn (69);
    std::vector<float> l (1024), r (1024), mono (1024);
    a.process ({ &on, 1 }, l.data(), r.data(), 1024, {});
    b.process ({ &on, 1 }, mono.data(), nullptr, 1024, {});
    for (std::size_t i = 0; i < mono.size(); ++i)
        REQUIRE (mono[i] == 0.5f * (l[i] + r[i]));
}

TEST_CASE ("Note off releases and the voice ends in silence", "[engine][lifecycle]")
{
    auto eng = makeEngine();
    const auto out = renderBlocks (eng, { { 0, noteOn (69) }, { 4800, noteOff (69) } },
                                   4800 + kReleaseTail + 4000, 256);
    CHECK (eng.activeVoiceCount() == 0);
    // The voice stops exactly; the decimator's IIR tail then decays below -160 dBFS.
    CHECK (peakOf (out.left, static_cast<std::size_t> (4800 + kReleaseTail + 2000)) < kSilence);
}

TEST_CASE ("Overlapping same-key notes (FL Studio piano roll order)", "[engine][lifecycle]")
{
    // Second Note On arrives before the first note's Note Off, same timestamp.
    auto eng = makeEngine();
    renderBlocks (eng, { { 0, noteOn (60) }, { 4800, noteOn (60) }, { 4800, noteOff (60) } },
                  4800 + kReleaseTail, 256);
    CHECK (eng.activeVoiceCount() == 1); // the new note keeps sounding

    std::vector<float> l (static_cast<std::size_t> (kReleaseTail)), r (l.size());
    const auto off = noteOff (60);
    eng.process ({ &off, 1 }, l.data(), r.data(), static_cast<int> (l.size()), {});
    CHECK (eng.activeVoiceCount() == 0);
}

TEST_CASE ("Sustain pedal holds released notes until pedal up", "[engine][lifecycle]")
{
    auto eng = makeEngine();
    renderBlocks (eng, { { 0, sustain (true) }, { 10, noteOn (60) }, { 500, noteOff (60) } },
                  500 + kReleaseTail * 2, 256);
    CHECK (eng.activeVoiceCount() == 1);

    std::vector<float> l (static_cast<std::size_t> (kReleaseTail)), r (l.size());
    const auto up = sustain (false);
    eng.process ({ &up, 1 }, l.data(), r.data(), static_cast<int> (l.size()), {});
    CHECK (eng.activeVoiceCount() == 0);
}

TEST_CASE ("Sustain pedal does not release keys that are still held", "[engine][lifecycle]")
{
    auto eng = makeEngine();
    renderBlocks (eng, { { 0, sustain (true) }, { 10, noteOn (60) }, { 20, sustain (false) } },
                  kReleaseTail * 2, 256);
    CHECK (eng.activeVoiceCount() == 1);
}

TEST_CASE ("All Notes Off releases, All Sound Off silences immediately", "[engine][lifecycle]")
{
    auto eng = makeEngine();
    renderBlocks (eng, { { 0, noteOn (60) }, { 0, noteOn (64) }, { 0, sustain (true) },
                         { 100, noteOff (60) }, { 200, simple (EventType::AllNotesOff) } },
                  200 + kReleaseTail, 256);
    CHECK (eng.activeVoiceCount() == 0);

    auto eng2 = makeEngine();
    const auto out = renderBlocks (eng2, { { 0, noteOn (60) }, { 1000, simple (EventType::AllSoundOff) } },
                                   4000, 256);
    CHECK (eng2.activeVoiceCount() == 0);
    CHECK (peakOf (out.left, 1000 + 64) < 1e-3f);    // gone within ~1.3 ms (decimator ring-out)
    CHECK (peakOf (out.left, 1000 + 2000) < kSilence);
}

TEST_CASE ("Voice stealing keeps the voice count bounded", "[engine][lifecycle]")
{
    auto eng = makeEngine();
    std::vector<TimedEvent> events;
    for (int i = 0; i < 40; ++i)
        events.push_back ({ i * 10, noteOn (30 + i) });
    renderBlocks (eng, events, 1000, 256);
    CHECK (eng.activeVoiceCount() == engine::VoiceManager::kMaxVoices);

    // Note Offs for the 8 stolen notes match nothing; the rest release normally.
    std::vector<TimedEvent> offs;
    for (int i = 0; i < 40; ++i)
        offs.push_back ({ 0, noteOff (30 + i) });
    renderBlocks (eng, offs, kReleaseTail, 256);
    CHECK (eng.activeVoiceCount() == 0);
}

TEST_CASE ("Master gain changes are smoothed", "[engine]")
{
    auto eng = makeEngine();
    const auto on = noteOn (45); // 110 Hz: max per-sample slope of the sine is small
    std::vector<float> l (4800), r (4800);
    EngineParameters p = unityGain();
    p.masterGainDb = -60.0f;
    eng.process ({ &on, 1 }, l.data(), r.data(), 4800, p);

    p.masterGainDb = 6.0f; // +66 dB jump
    eng.process ({}, l.data(), r.data(), 4800, p);

    // Unsmoothed, the jump would produce a step of up to ~0.5. With a 20 ms
    // ramp the largest sample-to-sample change stays near the sine's own slope.
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < l.size(); ++i)
        maxStep = std::max (maxStep, std::abs (l[i] - l[i - 1]));
    CHECK (maxStep < 0.01f);
}

TEST_CASE ("Engine process path does not allocate", "[engine][rt-safety]")
{
    auto eng = makeEngine();
    std::vector<float> l (4096), r (4096);
    std::vector<engine::Event> events { noteOn (60), noteOn (64), noteOn (67), sustain (true),
                                        noteOff (60), sustain (false),
                                        simple (EventType::AllNotesOff) };
    for (std::size_t i = 0; i < events.size(); ++i)
        events[i].sampleOffset = static_cast<int> (i * 300);

    // Every source and modulation path active, with parameter changes mid-run.
    EngineParameters p;
    for (auto& o : p.voice.osc)
        o.level = 0.5f;
    p.voice.osc[1].waveform = dsp::Waveform::Pulse;
    p.voice.osc[2].waveform = dsp::Waveform::Triangle;
    p.voice.osc2Sync = true;
    p.voice.osc2FmDepth = 2.0f;
    p.voice.subLevel = 0.5f;
    p.voice.noiseLevel = 0.5f;
    p.voice.noiseColor = dsp::NoiseColor::Pink;

    std::size_t allocs = 0, frees = 0;
    {
        const ScopedAllocationCounter counter;
        for (int block = 0; block < 8; ++block)
        {
            p.voice.osc[0].waveform = block % 2 ? dsp::Waveform::Saw : dsp::Waveform::Pulse;
            p.voice.subOctave = 1 + block % 2;
            eng.process (events, l.data(), r.data(), block == 3 ? 4096 : 256, p); // includes chunking
        }
        allocs = counter.allocations();
        frees = counter.deallocations();
    }
    CHECK (allocs == 0);
    CHECK (frees == 0);
}

TEST_CASE ("MIDI fuzz: no stuck voices, output always finite and bounded", "[engine][lifecycle][fuzz]")
{
    const auto seed = GENERATE (1u, 2u, 3u, 42u, 1234u);
    std::mt19937 rng (seed);
    auto uni = [&rng] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };

    auto eng = makeEngine();
    std::vector<engine::Event> events;
    events.reserve (64);
    std::vector<float> l (2048), r (2048);
    int outstanding[2][128] = {}; // note-ons not yet matched by a note-off, per channel/key
    bool pedal = false;
    int maxActive = 0;
    bool heardSound = false;

    // Default patch: one saw per voice. 1.5x covers Gibbs overshoot (~1.18)
    // plus the decimator's phase response reshaping the waveform.
    const float bound = 1.5f * engine::Voice::kVelocityGain * engine::VoiceManager::kMaxVoices
                        * dsp::decibelsToGain (EngineParameters {}.masterGainDb);

    auto check = [&] (int n) {
        for (int i = 0; i < n; ++i)
        {
            REQUIRE (std::isfinite (l[static_cast<std::size_t> (i)]));
            REQUIRE (std::abs (l[static_cast<std::size_t> (i)]) <= bound);
        }
    };

    for (int block = 0; block < 1500; ++block)
    {
        const int n = uni (0, 1200) == 0 ? 0 : uni (1, 2048); // includes empty and >maxBlock blocks
        events.clear();
        const int count = uni (0, 10);
        std::vector<int> offsets (static_cast<std::size_t> (count));
        for (auto& o : offsets)
            o = n > 0 ? uni (0, n - 1) : 0;
        std::sort (offsets.begin(), offsets.end());

        for (int o : offsets)
        {
            const int ch = uni (0, 1), key = uni (36, 60), kind = uni (0, 99);
            engine::Event e;
            if (kind < 45)
            {
                e = noteOn (key, static_cast<float> (uni (1, 127)) / 127.0f, ch);
                ++outstanding[ch][key];
            }
            else if (kind < 90)
            {
                e = noteOff (key, ch);
                if (outstanding[ch][key] > 0)
                    --outstanding[ch][key];
            }
            else if (kind < 98)
            {
                pedal = ! pedal;
                e = sustain (pedal);
            }
            else
            {
                e = simple (EventType::AllNotesOff);
            }
            e.sampleOffset = o;
            events.push_back (e);
        }

        eng.process (events, l.data(), r.data(), n, {});
        check (n);
        maxActive = std::max (maxActive, eng.activeVoiceCount());
        if (n > 0)
            heardSound = heardSound || peakOf (l, 0, static_cast<std::size_t> (n)) > 0.0f;
    }

    // Guard against a vacuous pass: the fuzz must really have driven voices,
    // including pool exhaustion (stealing).
    CHECK (heardSound);
    CHECK (maxActive == engine::VoiceManager::kMaxVoices);

    // Behave like a real controller at the end: lift the pedal, release every key.
    events.clear();
    events.push_back (sustain (false));
    for (int ch = 0; ch < 2; ++ch)
        for (int key = 0; key < 128; ++key)
            for (int k = 0; k < outstanding[ch][key]; ++k)
                events.push_back (noteOff (key, ch));
    eng.process (events, l.data(), r.data(), 256, {});
    for (int i = 0; i < 4; ++i)
        eng.process ({}, l.data(), r.data(), 2048, {});

    INFO ("seed " << seed);
    CHECK (eng.activeVoiceCount() == 0);
    CHECK (peakOf (l) < kSilence);
}
