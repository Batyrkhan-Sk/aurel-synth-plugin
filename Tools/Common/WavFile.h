#pragma once

// Minimal 32-bit IEEE-float WAV reader/writer for offline renders and tests.
//
// Float WAV is used so measurements see the engine's output without any
// quantisation or dithering added by the file format. Only what AUREL's own
// tools write is supported: WAVE_FORMAT_IEEE_FLOAT (3), 32-bit, interleaved.

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace aurel::tooling
{

struct AudioData
{
    std::uint32_t sampleRate = 0;
    std::uint16_t numChannels = 0;
    std::vector<float> interleaved; // frame-major: [f0c0, f0c1, f1c0, ...]

    [[nodiscard]] std::size_t numFrames() const noexcept
    {
        return numChannels == 0 ? 0 : interleaved.size() / numChannels;
    }
};

namespace detail
{
    inline void put16 (std::ofstream& out, std::uint16_t v)
    {
        const std::array<char, 2> b { char (v & 0xff), char ((v >> 8) & 0xff) };
        out.write (b.data(), 2);
    }

    inline void put32 (std::ofstream& out, std::uint32_t v)
    {
        const std::array<char, 4> b { char (v & 0xff), char ((v >> 8) & 0xff),
                                      char ((v >> 16) & 0xff), char ((v >> 24) & 0xff) };
        out.write (b.data(), 4);
    }

    inline std::uint16_t get16 (const unsigned char* p) noexcept
    {
        return std::uint16_t (p[0] | (p[1] << 8));
    }

    inline std::uint32_t get32 (const unsigned char* p) noexcept
    {
        return std::uint32_t (p[0]) | (std::uint32_t (p[1]) << 8)
             | (std::uint32_t (p[2]) << 16) | (std::uint32_t (p[3]) << 24);
    }
} // namespace detail

/** Writes a 32-bit float WAV. Returns false on invalid input or I/O failure. */
[[nodiscard]] inline bool writeWavFloat32 (const std::filesystem::path& path, const AudioData& audio)
{
    static_assert (sizeof (float) == 4, "IEEE-754 binary32 required");

    if (audio.numChannels == 0 || audio.sampleRate == 0
        || audio.interleaved.size() % audio.numChannels != 0)
        return false;

    const auto dataBytes = audio.interleaved.size() * sizeof (float);
    if (dataBytes > 0xFFFFFFFFull - 64)
        return false;

    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    if (! out)
        return false;

    const auto blockAlign = std::uint16_t (audio.numChannels * 4);
    const auto frames = std::uint32_t (audio.numFrames());

    // RIFF size = 4 ("WAVE") + (8 + 18) fmt + (8 + 4) fact + (8 + data)
    out.write ("RIFF", 4);
    detail::put32 (out, std::uint32_t (4 + 26 + 12 + 8 + dataBytes));
    out.write ("WAVE", 4);

    out.write ("fmt ", 4);
    detail::put32 (out, 18);
    detail::put16 (out, 3); // WAVE_FORMAT_IEEE_FLOAT
    detail::put16 (out, audio.numChannels);
    detail::put32 (out, audio.sampleRate);
    detail::put32 (out, audio.sampleRate * blockAlign);
    detail::put16 (out, blockAlign);
    detail::put16 (out, 32);
    detail::put16 (out, 0); // cbSize

    out.write ("fact", 4); // required for non-PCM formats
    detail::put32 (out, 4);
    detail::put32 (out, frames);

    out.write ("data", 4);
    detail::put32 (out, std::uint32_t (dataBytes));

    // Samples are written little-endian; all supported targets are LE.
    out.write (reinterpret_cast<const char*> (audio.interleaved.data()),
               std::streamsize (dataBytes));

    return bool (out);
}

/** Reads a 32-bit float WAV written by writeWavFloat32 (or compatible). */
[[nodiscard]] inline std::optional<AudioData> readWavFloat32 (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return std::nullopt;

    const std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (in)),
                                            std::istreambuf_iterator<char>());

    if (bytes.size() < 12 || std::memcmp (bytes.data(), "RIFF", 4) != 0
        || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return std::nullopt;

    AudioData audio;
    bool haveFormat = false;
    std::size_t pos = 12;

    while (pos + 8 <= bytes.size())
    {
        const auto* chunk = bytes.data() + pos;
        const auto chunkSize = std::size_t (detail::get32 (chunk + 4));
        const auto body = pos + 8;

        if (body + chunkSize > bytes.size())
            return std::nullopt; // truncated

        if (std::memcmp (chunk, "fmt ", 4) == 0)
        {
            if (chunkSize < 16)
                return std::nullopt;
            const auto* f = bytes.data() + body;
            if (detail::get16 (f) != 3 || detail::get16 (f + 14) != 32)
                return std::nullopt; // only 32-bit IEEE float supported
            audio.numChannels = detail::get16 (f + 2);
            audio.sampleRate = detail::get32 (f + 4);
            haveFormat = audio.numChannels > 0 && audio.sampleRate > 0;
        }
        else if (std::memcmp (chunk, "data", 4) == 0)
        {
            if (! haveFormat || chunkSize % (4u * audio.numChannels) != 0)
                return std::nullopt;
            audio.interleaved.resize (chunkSize / 4);
            std::memcpy (audio.interleaved.data(), bytes.data() + body, chunkSize);
            return audio;
        }

        pos = body + chunkSize + (chunkSize & 1u); // chunks are word-aligned
    }

    return std::nullopt;
}

} // namespace aurel::tooling
