#pragma once

#include <cstdint>

namespace aurel::engine
{

enum class EventType : std::uint8_t
{
    NoteOn,       // note, channel, value = velocity 0..1 (> 0)
    NoteOff,      // note, channel
    SustainPedal, // channel, value = 1 (down) or 0 (up)
    AllNotesOff,  // release every voice
    AllSoundOff   // silence every voice immediately
};

/** A timestamped engine event. sampleOffset is relative to the start of the
    process() call it is passed to. Events must be sorted by sampleOffset. */
struct Event
{
    int sampleOffset = 0;
    EventType type = EventType::NoteOn;
    std::uint8_t channel = 0; // 0..15
    std::uint8_t note = 0;    // 0..127
    float value = 0.0f;
};

} // namespace aurel::engine
