#pragma once

// Top-level AUREL engine: timestamped events in, stereo audio out.
//
// Voices run at an internal rate of at least 88.2 kHz (ADR 0001, D4;
// confirmed in docs/dsp/oscillator-antialiasing.md): 2x for 44.1/48 kHz
// hosts, 4x below 44.1 kHz, 1x at 88.2 kHz and above. The summed stereo
// voice bus is decimated once by polyphase IIR half-band filters
// (>= 110 dB design stopband, flat to 20 kHz).
//
// Host-robustness contract (tested):
//  * process() accepts any numSamples >= 0, including blocks larger than the
//    maxBlockSize given to prepare(); they are processed in internal chunks.
//  * numSamples == 0 still applies the events.
//  * Events are applied sample-accurately (at host-sample resolution).
//    Offsets outside [0, numSamples) are clamped. Output is identical for any
//    block segmentation.
//  * right == nullptr selects mono output (L/R average).
//  * No allocation, locks or I/O inside process().

#include "DSP/Core/Smoother.h"
#include "DSP/Oversampling/Halfband.h"
#include "Engine/Events.h"
#include "Engine/VoiceManager.h"
#include "Engine/VoiceParameters.h"

#include <array>
#include <span>
#include <vector>

namespace aurel::engine
{

/** Plain per-block parameter snapshot, built by the plugin layer. */
struct EngineParameters
{
    float masterGainDb = -6.0f;
    VoiceParameters voice;
};

class Engine
{
public:
    static constexpr double kGainRampSeconds = 0.020;
    static constexpr double kDecimatorAttenuationDb = 110.0;
    static constexpr double kPassbandEdgeHz = 20000.0;

    /** Allocates. Call from a non-real-time context. */
    void prepare (double hostSampleRate, int maxBlockSize);

    /** Silences all voices and resets state. Real-time safe. */
    void reset() noexcept;

    void process (std::span<const Event> events, float* left, float* right, int numSamples,
                  const EngineParameters& params) noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept { return voices.activeVoiceCount(); }
    [[nodiscard]] bool isPrepared() const noexcept { return maxBlock > 0; }
    [[nodiscard]] int getOversamplingFactor() const noexcept { return factor; }
    [[nodiscard]] double getInternalSampleRate() const noexcept { return internalRate; }

    /** 1, 2 or 4: smallest factor giving an internal rate >= 88.2 kHz
        (capped at 4). */
    [[nodiscard]] static int oversamplingFactorFor (double hostSampleRate) noexcept;

private:
    static constexpr int kMaxStages = 2;

    void decimate (int internalLength) noexcept;

    VoiceManager voices;
    dsp::LinearSmoother masterGain;
    std::vector<float> busLeft, busRight;
    std::array<std::array<dsp::HalfbandDecimator, kMaxStages>, 2> decimators {};
    int stages = 0;
    int factor = 1;
    double internalRate = 0.0;
    int maxBlock = 0;
    bool gainInitialised = false;
};

} // namespace aurel::engine
