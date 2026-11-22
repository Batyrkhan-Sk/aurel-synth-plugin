#pragma once

// Raw MIDI bytes -> engine Event. Works on bytes directly (no
// juce::MidiMessage) so the audio thread never constructs objects that may
// allocate (e.g. for SysEx).

#include "Engine/Events.h"

#include <cstdint>

namespace aurel::engine
{

/** Returns true and fills `out` if the message is one the engine handles.
    - Note On with velocity 0 is treated as Note Off (MIDI spec).
    - CC 64 >= 64 is pedal down.
    - CC 120 -> AllSoundOff; CC 123..127 -> AllNotesOff (channel mode
      messages 124..127 imply all notes off per the MIDI 1.0 spec).
    - CC 121 (reset all controllers) releases the sustain pedal. */
[[nodiscard]] bool parseMidi (const std::uint8_t* data, int numBytes, int sampleOffset,
                              Event& out) noexcept;

} // namespace aurel::engine
