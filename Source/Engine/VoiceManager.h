#pragma once

// Voice allocation and note lifecycle.
//
// Rules (each covered by a test):
//  * Note On takes an idle voice; otherwise steals the oldest releasing
//    voice; otherwise the oldest held voice.
//  * Note Off releases the OLDEST held voice matching (note, channel). FL
//    Studio's piano roll can send the next note's Note On before the
//    previous note's Note Off on the same key; FIFO matching makes that
//    Note Off end the old note, not the one that just started.
//  * A Note Off for a note whose voice was stolen finds no voice: ignored.
//  * Sustain pedal: Note Off while the pedal is down marks the voice
//    sustained; pedal up releases all sustained voices whose key is up.
//  * All Notes Off releases everything; All Sound Off silences everything.

#include "Engine/Events.h"
#include "Engine/Voice.h"

#include <array>
#include <cstdint>

namespace aurel::engine
{

class VoiceManager
{
public:
    static constexpr int kMaxVoices = 32;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void handleEvent (const Event& e) noexcept;

    /** Adds all active voices into left/right. */
    void render (float* left, float* right, int numSamples) noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept;
    [[nodiscard]] bool isSustainDown() const noexcept { return sustainDown; }

private:
    void noteOn (int note, int channel, float velocity) noexcept;
    void noteOff (int note, int channel) noexcept;
    void setSustain (bool down) noexcept;
    Voice& chooseVoiceForNoteOn() noexcept;

    std::array<Voice, kMaxVoices> voices {};
    std::uint64_t nextAge = 0;
    bool sustainDown = false;
};

} // namespace aurel::engine
