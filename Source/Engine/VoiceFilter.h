#pragma once

// Per-voice filter section (internal rate, mono):
//
//   in -> [+ noise floor] -> series HPF (bypassed at its minimum)
//      -> Ladder 24/12: 20 kHz band limit -> nonlinear ladder
//         SVF:          multimode morph
//         Off:          pass-through
//
// * Cutoffs are smoothed in log2(Hz) (musically even sweeps) and filter
//   coefficients are recomputed every kControlInterval samples, avoiding a
//   tan() per sample per filter. With 20 ms smoothing a 16-sample step is far
//   below audibility.
// * Changing the filter type crossfades the old and new outputs over 5 ms.
//   Each underlying filter runs at most once per sample; Ladder 24 <-> 12
//   share one ladder (different taps).
// * A -120 dBFS noise floor at the filter input (like an analog circuit's
//   thermal noise) lets a resonant filter start self-oscillating with no
//   oscillator input. A digital filter at exactly zero never would.
// * The 20 kHz band limit before the ladder removes oscillator harmonics that
//   exist only because of oversampling; it lowers the ladder's aliasing by
//   ~50 dB at default drive (docs/dsp/filters-m3.md, decision F3).

#include "DSP/Core/Smoother.h"
#include "DSP/Filters/LadderFilter.h"
#include "DSP/Filters/StateVariableFilter.h"
#include "Engine/VoiceParameters.h"

#include <array>
#include <cstdint>

namespace aurel::engine
{

class VoiceFilter
{
public:
    static constexpr int kControlInterval = 16;
    static constexpr double kRampSeconds = 0.020;
    // Type change: the newly started filter first runs silently for
    // kWarmupSeconds (its start-from-zero transient rings out), then the
    // outputs crossfade over kCrossfadeSeconds. Measured in VoiceFilterTests.
    static constexpr double kWarmupSeconds = 0.005;
    static constexpr double kCrossfadeSeconds = 0.010;
    static constexpr double kBandLimitHz = 20000.0;
    static constexpr float kNoiseFloor = 1e-6f;          // -120 dBFS
    static constexpr double kLadderMaxFeedback = 4.5;    // resonance 1 -> k 4.5 (self-oscillation from ~89 %)
    static constexpr double kSvfMaxResonance = 0.98;     // resonance 1 -> Q 25
    static constexpr double kHpfMaxResonance = 0.9;      // resonance 1 -> Q 5

    /** Non-real-time. */
    void prepare (double sampleRate, std::uint32_t noiseSeed);

    /** Note start of an idle voice: clears all filter state and jumps the
        smoothers to the current parameters (no ramps). */
    void reset (const FilterParameters& p, int note) noexcept;

    /** Once per block: new targets (ramped). */
    void setTargets (const FilterParameters& p, int note) noexcept;

    float process (float x) noexcept;

    [[nodiscard]] FilterType getActiveType() const noexcept { return active; }

private:
    enum Smoothed { LogCutoff, Resonance, DriveDb, BassComp, SvfMode, HpfLogCutoff, HpfResonance, NumSmoothed };

    static bool usesLadder (FilterType t) noexcept { return t == FilterType::Ladder24 || t == FilterType::Ladder12; }
    [[nodiscard]] bool ladderNeeded() const noexcept { return usesLadder (active) || (fadeRemaining > 0 && usesLadder (previous)); }
    [[nodiscard]] bool svfNeeded() const noexcept
    {
        return active == FilterType::StateVariable || (fadeRemaining > 0 && previous == FilterType::StateVariable);
    }

    void setSmoothers (const FilterParameters& p, int note, bool jump) noexcept;
    void updateCoefficients() noexcept;
    float noise() noexcept;

    dsp::LadderFilter ladder;
    dsp::StateVariableFilter svf, hpf, bandLimit1, bandLimit2;
    std::array<dsp::LinearSmoother, NumSmoothed> smoothed {};

    FilterType active = FilterType::Ladder24;
    FilterType previous = FilterType::Ladder24;
    int fadeLength = 1;     // crossfade part
    int warmupLength = 0;   // silent warm-up part before it
    int fadeRemaining = 0;  // counts down over warmup + crossfade
    int controlCountdown = 0;
    bool hpfActive = false;
    bool ladderRunning = false;
    bool svfRunning = false;
    std::uint32_t noiseState = 1;
    std::uint32_t initialNoiseState = 1;
};

} // namespace aurel::engine
