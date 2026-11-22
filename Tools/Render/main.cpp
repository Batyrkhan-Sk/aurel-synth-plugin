// aurel_render: offline rendering tool.
//
// M0 scope: reference test signals only, so the C++ -> WAV -> Python
// measurement pipeline can be validated against known-correct input before
// any synthesis code exists.
//
//   aurel_render --signal sine --freq 1000 --amp 0.5 --rate 48000
//                --seconds 2 --channels 2 --out renders/sine.wav

#include "DSP/Core/Denormals.h"
#include "WavFile.h"

#include <charconv>
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
    std::string outPath;
};

void printUsage()
{
    std::fputs (
        "usage: aurel_render --out <file.wav> [options]\n"
        "  --signal   sine | silence          (default sine)\n"
        "  --freq     Hz                      (default 1000)\n"
        "  --amp      linear peak amplitude   (default 0.5)\n"
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
        else
            return false;

        if (! ok)
            return false;
    }

    return ! o.outPath.empty() && (o.signal == "sine" || o.signal == "silence")
        && o.sampleRate >= 8000 && o.seconds > 0.0 && o.seconds <= 600.0
        && (o.channels == 1 || o.channels == 2)
        && o.frequency > 0.0 && o.frequency < o.sampleRate * 0.5;
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
