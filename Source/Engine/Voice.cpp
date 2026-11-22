#include "Engine/Voice.h"

#include "DSP/Core/Conversions.h"

#include <algorithm>
#include <cmath>

namespace aurel::engine
{

void Voice::prepare (double internalSampleRate, std::uint32_t noiseSeed, double pinkGain)
{
    sampleRate = internalSampleRate;
    attackStep = static_cast<float> (1.0 / std::max (1.0, kAttackSeconds * sampleRate));

    for (auto& o : osc)
        o.prepare();
    sub.prepare();
    noise.prepare (sampleRate, noiseSeed, pinkGain);

    for (auto& s : level)
        s.prepare (sampleRate, kParameterRampSeconds);
    for (auto& s : pulseWidth)
        s.prepare (sampleRate, kParameterRampSeconds);
    fmDepth.prepare (sampleRate, kParameterRampSeconds);

    reset();
}

void Voice::reset() noexcept
{
    state = State::Idle;
    envelope = 0.0f;
    keyDown = false;
    sustained = false;
    note = -1;
}

void Voice::start (int newNote, int newChannel, float velocity, std::uint64_t newAge, const VoiceParameters& p) noexcept
{
    note = newNote;
    channel = newChannel;
    age = newAge;
    velocityLevel = kVelocityGain * std::clamp (velocity, 0.0f, 1.0f);
    keyDown = true;
    sustained = false;

    if (state == State::Idle)
    {
        envelope = 0.0f;
        applyDiscreteParameters (p);
        setSmootherTargets (p, true);
        for (std::size_t i = 0; i < osc.size(); ++i)
        {
            osc[i].setPulseWidth (pulseWidth[i].getCurrent());
            osc[i].reset (0.0);
        }
        sub.reset();
    }

    updateIncrements (p);
    state = State::Active;
}

void Voice::release() noexcept
{
    if (state != State::Active)
        return;
    state = State::Releasing;
    releaseStep = static_cast<float> (std::max (envelope, 1e-6f) / std::max (1.0, kReleaseSeconds * sampleRate));
}

void Voice::applyDiscreteParameters (const VoiceParameters& p) noexcept
{
    for (std::size_t i = 0; i < osc.size(); ++i)
        osc[i].setWaveform (p.osc[i].waveform);
    sub.setShape (p.subShape);
    sub.setOctave (p.subOctave);
    noise.setColor (p.noiseColor);
}

void Voice::updateIncrements (const VoiceParameters& p) noexcept
{
    for (std::size_t i = 0; i < osc.size(); ++i)
    {
        const auto& o = p.osc[i];
        const double pitch = note + 12.0 * o.octave + o.semitone + o.fineCents / 100.0;
        increment[i] = dsp::noteToFrequency (pitch) / sampleRate;
    }
}

void Voice::setSmootherTargets (const VoiceParameters& p, bool jump) noexcept
{
    const std::array<float, NumSources> levels { p.osc[0].level, p.osc[1].level, p.osc[2].level,
                                                 p.subLevel, p.noiseLevel };
    for (std::size_t i = 0; i < level.size(); ++i)
    {
        if (jump)
            level[i].setCurrentAndTarget (levels[i]);
        else
            level[i].setTarget (levels[i]);
    }
    for (std::size_t i = 0; i < pulseWidth.size(); ++i)
    {
        const float pw = std::clamp (p.osc[i].pulseWidth, 0.01f, 0.99f);
        if (jump)
            pulseWidth[i].setCurrentAndTarget (pw);
        else
            pulseWidth[i].setTarget (pw);
    }
    const float fm = std::clamp (p.osc2FmDepth, 0.0f, VoiceParameters::kMaxFmDepth);
    if (jump)
        fmDepth.setCurrentAndTarget (fm);
    else
        fmDepth.setTarget (fm);
}

void Voice::render (float* left, float* right, int numSamples, const VoiceParameters& p) noexcept
{
    if (state == State::Idle)
        return;

    applyDiscreteParameters (p);
    updateIncrements (p);
    setSmootherTargets (p, false);

    auto sourceActive = [this] (int s) {
        const auto& sm = level[static_cast<std::size_t> (s)];
        return sm.getCurrent() != 0.0f || sm.getTarget() != 0.0f;
    };
    const bool osc2On = sourceActive (Osc2);
    const bool osc3On = sourceActive (Osc3);
    const bool subOn = sourceActive (Sub);
    const bool noiseOn = sourceActive (Noise);
    const bool sync = p.osc2Sync;

    for (int i = 0; i < numSamples; ++i)
    {
        if (state == State::Active)
        {
            envelope = std::min (1.0f, envelope + attackStep);
        }
        else
        {
            envelope -= releaseStep;
            if (envelope <= 0.0f)
            {
                reset();
                return;
            }
        }

        for (std::size_t k = 0; k < osc.size(); ++k)
            osc[k].setPulseWidth (pulseWidth[k].next());

        // OSC1 always runs: it is the sync, FM and sub source.
        const float o1 = osc[0].process (increment[0]);
        const double wrap1 = osc[0].getWrapFraction();

        float mix = level[Osc1].next() * o1;

        const float fm = fmDepth.next();
        const float l2 = level[Osc2].next();
        if (osc2On)
        {
            const double inc2 = increment[1] * (1.0 + static_cast<double> (fm) * o1);
            mix += l2 * osc[1].process (inc2, sync ? wrap1 : -1.0);
        }

        const float l3 = level[Osc3].next();
        if (osc3On)
            mix += l3 * osc[2].process (increment[2]);

        const float ls = level[Sub].next();
        if (subOn)
            mix += ls * sub.process (osc[0].getPhase(), wrap1, increment[0]);

        const float ln = level[Noise].next();
        if (noiseOn)
            mix += ln * noise.process();

        const float s = mix * envelope * velocityLevel;
        left[i] += s;
        right[i] += s;
    }
}

} // namespace aurel::engine
