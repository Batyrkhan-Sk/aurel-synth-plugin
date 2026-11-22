// aurel_render: offline rendering tool.
//
// Signals:
//   sine    - double-precision reference sine (validates the pipeline)
//   silence - all zeros
//   note    - one held note rendered through the AUREL engine
//   osc     - raw oscillator with a chosen anti-aliasing kernel
//
//   aurel_render --signal sine --freq 1000 --amp 0.5 --rate 48000
//                --seconds 2 --channels 2 --out renders/sine.wav
//   aurel_render --signal note --note 69 --rate 48000 --out renders/a4.wav
//   aurel_render --signal osc --wave saw --kernel sinc8 --freq 3520
//                --rate 96000 --out renders/saw.wav
//   aurel_render --bench osc          (oscillator CPU table, Release build)

#include "DSP/Core/Denormals.h"
#include "Engine/Engine.h"
#include "OscillatorTools.h"
#include "WavFile.h"

#include <vector>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <string_view>

namespace
{

struct Options
{
    std::string signal = "sine";
    double frequency = 1000.0;
    double amplitude = 0.5;
    std::uint32_t sampleRate = 48000;
    double seconds = 1.0;
    std::uint16_t channels = 2;
    int note = 69;
    std::string wave = "saw";
    std::string kernel = "sinc6";
    double pulseWidth = 0.5;
    double syncRatio = 0.0;
    double fmDepth = 0.0;
    double fmRatio = 1.0;
    std::string bench;
    std::string outPath;
};

void printUsage()
{
    std::fputs (
        "usage: aurel_render --out <file.wav> [options]\n"
        "       aurel_render --bench osc\n"
        "  --signal   sine | silence | note | osc (default sine)\n"
        "  --freq     Hz, sine / osc          (default 1000)\n"
        "  --wave     sine | triangle | saw | pulse; osc, or OSC1 for note (default saw)\n"
        "  --kernel   naive | polyblep2 | sinc4 | sinc6 | sinc8 | sinc12, osc only (default sinc6)\n"
        "  --pw       pulse width 0.01..0.99, osc only (default 0.5)\n"
        "  --sync     slave/master frequency ratio, osc only (default 0 = off)\n"
        "  --fm       through-zero FM depth by a sine modulator, osc only (default 0)\n"
        "  --fm-ratio modulator/carrier frequency ratio (default 1)\n"
        "  --amp      sine: peak amplitude; note: velocity 0..1 (default 0.5)\n"
        "  --note     MIDI note, note only    (default 69)\n"
        "  --rate     sample rate in Hz       (default 48000)\n"
        "  --seconds  duration                (default 1)\n"
        "  --channels 1 | 2                   (default 2)\n",
        stderr);
}

std::string jsonEscape (std::string_view s)
{
    std::string out;
    out.reserve (s.size() + 8);
    for (const char ch : s)
    {
        switch (ch)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char> (ch) < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof (buf), "\\u%04x", unsigned (static_cast<unsigned char> (ch)));
                    out += buf;
                }
                else
                {
                    out += ch;
                }
        }
    }
    return out;
}

template <typename T>
bool parseNumber (std::string_view text, T& value)
{
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars (text.data(), end, value);
    return ec == std::errc() && ptr == end;
}

bool parseArgs (int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view key = argv[i];
        if (i + 1 >= argc)
            return false;
        const std::string_view val = argv[++i];

        bool ok = true;
        if (key == "--signal")        o.signal = val;
        else if (key == "--out")      o.outPath = val;
        else if (key == "--freq")     ok = parseNumber (val, o.frequency);
        else if (key == "--amp")      ok = parseNumber (val, o.amplitude);
        else if (key == "--rate")     ok = parseNumber (val, o.sampleRate);
        else if (key == "--seconds")  ok = parseNumber (val, o.seconds);
        else if (key == "--channels") ok = parseNumber (val, o.channels);
        else if (key == "--note")     ok = parseNumber (val, o.note);
        else if (key == "--wave")     o.wave = val;
        else if (key == "--kernel")   o.kernel = val;
        else if (key == "--pw")       ok = parseNumber (val, o.pulseWidth);
        else if (key == "--sync")     ok = parseNumber (val, o.syncRatio);
        else if (key == "--fm")       ok = parseNumber (val, o.fmDepth);
        else if (key == "--fm-ratio") ok = parseNumber (val, o.fmRatio);
        else if (key == "--bench")    o.bench = val;
        else
            return false;

        if (! ok)
            return false;
    }

    if (! o.bench.empty())
        return o.bench == "osc" || o.bench == "engine";

    aurel::dsp::Waveform w {};
    const bool oscOk = (o.signal != "osc" && o.signal != "note")
                    || (aurel::render::parseWaveform (o.wave, w) && aurel::render::isKnownKernel (o.kernel)
                        && o.pulseWidth >= 0.01 && o.pulseWidth <= 0.99 && o.syncRatio >= 0.0
                        && o.fmDepth >= 0.0 && o.fmRatio > 0.0);

    return ! o.outPath.empty() && oscOk
        && (o.signal == "sine" || o.signal == "silence" || o.signal == "note" || o.signal == "osc")
        && o.sampleRate >= 8000 && o.seconds > 0.0 && o.seconds <= 600.0
        && (o.channels == 1 || o.channels == 2)
        && o.frequency > 0.0 && o.frequency < o.sampleRate * 0.5
        && o.note >= 0 && o.note <= 127 && o.amplitude > 0.0 && o.amplitude <= 1.0;
}

void renderEngineNote (const Options& o, aurel::tooling::AudioData& audio)
{
    constexpr int blockSize = 512;
    aurel::engine::Engine engine;
    engine.prepare (o.sampleRate, blockSize);

    aurel::engine::EngineParameters params;
    params.masterGainDb = 0.0f;
    (void) aurel::render::parseWaveform (o.wave, params.voice.osc[0].waveform);

    aurel::engine::Event on;
    on.type = aurel::engine::EventType::NoteOn;
    on.note = static_cast<std::uint8_t> (o.note);
    on.value = static_cast<float> (o.amplitude);

    const auto frames = audio.numFrames();
    std::vector<float> left (frames), right (frames);
    for (std::size_t start = 0; start < frames; start += blockSize)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (blockSize, frames - start));
        const std::span<const aurel::engine::Event> events =
            start == 0 ? std::span<const aurel::engine::Event> (&on, 1) : std::span<const aurel::engine::Event>();
        engine.process (events, left.data() + start, right.data() + start, n, params);
    }

    for (std::size_t i = 0; i < frames; ++i)
    {
        if (o.channels == 1)
        {
            audio.interleaved[i] = 0.5f * (left[i] + right[i]);
        }
        else
        {
            audio.interleaved[2 * i] = left[i];
            audio.interleaved[2 * i + 1] = right[i];
        }
    }
}

/** CPU of the full engine at 48 kHz host / 512-sample blocks: N held notes,
    every source on (3 saws/pulses + sub + pink noise, sync + FM). */
int benchmarkEngine()
{
#ifndef NDEBUG
    std::puts ("WARNING: debug build, timings are not meaningful. Use the Release build.");
#endif
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    constexpr double seconds = 10.0;

    aurel::engine::EngineParameters p;
    for (auto& o : p.voice.osc)
        o.level = 0.5f;
    p.voice.osc[1].waveform = aurel::dsp::Waveform::Pulse;
    p.voice.osc2Sync = true;
    p.voice.osc2FmDepth = 1.0f;
    p.voice.subLevel = 0.5f;
    p.voice.noiseLevel = 0.2f;
    p.voice.noiseColor = aurel::dsp::NoiseColor::Pink;

    std::printf ("%-8s %14s %16s\n", "voices", "% of realtime", "% per voice");
    for (const int voices : { 1, 8, 16, 32 })
    {
        aurel::engine::Engine engine;
        engine.prepare (rate, block);
        std::vector<aurel::engine::Event> on;
        for (int v = 0; v < voices; ++v)
        {
            aurel::engine::Event e;
            e.type = aurel::engine::EventType::NoteOn;
            e.note = static_cast<std::uint8_t> (36 + v * 2); // C2 upward, up to ~C7
            e.value = 0.8f;
            on.push_back (e);
        }
        std::vector<float> l (block), r (block);
        engine.process (on, l.data(), r.data(), block, p);

        const auto blocks = static_cast<int> (seconds * rate / block);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
            engine.process ({}, l.data(), r.data(), block, p);
        const auto t1 = std::chrono::steady_clock::now();

        const double pct = 100.0 * std::chrono::duration<double> (t1 - t0).count() / seconds;
        std::printf ("%-8d %14.2f %16.3f\n", voices, pct, pct / voices);
    }
    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    Options opts;
    if (! parseArgs (argc, argv, opts))
    {
        printUsage();
        return 1;
    }

    if (opts.bench == "osc")
        return aurel::render::benchmarkOscillators();
    if (opts.bench == "engine")
        return benchmarkEngine();

    const aurel::dsp::ScopedFlushDenormals noDenormals;

    aurel::tooling::AudioData audio;
    audio.sampleRate = opts.sampleRate;
    audio.numChannels = opts.channels;

    const auto frames = static_cast<std::size_t> (std::llround (opts.seconds * opts.sampleRate));
    audio.interleaved.assign (frames * opts.channels, 0.0f);

    if (opts.signal == "sine")
    {
        // Phase computed directly from the sample index in double precision:
        // no accumulator drift, so this is a valid measurement reference.
        const double w = 2.0 * std::numbers::pi * opts.frequency / opts.sampleRate;
        for (std::size_t n = 0; n < frames; ++n)
        {
            const auto s = static_cast<float> (opts.amplitude * std::sin (w * static_cast<double> (n)));
            for (std::uint16_t c = 0; c < opts.channels; ++c)
                audio.interleaved[n * opts.channels + c] = s;
        }
    }
    else if (opts.signal == "note")
    {
        renderEngineNote (opts, audio);
    }
    else if (opts.signal == "osc")
    {
        aurel::render::OscRenderOptions ro;
        ro.kernel = opts.kernel;
        (void) aurel::render::parseWaveform (opts.wave, ro.waveform);
        ro.frequency = opts.frequency;
        ro.pulseWidth = opts.pulseWidth;
        ro.sampleRate = opts.sampleRate;
        ro.syncRatio = opts.syncRatio;
        ro.fmDepth = opts.fmDepth;
        ro.fmRatio = opts.fmRatio;
        std::vector<float> mono (frames);
        (void) aurel::render::renderOscillator (ro, mono);
        for (std::size_t n = 0; n < frames; ++n)
            for (std::uint16_t c = 0; c < opts.channels; ++c)
                audio.interleaved[n * opts.channels + c] = mono[n];
    }

    if (! aurel::tooling::writeWavFloat32 (opts.outPath, audio))
    {
        std::fprintf (stderr, "error: could not write %s\n", opts.outPath.c_str());
        return 2;
    }

    std::printf ("{\"signal\":\"%s\",\"rate\":%u,\"frames\":%zu,\"channels\":%u,\"out\":\"%s\"}\n",
                 jsonEscape (opts.signal).c_str(), opts.sampleRate, frames, unsigned (opts.channels),
                 jsonEscape (opts.outPath).c_str());
    return 0;
}
