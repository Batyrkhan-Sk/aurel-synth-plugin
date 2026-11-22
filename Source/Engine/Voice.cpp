#include "Engine/Voice.h"

#include "DSP/Core/Conversions.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aurel::engine
{

void Voice::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    attackStep = static_cast<float> (1.0 / std::max (1.0, kAttackSeconds * sampleRate));
    reset();
}

void Voice::reset() noexcept
{
    state = State::Idle;
    phase = 0.0;
    envelope = 0.0f;
    keyDown = false;
    sustained = false;
    note = -1;
}

void Voice::start (int newNote, int newChannel, float velocity, std::uint64_t newAge) noexcept
{
    if (state == State::Idle)
    {
        phase = 0.0;
        envelope = 0.0f;
    }

    note = newNote;
    channel = newChannel;
    age = newAge;
    level = kVelocityGain * std::clamp (velocity, 0.0f, 1.0f);
    phaseIncrement = dsp::noteToFrequency (static_cast<double> (newNote)) / sampleRate;
    state = State::Active;
    keyDown = true;
    sustained = false;
}

void Voice::release() noexcept
{
    if (state != State::Active)
        return;
    state = State::Releasing;
    // Constant release *time* regardless of the level reached.
    releaseStep = static_cast<float> (std::max (envelope, 1e-6f)
                                      / std::max (1.0, kReleaseSeconds * sampleRate));
}

void Voice::render (float* left, float* right, int numSamples) noexcept
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    for (int i = 0; i < numSamples; ++i)
    {
        if (state == State::Active)
        {
            envelope = std::min (1.0f, envelope + attackStep);
        }
        else if (state == State::Releasing)
        {
            envelope -= releaseStep;
            if (envelope <= 0.0f)
            {
                reset();
                return;
            }
        }
        else
        {
            return;
        }

        const auto s = static_cast<float> (std::sin (twoPi * phase)) * envelope * level;
        left[i] += s;
        right[i] += s;

        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;
    }
}

} // namespace aurel::engine
