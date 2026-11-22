#include "WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace aurel::tooling;
namespace fs = std::filesystem;

namespace
{
    fs::path tempPath (const char* name)
    {
        return fs::temp_directory_path() / name;
    }
} // namespace

TEST_CASE ("WAV float32 round trip is bit-exact", "[tooling][wav]")
{
    AudioData in;
    in.sampleRate = 96000;
    in.numChannels = 2;
    for (int i = 0; i < 1001; ++i)
    {
        in.interleaved.push_back (static_cast<float> (i) * 1e-3f - 0.5f);
        in.interleaved.push_back (-static_cast<float> (i) * 1e-4f);
    }
    // Values that must survive untouched in a float container.
    in.interleaved[10] = 1.5f;  // > 0 dBFS
    in.interleaved[11] = 1e-30f;

    const auto path = tempPath ("aurel_wav_roundtrip.wav");
    REQUIRE (writeWavFloat32 (path, in));

    const auto out = readWavFloat32 (path);
    REQUIRE (out.has_value());
    CHECK (out->sampleRate == in.sampleRate);
    CHECK (out->numChannels == in.numChannels);
    CHECK (out->numFrames() == 1001);
    REQUIRE (out->interleaved.size() == in.interleaved.size());
    CHECK (std::memcmp (out->interleaved.data(), in.interleaved.data(),
                        in.interleaved.size() * sizeof (float)) == 0);

    fs::remove (path);
}

TEST_CASE ("WAV writer rejects invalid input", "[tooling][wav]")
{
    AudioData bad;
    bad.sampleRate = 48000;
    bad.numChannels = 2;
    bad.interleaved = { 0.0f, 0.0f, 0.0f }; // not a whole number of frames
    CHECK_FALSE (writeWavFloat32 (tempPath ("aurel_wav_bad.wav"), bad));

    AudioData noChannels;
    noChannels.sampleRate = 48000;
    CHECK_FALSE (writeWavFloat32 (tempPath ("aurel_wav_bad.wav"), noChannels));
}

TEST_CASE ("WAV reader rejects missing, garbage and truncated files", "[tooling][wav]")
{
    CHECK_FALSE (readWavFloat32 (tempPath ("aurel_does_not_exist.wav")).has_value());

    const auto garbage = tempPath ("aurel_wav_garbage.wav");
    {
        std::ofstream f (garbage, std::ios::binary);
        f << "this is not a wav file at all";
    }
    CHECK_FALSE (readWavFloat32 (garbage).has_value());

    // Valid header, then truncate the data chunk.
    AudioData in;
    in.sampleRate = 44100;
    in.numChannels = 1;
    in.interleaved.assign (100, 0.25f);
    const auto truncated = tempPath ("aurel_wav_truncated.wav");
    REQUIRE (writeWavFloat32 (truncated, in));
    fs::resize_file (truncated, fs::file_size (truncated) - 10);
    CHECK_FALSE (readWavFloat32 (truncated).has_value());

    fs::remove (garbage);
    fs::remove (truncated);
}
