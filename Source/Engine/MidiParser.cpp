#include "Engine/MidiParser.h"

namespace aurel::engine
{

bool parseMidi (const std::uint8_t* data, int numBytes, int sampleOffset, Event& out) noexcept
{
    if (data == nullptr || numBytes < 3)
        return false; // every message handled here is 3 bytes

    const auto status = data[0];
    const auto kind = static_cast<std::uint8_t> (status & 0xF0);
    const auto channel = static_cast<std::uint8_t> (status & 0x0F);
    const auto d1 = static_cast<std::uint8_t> (data[1] & 0x7F);
    const auto d2 = static_cast<std::uint8_t> (data[2] & 0x7F);

    out = {};
    out.sampleOffset = sampleOffset;
    out.channel = channel;

    switch (kind)
    {
        case 0x90:
            out.note = d1;
            if (d2 == 0)
            {
                out.type = EventType::NoteOff;
                return true;
            }
            out.type = EventType::NoteOn;
            out.value = static_cast<float> (d2) / 127.0f;
            return true;

        case 0x80:
            out.type = EventType::NoteOff;
            out.note = d1;
            return true;

        case 0xB0:
            switch (d1)
            {
                case 64:
                    out.type = EventType::SustainPedal;
                    out.value = d2 >= 64 ? 1.0f : 0.0f;
                    return true;
                case 121:
                    out.type = EventType::SustainPedal;
                    out.value = 0.0f;
                    return true;
                case 120:
                    out.type = EventType::AllSoundOff;
                    return true;
                case 123: case 124: case 125: case 126: case 127:
                    out.type = EventType::AllNotesOff;
                    return true;
                default:
                    return false;
            }

        default:
            return false;
    }
}

} // namespace aurel::engine
