#pragma once

// M1 voice: a sine oscillator with a fixed linear attack/release gate.
//
// TEMPORARY: this exists to prove the MIDI -> voice -> host path. It is
// replaced by the real oscillator/mixer/filter/ADSR voice in M2-M4. The note
// lifecycle (start / release / steal / sustain flags) is the part that stays.

#include <cstdint>

namespace aurel::engine
{

class Voice
{
public:
    enum class State : std::uint8_t { Idle, Active, Releasing };

    static constexpr double kAttackSeconds = 0.002;
    static constexpr double kReleaseSeconds = 0.030;
    static constexpr float kVelocityGain = 0.25f; // per-voice headroom for 32-voice sums

    void prepare (double sampleRate) noexcept;
    void reset() noexcept; // -> Idle, silent

    /** Starts (or retriggers / steals) the voice. When the voice is already
        sounding, phase and envelope continue from their current values so
        the waveform stays continuous (no step, no click). */
    void start (int note, int channel, float velocity, std::uint64_t age) noexcept;

    /** Begins the release ramp. No-op unless Active. */
    void release() noexcept;

    /** Adds this voice's output to left/right. */
    void render (float* left, float* right, int numSamples) noexcept;

    [[nodiscard]] State getState() const noexcept { return state; }
    [[nodiscard]] bool isActive() const noexcept { return state != State::Idle; }
    [[nodiscard]] int getNote() const noexcept { return note; }
    [[nodiscard]] int getChannel() const noexcept { return channel; }
    [[nodiscard]] std::uint64_t getAge() const noexcept { return age; }

    // Note-lifecycle flags, owned by VoiceManager.
    bool keyDown = false;   // the key that started this voice is still held
    bool sustained = false; // key released while the sustain pedal was down

private:
    double sampleRate = 48000.0;
    double phase = 0.0;          // cycles, [0, 1)
    double phaseIncrement = 0.0; // cycles per sample
    float level = 0.0f;
    float envelope = 0.0f;
    float attackStep = 0.0f;
    float releaseStep = 0.0f;
    State state = State::Idle;
    int note = -1;
    int channel = 0;
    std::uint64_t age = 0;
};

} // namespace aurel::engine
