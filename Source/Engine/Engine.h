#pragma once

// Top-level AUREL engine: timestamped events in, stereo audio out.
//
// Host-robustness contract (tested):
//  * process() accepts any numSamples >= 0, including blocks larger than the
//    maxBlockSize given to prepare(); they are processed in internal chunks.
//  * numSamples == 0 still applies the events (some hosts deliver MIDI in
//    empty blocks; dropping it could leave notes stuck).
//  * Events are applied sample-accurately. Offsets outside [0, numSamples)
//    are clamped. Output is identical for any block segmentation.
//  * right == nullptr selects mono output (L/R average).
//  * No allocation, locks or I/O inside process().

#include "DSP/Core/Smoother.h"
#include "Engine/Events.h"
#include "Engine/VoiceManager.h"

#include <span>
#include <vector>

namespace aurel::engine
{

/** Plain per-block parameter snapshot, built by the plugin layer from host
    parameters. */
struct EngineParameters
{
    float masterGainDb = -6.0f;
};

class Engine
{
public:
    static constexpr double kGainRampSeconds = 0.020;

    /** Allocates. Call from a non-real-time context. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Silences all voices and resets state. Real-time safe. */
    void reset() noexcept;

    void process (std::span<const Event> events, float* left, float* right, int numSamples,
                  const EngineParameters& params) noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept { return voices.activeVoiceCount(); }
    [[nodiscard]] bool isPrepared() const noexcept { return maxBlock > 0; }

private:
    VoiceManager voices;
    dsp::LinearSmoother masterGain;
    std::vector<float> busLeft, busRight;
    int maxBlock = 0;
    bool gainInitialised = false;
};

} // namespace aurel::engine
