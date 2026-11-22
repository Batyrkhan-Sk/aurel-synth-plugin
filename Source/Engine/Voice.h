#pragma once

// One synth voice at the internal (oversampled) rate:
//
//   OSC1 --+--> sync / FM --> OSC2 --+
//          +--> SUB                  +--> MIXER --> FILTER (VoiceFilter) --> gate envelope --> out
//   OSC3 ----------------------------+                                       (mono, added to L and R)
//   NOISE ---------------------------+
//
// The gate envelope (fixed linear attack/release) is TEMPORARY until the
// ADSRs arrive in M4. The note lifecycle (start / release / steal / sustain
// flags) is permanent.
//
// Mixer headroom rule (ADR 0001, D2): every source has a fixed gain equal
// to its level; there is no normalisation by the number of active sources.
// One oscillator at level 1 always yields the same mixer output level.

#include "DSP/Core/Smoother.h"
#include "Engine/VoiceFilter.h"
#include "Engine/VoiceParameters.h"

#include <array>
#include <cstdint>

namespace aurel::engine
{

class Voice
{
public:
    enum class State : std::uint8_t { Idle, Active, Releasing };

    static constexpr double kAttackSeconds = 0.002;
    static constexpr double kReleaseSeconds = 0.030;
    static constexpr double kParameterRampSeconds = 0.020;
    static constexpr float kVelocityGain = 0.25f; // per-voice headroom for 32-voice sums

    /** Non-real-time. pinkGain from NoiseGenerator::computePinkGain(). */
    void prepare (double internalSampleRate, std::uint32_t noiseSeed, double pinkGain);
    void reset() noexcept; // -> Idle, silent

    /** Starts, retriggers or steals the voice. A voice that was idle starts
        all oscillators at phase 0 with the current parameters (no ramps);
        a sounding voice continues its phases (no step, no click). */
    void start (int note, int channel, float velocity, std::uint64_t age, const VoiceParameters& p) noexcept;

    /** Begins the release ramp. No-op unless Active. */
    void release() noexcept;

    /** Adds this voice's output to left/right (internal rate). */
    void render (float* left, float* right, int numSamples, const VoiceParameters& p) noexcept;

    [[nodiscard]] State getState() const noexcept { return state; }
    [[nodiscard]] bool isActive() const noexcept { return state != State::Idle; }
    [[nodiscard]] int getNote() const noexcept { return note; }
    [[nodiscard]] int getChannel() const noexcept { return channel; }
    [[nodiscard]] std::uint64_t getAge() const noexcept { return age; }

    // Note-lifecycle flags, owned by VoiceManager.
    bool keyDown = false;
    bool sustained = false;

private:
    enum Source { Osc1, Osc2, Osc3, Sub, Noise, NumSources };

    void applyDiscreteParameters (const VoiceParameters& p) noexcept;
    void updateIncrements (const VoiceParameters& p) noexcept;
    void setSmootherTargets (const VoiceParameters& p, bool jump) noexcept;

    double sampleRate = 96000.0;

    std::array<dsp::Oscillator, 3> osc {};
    dsp::SubOscillator<dsp::OscillatorKernel> sub;
    dsp::NoiseGenerator noise;
    VoiceFilter filter;

    std::array<double, 3> increment {};
    std::array<dsp::LinearSmoother, NumSources> level {};
    std::array<dsp::LinearSmoother, 3> pulseWidth {};
    dsp::LinearSmoother fmDepth;

    float velocityLevel = 0.0f;
    float envelope = 0.0f;
    float attackStep = 0.0f;
    float releaseStep = 0.0f;
    State state = State::Idle;
    int note = -1;
    int channel = 0;
    std::uint64_t age = 0;
};

} // namespace aurel::engine
