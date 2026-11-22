#include "Engine/VoiceFilter.h"

#include "DSP/Core/Conversions.h"

#include <algorithm>
#include <cmath>

namespace aurel::engine
{

namespace
{
    // 4th-order Butterworth as two 2-pole sections (Q = 0.5412, 1.3066).
    constexpr double kButterworthDampingA = 1.0 / 0.5412;
    constexpr double kButterworthDampingB = 1.0 / 1.3066;
    constexpr double kMinLogCutoff = 3.3219280948873626;  // log2(10 Hz)
    constexpr double kMaxLogCutoff = 14.872674880270605;  // log2(30 kHz)
} // namespace

void VoiceFilter::prepare (double sampleRate, std::uint32_t noiseSeed)
{
    ladder.prepare (sampleRate);
    svf.prepare (sampleRate);
    hpf.prepare (sampleRate);
    bandLimit1.prepare (sampleRate);
    bandLimit2.prepare (sampleRate);
    bandLimit1.setCutoffAndDamping (kBandLimitHz, kButterworthDampingA);
    bandLimit2.setCutoffAndDamping (kBandLimitHz, kButterworthDampingB);

    for (auto& s : smoothed)
        s.prepare (sampleRate, kRampSeconds);
    fadeLength = std::max (1, static_cast<int> (kCrossfadeSeconds * sampleRate));
    warmupLength = static_cast<int> (kWarmupSeconds * sampleRate);
    initialNoiseState = noiseSeed == 0 ? 0x2545F491u : noiseSeed;
    reset (FilterParameters {}, 60);
}

void VoiceFilter::reset (const FilterParameters& p, int note) noexcept
{
    ladder.reset();
    svf.reset();
    hpf.reset();
    bandLimit1.reset();
    bandLimit2.reset();
    noiseState = initialNoiseState;

    active = previous = p.type;
    fadeRemaining = 0;
    ladderRunning = usesLadder (active);
    svfRunning = active == FilterType::StateVariable;
    setSmoothers (p, note, true);
    updateCoefficients();
    controlCountdown = kControlInterval;
}

void VoiceFilter::setTargets (const FilterParameters& p, int note) noexcept
{
    if (p.type != active)
    {
        previous = active;
        active = p.type;
        // Ladder 24 <-> 12 share a running ladder: no warm-up needed.
        const bool sameFilter = usesLadder (active) && usesLadder (previous);
        fadeRemaining = fadeLength + (sameFilter ? 0 : warmupLength);

        // A filter that was not running starts from a clean state; Ladder 24
        // and 12 share one ladder, so switching between them keeps it.
        if (usesLadder (active) && ! ladderRunning)
        {
            ladder.reset();
            bandLimit1.reset();
            bandLimit2.reset();
        }
        if (active == FilterType::StateVariable && ! svfRunning)
            svf.reset();
        setSmoothers (p, note, false);
        updateCoefficients(); // the new filter must not run on stale coefficients
        return;
    }
    setSmoothers (p, note, false);
}

void VoiceFilter::setSmoothers (const FilterParameters& p, int note, bool jump) noexcept
{
    const double tracking = p.keyTracking * (note - FilterParameters::kKeyTrackReferenceNote) / 12.0;
    const double logCutoff = std::clamp (std::log2 (std::max (p.cutoffHz, 1.0f)) + tracking, kMinLogCutoff, kMaxLogCutoff);
    const float hpfLog = static_cast<float> (std::log2 (std::max (p.hpfCutoffHz, FilterParameters::kHpfOffHz)));

    const std::array<float, NumSmoothed> targets {
        static_cast<float> (logCutoff), std::clamp (p.resonance, 0.0f, 1.0f), p.driveDb,
        std::clamp (p.bassCompensation, 0.0f, 1.0f), std::clamp (p.svfMode, 0.0f, 3.0f), hpfLog,
        std::clamp (p.hpfResonance, 0.0f, 1.0f)
    };
    for (std::size_t i = 0; i < smoothed.size(); ++i)
    {
        if (jump)
            smoothed[i].setCurrentAndTarget (targets[i]);
        else
            smoothed[i].setTarget (targets[i]);
    }
}

void VoiceFilter::updateCoefficients() noexcept
{
    const double cutoff = std::exp2 (static_cast<double> (smoothed[LogCutoff].getCurrent()));
    const double resonance = smoothed[Resonance].getCurrent();

    if (ladderNeeded())
        ladder.setParameters (cutoff, resonance * kLadderMaxFeedback,
                              dsp::decibelsToGain (smoothed[DriveDb].getCurrent()),
                              smoothed[BassComp].getCurrent());
    if (svfNeeded())
        svf.setParameters (cutoff, resonance * kSvfMaxResonance);

    const float offLog = std::log2 (FilterParameters::kHpfOffHz);
    const bool hpfWanted = smoothed[HpfLogCutoff].getCurrent() > offLog || smoothed[HpfLogCutoff].getTarget() > offLog;
    if (hpfWanted && ! hpfActive)
        hpf.reset();
    hpfActive = hpfWanted;
    if (hpfActive)
        hpf.setParameters (std::exp2 (static_cast<double> (smoothed[HpfLogCutoff].getCurrent())),
                           smoothed[HpfResonance].getCurrent() * kHpfMaxResonance);
}

float VoiceFilter::noise() noexcept
{
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return static_cast<float> (static_cast<std::int32_t> (noiseState)) * (kNoiseFloor / 2147483648.0f);
}

float VoiceFilter::process (float input) noexcept
{
    if (--controlCountdown <= 0)
    {
        for (auto& s : smoothed)
            (void) s.skip (kControlInterval);
        updateCoefficients();
        controlCountdown = kControlInterval;
    }

    if (active == FilterType::Off && fadeRemaining == 0)
    {
        ladderRunning = svfRunning = false;
        return hpfActive ? static_cast<float> (hpf.process (input).hp) : input;
    }

    double x = static_cast<double> (input) + noise();
    if (hpfActive)
        x = hpf.process (x).hp;

    dsp::LadderFilter::Taps taps {};
    ladderRunning = ladderNeeded();
    if (ladderRunning)
        taps = ladder.processTaps (bandLimit2.process (bandLimit1.process (x).lp).lp);

    double svfOut = 0.0;
    svfRunning = svfNeeded();
    if (svfRunning)
        svfOut = svf.morph (svf.process (x), smoothed[SvfMode].getCurrent());

    auto outputOf = [&] (FilterType t) {
        switch (t)
        {
            case FilterType::Ladder24:      return taps.lp24;
            case FilterType::Ladder12:      return taps.lp12;
            case FilterType::StateVariable: return svfOut;
            case FilterType::Off:           break;
        }
        return x;
    };

    double y = outputOf (active);
    if (fadeRemaining > 0)
    {
        // Weight of the old output: 1 during the warm-up, then 1 -> 0.
        const double t = std::min (1.0, static_cast<double> (fadeRemaining) / fadeLength);
        y = t * outputOf (previous) + (1.0 - t) * y;
        --fadeRemaining;
    }
    return static_cast<float> (y);
}

} // namespace aurel::engine
